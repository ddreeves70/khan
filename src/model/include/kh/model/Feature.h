#pragma once

/// @file
/// Features: the steps in a design's history.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/kernel/IKernel.h"
#include "kh/sketch/Sketch.h"
#include "kh/sketch/Solver.h"

/// Documents, features, commands and undo.
namespace kh::model {

/// The kind of a Feature. Saved in files, so values are never reordered.
enum class FeatureType {
    Box,       ///< A BoxFeature.
    Cylinder,  ///< A CylinderFeature.
    Boolean,   ///< A BooleanFeature.
    Move,      ///< A MoveFeature.
    Sketch,    ///< A SketchFeature.
    Extrude,   ///< An ExtrudeFeature.
};

/// The flat plane a sketch is drawn on.
enum class SketchPlane {
    XY,  ///< The ground: sketch x is x, sketch y is y. Seen from above (Top).
    XZ,  ///< Standing up, facing the front: sketch x is x, sketch y is z. Seen from the Front.
    YZ,  ///< Standing up, facing the right: sketch x is y, sketch y is z. Seen from the Right.
};

/// How one feature came out of a rebuild.
enum class FeatureStatus {
    Ok,          ///< Built successfully.
    Failed,      ///< The kernel could not build it.
    Blocked,     ///< An earlier step it uses has a problem or is turned off.
    Suppressed,  ///< Turned off by the user.
};

/// The rebuild result of one feature.
struct FeatureResult {
    FeatureStatus status = FeatureStatus::Ok;  ///< How it came out.
    kernel::ShapeHandle shape;                 ///< The shape; empty unless status is Ok.
    Diagnostics issues;                        ///< Why it failed or is blocked.
    /// For a Sketch step: what the solver gave back (the solved geometry, the
    /// degrees of freedom, conflicting and repeated constraints). Null otherwise.
    std::shared_ptr<const sketch::SolveResult> sketch;
    /// For a Sketch step: the plane it's drawn on (so a later step, like
    /// Extrude, knows where its shapes are). Empty otherwise.
    std::optional<SketchPlane> plane;
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

    /// Finds everything a feature produced: for a Sketch step, its solved sketch
    /// and plane (see FeatureResult::sketch and FeatureResult::plane).
    /// @param id The feature to look up.
    /// @return Its result, or nullptr if it wasn't rebuilt before this one.
    virtual const FeatureResult* resultOf(const FeatureId& id) const = 0;
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

    /// Lists the inputs (from dependencies()) that are sketches, not bodies. A
    /// sketch input is ready when it solved; other inputs when they made a body.
    /// @return The sketch inputs; empty for features that use no sketches.
    virtual std::vector<FeatureId> sketchInputs() const { return {}; }

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

/// Moves and turns the body made by an earlier step. The body keeps its size and
/// shape; the earlier step's body is used up, like the bodies a Boolean combines.
///
/// The turn comes first (about the line through `center` along `axis`), then the
/// slide by `offset`.
class MoveFeature final : public Feature {
public:
    /// Makes a Move step with a new id.
    /// @param name The name shown in the feature tree.
    /// @param body The step whose body is moved.
    /// @param motion How far to turn (radians) and slide (mm).
    MoveFeature(std::string name, FeatureId body, kernel::Motion motion);

    /// Makes a Move step with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    /// @param body The step whose body is moved.
    /// @param motion How far to turn (radians) and slide (mm).
    MoveFeature(FeatureId id, std::string name, FeatureId body, kernel::Motion motion);

    /// Gives the step whose body is moved.
    /// @return Its id.
    const FeatureId& body() const { return body_; }

    /// Gives how far the body is turned and slid.
    /// @return The motion (angle in radians, lengths in mm).
    const kernel::Motion& motion() const { return motion_; }

    /// Changes how far the body is turned and slid.
    /// @param m The new motion (angle in radians, lengths in mm).
    void setMotion(const kernel::Motion& m) { motion_ = m; }

