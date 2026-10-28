#include "kh/model/Feature.h"

#include <utility>

namespace kh::model {

BoxFeature::BoxFeature(std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : BoxFeature(FeatureId::generate(), std::move(name), corner, size) {}

BoxFeature::BoxFeature(FeatureId id, std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : Feature(id, std::move(name)), corner_(corner), size_(size) {}

kernel::OpResult BoxFeature::execute(kernel::IKernel& kernel, const ResultLookup&) const {
    return kernel.makeBox(corner_, size_.x, size_.y, size_.z);
}

CylinderFeature::CylinderFeature(std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius,
                                 double height)
    : CylinderFeature(FeatureId::generate(), std::move(name), base, axis, radius, height) {}

CylinderFeature::CylinderFeature(FeatureId id, std::string name, kernel::Vec3 base, kernel::Vec3 axis,
                                 double radius, double height)
    : Feature(id, std::move(name)), base_(base), axis_(axis), radius_(radius), height_(height) {}

kernel::OpResult CylinderFeature::execute(kernel::IKernel& kernel, const ResultLookup&) const {
    return kernel.makeCylinder(base_, axis_, radius_, height_);
}

BooleanFeature::BooleanFeature(std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : BooleanFeature(FeatureId::generate(), std::move(name), op, a, b) {}

BooleanFeature::BooleanFeature(FeatureId id, std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : Feature(id, std::move(name)), op_(op), a_(a), b_(b) {}

kernel::OpResult BooleanFeature::execute(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const auto* sa = earlier.shapeOf(a_);
    const auto* sb = earlier.shapeOf(b_);
    if (!sa || !sb) {
        kernel::OpResult r;
        r.issues.push_back({"feature.input_missing",
                            Severity::Error,
                            "This step uses another step that is missing or has a problem.",
                            {}});
        return r;
    }
    return kernel.boolean(op_, *sa, *sb);
}

MoveFeature::MoveFeature(std::string name, FeatureId body, kernel::Motion motion)
    : MoveFeature(FeatureId::generate(), std::move(name), body, motion) {}

MoveFeature::MoveFeature(FeatureId id, std::string name, FeatureId body, kernel::Motion motion)
    : Feature(id, std::move(name)), body_(body), motion_(motion) {}

kernel::OpResult MoveFeature::execute(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const auto* s = earlier.shapeOf(body_);
    if (!s) {
        kernel::OpResult r;
        r.issues.push_back({"feature.input_missing",
                            Severity::Error,
                            "This step uses another step that is missing or has a problem.",
                            {}});
        return r;
    }
    return kernel.transform(*s, motion_);
}

SketchFeature::SketchFeature(std::string name, SketchPlane plane)
    : SketchFeature(FeatureId::generate(), std::move(name), plane, {}) {}

SketchFeature::SketchFeature(FeatureId id, std::string name, SketchPlane plane, sketch::Sketch sketch)
    : Feature(id, std::move(name)), plane_(plane), sketch_(std::move(sketch)) {}

kernel::OpResult SketchFeature::execute(kernel::IKernel&, const ResultLookup&) const {
    kernel::OpResult r;
    r.issues.push_back({"sketch.no_solver",
                        Severity::Error,
                        "Sketches are solved by the sketch solver, not the kernel.",
                        {}});
    return r;
}

}  // namespace kh::model
