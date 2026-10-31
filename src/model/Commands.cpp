#include "kh/model/Commands.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
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

SetUnitsCommand::SetUnitsCommand(LengthUnit unit) : unit_(unit) {}

std::string SetUnitsCommand::label() const {
    std::string name(unitName(unit_));
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return "Use " + name;
}

Diagnostics SetUnitsCommand::apply(DocumentEditor& editor) {
    old_ = editor.document().lengthUnit();
    editor.setLengthUnit(unit_);
    return {};
}

void SetUnitsCommand::revert(DocumentEditor& editor) {
    editor.setLengthUnit(old_);
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

namespace kh::model {

// ---------------------------------------------------------------------------
// Copying steps, and AddFeaturesCommand
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<Feature>> freshIds(std::vector<std::unique_ptr<Feature>> steps) {
    std::map<FeatureId, FeatureId> map;
    for (const auto& f : steps) map[f->id()] = FeatureId::generate();
    std::vector<std::unique_ptr<Feature>> out;
    for (const auto& f : steps) {
        auto copy = f->copyWithId(map.at(f->id()));
        copy->remapInputs(map);
        out.push_back(std::move(copy));
    }
    return out;
}

std::vector<std::unique_ptr<Feature>> copySteps(const Document& doc, const std::vector<FeatureId>& ids) {
    // The steps and everything they use, found by following inputs.
    std::set<FeatureId> wanted;
    std::vector<FeatureId> todo(ids.begin(), ids.end());
    while (!todo.empty()) {
        const FeatureId id = todo.back();
        todo.pop_back();
        const auto* f = doc.find(id);
        if (!f || !wanted.insert(id).second) continue;
        for (const auto& dep : f->dependencies()) todo.push_back(dep);
    }
    std::vector<std::unique_ptr<Feature>> steps;
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {  // in design order
        const auto& f = doc.featureAt(i);
        if (wanted.count(f.id())) steps.push_back(f.clone());
    }
    return freshIds(std::move(steps));
}

namespace {

// "Box12" -> "Box"; "Plate" -> "Plate": the name a renamed copy is numbered from.
std::string nameBase(const std::string& name) {
    std::size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
    return end == 0 ? name : name.substr(0, end);
}

}  // namespace

AddFeaturesCommand::AddFeaturesCommand(std::vector<std::unique_ptr<Feature>> features,
                                       std::vector<Material> materials, std::string label)
    : pending_(std::move(features)), materials_(std::move(materials)), label_(std::move(label)) {
    for (const auto& f : pending_) ids_.push_back(f->id());
}

Diagnostics AddFeaturesCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    if (pending_.empty()) return {error("feature.nothing", "There are no steps to add.")};
    for (const auto& f : pending_) {
        if (doc.find(f->id())) return {error("feature.duplicate", "These steps are already in the design.")};
    }
    // Materials first, so the steps can use them.
    auto& list = editor.materials();
    addedMaterials_.clear();
    for (const auto& m : materials_) {
        if (doc.findMaterial(m.key)) continue;  // pasted before: it's already here
        if (const auto* same = doc.findMaterial(m.name)) {
            // The design has its own material with this name: the steps use that one.
            for (auto& f : pending_) {
                if (f->material() == m.key) f->setMaterial(same->key);
            }
            continue;
        }
        list.push_back(m);
        addedMaterials_.push_back(m.key);
    }
    // Then the steps, one after the other, renamed if their names are taken.
    std::size_t added = 0;
    for (auto& f : pending_) {
        if (f->name().empty() || nameTaken(doc, f->name(), f->id()))
            f->setName(doc.uniqueName(nameBase(f->name())));
        if (auto issues = checkInputs(doc, *f, doc.featureCount()); !issues.empty()) {
            // Put everything back: the design must be unchanged.
            for (std::size_t i = added; i-- > 0;) pending_[i] = editor.remove(ids_[i]);
            revertMaterials(editor);
            return issues;
        }
        editor.insert(doc.featureCount(), std::move(f));
        ++added;
    }
    return {};
}

void AddFeaturesCommand::revert(DocumentEditor& editor) {
    for (std::size_t i = ids_.size(); i-- > 0;) pending_[i] = editor.remove(ids_[i]);
    revertMaterials(editor);
}

void AddFeaturesCommand::revertMaterials(DocumentEditor& editor) {
    auto& list = editor.materials();
    for (const auto& key : addedMaterials_) {
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Material& m) { return m.key == key; }),
                   list.end());
    }
    addedMaterials_.clear();
}

}  // namespace kh::model

