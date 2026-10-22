#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "kh/model/Command.h"

namespace kh::model {

// Adds a feature, at the end or at a given position.
class AddFeatureCommand final : public Command {
public:
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

// Deletes a feature that no other step uses.
class DeleteFeatureCommand final : public Command {
public:
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

// Replaces a feature with an edited copy that has the same id and type.
// Typical use: auto copy = doc.find(id)->clone(); change copy; execute(Edit(copy)).
class EditFeatureCommand final : public Command {
public:
    // `continuesPrevious`: this edit is part of the same continuous action as the
    // edit before it (for example the next value while dragging a size), so the
    // two become one undo step. Separate edits leave it false.
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

// Renames a feature. Names must be non-empty and unique in the document.
class RenameFeatureCommand final : public Command {
public:
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
