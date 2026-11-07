#pragma once

/// @file
/// Value types that cross the kernel boundary. None of them mention OCCT.

#include <array>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "sb/core/Diagnostic.h"

/// Geometry kernel interface and its value types.
///
/// Lengths are millimeters and angles are radians everywhere inside the kernel layer.
namespace sb::kernel {

/// A 3D vector or point, in millimeters.
struct Vec3 {
    double x = 0;  ///< X component (mm).
    double y = 0;  ///< Y component (mm).
    double z = 0;  ///< Z component (mm).
};
/// A 3D point, in millimeters. Same type as Vec3; the name says how it is used.
using Point3 = Vec3;

/// A rigid move: first turn by `angle` about the line through `center` along
/// `axis`, then slide by `offset`. Shapes keep their size and shape.
struct Motion {
    Vec3 offset{};       ///< How far to slide, along x, y and z (mm).
    Point3 center{};     ///< A point on the line to turn about (mm).
    Vec3 axis{0, 0, 1};  ///< The direction of that line; any length except zero when `angle` isn't 0.
    double angle = 0;    ///< How far to turn (radians), counterclockwise looking down `axis` toward `center`.
};

/// A point on a flat plane, in the plane's own x and y (mm).
struct Point2 {
    double x = 0;  ///< Along the plane's x direction (mm).
    double y = 0;  ///< Along the plane's y direction (mm).
};

/// A flat plane in space. Seen from the side `xAxis` × `yAxis` points to (its
/// normal), the plane's x goes right and its y goes up.
struct Plane {
    Point3 origin{};      ///< Where the plane's x and y are 0 (mm).
    Vec3 xAxis{1, 0, 0};  ///< The plane's x direction (a unit vector).
    Vec3 yAxis{0, 1, 0};  ///< The plane's y direction (a unit vector, square to xAxis).
};

/// A straight piece of a flat outline, from `start` to `end`.
struct Line2 {
    Point2 start{};  ///< Where it starts (mm, in the plane).
    Point2 end{};    ///< Where it ends (mm, in the plane).
};

/// A piece of a circle in a flat outline, going counterclockwise (seen from the
/// plane's normal) from `startAngle` to `endAngle`. Angles are measured from the
/// plane's x direction.
struct Arc2 {
    Point2 center{};        ///< The circle's center (mm, in the plane).
    double radius = 0;      ///< The circle's radius (mm). Must be greater than 0.
    double startAngle = 0;  ///< Where it starts (radians).
    double endAngle = 0;    ///< Where it ends (radians): more than startAngle, by less than a full turn.
};

/// A whole circle in a flat outline (counterclockwise unless reversed).
struct Circle2 {
    Point2 center{};    ///< The center (mm, in the plane).
    double radius = 0;  ///< The radius (mm). Must be greater than 0.
};

/// A B-spline in a flat outline, the same as sb::sketch::SplineShape: its knots
/// are spread evenly. An open one is clamped (it starts at its first pole and ends
/// at its last); a closed one is periodic and makes a loop on its own, like a circle.
struct Spline2 {
    std::vector<Point2> poles{};    ///< Control points (mm, in the plane): at least degree + 1.
    std::vector<double> weights{};  ///< One per pole, each more than 0; empty means all 1.
    int degree = 3;                 ///< 1 to 5.
    bool closed = false;            ///< True for a closed loop.
};

/// One piece of a closed outline, and which way the outline runs along it.
struct Edge2 {
    std::variant<Line2, Arc2, Circle2, Spline2> curve{};  ///< The line, arc, circle or spline.
    bool reversed = false;  ///< True if the outline goes from its end to its start.
};

/// A closed outline: edges joined end to end, the last back to the first. A
/// circle, or a closed spline, is a loop of one edge.
using Loop2 = std::vector<Edge2>;

/// A flat shape for a face: an outline, and holes inside it. The outline goes
/// counterclockwise and the holes clockwise (seen from the plane's normal), as
/// sb::sketch::findProfiles() gives them.
struct Profile2 {
    Loop2 outer{};               ///< The outline.
    std::vector<Loop2> holes{};  ///< Loops cut out of it.
};

/// An axis-aligned bounding box, in millimeters.
struct Box3 {
    Point3 min;  ///< Corner with the smallest x, y and z.
    Point3 max;  ///< Corner with the largest x, y and z.
};

/// Base for a kernel's private shape representation.
///
/// Each kernel adapter derives its own type (the OCCT kernel stores a
/// TopoDS_Shape); nothing outside the adapter ever sees what is inside.
struct ShapeImpl {
    /// Lets adapters delete their derived shape types through this base.
    virtual ~ShapeImpl() = default;
};

/// Opaque, cheap-to-copy handle to an immutable kernel shape.
///
/// Copies share the same shape. A default-constructed handle is empty; see valid().
class ShapeHandle {
public:
    /// Makes an empty handle that refers to no shape.
    ShapeHandle() = default;

