#pragma once

/// @file
/// The built-in commands that change features.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "kh/model/Command.h"

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

}  // namespace kh::model
