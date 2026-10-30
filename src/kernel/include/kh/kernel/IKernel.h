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
/// Phase 0 covers primitives, Booleans, meshing and export; moving came next, then
/// faces from sketch shapes and extrude. Fillet, sub-shape queries and shape history
/// follow in Phase 2.
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

    // Added after Phase 0. New operations go at the end, so plugins built
    // against an earlier IKernel keep working.

    /// Moves and turns a copy of a shape. The shape itself isn't changed.
    /// @param shape The shape to move.
    /// @param motion The turn (radians, about a line in mm), then the slide (mm).
    /// @return The moved shape, or diagnostics if the axis is 0, 0, 0 while turning.
    virtual OpResult transform(const ShapeHandle& shape, const Motion& motion) = 0;

    /// Makes a flat face from an outline and its holes, on a plane (a sketch's
    /// closed shape, ready to extrude).
    ///
    /// Ends of neighboring edges may be a hair apart (up to about 1e-5 mm, as a
    /// solver leaves them); they're joined where they meet.
    /// @param profile The outline (counterclockwise) and holes (clockwise), in the
    ///        plane's x and y (mm).
    /// @param plane Where the face lies.
    /// @return The face, or diagnostics if the outline doesn't close, a piece has no
    ///         length or size, or the plane's directions aren't square to each other.
    virtual OpResult makeFace(const Profile2& profile, const Plane& plane) = 0;

    /// Sweeps a flat face along a straight line, making a solid: a rectangle
    /// becomes a block, a circle a cylinder.
    /// @code
    /// auto face = kernel.makeFace(profile, plane);
    /// auto solid = kernel.extrude(face.shape, {0, 0, 10});  // 10 mm straight up
    /// @endcode
    /// @param face A flat face, like one from makeFace().
    /// @param vector Which way and how far to sweep it (mm). Not 0, 0, 0, and not
    ///        lying flat in the face.
    /// @return The solid, or diagnostics if the face or the vector won't do.
    virtual OpResult extrude(const ShapeHandle& face, const Vec3& vector) = 0;
};

}  // namespace kh::kernel
