#pragma once

/// @file
/// Features: the steps in a design's history.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "kh/core/Id.h"
#include "kh/kernel/IKernel.h"

/// Documents, features, commands and undo.
namespace kh::model {

/// The kind of a Feature. Saved in files, so values are never reordered.
enum class FeatureType {
    Box,       ///< A BoxFeature.
    Cylinder,  ///< A CylinderFeature.
    Boolean,   ///< A BooleanFeature.
};

/// Gives a feature access to the results of features before it during a rebuild.
class ResultLookup {
public:
    /// Allows deleting through this interface.
    virtual ~ResultLookup() = default;

    /// Finds the shape a feature produced.
    /// @param id The feature to look up.
    /// @return The shape, or nullptr if it has none (failed, blocked, suppressed or unknown).
    virtual const kernel::ShapeHandle* shapeOf(const FeatureId& id) const = 0;
};

/// One step in a design's history.
///
/// Phase 0 has primitive solids and Booleans; sketches, extrude, fillet and the
/// rest arrive in Phases 1-2.
///
/// Features are owned by a Document. Outside code only ever gets const access;
/// changes go through Commands (see Command.h).
class Feature {
public:
    /// Destroys the feature.
    virtual ~Feature() = default;

    /// Gives the feature's id, which never changes, even through edits, undo and redo.
    /// @return The id.
    const FeatureId& id() const { return id_; }

    /// Gives the name shown in the feature tree, e.g. "Box1".
    /// @return The name. Unique within its document.
    const std::string& name() const { return name_; }

    /// Checks whether the user has turned this step off.
    /// @return True if the step is skipped during a rebuild.
    bool suppressed() const { return suppressed_; }

    /// Changes the name. Use on a copy made with clone(), or from a Command.
    /// @param name The new name.
    void setName(std::string name) { name_ = std::move(name); }

    /// Turns the step off or on. Use on a copy made with clone(), or from a Command.
    /// @param s True to skip this step during a rebuild.
    void setSuppressed(bool s) { suppressed_ = s; }

    /// Gives the material picked for this step's body.
    /// @return A material's key (see Material::key), or empty if none was picked:
    ///         then the body's material comes from the step it was made from, or
    ///         is the default (see Document::materialOf()).
    const std::string& material() const { return material_; }

    /// Picks the material for this step's body. Use from a Command (SetMaterialCommand).
    /// @param key A material's key, or empty to go back to the material it gets by default.
    void setMaterial(std::string key) { material_ = std::move(key); }

    /// Gives the kind of feature.
    /// @return The feature type.
    virtual FeatureType type() const = 0;

    /// Makes a deep copy that keeps the same id.
    ///
    /// Used to edit a feature through a command: clone it, change the copy,
    /// then replace the original with it (see EditFeatureCommand).
    /// @return The copy.
    virtual std::unique_ptr<Feature> clone() const = 0;

    /// Lists the features whose results this one uses. They must come earlier in the document.
    /// @return The input features; empty for features that use no others.
    virtual std::vector<FeatureId> dependencies() const { return {}; }

    /// Makes a copy with a new id, for duplicating and pasting. Everything else
    /// (name, numbers, inputs, material, turned off) is copied as it is.
    /// @param id The copy's id.
    /// @return The copy.
    std::unique_ptr<Feature> copyWithId(FeatureId id) const {
        auto copy = clone();
        copy->id_ = id;
        return copy;
    }

    /// Points this feature's inputs at other features: an input found in @p map
    /// is replaced by the id it maps to; others are left alone. Used when a set of
    /// steps is copied, so the copies use each other, not the originals.
    /// @param map Old input id to new input id.
    virtual void remapInputs(const std::map<FeatureId, FeatureId>& map) { (void)map; }

    /// Builds this feature's shape. Must be deterministic and must not throw.
    /// @param kernel The geometry kernel to build with.
    /// @param earlier The results of the features before this one.
    /// @return The shape, or diagnostics saying why it could not be built.
    virtual kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const = 0;

protected:
    /// Makes a feature with a new id.
    /// @param name The name shown in the feature tree.
    explicit Feature(std::string name) : id_(FeatureId::generate()), name_(std::move(name)) {}

    /// Makes a feature with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    Feature(FeatureId id, std::string name) : id_(id), name_(std::move(name)) {}

    /// Copies a feature, keeping its id (for clone()).
    /// @param other The feature to copy.
    Feature(const Feature& other) = default;

    /// Copies a feature, keeping its id.
    /// @param other The feature to copy.
    /// @return This feature.
    Feature& operator=(const Feature& other) = default;

private:
    FeatureId id_;
    std::string name_;
    bool suppressed_ = false;
    std::string material_;
};

/// A rectangular block from a corner point and a size along x, y and z (mm).
class BoxFeature final : public Feature {
public:
    /// Makes a box feature with a new id.
    /// @param name The name shown in the feature tree.
    /// @param corner The corner with the smallest x, y and z (mm).
    /// @param size Width, depth and height along x, y and z (mm).
    BoxFeature(std::string name, kernel::Vec3 corner, kernel::Vec3 size);

