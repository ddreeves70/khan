#include "sb/kernel_occt/OcctKernel.h"
#include "sb/kernel_occt/OcctShape.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>

namespace sb::kernel {

namespace {

// OCCT's private shape representation behind a ShapeHandle.
struct OcctShape final : ShapeImpl {
    explicit OcctShape(TopoDS_Shape s) : shape(std::move(s)) {}
    TopoDS_Shape shape;
};

ShapeHandle wrap(const TopoDS_Shape& s) {
    return ShapeHandle(std::make_shared<const OcctShape>(s));
}

const TopoDS_Shape* unwrap(const ShapeHandle& h) {
    const auto* impl = h.as<OcctShape>();
    return impl ? &impl->shape : nullptr;
}

Diagnostic error(std::string code, std::string message, std::string detail = {}) {
    return Diagnostic{std::move(code), Severity::Error, std::move(message), std::move(detail)};
}

OpResult failed(std::string code, std::string message, std::string detail = {}) {
    OpResult r;
    r.issues.push_back(error(std::move(code), std::move(message), std::move(detail)));
    return r;
}

std::string describe(const Standard_Failure& e) {
    const char* msg = e.GetMessageString();
    return std::string(e.DynamicType()->Name()) + (msg && *msg ? std::string(": ") + msg : "");
}

std::string utf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

// OCCT prints progress and statistics (for example from the STEP writer) to the
// console by default. SketchBillet reports problems through Diagnostics instead, so the
// default console printers are removed once per process.
void silenceOcctConsole() {
    static std::once_flag once;
    std::call_once(
        once, [] { Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream)); });
}

// Turns a flat outline (in a plane's x and y) into an OCCT wire. Neighboring
// edges share one vertex, put halfway between their ends, so ends a hair apart
// (as a solver leaves them) still join; the vertex's tolerance covers the gap.
class WireMaker {
public:
    WireMaker(const Plane& plane, const gp_Ax3& axes) : plane_(plane), axes_(axes) {}

    // The wire, or why it can't be made (a plain sentence).
    // Each edge made is added to `made`, in the loop's order (one per Edge2).
    std::optional<TopoDS_Wire> make(const Loop2& loop, std::string& why,
                                    std::vector<TopoDS_Edge>& made) const {
        if (loop.empty()) {
            why = "An outline has no lines, arcs or circles in it.";
            return std::nullopt;
        }
        // One closed spline on its own is a closed loop already, like a circle.
        if (const auto* s = std::get_if<Spline2>(&loop[0].curve); loop.size() == 1 && s && s->closed) {
            const auto curve = spline(*s, why);
            if (!curve) return std::nullopt;
            TopoDS_Edge e = BRepBuilderAPI_MakeEdge(curve).Edge();
            if (loop[0].reversed) e.Reverse();
            made.push_back(e);
            return BRepBuilderAPI_MakeWire(e).Wire();
        }
        // One circle on its own is a closed loop already.
        if (loop.size() == 1 && std::holds_alternative<Circle2>(loop[0].curve)) {
            const auto& c = std::get<Circle2>(loop[0].curve);
            if (!(c.radius > kSmall)) {
                why = "A circle in the outline has no size.";
                return std::nullopt;
            }
            TopoDS_Edge e = BRepBuilderAPI_MakeEdge(circle(c.center, c.radius)).Edge();
            if (loop[0].reversed) e.Reverse();
            made.push_back(e);
            return BRepBuilderAPI_MakeWire(e).Wire();
        }
        // Where each edge starts and ends along the loop, and the shared vertices.
        const std::size_t n = loop.size();
        std::vector<gp_Pnt> from(n), to(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto ends = endsOf(loop[i], why);
            if (!ends) return std::nullopt;
            from[i] = ends->first;
            to[i] = ends->second;
        }
        std::vector<TopoDS_Vertex> joints(n);  // joints[i]: where edge i starts (and edge i-1 ends)
        for (std::size_t i = 0; i < n; ++i) {
            const gp_Pnt& a = to[(i + n - 1) % n];
            const gp_Pnt& b = from[i];
            const double gap = a.Distance(b);
            if (gap > kJoin) {
                why = "The outline isn't closed: two of its pieces end " + std::to_string(gap) +
                      " mm apart. Join their ends in the sketch.";
                return std::nullopt;
            }
            joints[i] = BRepBuilderAPI_MakeVertex(gp_Pnt((a.XYZ() + b.XYZ()) / 2)).Vertex();
            if (gap > Precision::Confusion()) BRep_Builder().UpdateVertex(joints[i], gap);
        }
        BRepBuilderAPI_MakeWire wire;
        for (std::size_t i = 0; i < n; ++i) {
            const TopoDS_Vertex& v0 = joints[i];
            const TopoDS_Vertex& v1 = joints[(i + 1) % n];
            const Edge2& e = loop[i];
            // The edge is made the way its curve goes, then flipped if the loop runs backwards.
            const TopoDS_Vertex& first = e.reversed ? v1 : v0;
            const TopoDS_Vertex& last = e.reversed ? v0 : v1;
            TopoDS_Edge edge;
            if (std::holds_alternative<Line2>(e.curve)) {
                BRepBuilderAPI_MakeEdge mk(first, last);
                if (!mk.IsDone()) {
                    why = "A line in the outline has no length.";
                    return std::nullopt;
                }
                edge = mk.Edge();
            } else if (const auto* a = std::get_if<Arc2>(&e.curve)) {
                BRepBuilderAPI_MakeEdge mk(circle(a->center, a->radius), first, last);
                if (!mk.IsDone()) {
                    why = "An arc in the outline couldn't be made.";
                    return std::nullopt;
                }
                edge = mk.Edge();
            } else if (const auto* s = std::get_if<Spline2>(&e.curve)) {
                const auto curve = spline(*s, why);
                if (!curve) return std::nullopt;
                BRepBuilderAPI_MakeEdge mk(curve, first, last);
                if (!mk.IsDone()) {
                    why = "A spline in the outline couldn't be made.";
                    return std::nullopt;
                }
                edge = mk.Edge();
            } else {
                why = "A circle can only be an outline on its own.";
                return std::nullopt;
            }
            if (e.reversed) edge.Reverse();
            wire.Add(edge);
            if (!wire.IsDone()) {
                why = "The pieces of the outline don't join end to end.";
                return std::nullopt;
            }
            made.push_back(wire.Edge());  // as the wire keeps it
        }
        if (!wire.Wire().Closed()) {
            why = "The outline isn't closed.";
            return std::nullopt;
        }
        return wire.Wire();
    }

