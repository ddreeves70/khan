#include "kh/kernel_occt/OcctKernel.h"
#include "kh/kernel_occt/OcctShape.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
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
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace kh::kernel {

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
// console by default. Khan reports problems through Diagnostics instead, so the
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
    std::optional<TopoDS_Wire> make(const Loop2& loop, std::string& why) const {
        if (loop.empty()) {
            why = "An outline has no lines, arcs or circles in it.";
            return std::nullopt;
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

class OcctKernel final : public IKernel {
public:
    OcctKernel() { silenceOcctConsole(); }

    OpResult makeBox(const Point3& c, double dx, double dy, double dz) override {
        if (dx <= 0 || dy <= 0 || dz <= 0) {
            return failed("kernel.box.size", "A box needs a width, depth and height greater than 0 mm.");
        }
        try {
            BRepPrimAPI_MakeBox mk(gp_Pnt(c.x, c.y, c.z), dx, dy, dz);
            return {wrap(mk.Shape()), {}};
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
            return {wrap(mk.Shape()), {}};
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
            return {wrap(mk.Shape()), {}};
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
            const auto outer = wires.make(profile.outer, why);
            if (!outer) return failed("kernel.face.outline", why + " (the outside of the shape)");
            BRepBuilderAPI_MakeFace mk(gp_Pln(axes), *outer, Standard_True);
            if (!mk.IsDone()) return failed("kernel.face.failed", "The shape's face couldn't be made.");
            for (const auto& hole : profile.holes) {
                const auto w = wires.make(hole, why);
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
            return {wrap(face), {}};
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
            return {wrap(mk.Shape()), {}};
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
            TopoDS_Shape result;
            bool done = false;
            switch (op) {
                case BoolOp::Union: {
                    BRepAlgoAPI_Fuse alg(*sa, *sb);
                    done = alg.IsDone();
                    if (done) result = alg.Shape();
                    break;
                }
                case BoolOp::Cut: {
                    BRepAlgoAPI_Cut alg(*sa, *sb);
                    done = alg.IsDone();
                    if (done) result = alg.Shape();
                    break;
                }
                case BoolOp::Intersect: {
                    BRepAlgoAPI_Common alg(*sa, *sb);
                    done = alg.IsDone();
                    if (done) result = alg.Shape();
                    break;
                }
            }
            if (!done || result.IsNull()) {
                return failed(
                    "kernel.boolean.failed",
                    "These bodies could not be combined. Check that they overlap and are closed solids.");
            }
            return {wrap(result), {}};
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
        GProp_GProps vol;
        BRepGProp::VolumeProperties(*s, vol);
        GProp_GProps surf;
        BRepGProp::SurfaceProperties(*s, surf);
        gp_Pnt c = vol.CentreOfMass();
        out.volume = vol.Mass();
        out.area = surf.Mass();
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
};

}  // namespace

const TopoDS_Shape* occtShape(const ShapeHandle& shape) {
    return unwrap(shape);
}

std::unique_ptr<IKernel> makeOcctKernel() {
    return std::make_unique<OcctKernel>();
}

}  // namespace kh::kernel
