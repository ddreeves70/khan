#include "kh/model/Feature.h"

#include <utility>

namespace kh::model {

BoxFeature::BoxFeature(std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : Feature(std::move(name)), corner_(corner), size_(size) {}

kernel::OpResult BoxFeature::execute(kernel::IKernel& kernel, const ResultLookup&) const {
    return kernel.makeBox(corner_, size_.x, size_.y, size_.z);
}

CylinderFeature::CylinderFeature(std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius,
                                 double height)
    : Feature(std::move(name)), base_(base), axis_(axis), radius_(radius), height_(height) {}

kernel::OpResult CylinderFeature::execute(kernel::IKernel& kernel, const ResultLookup&) const {
    return kernel.makeCylinder(base_, axis_, radius_, height_);
}

BooleanFeature::BooleanFeature(std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : Feature(std::move(name)), op_(op), a_(a), b_(b) {}

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

}  // namespace kh::model