    gp_Pnt at(Point2 p) const {
        const auto& o = plane_.origin;
        const auto& x = plane_.xAxis;
        const auto& y = plane_.yAxis;
        return {o.x + x.x * p.x + y.x * p.y, o.y + x.y * p.x + y.y * p.y, o.z + x.z * p.x + y.z * p.y};
    }

private:
    static constexpr double kSmall = 1e-9;  // a size smaller than this is no size (mm)
    static constexpr double kJoin = 1e-5;   // ends closer than this join (mm)

    gp_Circ circle(Point2 center, double radius) const {
        // Its angle 0 is the plane's x direction, and it goes counterclockwise seen
        // from the plane's normal, like an Arc2.
        return gp_Circ(gp_Ax2(at(center), axes_.Direction(), axes_.XDirection()), radius);
    }

    // A spline as an OCCT B-spline curve in the plane, with the knots Spline2
    // describes: clamped (end knots repeated degree + 1 times) for an open one,
    // periodic for a closed one. Null, with why, if it can't be one.
    Handle(Geom_BSplineCurve) spline(const Spline2& s, std::string& why) const {
        const int n = static_cast<int>(s.poles.size());
        const int p = s.degree;
        if (p < 1 || p > 5 || n < p + 1) {
            why =
                "A spline in the outline needs a degree of 1 to 5 and at least one more control point than "
                "its degree.";
            return {};
        }
        if (!s.weights.empty() &&
            (static_cast<int>(s.weights.size()) != n ||
             std::any_of(s.weights.begin(), s.weights.end(), [](double w) { return !(w > 0); }))) {
            why = "A spline in the outline needs one weight more than 0 for each control point.";
            return {};
        }
        TColgp_Array1OfPnt poles(1, n);
        TColStd_Array1OfReal weights(1, n);
        for (int i = 0; i < n; ++i) {
            poles.SetValue(i + 1, at(s.poles[static_cast<std::size_t>(i)]));
            weights.SetValue(i + 1, s.weights.empty() ? 1.0 : s.weights[static_cast<std::size_t>(i)]);
        }
        const int count = s.closed ? n + 1 : n - p + 1;  // distinct knots: 0, 1, ...
        TColStd_Array1OfReal knots(1, count);
        TColStd_Array1OfInteger mults(1, count);
        for (int i = 1; i <= count; ++i) {
            knots.SetValue(i, i - 1);
            mults.SetValue(i, !s.closed && (i == 1 || i == count) ? p + 1 : 1);
        }
        try {
            return new Geom_BSplineCurve(poles, weights, knots, mults, p, s.closed);
        } catch (const Standard_Failure& e) {
            why = "A spline in the outline couldn't be made (" + describe(e) + ").";
            return {};
        }
    }

