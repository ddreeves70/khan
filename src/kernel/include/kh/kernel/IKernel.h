#pragma once

/// @file
/// The kernel abstraction layer.

#include <filesystem>

#include "kh/kernel/Types.h"

namespace kh::kernel {

/// The Kernel Abstraction Layer.
///
/// Every geometry operation in Khan goes through this interface, so the kernel
/// can be tested, recorded and replaced without touching the rest of the code.
///
/// Rules for implementations:
///  - Never throw across this boundary; report problems in OpResult::issues.
///  - Never expose kernel-specific types in this header.
///  - Be deterministic: same inputs, same output.
///
/// Phase 0 covers primitives, Booleans, meshing and export. Sketch faces,
/// extrude, fillet, sub-shape queries and shape history follow in Phases 1-2.
class IKernel {
public:
    /// Destroys the kernel. Shapes it made stay valid.
    virtual ~IKernel() = default;

    /// Makes a rectangular block with edges along the x, y and z axes.
    /// @param corner The corner with the smallest x, y and z (mm).
    /// @param dx Size along x (mm). Must be greater than 0.
    /// @param dy Size along y (mm). Must be greater than 0.
    /// @param dz Size along z (mm). Must be greater than 0.
    /// @return The solid, or diagnostics if a size is not positive.
    virtual OpResult makeBox(const Point3& corner, double dx, double dy, double dz) = 0;

    /// Makes a solid cylinder.
    /// @param baseCenter Center of the bottom face (mm).
    /// @param axis Direction from the bottom face to the top face. Any length except zero.
    /// @param radius Radius (mm). Must be greater than 0.
    /// @param height Height along @p axis (mm). Must be greater than 0.
    /// @return The solid, or diagnostics if an input is not valid.
    virtual OpResult makeCylinder(const Point3& baseCenter, const Vec3& axis, double radius,
                                  double height) = 0;

    /// Combines two shapes.
    /// @param op Union, cut (@p a minus @p b) or intersect.
    /// @param a The first shape; for a cut, the one that keeps its material.
    /// @param b The second shape; for a cut, the tool that is removed.
    /// @return The combined shape, or diagnostics if the operation failed.
    virtual OpResult boolean(BoolOp op, const ShapeHandle& a, const ShapeHandle& b) = 0;

    /// Measures the smallest axis-aligned box around a shape.
    /// @param shape The shape to measure.
    /// @return A tight (not padded) bounding box (mm).
    virtual Box3 bounds(const ShapeHandle& shape) const = 0;

    /// Measures volume, area and center of mass.
    /// @param shape The shape to measure.
    /// @return The mass properties, assuming uniform density.
    virtual MassProps massProps(const ShapeHandle& shape) const = 0;

    /// Counts the topological entities of a shape.
    /// @param shape The shape to count.
    /// @return Counts of solids, shells, faces, edges and vertices.
    virtual Topology topology(const ShapeHandle& shape) const = 0;

    /// Checks that the shape is valid and that every solid is closed (watertight).
    /// @param shape The shape to check.
    /// @return No diagnostics for a valid closed solid; otherwise what is wrong.
    virtual Diagnostics validate(const ShapeHandle& shape) const = 0;

    /// Turns exact geometry into triangles, for display and STL export.
    /// @param shape The shape to mesh.
    /// @param quality How closely the triangles follow the surface.
    /// @return The mesh; empty if the shape has no faces.
    virtual Mesh tessellate(const ShapeHandle& shape, const MeshQuality& quality) const = 0;

    /// Writes a shape to a file.
    /// @param shape The shape to write.
    /// @param format STL or STEP.
    /// @param path Where to write. An existing file is replaced.
    /// @return No diagnostics on success; otherwise why the file could not be written.
    virtual Diagnostics exportFile(const ShapeHandle& shape, ExportFormat format,
                                   const std::filesystem::path& path) const = 0;
};

}  // namespace kh::kernel