namespace kh::model {

// ---------------------------------------------------------------------------
// Turning steps off and on
// ---------------------------------------------------------------------------

SuppressFeaturesCommand::SuppressFeaturesCommand(std::vector<FeatureId> ids, bool off)
    : ids_(std::move(ids)), off_(off) {}

std::string SuppressFeaturesCommand::label() const {
    return label_;
}

Diagnostics SuppressFeaturesCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    if (ids_.empty()) {
        return {
            error("feature.nothing", off_ ? "Pick the steps to turn off." : "Pick the steps to turn on.")};
    }
    for (const auto& id : ids_) {
        if (!doc.find(id)) return {error("feature.not_found", "That step isn't in the design anymore.")};
    }
    changed_.clear();
    for (const auto& id : ids_) {
        Feature* f = editor.find(id);
        if (f->suppressed() != off_ && std::find(changed_.begin(), changed_.end(), id) == changed_.end()) {
            changed_.push_back(id);
        }
    }
    if (changed_.empty()) {
        const std::string what = ids_.size() == 1 ? doc.find(ids_[0])->name() + " is" : "Those steps are";
        return {error(off_ ? "feature.already_off" : "feature.already_on",
                      what + (off_ ? " already turned off." : " already turned on."))};
    }
    for (const auto& id : changed_) editor.find(id)->setSuppressed(off_);
    const std::string verb = off_ ? "Turn off " : "Turn on ";
    label_ = verb + (changed_.size() == 1 ? doc.find(changed_[0])->name()
                                          : std::to_string(changed_.size()) + " steps");
    return {};
}

void SuppressFeaturesCommand::revert(DocumentEditor& editor) {
    for (const auto& id : changed_) {
        if (Feature* f = editor.find(id)) f->setSuppressed(!off_);
    }
}

}  // namespace kh::model

namespace kh::model {

// ---------------------------------------------------------------------------
// Folders
// ---------------------------------------------------------------------------

namespace {

std::vector<Folder>::iterator rawFolder(std::vector<Folder>& folders, const FolderId& id) {
    return std::find_if(folders.begin(), folders.end(), [&](const Folder& f) { return f.id == id; });
}

}  // namespace

AddFolderCommand::AddFolderCommand(std::vector<FeatureId> steps, std::string name, FolderId id)
    : steps_(std::move(steps)), askedName_(std::move(name)), id_(id) {}

std::string AddFolderCommand::label() const {
    return "New folder " + name_;
}

Diagnostics AddFolderCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    if (steps_.empty()) return {error("folder.nothing", "Pick the steps to put in the folder.")};
    std::vector<std::pair<std::size_t, FeatureId>> at;
    for (const auto& id : steps_) {
        const auto index = doc.indexOf(id);
        if (!index) return {error("feature.not_found", "That step isn't in the design anymore.")};
        if (const auto in = doc.folderOf(id); !in.isNil()) {
            return {error("folder.step_taken", doc.find(id)->name() + " is already in the folder " +
                                                   doc.findFolder(in)->name +
                                                   ". A step can only be in one folder.")};
        }
        if (std::none_of(at.begin(), at.end(), [&](const auto& p) { return p.second == id; })) {
            at.emplace_back(*index, id);
        }
    }
    std::sort(at.begin(), at.end());
    for (std::size_t i = 1; i < at.size(); ++i) {
        if (at[i].first != at[i - 1].first + 1) {
            return {
                error("folder.not_together",
                      "Only steps next to each other in the feature tree can go in one folder. Pick steps "
                      "with no other steps between them.")};
        }
    }
    if (doc.findFolder(id_)) return {error("folder.duplicate", "That folder is already in the design.")};
    name_ = askedName_.empty() ? doc.uniqueFolderName("Folder") : askedName_;
    if (doc.findFolder(std::string_view(name_))) {
        return {error("folder.name_taken",
                      "Another folder is already called " + name_ + ". Pick a different name.")};
    }
    Folder folder{id_, name_, {}};
    for (const auto& [index, id] : at) folder.steps.push_back(id);
    editor.folders().push_back(std::move(folder));
    return {};
}

void AddFolderCommand::revert(DocumentEditor& editor) {
    auto& folders = editor.folders();
    if (auto it = rawFolder(folders, id_); it != folders.end()) folders.erase(it);
}

RenameFolderCommand::RenameFolderCommand(FolderId id, std::string name)
    : id_(id), newName_(std::move(name)) {}

std::string RenameFolderCommand::label() const {
    return "Rename folder " + oldName_ + " to " + newName_;
}

Diagnostics RenameFolderCommand::apply(DocumentEditor& editor) {
    const auto& doc = editor.document();
    auto& folders = editor.folders();
    auto it = rawFolder(folders, id_);
    if (!doc.findFolder(id_) || it == folders.end()) {
        return {error("folder.not_found", "That folder isn't in the design anymore.")};
    }
    if (newName_.empty()) return {error("folder.name_empty", "Give this folder a name.")};
    if (const auto* other = doc.findFolder(std::string_view(newName_)); other && other->id != id_) {
        return {error("folder.name_taken",
                      "Another folder is already called " + newName_ + ". Pick a different name.")};
    }
    oldName_ = it->name;
    it->name = newName_;
    return {};
}