    // Where an edge starts and ends, walking the loop's way.
    std::optional<std::pair<gp_Pnt, gp_Pnt>> endsOf(const Edge2& e, std::string& why) const {
        gp_Pnt s, t;
        if (const auto* l = std::get_if<Line2>(&e.curve)) {
            s = at(l->start);
            t = at(l->end);
            if (s.Distance(t) <= kSmall) {
                why = "A line in the outline has no length.";
                return std::nullopt;
            }
        } else if (const auto* a = std::get_if<Arc2>(&e.curve)) {
            if (!(a->radius > kSmall) || !(a->endAngle > a->startAngle)) {
                why = "An arc in the outline has no size.";
                return std::nullopt;
            }
            s = at({a->center.x + a->radius * std::cos(a->startAngle),
                    a->center.y + a->radius * std::sin(a->startAngle)});
            t = at({a->center.x + a->radius * std::cos(a->endAngle),
                    a->center.y + a->radius * std::sin(a->endAngle)});
        } else if (const auto* sp = std::get_if<Spline2>(&e.curve); sp && !sp->closed) {
            if (sp->poles.size() < 2) {
                why = "A spline in the outline has too few control points.";
                return std::nullopt;
            }
            // A clamped spline starts at its first pole and ends at its last.
            s = at(sp->poles.front());
            t = at(sp->poles.back());
            if (s.Distance(t) <= kSmall) {
                why = "A spline that ends where it starts must be a closed spline on its own.";
                return std::nullopt;
            }
        } else if (std::holds_alternative<Spline2>(e.curve)) {
            why = "A closed spline can only be an outline on its own.";
            return std::nullopt;
        } else {
            why = "A circle can only be an outline on its own.";
            return std::nullopt;
        }
        if (e.reversed) std::swap(s, t);
        return std::pair{s, t};
    }

    Plane plane_;
    gp_Ax3 axes_;
};

// A copy of a face as a B-spline surface, each knot span cut into 8 (the same
// shape), so OCCT's surface integration measures it accurately (see massProps()).
TopoDS_Shape finelySplit(const TopoDS_Face& face) {
    TopoDS_Shape copy = BRepBuilderAPI_NurbsConvert(face).Shape();
    BRep_Builder b;
    for (TopExp_Explorer x(copy, TopAbs_FACE); x.More(); x.Next()) {
        // Forward, so a seam's two curves are read and written the same way round
        // (OCCT swaps them for a reversed face).
        TopoDS_Face f = TopoDS::Face(x.Current().Oriented(TopAbs_FORWARD));
        TopLoc_Location loc;
        Handle(Geom_BSplineSurface) g = Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(f, loc));
        if (g.IsNull()) continue;
        g = Handle(Geom_BSplineSurface)::DownCast(g->Copy());
        constexpr int kPieces = 8;
        for (const bool u : {true, false}) {
            std::vector<double> at;
            const int n = u ? g->NbUKnots() : g->NbVKnots();
            for (int k = 1; k < n; ++k) {
                const double a = u ? g->UKnot(k) : g->VKnot(k), c = u ? g->UKnot(k + 1) : g->VKnot(k + 1);
                for (int j = 1; j < kPieces; ++j) at.push_back(a + (c - a) * j / kPieces);
            }
            for (double t : at) {
                if (u)
                    g->InsertUKnot(t, 1, Precision::PConfusion());
                else
                    g->InsertVKnot(t, 1, Precision::PConfusion());
            }
        }
        // The edges' curves on the face stay right (nothing moved), but they're
        // stored per surface, so they're given again for the new one.
        struct OnFace {
            TopoDS_Edge edge;
            Handle(Geom2d_Curve) curve, other;  // `other` for a seam (an edge on the face twice)
            double first, last;
        };
        std::vector<OnFace> curves;
        TopTools_IndexedMapOfShape edges;  // each edge once, though a seam is on the face twice
        TopExp::MapShapes(f, TopAbs_EDGE, edges);
        for (int k = 1; k <= edges.Extent(); ++k) {
            // Forward, then reversed for a seam's second curve, as UpdateEdge() takes them.
            const TopoDS_Edge e = TopoDS::Edge(edges(k).Oriented(TopAbs_FORWARD));
            OnFace o{e, {}, {}, 0, 0};
            o.curve = BRep_Tool::CurveOnSurface(e, f, o.first, o.last);
            if (BRep_Tool::IsClosed(e, f)) {
                double a = 0, c = 0;
                o.other = BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Reversed()), f, a, c);
            }
            curves.push_back(o);
        }
        b.UpdateFace(f, g, loc, BRep_Tool::Tolerance(f));
        for (const auto& o : curves) {
            if (o.curve.IsNull()) continue;
            if (o.other.IsNull())
                b.UpdateEdge(o.edge, o.curve, f, BRep_Tool::Tolerance(o.edge));
            else
                b.UpdateEdge(o.edge, o.curve, o.other, f, BRep_Tool::Tolerance(o.edge));
            b.Range(o.edge, f, o.first, o.last);
        }
    }
    return copy;
}

// ---------------------------------------------------------------------------
// Faces, edges and vertices
// ---------------------------------------------------------------------------

// A shape's faces, edges and vertices, numbered as Elements numbers them (OCCT
// counts from 1, Elements from 0).
struct Maps {
    TopTools_IndexedMapOfShape faces, edges, vertices;

    explicit Maps(const TopoDS_Shape& s) {
        TopExp::MapShapes(s, TopAbs_FACE, faces);
        TopExp::MapShapes(s, TopAbs_EDGE, edges);
        TopExp::MapShapes(s, TopAbs_VERTEX, vertices);
    }

