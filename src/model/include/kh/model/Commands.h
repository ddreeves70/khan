#pragma once

/// @file
/// The built-in commands that change features.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

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

}  // namespace kh::model
