#pragma once

/// @file
/// The built-in commands that change features.

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kh/model/Command.h"
#include "kh/model/Material.h"

namespace kh::model {

/// Adds a feature, at the end or at a given position.
///
/// Fails if the name is empty or taken, or if an input is missing or comes later.
class AddFeatureCommand final : public Command {
public:
    /// Prepares to add a feature.
    /// @param feature The feature to add. The command owns it until it is applied.
    /// @param index Position in the feature list; std::nullopt (the default) means at the end.
    explicit AddFeatureCommand(std::unique_ptr<Feature> feature,
                               std::optional<std::size_t> index = std::nullopt);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::unique_ptr<Feature> pending_;  // owned here while not in the document
    FeatureId id_;
    std::string name_;
    std::optional<std::size_t> index_;
    std::size_t insertedAt_ = 0;
};

/// Deletes a feature that no other step uses.
///
/// Undo puts it back at the same position with the same id.
class DeleteFeatureCommand final : public Command {
public:
    /// Prepares to delete a feature.
    /// @param id The feature to delete.
    explicit DeleteFeatureCommand(FeatureId id);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FeatureId id_;
    std::string name_;
    std::unique_ptr<Feature> removed_;
    std::size_t index_ = 0;
};

/// Turns steps off (suppresses them) so they aren't built, or back on, as one
/// undo step. A step that uses a step that's off is blocked until it's on again.
///
/// @code
/// doc.execute(std::make_unique<SuppressFeaturesCommand>(std::vector<FeatureId>{holeId}, true));
/// @endcode
class SuppressFeaturesCommand final : public Command {
public:
    /// Prepares the change.
    /// @param ids The steps, in the order picked.
    /// @param off True to turn them off, false to turn them back on.
    SuppressFeaturesCommand(std::vector<FeatureId> ids, bool off);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::vector<FeatureId> ids_;
    bool off_;
    std::vector<FeatureId> changed_;  // the steps that weren't already that way
    std::string label_;
};

/// Puts steps that are next to each other in the feature tree into a new folder.
///
/// @code
/// doc.execute(std::make_unique<AddFolderCommand>(std::vector<FeatureId>{holeId, slotId}, "Holes"));
/// @endcode
class AddFolderCommand final : public Command {
public:
    /// Prepares the new folder.
    /// @param steps Its steps, in any order. They must be next to each other in
    ///        the design and not in another folder.
    /// @param name Its name; empty for the next free "Folder1", "Folder2", ...
    /// @param id Its id; a new one unless given (opening a file gives the saved one).
    AddFolderCommand(std::vector<FeatureId> steps, std::string name, FolderId id = FolderId::generate());

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

    /// Gives the folder's id, which stays the same across undo and redo.
    /// @return The id.
    const FolderId& id() const { return id_; }

private:
    std::vector<FeatureId> steps_;
    std::string askedName_;
    std::string name_;
    FolderId id_;
};

/// Renames a folder. Its steps stay as they are.
class RenameFolderCommand final : public Command {
public:
    /// Prepares the rename.
    /// @param id The folder.
    /// @param name Its new name. No other folder may have it.
    RenameFolderCommand(FolderId id, std::string name);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FolderId id_;
    std::string newName_;
    std::string oldName_;
};

/// Removes a folder but keeps its steps, which go back to the top of the tree.
class DeleteFolderCommand final : public Command {
public:
    /// Prepares the change.
    /// @param id The folder.
    explicit DeleteFolderCommand(FolderId id);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FolderId id_;
    Folder removed_;
    std::size_t index_ = 0;
};

/// Changes a Sketch step's entities or constraints, as one undo step.
///
/// The change is a function that edits a copy of the sketch. It runs once; redo
/// puts back the same result, so new ids stay the same. The result must pass
/// sketch::check(), or nothing changes.
///
/// @code
/// doc.execute(std::make_unique<EditSketchCommand>(sketchId, [](sketch::Sketch& s, std::string& label) {
///     s.entities.push_back({EntityId::generate(), "Line1", sketch::LineShape{{0, 0}, {40, 0}}, false});
///     label = "Add Line1";
///     return Diagnostics{};
/// }));
/// @endcode
class EditSketchCommand final : public Command {
public:
    /// Edits a copy of the sketch and sets the label for Edit > Undo.
    using Change = std::function<Diagnostics(sketch::Sketch& sketch, std::string& label)>;