    const TopTools_IndexedMapOfShape& of(TopAbs_ShapeEnum type) const {
        return type == TopAbs_FACE ? faces : type == TopAbs_EDGE ? edges : vertices;
    }
};

Vec3 vec(const gp_Dir& d) {
    return {d.X(), d.Y(), d.Z()};
}

Point3 point(const gp_Pnt& p) {
    return {p.X(), p.Y(), p.Z()};
}

ElementInfo faceInfo(const TopoDS_Face& face) {
    ElementInfo info;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    info.size = props.Mass();
    info.center = point(props.CentreOfMass());
    const BRepAdaptor_Surface surface(face, Standard_False);
    switch (surface.GetType()) {
        case GeomAbs_Plane: {
            info.geom = GeomKind::Plane;
            gp_Dir n = surface.Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) n.Reverse();  // outward
            info.direction = vec(n);
            break;
        }
        case GeomAbs_Cylinder:
            info.geom = GeomKind::Cylinder;
            info.direction = vec(surface.Cylinder().Axis().Direction());
            info.radius = surface.Cylinder().Radius();
            break;
        case GeomAbs_Cone:
            info.geom = GeomKind::Cone;
            info.direction = vec(surface.Cone().Axis().Direction());
            break;
        case GeomAbs_Sphere:
            info.geom = GeomKind::Sphere;
            info.radius = surface.Sphere().Radius();
            break;
        case GeomAbs_Torus:
            info.geom = GeomKind::Torus;
            info.direction = vec(surface.Torus().Axis().Direction());
            info.radius = surface.Torus().MinorRadius();  // a rounded edge's radius
            break;
        case GeomAbs_BSplineSurface:
        case GeomAbs_BezierSurface:
            info.geom = GeomKind::Spline;
            break;
        default:
            info.geom = GeomKind::Other;
            break;
    }
    return info;
}

ElementInfo edgeInfo(const TopoDS_Edge& edge) {
    ElementInfo info;
    if (BRep_Tool::Degenerated(edge)) return info;  // a point, like a cone's tip
    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);
    info.size = props.Mass();
    info.center = point(props.CentreOfMass());
    const BRepAdaptor_Curve curve(edge);
    switch (curve.GetType()) {
        case GeomAbs_Line:
            info.geom = GeomKind::Line;
            info.direction = vec(curve.Line().Direction());
            break;
        case GeomAbs_Circle:
            info.geom = GeomKind::Circle;
            info.direction = vec(curve.Circle().Axis().Direction());
            info.radius = curve.Circle().Radius();
            break;
        case GeomAbs_Ellipse:
            info.geom = GeomKind::Ellipse;
            info.direction = vec(curve.Ellipse().Axis().Direction());
            break;
        case GeomAbs_BSplineCurve:
        case GeomAbs_BezierCurve:
            info.geom = GeomKind::Spline;
            break;
        default:
            info.geom = GeomKind::Other;
            break;
    }
    return info;
}

// Numbers (from 0) of the shapes in `list` that are in `map`, each once, smallest first.
std::vector<int> numbersIn(const TopTools_ListOfShape& list, const TopTools_IndexedMapOfShape& map) {
    std::set<int> out;
    for (const auto& s : list) {
        const int i = map.FindIndex(s);
        if (i > 0) out.insert(i - 1);
    }
    return {out.begin(), out.end()};
}

// Fills in where each face of `result` came from in `input` (input number
// `which`), from an OCCT operation's own record: faces it kept, changed or made
// from an edge or a vertex.
void traceFaces(BRepBuilderAPI_MakeShape& mk, const TopoDS_Shape& input, int which, const Maps& result,
                History& history) {
    history.faces.resize(static_cast<std::size_t>(result.faces.Extent()));
    auto add = [&](const TopoDS_Shape& made, OriginKind kind, int index) {
        if (made.ShapeType() != TopAbs_FACE) return;
        const int at = result.faces.FindIndex(made);
        if (at == 0) return;
        auto& origins = history.faces[static_cast<std::size_t>(at - 1)];
        const Origin o{which, kind, index};
        const bool seen = std::any_of(origins.begin(), origins.end(), [&](const Origin& x) {
            return x.input == o.input && x.kind == o.kind && x.index == o.index;
        });
        if (!seen) origins.push_back(o);
    };
    const Maps in(input);
    for (int i = 1; i <= in.faces.Extent(); ++i) {
        const TopoDS_Shape& f = in.faces(i);
        if (mk.IsDeleted(f)) continue;
        const TopTools_ListOfShape& changed = mk.Modified(f);
        if (changed.IsEmpty()) {
            add(f, OriginKind::Face, i - 1);  // kept as it was
        } else {
            for (const auto& m : changed) add(m, OriginKind::Face, i - 1);
        }
    }
    for (const auto& [type, kind] :
         {std::pair{TopAbs_EDGE, OriginKind::Edge}, std::pair{TopAbs_VERTEX, OriginKind::Vertex}}) {
        const auto& map = in.of(type);
        for (int i = 1; i <= map.Extent(); ++i) {
            for (const auto& g : mk.Generated(map(i))) add(g, kind, i - 1);
        }
    }
}

