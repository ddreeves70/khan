#pragma once

#include <memory>

#include "kh/kernel/IKernel.h"

namespace kh::test {

// A stand-in kernel for model tests that do not need real geometry. It "builds"
// a shape whose only content is a volume, so tests run without OCCT.
// Boxes and cylinders with a size of zero or less fail, like a real kernel.
class FakeKernel final : public kernel::IKernel {
public:
    struct Shape final : kernel::ShapeImpl {
        explicit Shape(double v) : volume(v) {}
        double volume;
    };

    int calls = 0;

    kernel::OpResult makeBox(const kernel::Point3&, double dx, double dy, double dz) override {
        ++calls;
        if (dx <= 0 || dy <= 0 || dz <= 0) return fail();
        return make(dx * dy * dz);
    }
    kernel::OpResult makeCylinder(const kernel::Point3&, const kernel::Vec3&, double r, double h) override {
        ++calls;
        if (r <= 0 || h <= 0) return fail();
        return make(3.0 * r * r * h);  // fake but easy to check
    }
    kernel::OpResult boolean(kernel::BoolOp op, const kernel::ShapeHandle& a,
                             const kernel::ShapeHandle& b) override {
        ++calls;
        const double va = volumeOf(a), vb = volumeOf(b);
        switch (op) {
            case kernel::BoolOp::Union:
                return make(va + vb);
            case kernel::BoolOp::Cut:
                return make(va - vb);
            case kernel::BoolOp::Intersect:
                return make(va < vb ? va : vb);
        }
        return fail();
    }

    kernel::OpResult transform(const kernel::ShapeHandle& s, const kernel::Motion&) override {
        ++calls;
        return make(volumeOf(s));  // moving never changes the volume
    }

    kernel::Box3 bounds(const kernel::ShapeHandle&) const override { return {}; }
    kernel::MassProps massProps(const kernel::ShapeHandle& s) const override {
        kernel::MassProps m;
        m.volume = volumeOf(s);
        return m;
    }
    kernel::Topology topology(const kernel::ShapeHandle&) const override { return {}; }
    Diagnostics validate(const kernel::ShapeHandle&) const override { return {}; }
    kernel::Mesh tessellate(const kernel::ShapeHandle&, const kernel::MeshQuality&) const override {
        return {};
    }
    Diagnostics exportFile(const kernel::ShapeHandle&, kernel::ExportFormat,
                           const std::filesystem::path&) const override {
        return {};
    }

    static double volumeOf(const kernel::ShapeHandle& s) {
        const auto* impl = s.as<Shape>();
        return impl ? impl->volume : 0.0;
    }

private:
    static kernel::OpResult make(double v) {
        return {kernel::ShapeHandle(std::make_shared<const Shape>(v)), {}};
    }
    static kernel::OpResult fail() {
        kernel::OpResult r;
        r.issues.push_back({"fake.failed", Severity::Error, "The fake kernel could not build this.", {}});
        return r;
    }
};

}  // namespace kh::test
