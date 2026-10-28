#include "kh/kernel_occt/OcctKernel.h"
#include "kh/kernel_occt/OcctShape.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <Poly_Triangulation.hxx>
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
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <mutex>
#include <string>
#include <utility>

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