// A copy of `shape` with each of `edges` (numbers from 0) rounded or beveled.
template <typename Maker>
OpResult blend(const TopoDS_Shape* s, const std::vector<int>& edges, double size, const char* what,
               const std::string& code) {
    const std::string name(what);
    if (!s) return failed(code + ".input", name + " needs a valid body.");
    if (edges.empty())
        return failed(code + ".edges", name + " needs at least one edge. Pick the edges to change.");
    if (!(size > 0)) {
        return failed(code + ".size", name == "Fillet"
                                          ? "A fillet needs a radius greater than 0 mm, like 2."
                                          : "A chamfer needs a distance greater than 0 mm, like 1.");
    }
    try {
        const Maps in(*s);
        Maker mk(*s);
        for (const int e : edges) {
            if (e < 0 || e >= in.edges.Extent()) {
                return failed(code + ".edge",
                              name + " uses an edge that isn't on the body. Pick the edges again.");
            }
            mk.Add(size, TopoDS::Edge(in.edges(e + 1)));
        }
        mk.Build();
        if (!mk.IsDone() || mk.Shape().IsNull()) {
            return failed(code + ".failed",
                          name == "Fillet"
                              ? "The edges couldn't be rounded. The radius may be too big for the faces "
                                "next to them: try a smaller one."
                              : "The edges couldn't be beveled. The distance may be too big for the faces "
                                "next to them: try a smaller one.");
        }
        OpResult out{wrap(mk.Shape()), {}, {}};
        traceFaces(mk, *s, 0, Maps(mk.Shape()), out.history);
        return out;
    } catch (const Standard_Failure& e) {
        return failed(code + ".failed",
                      name == "Fillet" ? "The edges couldn't be rounded. Try a smaller radius."
                                       : "The edges couldn't be beveled. Try a smaller distance.",
                      describe(e));
    }
}

class OcctKernel final : public IKernel {
public:
    OcctKernel() { silenceOcctConsole(); }

    OpResult makeBox(const Point3& c, double dx, double dy, double dz) override {
        if (dx <= 0 || dy <= 0 || dz <= 0) {
            return failed("kernel.box.size", "A box needs a width, depth and height greater than 0 mm.");
        }
        try {
            BRepPrimAPI_MakeBox mk(gp_Pnt(c.x, c.y, c.z), dx, dy, dz);
            return {wrap(mk.Shape()), {}, {}};
        } catch (const Standard_Failure& e) {
            return failed("kernel.box.failed", "The box could not be created.", describe(e));
        }
    }

    OpResult makeCylinder(const Point3& base, const Vec3& axis, double radius, double height) override {
        if (radius <= 0 || height <= 0) {
            return failed("kernel.cylinder.size", "A cylinder needs a radius and height greater than 0 mm.");
        }
        if (axis.x * axis.x + axis.y * axis.y + axis.z * axis.z < 1e-18) {
            return failed(
                "kernel.cylinder.axis",
                "A cylinder's axis can't be 0, 0, 0. Give it a direction, like 0, 0, 1 for straight up.");
        }
        try {
            gp_Ax2 ax(gp_Pnt(base.x, base.y, base.z), gp_Dir(axis.x, axis.y, axis.z));
            BRepPrimAPI_MakeCylinder mk(ax, radius, height);
            return {wrap(mk.Shape()), {}, {}};
        } catch (const Standard_Failure& e) {
            return failed("kernel.cylinder.failed", "The cylinder could not be created.", describe(e));
        }
    }

    OpResult transform(const ShapeHandle& shape, const Motion& motion) override {
        const TopoDS_Shape* s = unwrap(shape);
        if (!s) return failed("kernel.move.input", "Move needs a valid body.");
        const Vec3& a = motion.axis;
        if (motion.angle != 0 && a.x * a.x + a.y * a.y + a.z * a.z < 1e-18) {
            return failed(
                "kernel.move.axis",
                "The axis to turn about can't be 0, 0, 0. Give it a direction, like 0, 0, 1 to turn "
                "around an up-and-down line.");
        }
        try {
            gp_Trsf turn;
            if (motion.angle != 0) {
                turn.SetRotation(
                    gp_Ax1(gp_Pnt(motion.center.x, motion.center.y, motion.center.z), gp_Dir(a.x, a.y, a.z)),
                    motion.angle);
            }
            gp_Trsf slide;
            slide.SetTranslation(gp_Vec(motion.offset.x, motion.offset.y, motion.offset.z));
            // Copy the geometry, so the result has no hidden placement (cleaner meshes and files).
            BRepBuilderAPI_Transform mk(*s, slide * turn, Standard_True);
            if (!mk.IsDone()) return failed("kernel.move.failed", "The body could not be moved.");
            OpResult out{wrap(mk.Shape()), {}, {}};
            traceFaces(mk, *s, 0, Maps(mk.Shape()), out.history);
            return out;
        } catch (const Standard_Failure& e) {
            return failed("kernel.move.failed", "The body could not be moved.", describe(e));
        }
    }

