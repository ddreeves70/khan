#pragma once

/// @file
/// Value types that cross the kernel boundary. None of them mention OCCT.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "kh/core/Diagnostic.h"

/// Geometry kernel interface and its value types.
///
/// Lengths are millimeters and angles are radians everywhere inside the kernel layer.
namespace kh::kernel {

/// A 3D vector or point, in millimeters.
struct Vec3 {
    double x = 0;  ///< X component (mm).
    double y = 0;  ///< Y component (mm).
    double z = 0;  ///< Z component (mm).
};
/// A 3D point, in millimeters. Same type as Vec3; the name says how it is used.
using Point3 = Vec3;

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

/// Result of every kernel operation. Failures carry diagnostics, never exceptions.
///
/// Shape history for topological naming is added in Phase 2.
struct OpResult {
    ShapeHandle shape;   ///< The new shape; empty if the operation failed.
    Diagnostics issues;  ///< Why it failed, or warnings about a shape that was made.

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

}  // namespace kh::kernel
