#pragma once

#include <memory>
#include <string>
#include <vector>

#include "kh/core/Id.h"
#include "kh/kernel/IKernel.h"

namespace kh::model {

enum class FeatureType { Box, Cylinder, Boolean };

// Gives a feature access to the results of features before it during a rebuild.
class ResultLookup {
public:
    virtual ~ResultLookup() = default;
    // The shape a feature produced, or nullptr if it has none (failed, blocked, suppressed).
    virtual const kernel::ShapeHandle* shapeOf(const FeatureId& id) const = 0;
};

// One step in a design's history. Phase 0 has primitive solids and Booleans;
// sketches, extrude, fillet and the rest arrive in Phases 1-2.
//
// Features are owned by a Document. Outside code only ever gets const access;
// changes go through Commands (see Command.h).
class Feature {
public:
    virtual ~Feature() = default;

    const FeatureId& id() const { return id_; }
    const std::string& name() const { return name_; }
    bool suppressed() const { return suppressed_; }

    void setName(std::string name) { name_ = std::move(name); }
    void setSuppressed(bool s) { suppressed_ = s; }

    virtual FeatureType type() const = 0;

    // Deep copy that keeps the same id. Used to edit a feature through a command:
    // clone it, change the copy, then replace the original with it.
    virtual std::unique_ptr<Feature> clone() const = 0;

    // Features whose results this one uses. They must come earlier in the document.
    virtual std::vector<FeatureId> dependencies() const { return {}; }

    // Builds this feature's shape. Must be deterministic and must not throw.
    virtual kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const = 0;

protected:
    explicit Feature(std::string name) : id_(FeatureId::generate()), name_(std::move(name)) {}
    Feature(const Feature&) = default;  // copies keep the id (for clone)
    Feature& operator=(const Feature&) = default;

private:
    FeatureId id_;
    std::string name_;
    bool suppressed_ = false;
};

// A rectangular block from a corner point and a size along x, y and z (mm).
class BoxFeature final : public Feature {
public:
    BoxFeature(std::string name, kernel::Vec3 corner, kernel::Vec3 size);

    const kernel::Vec3& corner() const { return corner_; }
    const kernel::Vec3& size() const { return size_; }
    void setCorner(const kernel::Vec3& c) { corner_ = c; }
    void setSize(const kernel::Vec3& s) { size_ = s; }

    FeatureType type() const override { return FeatureType::Box; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<BoxFeature>(*this); }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    kernel::Vec3 corner_;
    kernel::Vec3 size_;
};

// A cylinder from the center of its base, an axis direction, a radius and a height (mm).
class CylinderFeature final : public Feature {
public:
    CylinderFeature(std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius, double height);

    const kernel::Vec3& base() const { return base_; }
    const kernel::Vec3& axis() const { return axis_; }
    double radius() const { return radius_; }
    double height() const { return height_; }
    void setBase(const kernel::Vec3& b) { base_ = b; }
    void setAxis(const kernel::Vec3& a) { axis_ = a; }
    void setRadius(double r) { radius_ = r; }
    void setHeight(double h) { height_ = h; }

    FeatureType type() const override { return FeatureType::Cylinder; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<CylinderFeature>(*this); }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    kernel::Vec3 base_;
    kernel::Vec3 axis_;
    double radius_;
    double height_;
};

// Combines the results of two earlier features: union, cut (a minus b) or intersect.
class BooleanFeature final : public Feature {
public:
    BooleanFeature(std::string name, kernel::BoolOp op, FeatureId a, FeatureId b);

    kernel::BoolOp op() const { return op_; }
    const FeatureId& a() const { return a_; }
    const FeatureId& b() const { return b_; }
    void setOp(kernel::BoolOp op) { op_ = op; }

    FeatureType type() const override { return FeatureType::Boolean; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<BooleanFeature>(*this); }
    std::vector<FeatureId> dependencies() const override { return {a_, b_}; }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    kernel::BoolOp op_;
    FeatureId a_;
    FeatureId b_;
};

}  // namespace kh::model
