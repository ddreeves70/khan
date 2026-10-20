#pragma once

#include <filesystem>

#include "kh/kernel/Types.h"

namespace kh::kernel {

// The Kernel Abstraction Layer. Every geometry operation in Khan
// goes through this interface, so the kernel can be tested, recorded and
// replaced without touching the rest of the code.
//
// Rules for implementations:
//  - Never throw across this boundary; report problems in OpResult::issues.
//  - Never expose kernel-specific types in this header.
//  - Be deterministic: same inputs, same output.
//
// Phase 0 covers primitives, Booleans, meshing and export. Sketch faces,
// extrude, fillet, sub-shape queries and shape history follow in Phases 1-2.
class IKernel {
public:
    virtual ~IKernel() = default;

    // Primitives
    virtual OpResult makeBox(const Point3& corner, double dx, double dy, double dz) = 0;
    virtual OpResult makeCylinder(const Point3& baseCenter, const Vec3& axis,
                                  double radius, double height) = 0;

    // Booleans
    virtual OpResult boolean(BoolOp op, const ShapeHandle& a, const ShapeHandle& b) = 0;

    // Queries
    virtual Box3 bounds(const ShapeHandle& shape) const = 0;
    virtual MassProps massProps(const ShapeHandle& shape) const = 0;

    // Meshing and export
    virtual Mesh tessellate(const ShapeHandle& shape, const MeshQuality& quality) const = 0;
    virtual Diagnostics exportFile(const ShapeHandle& shape, ExportFormat format,
                                   const std::filesystem::path& path) const = 0;
};

}  // namespace kh::kernel
