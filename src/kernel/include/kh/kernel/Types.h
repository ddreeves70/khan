#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "kh/core/Diagnostic.h"

namespace kh::kernel {

// Lengths are millimeters and angles are radians everywhere inside the kernel layer.
struct Vec3 {
    double x = 0, y = 0, z = 0;
};
using Point3 = Vec3;

struct Box3 {
    Point3 min;
    Point3 max;
};

// Base for a kernel's private shape representation. Each kernel adapter
// derives its own type (OcctKernel stores a TopoDS_Shape); nothing outside
// the adapter ever sees what is inside.
struct ShapeImpl {
    virtual ~ShapeImpl() = default;
};

// Opaque, cheap-to-copy handle to an immutable kernel shape.
class ShapeHandle {
public:
    ShapeHandle() = default;
    explicit ShapeHandle(std::shared_ptr<const ShapeImpl> impl);

    bool valid() const { return impl_ != nullptr; }
    std::uint64_t id() const { return id_; }  // unique per handle created in this session

    // For kernel adapters only: recover their own representation.
    template <typename T>
    const T* as() const { return dynamic_cast<const T*>(impl_.get()); }

private:
    std::shared_ptr<const ShapeImpl> impl_;
    std::uint64_t id_ = 0;
};

enum class BoolOp { Union, Cut, Intersect };

enum class ExportFormat { Stl, Step };

// Result of every kernel operation. Failures carry diagnostics, never exceptions.
// (Shape history for topological naming is added in Phase 2.)
struct OpResult {
    ShapeHandle shape;
    Diagnostics issues;

    bool ok() const { return shape.valid(); }
};

struct MeshQuality {
    double linearDeflection = 0.05;   // mm
    double angularDeflection = 0.35;  // radians (about 20 degrees)
};

struct Mesh {
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

struct MassProps {
    double volume = 0;  // mm^3
    double area = 0;    // mm^2
    Point3 centerOfMass;
};

}  // namespace kh::kernel