    /// Wraps a kernel's shape. Used by kernel adapters only.
    /// @param impl The adapter's shape. Each call gets a new id().
    explicit ShapeHandle(std::shared_ptr<const ShapeImpl> impl);

    /// Checks whether the handle refers to a shape.
    /// @return True if there is a shape.
    bool valid() const { return impl_ != nullptr; }

    /// Gives a number that identifies this handle in the current session.
    /// It is not saved and changes between runs; use it for caching only.
    /// @return The handle's id, or 0 for an empty handle.
    std::uint64_t id() const { return id_; }

    /// Recovers a kernel adapter's own representation. For kernel adapters only.
    /// @tparam T The adapter's ShapeImpl subclass.
    /// @return The shape as @p T, or nullptr if it is empty or of another type.
    template <typename T>
    const T* as() const {
        return dynamic_cast<const T*>(impl_.get());
    }

private:
    std::shared_ptr<const ShapeImpl> impl_;
    std::uint64_t id_ = 0;
};

/// Which Boolean operation to run on two solids.
enum class BoolOp {
    Union,      ///< Join both solids into one.
    Cut,        ///< Remove the second solid from the first.
    Intersect,  ///< Keep only the space both solids fill.
};

/// File formats the kernel can export.
enum class ExportFormat {
    Stl,   ///< Triangle mesh, for 3D printing.
    Step,  ///< Exact geometry (ISO 10303), for other CAD and CAM programs.
};

/// The three kinds of piece a solid is made of.
enum class TopoKind {
    Face,    ///< A bounded surface, like the top of a box.
    Edge,    ///< A curve where faces meet.
    Vertex,  ///< A point where edges meet.
};

/// What kind of surface a face lies on, or what kind of curve an edge follows.
enum class GeomKind {
    Plane,     ///< A flat face.
    Cylinder,  ///< Part of a cylinder's side.
    Cone,      ///< Part of a cone's side.
    Sphere,    ///< Part of a ball.
    Torus,     ///< Part of a ring (a doughnut), like a rounded corner around a curve.
    Line,      ///< A straight edge.
    Circle,    ///< An edge along a circle (a whole circle or an arc).
    Ellipse,   ///< An edge along an ellipse.
    Spline,    ///< A free-form (B-spline) surface or curve.
    Point,     ///< A vertex.
    Other,     ///< Any other surface or curve, like a swept one.
};

/// What one face, edge or vertex looks like: enough to tell it apart from the
/// others, and to find it again after the shape changes a little.
struct ElementInfo {
    GeomKind geom = GeomKind::Other;  ///< What it lies on or follows.
    Point3 center{};                  ///< Its middle: the center of its area or length, or the point (mm).
    /// A flat face's outward normal; a cylinder's, cone's or circle's axis; a line's
    /// direction. A unit vector, or 0, 0, 0 when none of these fits.
    Vec3 direction{};
    double size = 0;    ///< A face's area (mm^2) or an edge's length (mm); 0 for a vertex.
    double radius = 0;  ///< A cylinder's, sphere's or circle's radius (mm); otherwise 0.
};

/// The faces, edges and vertices of a shape, and how they connect.
///
/// Each list is in the kernel's order for this shape, which is the same every
/// time the same steps are built (see IKernel). An element's number is its place in its list,
/// counting from 0. Numbers are only good for this one shape: after any change,
/// use names (`sb::model` naming) to find an element again.
struct Elements {
    std::vector<ElementInfo> faces;     ///< The faces.
    std::vector<ElementInfo> edges;     ///< The edges.
    std::vector<ElementInfo> vertices;  ///< The vertices.
    /// For each edge, the faces it borders, smallest number first: usually two, one
    /// for a seam (an edge where a face meets itself, like down a cylinder's side).
    std::vector<std::vector<int>> edgeFaces;
    /// For each edge, its vertices: two, one for a closed edge like a circle, or none.
    std::vector<std::vector<int>> edgeVertices;

