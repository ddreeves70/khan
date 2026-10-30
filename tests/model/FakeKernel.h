#pragma once

#include <cmath>
#include <memory>
#include <variant>

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

    // A face's "volume" is its area (lines only: the shoelace sum; arcs and circles
    // count as their full circle, which is enough for these tests); extrude
    // multiplies it by the distance.
    kernel::OpResult makeFace(const kernel::Profile2& p, const kernel::Plane&) override {
        ++calls;
        auto area = [](const kernel::Loop2& loop) {
            double a = 0;
            for (const auto& e : loop) {
                if (const auto* l = std::get_if<kernel::Line2>(&e.curve)) {
                    const auto& s = e.reversed ? l->end : l->start;
                    const auto& t = e.reversed ? l->start : l->end;
                    a += (s.x * t.y - t.x * s.y) / 2;
                } else if (const auto* c = std::get_if<kernel::Circle2>(&e.curve)) {
                    a += (e.reversed ? -1 : 1) * 3.0 * c->radius * c->radius;
                }
            }
            return a < 0 ? -a : a;
        };
        if (p.outer.empty()) return fail();
        double a = area(p.outer);
        for (const auto& h : p.holes) a -= area(h);
        return make(a);
    }
    kernel::OpResult extrude(const kernel::ShapeHandle& face, const kernel::Vec3& v) override {
        ++calls;
        const double d = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (d <= 0) return fail();
        return make(volumeOf(face) * d);
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
