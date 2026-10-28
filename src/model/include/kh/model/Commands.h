#pragma once

/// @file
/// The built-in commands that change features.

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

/// Makes a name no entity or constraint in a sketch has yet.
/// @param sketch The sketch.
/// @param base The start of the name, e.g. "Line".
/// @return The first free name of "Line1", "Line2", ...
std::string uniqueSketchName(const sketch::Sketch& sketch, const std::string& base);

/// Replaces a feature with an edited copy that has the same id and type.
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