void RenameFolderCommand::revert(DocumentEditor& editor) {
    auto& folders = editor.folders();
    if (auto it = rawFolder(folders, id_); it != folders.end()) it->name = oldName_;
}

DeleteFolderCommand::DeleteFolderCommand(FolderId id) : id_(id) {}

std::string DeleteFolderCommand::label() const {
    return "Remove folder " + removed_.name;
}

Diagnostics DeleteFolderCommand::apply(DocumentEditor& editor) {
    auto& folders = editor.folders();
    auto it = rawFolder(folders, id_);
    if (!editor.document().findFolder(id_) || it == folders.end()) {
        return {error("folder.not_found", "That folder isn't in the design anymore.")};
    }
    index_ = static_cast<std::size_t>(it - folders.begin());
    removed_ = *it;
    folders.erase(it);
    return {};
}

void DeleteFolderCommand::revert(DocumentEditor& editor) {
    auto& folders = editor.folders();
    folders.insert(folders.begin() + static_cast<std::ptrdiff_t>(std::min(index_, folders.size())), removed_);
}

}  // namespace kh::model

namespace kh::model {

// ---------------------------------------------------------------------------
// Editing sketches
// ---------------------------------------------------------------------------

std::string uniqueSketchName(const sketch::Sketch& sketch, const std::string& base) {
    for (int n = 1;; ++n) {
        std::string candidate = base + std::to_string(n);
        if (!sketch.findEntity(candidate) && !sketch.findConstraint(candidate)) return candidate;
    }
}

EditSketchCommand::EditSketchCommand(FeatureId sketch, Change change)
    : id_(sketch), change_(std::move(change)) {}

RectangleLines addRectangle(sketch::Sketch& s, sketch::Point2 a, sketch::Point2 c, bool construction) {
    using sketch::ConstraintKind;
    using sketch::PointRole;
    const sketch::Point2 corners[4] = {a, {c.x, a.y}, c, {a.x, c.y}};
    RectangleLines out;
    for (int i = 0; i < 4; ++i) {
        const auto id = EntityId::generate();
        out.lines[static_cast<std::size_t>(i)] = id;
        s.entities.push_back({id, uniqueSketchName(s, "Line"),
                              sketch::LineShape{corners[i], corners[(i + 1) % 4]}, construction});
    }
    auto add = [&](ConstraintKind kind, std::vector<sketch::PointRef> points,
                   std::vector<EntityId> entities) {
        sketch::Constraint k;
        k.id = ConstraintId::generate();
        k.kind = kind;
        k.points = std::move(points);
        k.entities = std::move(entities);
        // "Horizontal" for Horizontal, as the sketcher's buttons name them.
        std::string base;
        for (char ch : sketch::kindName(kind)) {
            if (ch != ' ') base += ch;
        }
        k.name = uniqueSketchName(s, base);
        s.constraints.push_back(std::move(k));
    };
    const auto& l = out.lines;
    for (std::size_t i = 0; i < 4; ++i)
        add(ConstraintKind::Coincident, {{l[i], PointRole::End}, {l[(i + 1) % 4], PointRole::Start}}, {});
    add(ConstraintKind::Horizontal, {}, {l[0]});
    add(ConstraintKind::Vertical, {}, {l[1]});
    add(ConstraintKind::Horizontal, {}, {l[2]});
    add(ConstraintKind::Vertical, {}, {l[3]});
    return out;
}

Diagnostics EditSketchCommand::apply(DocumentEditor& editor) {
    Feature* f = editor.find(id_);
    if (!f) return {error("feature.not_found", "That step isn't in the design anymore.")};
    if (f->type() != FeatureType::Sketch) {
        return {error("sketch.not_sketch", f->name() + " isn't a sketch. Pick a sketch, like Sketch1.")};
    }
    auto& step = static_cast<SketchFeature&>(*f);
    if (!after_) {
        sketch::Sketch edited = step.sketch();
        std::string label;
        if (auto problems = change_(edited, label); !problems.empty()) return problems;
        if (auto problems = sketch::check(edited); !problems.empty()) return problems;
        label_ = label.empty() ? "Change " + step.name() : label;
        after_ = std::move(edited);
    }
    before_ = step.sketch();
    step.setSketch(*after_);
    return {};
}

void EditSketchCommand::revert(DocumentEditor& editor) {
    if (Feature* f = editor.find(id_); f && f->type() == FeatureType::Sketch && before_) {
        static_cast<SketchFeature&>(*f).setSketch(*before_);
    }
}

}  // namespace kh::model
