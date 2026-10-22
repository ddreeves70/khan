#include "kh/model/Commands.h"

#include <algorithm>
#include <utility>

#include "kh/model/Document.h"

namespace kh::model {

namespace {

Diagnostic error(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

// Every input of `f` must exist and come before position `index`.
Diagnostics checkInputs(const Document& doc, const Feature& f, std::size_t index) {
    for (const auto& dep : f.dependencies()) {
        const auto at = doc.indexOf(dep);
        if (!at) return {error("feature.input_missing", "This step uses a step that doesn't exist.")};
        if (*at >= index) {
            return {error("feature.input_order", "This step uses " + doc.featureAt(*at).name() +
                                                     ", which comes after it. A step can only use "
                                                     "earlier steps.")};
        }
    }
    return {};
}

bool nameTaken(const Document& doc, const std::string& name, const FeatureId& except) {
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const auto& f = doc.featureAt(i);
        if (f.name() == name && f.id() != except) return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// AddFeatureCommand
// ---------------------------------------------------------------------------

AddFeatureCommand::AddFeatureCommand(std::unique_ptr<Feature> feature, std::optional<std::size_t> index)
    : pending_(std::move(feature)), id_(pending_->id()), name_(pending_->name()), index_(index) {}

std::string AddFeatureCommand::label() const {
    return "Add " + name_;
}

Diagnostics AddFeatureCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    if (doc.find(id_)) return {error("feature.duplicate", "This step is already in the design.")};
    if (name_.empty()) return {error("feature.name_empty", "Give this step a name.")};
    if (nameTaken(doc, name_, id_)) {
        return {error("feature.name_taken",
                      "Another step is already called " + name_ + ". Pick a different name.")};
    }
    const std::size_t at = std::min(index_.value_or(doc.featureCount()), doc.featureCount());
    if (auto issues = checkInputs(doc, *pending_, at); !issues.empty()) return issues;

    insertedAt_ = at;
    editor.insert(at, std::move(pending_));
    return {};
}

void AddFeatureCommand::revert(DocumentEditor& editor) {
    pending_ = editor.remove(id_);
}

// ---------------------------------------------------------------------------
// DeleteFeatureCommand
// ---------------------------------------------------------------------------

DeleteFeatureCommand::DeleteFeatureCommand(FeatureId id) : id_(id) {}

std::string DeleteFeatureCommand::label() const {
    return "Delete " + name_;
}

Diagnostics DeleteFeatureCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    const auto at = doc.indexOf(id_);
    if (!at) return {error("feature.not_found", "That step isn't in the design anymore.")};
    name_ = doc.featureAt(*at).name();

    const auto users = doc.dependents(id_);
    if (!users.empty()) {
        const auto& user = *doc.find(users.front());
        return {error("feature.in_use", name_ + " is used by " + user.name() + ". Delete or change " +
                                            user.name() + " first.")};
    }
    index_ = *at;
    removed_ = editor.remove(id_);
    return {};
}

void DeleteFeatureCommand::revert(DocumentEditor& editor) {
    editor.insert(index_, std::move(removed_));  // same position, same id
}

// ---------------------------------------------------------------------------
// EditFeatureCommand
// ---------------------------------------------------------------------------

EditFeatureCommand::EditFeatureCommand(std::unique_ptr<Feature> replacement, bool continuesPrevious)
    : other_(std::move(replacement)),
      id_(other_->id()),
      name_(other_->name()),
      continuesPrevious_(continuesPrevious) {}

std::string EditFeatureCommand::label() const {
    return "Edit " + name_;
}

Diagnostics EditFeatureCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    const auto at = doc.indexOf(id_);
    if (!at) return {error("feature.not_found", "That step isn't in the design anymore.")};
    const auto& current = doc.featureAt(*at);
    if (current.type() != other_->type()) {
        return {error("feature.type_changed", "A step can't change into a different kind of step.")};
    }
    if (other_->name().empty()) return {error("feature.name_empty", "Give this step a name.")};
    if (nameTaken(doc, other_->name(), id_)) {
        return {error("feature.name_taken",
                      "Another step is already called " + other_->name() + ". Pick a different name.")};
    }
    if (auto issues = checkInputs(doc, *other_, *at); !issues.empty()) return issues;

    other_ = editor.replace(std::move(other_));  // swap in; keep the old version for revert
    return {};
}

void EditFeatureCommand::revert(DocumentEditor& editor) {
    other_ = editor.replace(std::move(other_));
}

bool EditFeatureCommand::mergeWith(const Command& next) {
    const auto* edit = dynamic_cast<const EditFeatureCommand*>(&next);
    if (!edit || !edit->continuesPrevious_ || edit->id_ != id_) return false;
    // `next` has been applied and holds the version it replaced (our new state).
    // Keeping our own `other_` (the state before the first edit) makes one undo
    // step go all the way back.
    return true;
}

// ---------------------------------------------------------------------------
// RenameFeatureCommand
// ---------------------------------------------------------------------------

RenameFeatureCommand::RenameFeatureCommand(FeatureId id, std::string newName)
    : id_(id), newName_(std::move(newName)) {}

std::string RenameFeatureCommand::label() const {
    return "Rename " + oldName_ + " to " + newName_;
}

Diagnostics RenameFeatureCommand::apply(DocumentEditor& editor) {
    Feature* f = editor.find(id_);
    if (!f) return {error("feature.not_found", "That step isn't in the design anymore.")};
    if (newName_.empty()) return {error("feature.name_empty", "Give this step a name.")};
    if (nameTaken(editor.document(), newName_, id_)) {
        return {error("feature.name_taken",
                      "Another step is already called " + newName_ + ". Pick a different name.")};
    }
    oldName_ = f->name();
    f->setName(newName_);
    return {};
}

void RenameFeatureCommand::revert(DocumentEditor& editor) {
    if (Feature* f = editor.find(id_)) f->setName(oldName_);
}

}  // namespace kh::model
