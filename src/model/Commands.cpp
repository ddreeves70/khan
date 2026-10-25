#include "kh/model/Commands.h"

#include <algorithm>
#include <cctype>
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

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

namespace {

bool sameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

SetMaterialCommand::SetMaterialCommand(FeatureId id, std::string keyOrName)
    : id_(id), wanted_(std::move(keyOrName)) {}

std::string SetMaterialCommand::label() const {
    return material_.empty() ? "Clear material of " + name_ : "Make " + name_ + " " + material_;
}

Diagnostics SetMaterialCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    Feature* f = editor.find(id_);
    if (!f) return {error("feature.not_found", "That step isn't in the design anymore.")};
    std::string key;
    material_.clear();
    if (!wanted_.empty()) {
        const Material* m = doc.findMaterial(wanted_);
        if (!m) {
            return {error("material.unknown", "There is no material called " + wanted_ +
                                                  ". See the list with kh_cli materials, or add your own "
                                                  "with material.define.")};
        }
        key = m->key;
        material_ = m->name;
    }
    name_ = f->name();
    oldKey_ = f->material();
    f->setMaterial(key);
    return {};
}

void SetMaterialCommand::revert(DocumentEditor& editor) {
    if (Feature* f = editor.find(id_)) f->setMaterial(oldKey_);
}

DefineMaterialCommand::DefineMaterialCommand(Material material, std::string badColor)
    : material_(std::move(material)), badColor_(std::move(badColor)) {
    material_.builtin = false;
}

std::string DefineMaterialCommand::label() const {
    return (old_ ? "Change material " : "Add material ") + material_.name;
}

Diagnostics DefineMaterialCommand::apply(DocumentEditor& editor) {
    if (!badColor_.empty()) {
        return {error("material.color", "Khan can't read the color " + badColor_ +
                                            ". Write it as # and six hex digits, like #3366cc.")};
    }
    if (auto issues = checkMaterial(material_); !issues.empty()) return issues;
    if (findBuiltinMaterial(material_.name)) {
        return {error("material.builtin", material_.name +
                                              " is one of Khan's own materials, which can't be changed. Give "
                                              "yours another name, like My " +
                                              material_.name + ".")};
    }
    auto& list = editor.materials();
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const Material& m) { return sameName(m.name, material_.name); });
    if (it != list.end()) {
        old_ = *it;
        material_.key = it->key;  // steps that use it keep using it
        index_ = static_cast<std::size_t>(it - list.begin());
        *it = material_;
    } else {
        old_.reset();
        if (material_.key.empty()) material_.key = MaterialId::generate().toString();  // same id on redo
        index_ = list.size();
        list.push_back(material_);
    }
    return {};
}

void DefineMaterialCommand::revert(DocumentEditor& editor) {
    auto& list = editor.materials();
    if (index_ >= list.size()) return;
    if (old_) {
        list[index_] = *old_;
    } else {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(index_));
    }
}

DeleteMaterialCommand::DeleteMaterialCommand(std::string keyOrName) : wanted_(std::move(keyOrName)) {}

std::string DeleteMaterialCommand::label() const {
    return "Delete material " + (removed_ ? removed_->name : wanted_);
}

Diagnostics DeleteMaterialCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    auto& list = editor.materials();
    const auto it = std::find_if(list.begin(), list.end(), [&](const Material& m) {
        return m.key == wanted_ || sameName(m.name, wanted_);
    });
    if (it == list.end()) {
        if (findBuiltinMaterial(wanted_)) {
            return {error("material.builtin",
                          wanted_ + " is one of Khan's own materials, which can't be deleted.")};
        }
        return {error("material.unknown", "This design has no material of its own called " + wanted_ + ".")};
    }
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const auto& f = doc.featureAt(i);
        if (f.material() == it->key) {
            return {error("material.in_use", it->name + " is used by " + f.name() +
                                                 ". Pick another material for " + f.name() + " first.")};
        }
    }
    index_ = static_cast<std::size_t>(it - list.begin());
    removed_ = *it;
    list.erase(it);
    return {};
}

void DeleteMaterialCommand::revert(DocumentEditor& editor) {
    if (!removed_) return;
    auto& list = editor.materials();
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index_, list.size())), *removed_);
}

}  // namespace kh::model