    /// Gives one list by kind.
    /// @param kind Faces, edges or vertices.
    /// @return That list.
    const std::vector<ElementInfo>& of(TopoKind kind) const {
        return kind == TopoKind::Face ? faces : kind == TopoKind::Edge ? edges : vertices;
    }
};

/// Where an operation's input was, for one piece of its result (see History).
enum class OriginKind {
    Face,    ///< A face of an input: kept, or changed (trimmed, split, moved).
    Edge,    ///< An edge of an input: the piece was made from it, like an extrude's side or a fillet's round
             ///< face.
    Vertex,  ///< A vertex of an input: the piece was made from it, like a fillet's corner.
    Curve,   ///< A curve of the outline given to IKernel::makeFace(): outline first, then each hole.
    Start,   ///< An extrude's face where it starts (the face it swept).
    End,     ///< An extrude's face where it ends.
};

/// One place a piece of a result came from.
struct Origin {
    int input = 0;  ///< Which input: 0 for the first shape, 1 for the second (a Boolean's tool).
    OriginKind kind = OriginKind::Face;  ///< What it came from.
    int index = -1;  ///< That face's, edge's or vertex's number in the input's Elements, or the curve's
                     ///< number; -1 for Start and End.
};

/// Where each face and edge of a result came from, so names given to the inputs
/// can be passed on to the result (topological naming).
///
/// A list is empty when the operation doesn't say: primitives (box, cylinder)
/// make new shapes with no inputs.
struct History {
    /// For each face of the result (in Elements order), where it came from; empty
    /// for a face that is new. Several origins mean faces that were merged.
    std::vector<std::vector<Origin>> faces;
    /// The same for each edge of the result. Only IKernel::makeFace() fills it in
    /// (each edge's outline curve); other edges are named from the faces they border.
    std::vector<std::vector<Origin>> edges;
};

/// Result of every kernel operation. Failures carry diagnostics, never exceptions.
struct OpResult {
    ShapeHandle shape;   ///< The new shape; empty if the operation failed.
    Diagnostics issues;  ///< Why it failed, or warnings about a shape that was made.
    History history;     ///< Where the result's faces and edges came from, for operations that say.

    /// Checks whether a shape was made.
    /// @return True if @ref shape is valid.
    bool ok() const { return shape.valid(); }
};

/// How finely to turn exact geometry into triangles.
struct MeshQuality {
    /// Largest distance between the mesh and the true surface (mm).
    double linearDeflection = 0.05;
    /// Largest angle between neighboring triangles on a curve (radians; 0.35 is about 20 degrees).
    double angularDeflection = 0.35;
};

/// A triangle mesh.
struct Mesh {
    /// Vertex positions (mm).
    std::vector<std::array<float, 3>> vertices;
    /// Triangles as three indices into @ref vertices, counter-clockwise seen from outside.
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

/// Number of distinct topological entities in a shape.
///
/// Used by tests and the regression corpus to catch changes in how the kernel builds a shape.
struct Topology {
    int solids = 0;    ///< Closed volumes.
    int shells = 0;    ///< Connected sets of faces.
    int faces = 0;     ///< Bounded surfaces.
    int edges = 0;     ///< Curves where faces meet.
    int vertices = 0;  ///< Points where edges meet.
};

/// Volume, area and center of mass of a shape.
struct MassProps {
    double volume = 0;    ///< Volume (mm^3).
    double area = 0;      ///< Surface area (mm^2).
    Point3 centerOfMass;  ///< Center of mass for uniform density (mm).
};

}  // namespace sb::kernel