    OpResult makeFace(const Profile2& profile, const Plane& plane) override {
        const Vec3& x = plane.xAxis;
        const Vec3& y = plane.yAxis;
        const double lx = std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
        const double ly = std::sqrt(y.x * y.x + y.y * y.y + y.z * y.z);
        if (lx < 1e-9 || ly < 1e-9 || std::abs(x.x * y.x + x.y * y.y + x.z * y.z) > 1e-9 * lx * ly) {
            return failed(
                "kernel.face.plane",
                "A sketch plane's x and y directions must be square to each other and not 0, 0, 0.");
        }
        try {
            const gp_Dir dx(x.x, x.y, x.z), dy(y.x, y.y, y.z);
            const gp_Ax3 axes(gp_Pnt(plane.origin.x, plane.origin.y, plane.origin.z), dx.Crossed(dy), dx);
            const WireMaker wires(plane, axes);
            std::string why;
            std::vector<TopoDS_Edge> made;  // one per curve: the outline's, then each hole's
            const auto outer = wires.make(profile.outer, why, made);
            if (!outer) return failed("kernel.face.outline", why + " (the outside of the shape)");
            BRepBuilderAPI_MakeFace mk(gp_Pln(axes), *outer, Standard_True);
            if (!mk.IsDone()) return failed("kernel.face.failed", "The shape's face couldn't be made.");
            for (const auto& hole : profile.holes) {
                const auto w = wires.make(hole, why, made);
                if (!w) return failed("kernel.face.outline", why + " (a hole in the shape)");
                mk.Add(*w);
            }
            const TopoDS_Face face = mk.Face();
            if (!BRepCheck_Analyzer(face).IsValid()) {
                return failed(
                    "kernel.face.invalid",
                    "The shape's face isn't valid: its outline may cross itself, or a hole may poke "
                    "outside it. Check the sketch.");
            }
            // Each edge of the face is one of the curves it was made from.
            OpResult out{wrap(face), {}, {}};
            const Maps result(face);
            out.history.faces.resize(1);
            out.history.edges.resize(static_cast<std::size_t>(result.edges.Extent()));
            for (std::size_t k = 0; k < made.size(); ++k) {
                const int at = result.edges.FindIndex(made[k]);
                if (at > 0) {
                    out.history.edges[static_cast<std::size_t>(at - 1)].push_back(
                        {0, OriginKind::Curve, static_cast<int>(k)});
                }
            }
            return out;
        } catch (const Standard_Failure& e) {
            return failed("kernel.face.failed", "The shape's face couldn't be made.", describe(e));
        }
    }