    /// Prepares the change.
    /// @param sketch The Sketch step.
    /// @param change What to change. It returns problems, in plain words, if it can't.
    EditSketchCommand(FeatureId sketch, Change change);

    std::string label() const override { return label_; }
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FeatureId id_;
    Change change_;
    std::string label_;
    std::optional<sketch::Sketch> before_;
    std::optional<sketch::Sketch> after_;
};

/// The four lines of a rectangle added by addRectangle(), counterclockwise from the
/// first corner: along the bottom (or top), up (or down) the far side, back, and
/// down (or up) to the start. Each line starts where the one before ends.
struct RectangleLines {
    std::array<EntityId, 4> lines{};  ///< Line1 to Line4, as added.
};

/// Adds a rectangle to a sketch: four lines between two opposite corners, joined
/// at the corners (Coincident) and kept level and upright (Horizontal and
/// Vertical), so it can only move and change size: 4 ways.
/// @code
/// auto r = kh::model::addRectangle(sketch, {0, 0}, {40, 20});  // 40 x 20 mm
/// @endcode
/// @param sketch The sketch to add it to.
/// @param corner One corner (mm).
/// @param opposite The opposite corner (mm): it must differ in x and in y.
/// @param construction True for helper geometry.
/// @return The new lines' ids.
RectangleLines addRectangle(sketch::Sketch& sketch, sketch::Point2 corner, sketch::Point2 opposite,
                            bool construction = false);

/// What roundCorner() added.
struct RoundedCorner {
    EntityId arc{};         ///< The arc that rounds the corner.
    EntityId corner{};      ///< The helper point where the lines would meet (the "virtual sharp").
    ConstraintId radius{};  ///< The arc's Radius dimension.
};

/// Rounds the corner where two lines meet (fillets it): each line is cut back,
/// and an arc of the given radius joins them, tangent to both.
///
/// The sharp corner stays as a helper point, kept on both lines (as if they
/// carried on): constraints on the corner move to it, and a line's Length
/// becomes the distance from its far end to it, so the sizes still measure to
/// the corner. The sketch can move just as many ways as before.
/// @code
/// kh::model::RoundedCorner made;
/// auto problems = kh::model::roundCorner(sketch, {line1, kh::sketch::PointRole::End}, 5, &made);
/// @endcode
/// @param sketch The sketch.
/// @param at A line's end that a Coincident joins to another line's end.
/// @param radius The arc's radius (mm), more than 0, small enough to fit on both lines.
/// @param made Where to put what was added; may be nullptr.
/// @return What's wrong, in plain words (not a joined corner, parallel lines, a
///         radius too big); empty if it worked.
Diagnostics roundCorner(sketch::Sketch& sketch, const sketch::PointRef& at, double radius,
                        RoundedCorner* made = nullptr);

/// Finds the biggest radius roundCorner() could round a corner with.
/// @param sketch The sketch.
/// @param at A line's end that a Coincident joins to another line's end.
/// @return The radius (mm) that would cut the shorter line back to nothing; nothing
///         if `at` isn't a corner that can be rounded.
std::optional<double> largestCornerRadius(const sketch::Sketch& sketch, const sketch::PointRef& at);

/// Finds the piece trim() would remove, to show it before the click.
/// @param sketch The sketch.
/// @param entity A line, circle or arc.
/// @param near A point near the piece (mm), like where the mouse is.
/// @return The piece (a line or an arc), or nothing if there's nothing to trim.
std::optional<sketch::Shape> trimmedPiece(const sketch::Sketch& sketch, const EntityId& entity,
                                          sketch::Point2 near);

/// Trims a line, circle or arc: removes the piece near a point, between the two
/// places nearest it where other lines, circles and arcs cross it (or from one of
/// them to the end). A cut end stays on the curve that cut it (a Point on line or
/// Point on circle). A line cut in the middle becomes two pieces kept in line; an
/// arc, two arcs of the same circle; a circle cut twice or more, an arc. Something
/// nothing crosses is deleted. Constraints on a removed end go, and so does a
/// trimmed line's Length.
/// @code
/// auto problems = kh::model::trim(sketch, line1, {35, 0});  // the bit of Line1 near (35, 0)
/// @endcode
/// @param sketch The sketch.
/// @param entity A line, circle or arc.
/// @param near A point near the piece to remove (mm).
/// @return What's wrong, in plain words; empty if it worked.
Diagnostics trim(sketch::Sketch& sketch, const EntityId& entity, sketch::Point2 near);

/// Makes a name no entity or constraint in a sketch has yet.
/// @param sketch The sketch.
/// @param base The start of the name, e.g. "Line".
/// @return The first free name of "Line1", "Line2", ...
std::string uniqueSketchName(const sketch::Sketch& sketch, const std::string& base);

/// Replaces a feature with an edited copy that has the same id and type.
/// A number that the copy changes but whose formula it leaves as it was (see
/// Feature::formulas()) was typed over, so it stops coming from the formula.
///
/// @code
/// auto copy = doc.find(id)->clone();
/// static_cast<BoxFeature&>(*copy).setSize({20, 30, 40});
/// doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy)));
/// @endcode
class EditFeatureCommand final : public Command {
public:
    /// Prepares an edit.
    /// @param replacement The edited copy, usually made with Feature::clone().
    /// @param continuesPrevious True if this edit is part of the same continuous
    ///        action as the edit before it (for example the next value while
    ///        dragging a size), so the two become one undo step. Separate edits
    ///        leave it false.
    explicit EditFeatureCommand(std::unique_ptr<Feature> replacement, bool continuesPrevious = false);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;
    bool mergeWith(const Command& next) override;

private:
    std::unique_ptr<Feature> other_;  // the version not currently in the document
    FeatureId id_;
    std::string name_;
    bool continuesPrevious_;
};

/// Renames a feature. Names must be non-empty and unique in the document.
class RenameFeatureCommand final : public Command {
public:
    /// Prepares a rename.
    /// @param id The feature to rename.
    /// @param newName The new name.
    RenameFeatureCommand(FeatureId id, std::string newName);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FeatureId id_;
    std::string newName_;
    std::string oldName_;
};

/// Changes the unit the design shows its lengths in: mm, cm, m, in or ft (its
/// Document settings). Nothing changes size: every length in the design stays in
/// mm inside; Properties, the Sketch panel and kh_cli show and take them in the
/// new unit.
class SetUnitsCommand final : public Command {
public:
    /// Prepares the change.
    /// @param unit The unit to show lengths in.
    explicit SetUnitsCommand(LengthUnit unit);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    LengthUnit unit_;
    LengthUnit old_ = LengthUnit::Millimeter;
};

/// Picks the material for a step's body, or clears it so the body gets its
/// material from the step it was made from (see Document::materialOf()).
class SetMaterialCommand final : public Command {
public:
    /// Prepares the change.
    /// @param id The step.
    /// @param keyOrName A material's key or name (see Document::findMaterial()),
    ///        or empty to clear the step's material.
    SetMaterialCommand(FeatureId id, std::string keyOrName);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FeatureId id_;
    std::string wanted_;
    std::string name_;      // the step's name, for the label
    std::string material_;  // the material's name, for the label
    std::string oldKey_;
};

/// Adds a material to the design, or changes one the design added earlier
/// (matched by name, ignoring upper and lower case). Khan's built-in materials
/// can't be changed: give a new material another name.
class DefineMaterialCommand final : public Command {
public:
    /// Prepares the change.
    /// @param material The material. Its builtin flag is ignored. A changed
    ///        material keeps its id; a new one keeps the key it has (used when
    ///        opening a file) or, if that's empty, gets a new id.
    /// @param badColor Color text that couldn't be read (see parseColor()), so
    ///        apply() refuses with a message; empty if the color is fine.
    explicit DefineMaterialCommand(Material material, std::string badColor = {});

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    Material material_;
    std::string badColor_;
    std::optional<Material> old_;  // the version it replaced, if it changed one
    std::size_t index_ = 0;
};

/// Removes a material the design added. Refused while any step uses it.
class DeleteMaterialCommand final : public Command {
public:
    /// Prepares the change.
    /// @param keyOrName The custom material's key or name.
    explicit DeleteMaterialCommand(std::string keyOrName);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::string wanted_;
    std::optional<Material> removed_;
    std::size_t index_ = 0;
};

/// Adds a parameter, like `wall = 3 mm`, or changes the one with that name.
/// Sketch dimensions whose formulas use it change to their new values.
///
/// Refused if the name can't be a parameter's (see ParameterTable::checkName()),
/// differs from another parameter's only in upper and lower case, or the
/// formula can't be read or worked out; and if the change would stop another
/// parameter or a sketch dimension that uses it from being worked out.
class SetParameterCommand final : public Command {
public:
    /// Prepares the change.
    /// @param name The parameter's name, like "wall".
    /// @param expression Its formula, like "3 mm" or "wall * 4".
    /// @param note What it's for; nothing keeps the note it has (or none, for a new one).
    SetParameterCommand(std::string name, std::string expression,
                        std::optional<std::string> note = std::nullopt);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::string name_, expression_;
    std::optional<std::string> note_;
    ParamId id_;                                   // a new parameter's id, the same on redo
    std::optional<Parameter> old_;                 // the version it changed, if it changed one
    std::vector<std::unique_ptr<Feature>> steps_;  // sketches whose dimensions it changed, swapped in and out
};

/// Renames a parameter, and every formula that uses it (other parameters' and
/// sketch dimensions'), so they still use it.
class RenameParameterCommand final : public Command {
public:
    /// Prepares the change.
    /// @param name The parameter's name now.
    /// @param to Its new name.
    RenameParameterCommand(std::string name, std::string to);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::string name_, to_;
    std::vector<Parameter> old_;                   // every parameter it changed, as it was
    std::vector<std::unique_ptr<Feature>> steps_;  // sketches whose formulas it changed, swapped in and out
};

/// Removes a parameter. Refused while another parameter or a sketch dimension uses it.
class DeleteParameterCommand final : public Command {
public:
    /// Prepares the change.
    /// @param name The parameter's name.
    explicit DeleteParameterCommand(std::string name);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::string name_;
    std::optional<Parameter> removed_;
    std::size_t index_ = 0;
};

/// Puts in all of a design's parameters at once, as they are, in place of the
/// ones it has: used when opening a file. Names are checked (each must be one
/// a parameter can have, and no two may differ only in upper and lower case);
/// formulas aren't, so one with a problem stays in the design to be fixed.
class ReplaceParametersCommand final : public Command {
public:
    /// Prepares the change.
    /// @param parameters The parameters, in order, with their ids.
    explicit ReplaceParametersCommand(std::vector<Parameter> parameters);