    /// Makes a box feature with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    /// @param corner The corner with the smallest x, y and z (mm).
    /// @param size Width, depth and height along x, y and z (mm).
    BoxFeature(FeatureId id, std::string name, kernel::Vec3 corner, kernel::Vec3 size);

    /// Gives the corner with the smallest x, y and z.
    /// @return The corner (mm).
    const kernel::Vec3& corner() const { return corner_; }

    /// Gives the size along x, y and z.
    /// @return The size (mm).
    const kernel::Vec3& size() const { return size_; }

    /// Moves the box.
    /// @param c The new corner (mm).
    void setCorner(const kernel::Vec3& c) { corner_ = c; }

    /// Resizes the box.
    /// @param s The new size along x, y and z (mm).
    void setSize(const kernel::Vec3& s) { size_ = s; }

    FeatureType type() const override { return FeatureType::Box; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<BoxFeature>(*this); }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    kernel::Vec3 corner_;
    kernel::Vec3 size_;
};

/// A cylinder from the center of its base, an axis direction, a radius and a height (mm).
class CylinderFeature final : public Feature {
public:
    /// Makes a cylinder feature with a new id.
    /// @param name The name shown in the feature tree.
    /// @param base Center of the bottom face (mm).
    /// @param axis Direction from the bottom face to the top face.
    /// @param radius Radius (mm).
    /// @param height Height along @p axis (mm).
    CylinderFeature(std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius, double height);

    /// Makes a cylinder feature with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    /// @param base Center of the bottom face (mm).
    /// @param axis Direction from the bottom face to the top face.
    /// @param radius Radius (mm).
    /// @param height Height along @p axis (mm).
    CylinderFeature(FeatureId id, std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius,
                    double height);

    /// Gives the center of the bottom face.
    /// @return The base center (mm).
    const kernel::Vec3& base() const { return base_; }

    /// Gives the direction from the bottom face to the top face.
    /// @return The axis direction.
    const kernel::Vec3& axis() const { return axis_; }

    /// Gives the radius.
    /// @return The radius (mm).
    double radius() const { return radius_; }

    /// Gives the height.
    /// @return The height (mm).
    double height() const { return height_; }

    /// Moves the cylinder.
    /// @param b The new base center (mm).
    void setBase(const kernel::Vec3& b) { base_ = b; }

    /// Points the cylinder another way.
    /// @param a The new axis direction.
    void setAxis(const kernel::Vec3& a) { axis_ = a; }

    /// Changes the radius.
    /// @param r The new radius (mm).
    void setRadius(double r) { radius_ = r; }

    /// Changes the height.
    /// @param h The new height (mm).
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

/// Combines the results of two earlier features: union, cut (a minus b) or intersect.
class BooleanFeature final : public Feature {
public:
    /// Makes a Boolean feature with a new id.
    /// @param name The name shown in the feature tree.
    /// @param op Union, cut or intersect.
    /// @param a The first input; for a cut, the one that keeps its material.
    /// @param b The second input; for a cut, the tool that is removed.
    BooleanFeature(std::string name, kernel::BoolOp op, FeatureId a, FeatureId b);

    /// Makes a Boolean feature with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    /// @param op Union, cut or intersect.
    /// @param a The first input; for a cut, the one that keeps its material.
    /// @param b The second input; for a cut, the tool that is removed.
    BooleanFeature(FeatureId id, std::string name, kernel::BoolOp op, FeatureId a, FeatureId b);

    /// Gives the operation.
    /// @return Union, cut or intersect.
    kernel::BoolOp op() const { return op_; }

    /// Gives the first input.
    /// @return The id of the first input feature.
    const FeatureId& a() const { return a_; }

    /// Gives the second input.
    /// @return The id of the second input feature.
    const FeatureId& b() const { return b_; }

    /// Changes the operation.
    /// @param op Union, cut or intersect.
    void setOp(kernel::BoolOp op) { op_ = op; }

    FeatureType type() const override { return FeatureType::Boolean; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<BooleanFeature>(*this); }
    std::vector<FeatureId> dependencies() const override { return {a_, b_}; }
    void remapInputs(const std::map<FeatureId, FeatureId>& map) override {
        if (auto it = map.find(a_); it != map.end()) a_ = it->second;
        if (auto it = map.find(b_); it != map.end()) b_ = it->second;
    }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    kernel::BoolOp op_;
    FeatureId a_;
    FeatureId b_;
};

}  // namespace kh::model