    OpResult extrude(const ShapeHandle& face, const Vec3& v) override {
        const TopoDS_Shape* s = unwrap(face);
        if (!s || s->ShapeType() != TopAbs_FACE)
            return failed("kernel.extrude.input", "Extrude needs a flat face.");
        const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length < 1e-9) {
            return failed("kernel.extrude.distance", "Extrude needs a distance that isn't 0 mm.");
        }
        try {
            // Sweeping along the face (not out of it) makes no solid.
            BRepGProp_Face props(TopoDS::Face(*s));
            Standard_Real u0 = 0, u1 = 0, v0 = 0, v1 = 0;
            props.Bounds(u0, u1, v0, v1);
            gp_Pnt p;
            gp_Vec normal;
            props.Normal((u0 + u1) / 2, (v0 + v1) / 2, p, normal);
            if (normal.Magnitude() > 0 &&
                std::abs(normal.Normalized().Dot(gp_Vec(v.x, v.y, v.z)) / length) < 1e-6) {
                return failed(
                    "kernel.extrude.direction",
                    "Extrude can't sweep a shape along its own flat side: give it a direction out of "
                    "the sketch.");
            }
            BRepPrimAPI_MakePrism mk(*s, gp_Vec(v.x, v.y, v.z), Standard_True);
            if (!mk.IsDone()) return failed("kernel.extrude.failed", "The shape couldn't be extruded.");
            OpResult out{wrap(mk.Shape()), {}, {}};
            const Maps result(mk.Shape());
            traceFaces(mk, *s, 0, result, out.history);  // the sides, from the face's edges
            for (const auto& [end, kind] : {std::pair{mk.FirstShape(), OriginKind::Start},
                                            std::pair{mk.LastShape(), OriginKind::End}}) {
                const int at = end.IsNull() ? 0 : result.faces.FindIndex(end);
                if (at > 0) out.history.faces[static_cast<std::size_t>(at - 1)] = {{0, kind, -1}};
            }
            return out;
        } catch (const Standard_Failure& e) {
            return failed("kernel.extrude.failed", "The shape couldn't be extruded.", describe(e));
        }
    }

    OpResult boolean(BoolOp op, const ShapeHandle& a, const ShapeHandle& b) override {
        const TopoDS_Shape* sa = unwrap(a);
        const TopoDS_Shape* sb = unwrap(b);
        if (!sa || !sb) {
            return failed("kernel.boolean.input", "A Boolean needs two valid bodies.");
        }
        try {
            auto run = [&](BRepAlgoAPI_BooleanOperation& alg) -> OpResult {
                if (!alg.IsDone() || alg.Shape().IsNull()) {
                    return failed(
                        "kernel.boolean.failed",
                        "These bodies could not be combined. Check that they overlap and are closed solids.");
                }
                OpResult out{wrap(alg.Shape()), {}, {}};
                const Maps result(alg.Shape());
                traceFaces(alg, *sa, 0, result, out.history);
                traceFaces(alg, *sb, 1, result, out.history);
                return out;
            };
            switch (op) {
                case BoolOp::Union: {
                    BRepAlgoAPI_Fuse alg(*sa, *sb);
                    return run(alg);
                }
                case BoolOp::Cut: {
                    BRepAlgoAPI_Cut alg(*sa, *sb);
                    return run(alg);
                }
                case BoolOp::Intersect: {
                    BRepAlgoAPI_Common alg(*sa, *sb);
                    return run(alg);
                }
            }
            return failed("kernel.boolean.failed", "These bodies could not be combined.");
        } catch (const Standard_Failure& e) {
            return failed("kernel.boolean.failed", "These bodies could not be combined.", describe(e));
        }
    }

    Box3 bounds(const ShapeHandle& h) const override {
        Box3 out;
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return out;
        Bnd_Box box;
        // Tight bounds from the exact geometry, without tolerance padding.
        BRepBndLib::AddOptimal(*s, box, Standard_False, Standard_False);
        if (box.IsVoid()) return out;
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        out.min = {x0, y0, z0};
        out.max = {x1, y1, z1};
        return out;
    }

    MassProps massProps(const ShapeHandle& h) const override {
        MassProps out;
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return out;
        // Integrated to a relative error of 1e-9 (volumes by Gauss-Kronrod): OCCT's quick default
        // is exact for flat and round faces, but off by up to 1e-3 on free-form
        // (spline) ones, like a solid with a closed spline hole.
        GProp_GProps vol;
        BRepGProp::VolumePropertiesGK(*s, vol, 1e-9);
        // Areas face by face. OCCT's surface integration is exact for flat and
        // round faces, but on free-form ones it uses a fixed number of points per
        // knot span (or all of a swept surface at once), which misses kinks and
        // sudden bends: 1% off on an extruded degree 1 spline, 1e-4 on a weighted
        // one, even asked for 1e-12. So a free-form face is measured as a copy cut
        // into B-spline pieces 8 times shorter, the same shape, which comes out
        // right to 1e-9. Only the measuring copy changes.
        double area = 0;
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(*s, TopAbs_FACE, faces);
        for (int i = 1; i <= faces.Extent(); ++i) {
            const TopoDS_Face& face = TopoDS::Face(faces(i));
            const auto type = BRepAdaptor_Surface(face, Standard_False).GetType();
            const bool exact = type == GeomAbs_Plane || type == GeomAbs_Cylinder || type == GeomAbs_Cone ||
                               type == GeomAbs_Sphere || type == GeomAbs_Torus;
            GProp_GProps surf;
            BRepGProp::SurfaceProperties(exact ? TopoDS_Shape(face) : finelySplit(face), surf, 1e-9);
            area += surf.Mass();
        }
        gp_Pnt c = vol.CentreOfMass();
        out.volume = vol.Mass();
        out.area = area;
        out.centerOfMass = {c.X(), c.Y(), c.Z()};
        return out;
    }

    Topology topology(const ShapeHandle& h) const override {
        Topology out;
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return out;
        auto count = [s](TopAbs_ShapeEnum type) {
            TopTools_IndexedMapOfShape map;
            TopExp::MapShapes(*s, type, map);
            return map.Extent();
        };
        out.solids = count(TopAbs_SOLID);
        out.shells = count(TopAbs_SHELL);
        out.faces = count(TopAbs_FACE);
        out.edges = count(TopAbs_EDGE);
        out.vertices = count(TopAbs_VERTEX);
        return out;
    }

    Diagnostics validate(const ShapeHandle& h) const override {
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return {error("shape.missing", "There is no body to check.")};
        Diagnostics out;
        try {
            BRepCheck_Analyzer analyzer(*s);
            if (!analyzer.IsValid()) {
                out.push_back(error("shape.invalid",
                                    "This body has a geometry problem. Try undoing the last step or "
                                    "changing its sizes slightly."));
            }
            if (topology(h).solids == 0) {
                out.push_back(error("shape.not_solid", "This is not a solid body, so it has no inside."));
            }
            for (TopExp_Explorer ex(*s, TopAbs_SHELL); ex.More(); ex.Next()) {
                if (!BRep_Tool::IsClosed(ex.Current())) {
                    out.push_back(
                        error("shape.open", "This body has a gap in its surface, so it is not watertight."));
                    break;
                }
            }
        } catch (const Standard_Failure& e) {
            out.push_back(error("shape.check_failed", "The body could not be checked.", describe(e)));
        }
        return out;
    }

    Mesh tessellate(const ShapeHandle& h, const MeshQuality& q) const override {
        Mesh mesh;
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return mesh;
        BRepMesh_IncrementalMesh mesher(*s, q.linearDeflection, Standard_False, q.angularDeflection);
        for (TopExp_Explorer ex(*s, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face& face = TopoDS::Face(ex.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;
            const gp_Trsf trsf = loc.Transformation();
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i) {
                gp_Pnt p = tri->Node(i).Transformed(trsf);
                mesh.vertices.push_back(
                    {static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())});
            }
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i) {
                Standard_Integer n1, n2, n3;
                tri->Triangle(i).Get(n1, n2, n3);
                if (reversed) std::swap(n2, n3);
                mesh.triangles.push_back({base + static_cast<std::uint32_t>(n1 - 1),
                                          base + static_cast<std::uint32_t>(n2 - 1),
                                          base + static_cast<std::uint32_t>(n3 - 1)});
            }
        }
        return mesh;
    }

    Diagnostics exportFile(const ShapeHandle& h, ExportFormat format,
                           const std::filesystem::path& path) const override {
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return {error("export.input", "There is no body to export.")};
        const std::string file = utf8(path);
        try {
            switch (format) {
                case ExportFormat::Stl: {
                    BRepMesh_IncrementalMesh mesher(*s, 0.05, Standard_False, 0.35);
                    StlAPI_Writer writer;
                    writer.ASCIIMode() = Standard_False;
                    if (!writer.Write(*s, file.c_str())) {
                        return {error("export.stl.failed", "The STL file could not be written.", file)};
                    }
                    return {};
                }
                case ExportFormat::Step: {
                    STEPControl_Writer writer;
                    if (writer.Transfer(*s, STEPControl_AsIs) != IFSelect_RetDone ||
                        writer.Write(file.c_str()) != IFSelect_RetDone) {
                        return {error("export.step.failed", "The STEP file could not be written.", file)};
                    }
                    return {};
                }
            }
        } catch (const Standard_Failure& e) {
            return {error("export.failed", "The file could not be exported.", describe(e))};
        }
        return {error("export.format", "That file type is not supported.")};
    }

    Elements elements(const ShapeHandle& h) const override {
        Elements out;
        const TopoDS_Shape* s = unwrap(h);
        if (!s) return out;
        try {
            const Maps maps(*s);
            for (int i = 1; i <= maps.faces.Extent(); ++i)
                out.faces.push_back(faceInfo(TopoDS::Face(maps.faces(i))));
            TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
            TopExp::MapShapesAndAncestors(*s, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
            for (int i = 1; i <= maps.edges.Extent(); ++i) {
                const TopoDS_Edge& e = TopoDS::Edge(maps.edges(i));
                out.edges.push_back(edgeInfo(e));
                const int at = edgeToFaces.FindIndex(e);
                out.edgeFaces.push_back(at > 0 ? numbersIn(edgeToFaces(at), maps.faces) : std::vector<int>{});
                TopTools_ListOfShape ends;
                TopoDS_Vertex v0, v1;
                TopExp::Vertices(e, v0, v1);
                if (!v0.IsNull()) ends.Append(v0);
                if (!v1.IsNull()) ends.Append(v1);
                out.edgeVertices.push_back(numbersIn(ends, maps.vertices));
            }
            for (int i = 1; i <= maps.vertices.Extent(); ++i) {
                ElementInfo v;
                v.geom = GeomKind::Point;
                v.center = point(BRep_Tool::Pnt(TopoDS::Vertex(maps.vertices(i))));
                out.vertices.push_back(v);
            }
        } catch (const Standard_Failure&) {
            return {};  // a shape OCCT can't measure has no elements to offer
        }
        return out;
    }

    OpResult fillet(const ShapeHandle& h, const std::vector<int>& edges, double radius) override {
        return blend<BRepFilletAPI_MakeFillet>(unwrap(h), edges, radius, "Fillet", "kernel.fillet");
    }

    OpResult chamfer(const ShapeHandle& h, const std::vector<int>& edges, double distance) override {
        return blend<BRepFilletAPI_MakeChamfer>(unwrap(h), edges, distance, "Chamfer", "kernel.chamfer");
    }
};

}  // namespace

const TopoDS_Shape* occtShape(const ShapeHandle& shape) {
    return unwrap(shape);
}

std::unique_ptr<IKernel> makeOcctKernel() {
    return std::make_unique<OcctKernel>();
}

}  // namespace sb::kernel