    std::string label() const override { return "Set parameters"; }
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    std::vector<Parameter> parameters_;
    ParameterTable old_;
};

/// Changes one of a step's numbers by its key (see Feature::numbers()), like an
/// Extrude's distance: to a typed number, or to a formula that may use the
/// design's parameters, like `wall * 4`. A number with a formula follows the
/// parameters when they change (see SetParameterCommand).
///
/// Refused if the step has no number by that key, or the number or formula
/// gives the wrong kind of value (a length for a length, an angle for an angle)
/// or isn't more than 0 where it must be.
class SetNumberCommand final : public Command {
public:
    /// Prepares the change to a typed number.
    /// @param step The step.
    /// @param key Like "distance".
    /// @param value The number: mm, or radians for an angle.
    SetNumberCommand(FeatureId step, std::string key, double value);

    /// Prepares the change to a formula.
    /// @param step The step.
    /// @param key Like "distance".
    /// @param formula The formula, like "wall * 4".
    SetNumberCommand(FeatureId step, std::string key, std::string formula);

    std::string label() const override;
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

private:
    FeatureId id_;
    std::string key_;
    double value_ = 0;
    std::string formula_;             // empty for a typed number
    std::string name_;                // the step's name, for the label
    std::unique_ptr<Feature> other_;  // the version not in the design
};

/// Slides a body by a distance along x, y and z: what dragging it in the 3D view
/// does. A Box moves its corner, a Cylinder its base and a Move step how far it
/// slides; any other body (an Extrude, a union, ...) gets a new Move step at the
/// end of the design that slides it. Numbers it changes stop coming from their
/// formulas (see Feature::formulas()).
///
/// Refused for a sketch, and for a body that a later step uses unless it's a
/// Box, Cylinder or Move (moving that one would leave the later step behind).
/// Commands made with `continuesPrevious` (the rest of a drag) join the one
/// before into one undo step, even after it added a Move step.
///
/// @code
/// doc.execute(std::make_unique<SlideBodyCommand>(boxId, kernel::Vec3{10, 0, 0}));
/// @endcode
class SlideBodyCommand final : public Command {
public:
    /// Prepares the slide.
    /// @param step The step whose body is slid.
    /// @param by How far, along x, y and z (mm).
    /// @param continuesPrevious True for the next part of the same drag (see mergeWith()).
    /// @param moveId The id to give a Move step, if one is added; a new id if left out.
    SlideBodyCommand(FeatureId step, kernel::Vec3 by, bool continuesPrevious = false,
                     FeatureId moveId = FeatureId::generate());