    FeatureType type() const override { return FeatureType::Move; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<MoveFeature>(*this); }
    std::vector<FeatureId> dependencies() const override { return {body_}; }
    void remapInputs(const std::map<FeatureId, FeatureId>& map) override {
        if (auto it = map.find(body_); it != map.end()) body_ = it->second;
    }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    FeatureId body_;
    kernel::Motion motion_;
};

/// Where a sketch plane sits in the design.
struct PlaneAxes {
    kernel::Vec3 origin{};  ///< Where the sketch's x and y are 0, in mm.
    kernel::Vec3 x{};       ///< The sketch's x direction in the design (a unit vector).
    kernel::Vec3 y{};       ///< The sketch's y direction in the design (a unit vector).
};

/// Gives where a sketch plane sits: XY has sketch x, y along x, y; XZ along x, z;
/// YZ along y, z. Each is seen from the side x × y points to (the top, the front,
/// the right).
/// @param plane The plane.
/// @return Its origin and directions.
PlaneAxes planeAxes(SketchPlane plane);

/// Turns a point of a sketch into a point of the design.
/// @param plane The sketch's plane.
/// @param point The point in the sketch (mm).
/// @return The same point in the design's x, y and z (mm).
kernel::Vec3 toDesign(SketchPlane plane, const sketch::Point2& point);

/// Turns a point of the design into a point of a sketch, dropping the distance
/// off the plane.
/// @param plane The sketch's plane.
/// @param point A point in the design (mm).
/// @return The point in the sketch's x and y (mm).
sketch::Point2 toSketch(SketchPlane plane, const kernel::Vec3& point);

/// A sketch: points, lines, circles and arcs on a plane, with constraints and
/// dimensions. It isn't a body; its rebuild result is the solved sketch
/// (FeatureResult::sketch). Later steps (extrude) turn its closed shapes into bodies.
///
/// The sketch keeps the geometry as the student drew it; every rebuild solves it
/// from there.
class SketchFeature final : public Feature {
public:
    /// Makes an empty sketch step with a new id.
    /// @param name The name shown in the feature tree.
    /// @param plane The plane it's drawn on.
    SketchFeature(std::string name, SketchPlane plane);

    /// Makes a sketch step with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    /// @param plane The plane it's drawn on.
    /// @param sketch Its entities and constraints.
    SketchFeature(FeatureId id, std::string name, SketchPlane plane, sketch::Sketch sketch);

    /// Gives the plane it's drawn on.
    /// @return The plane.
    SketchPlane plane() const { return plane_; }

    /// Gives its entities and constraints, as drawn (not solved).
    /// @return The sketch.
    const sketch::Sketch& sketch() const { return sketch_; }

    /// Replaces its entities and constraints. Use on a copy, or from a Command.
    /// @param s The new sketch.
    void setSketch(sketch::Sketch s) { sketch_ = std::move(s); }

    FeatureType type() const override { return FeatureType::Sketch; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<SketchFeature>(*this); }
    /// A sketch isn't built by the kernel: Document::regenerate() solves it with
    /// the sketch solver instead. This returns an error if it's called.
    /// @param kernel Not used.
    /// @param earlier Not used.
    /// @return An error.
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    SketchPlane plane_;
    sketch::Sketch sketch_;
};

/// Which way an Extrude goes from its sketch.
enum class ExtrudeSide {
    One,   ///< Out of the sketch's front (the side it's seen from: up for XY).
    Flip,  ///< Out of the back instead (down for XY).
    Both,  ///< Half the distance each way.
};

/// An Extrude step: every closed shape of a sketch, swept straight out of its
/// plane into a solid. Holes in a shape go through; several shapes make one body.
///
/// @code
/// // A 40 x 20 mm plate, 5 mm thick, from the closed rectangle in Sketch1.
/// auto plate = std::make_unique<ExtrudeFeature>("Plate", sketchId, 5.0, ExtrudeSide::One);
/// @endcode
class ExtrudeFeature final : public Feature {
public:
    /// Makes an Extrude step with a new id.
    /// @param name The name shown in the feature tree.
    /// @param sketch The Sketch step whose closed shapes are extruded.
    /// @param distance How far (mm). Must be greater than 0.
    /// @param side Which way.
    ExtrudeFeature(std::string name, FeatureId sketch, double distance, ExtrudeSide side);

    /// Makes an Extrude step with a given id, for loading a saved design.
    /// @param id The id it was saved with.
    /// @param name The name shown in the feature tree.
    /// @param sketch The Sketch step whose closed shapes are extruded.
    /// @param distance How far (mm).
    /// @param side Which way.
    ExtrudeFeature(FeatureId id, std::string name, FeatureId sketch, double distance, ExtrudeSide side);

    /// Gives the Sketch step it extrudes.
    /// @return Its id.
    const FeatureId& sketch() const { return sketch_; }

    /// Gives how far it goes.
    /// @return The distance (mm); with ExtrudeSide::Both, the whole thickness.
    double distance() const { return distance_; }

    /// Gives which way it goes.
    /// @return The side.
    ExtrudeSide side() const { return side_; }

    /// Changes how far it goes. Use on a copy, or from a Command.
    /// @param d The distance (mm).
    void setDistance(double d) { distance_ = d; }

    /// Changes which way it goes. Use on a copy, or from a Command.
    /// @param s The side.
    void setSide(ExtrudeSide s) { side_ = s; }

    FeatureType type() const override { return FeatureType::Extrude; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ExtrudeFeature>(*this); }
    std::vector<FeatureId> dependencies() const override { return {sketch_}; }
    std::vector<FeatureId> sketchInputs() const override { return {sketch_}; }
    void remapInputs(const std::map<FeatureId, FeatureId>& map) override {
        if (auto it = map.find(sketch_); it != map.end()) sketch_ = it->second;
    }
    kernel::OpResult execute(kernel::IKernel& kernel, const ResultLookup& earlier) const override;

private:
    FeatureId sketch_;
    double distance_;
    ExtrudeSide side_;
};

}  // namespace kh::model