    std::string label() const override { return "Move " + name_; }
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;
    bool mergeWith(const Command& next) override;

    /// Checks whether a step's body can be slid, without sliding it.
    /// @param doc The design.
    /// @param step The step.
    /// @return Why not, in plain words; empty if it can.
    static Diagnostics check(const Document& doc, const FeatureId& step);

private:
    FeatureId step_;
    kernel::Vec3 by_;
    bool continuesPrevious_;
    FeatureId moveId_;                // the Move step it adds, if it adds one
    bool added_ = false;              // it added a Move step
    std::string name_;                // the body's step, for the label
    std::unique_ptr<Feature> other_;  // the version it replaced (or replaced it), when it edits a step
};

/// Copies steps for duplicating or pasting: the given steps and every step they
/// use (their inputs, and their inputs' inputs), in design order. The copies get
/// new ids, and their inputs point at the other copies, so the copies don't
/// depend on the originals. Names are unchanged; AddFeaturesCommand renames
/// copies whose names are taken.
/// @param doc The design the steps are in.
/// @param ids The steps to copy, in any order. Ids not in the design are ignored.
/// @return The copies, ready for AddFeaturesCommand; empty if none of @p ids is in the design.
std::vector<std::unique_ptr<Feature>> copySteps(const Document& doc, const std::vector<FeatureId>& ids);

/// Gives steps new ids, keeping them pointed at each other (see copySteps()).
/// Used for steps read from the clipboard, which may already be in the design.
/// @param steps Steps that only use each other, in order.
/// @return The same steps with new ids.
std::vector<std::unique_ptr<Feature>> freshIds(std::vector<std::unique_ptr<Feature>> steps);

/// Adds several steps at the end of the design as one undo step: a duplicate or a
/// paste. A step whose name is taken is renamed like a new one (Box1 becomes the
/// next free BoxN). Materials the steps use that the design doesn't have yet are
/// added too.
class AddFeaturesCommand final : public Command {
public:
    /// Prepares the command.
    /// @param features The steps, in order; a step may only use earlier steps in this list or the design.
    /// @param materials Custom materials the steps may use (added if the design lacks them).
    /// @param label The undo label, e.g. "Duplicate Box1" or "Paste 3 steps".
    AddFeaturesCommand(std::vector<std::unique_ptr<Feature>> features, std::vector<Material> materials,
                       std::string label);

    std::string label() const override { return label_; }
    Diagnostics apply(DocumentEditor& editor) override;
    void revert(DocumentEditor& editor) override;

    /// Gives the ids of the steps it adds.
    /// @return Their ids, in order.
    const std::vector<FeatureId>& ids() const { return ids_; }

private:
    void revertMaterials(DocumentEditor& editor);

    std::vector<std::unique_ptr<Feature>> pending_;  // owned here while not in the document
    std::vector<Material> materials_;
    std::vector<std::string> addedMaterials_;  // keys added by apply()
    std::vector<FeatureId> ids_;
    std::string label_;
};

}  // namespace kh::model
