#include "kh/model/Commands.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include "kh/model/Document.h"

namespace kh::model {

namespace {

// A constraint's name base, as the sketcher's buttons name them: "Point on line"
// becomes "PointOnLine".
std::string constraintBaseOf(sketch::ConstraintKind kind) {
    std::string base;
    bool upper = true;
    for (char ch : sketch::kindName(kind)) {
        if (ch == ' ') {
            upper = true;
            continue;
        }
        base += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
        upper = false;
    }
    return base;
}

}  // namespace

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
        k.name = uniqueSketchName(s, constraintBaseOf(kind));
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

namespace {

// Two lines meeting at a corner, for roundCorner() and largestCornerRadius().
struct Corner {
    sketch::PointRef first, second;  // the two lines' joined ends
    ConstraintId joint;              // the Coincident joining them
    sketch::Point2 at, far1, far2;   // the corner, and the lines' other ends
    double len1 = 0, len2 = 0;       // the lines' lengths
    double theta = 0;                // the corner's angle, 0 to pi
};

Diagnostics cornerProblem(std::string message) {
    return {{"sketch.round_corner", Severity::Error, std::move(message), {}}};
}

// Finds the corner at a line's end, or says what's wrong.
std::optional<Corner> findCorner(const sketch::Sketch& s, const sketch::PointRef& at, Diagnostics& problems) {
    using sketch::ConstraintKind;
    using sketch::PointRef;
    using sketch::PointRole;
    auto lineOf = [&](const EntityId& id) -> const sketch::LineShape* {
        const auto* e = s.find(id);
        return e ? std::get_if<sketch::LineShape>(&e->shape) : nullptr;
    };
    auto nameOf = [&](const EntityId& id) { return s.find(id) ? s.find(id)->name : std::string("?"); };
    auto isLineEnd = [&](const PointRef& p) {
        return lineOf(p.entity) && (p.role == PointRole::Start || p.role == PointRole::End);
    };
    if (!isLineEnd(at)) {
        problems =
            cornerProblem("Round a corner where two lines meet: pick a line's start or end, like Line1.end.");
        return std::nullopt;
    }
    const std::string here = nameOf(at.entity) + "'s " + (at.role == PointRole::Start ? "start" : "end");
    Corner c;
    c.first = at;
    int partners = 0;
    for (const auto& k : s.constraints) {
        if (k.kind != ConstraintKind::Coincident || k.points.size() != 2) continue;
        for (int i = 0; i < 2; ++i) {
            const auto& mine = k.points[static_cast<std::size_t>(i)];
            const auto& theirs = k.points[static_cast<std::size_t>(1 - i)];
            if (mine == at && isLineEnd(theirs) && theirs.entity != at.entity) {
                c.second = theirs;
                c.joint = k.id;
                ++partners;
            }
        }
    }
    if (partners == 0) {
        problems = cornerProblem(
            here + " isn't joined to another line's end. Join two lines' ends with Coincident first.");
        return std::nullopt;
    }
    if (partners > 1) {
        problems = cornerProblem("More than two lines meet at " + here +
                                 ", so Khan can't tell which corner to round.");
        return std::nullopt;
    }
    const auto* l1 = lineOf(c.first.entity);
    const auto* l2 = lineOf(c.second.entity);
    c.at = c.first.role == PointRole::Start ? l1->start : l1->end;
    c.far1 = c.first.role == PointRole::Start ? l1->end : l1->start;
    c.far2 = c.second.role == PointRole::Start ? l2->end : l2->start;
    c.len1 = std::hypot(c.far1.x - c.at.x, c.far1.y - c.at.y);
    c.len2 = std::hypot(c.far2.x - c.at.x, c.far2.y - c.at.y);
    if (c.len1 <= 0 || c.len2 <= 0) {
        problems = cornerProblem("A line at that corner has no length, so there's nothing to round.");
        return std::nullopt;
    }
    const sketch::Point2 u1{(c.far1.x - c.at.x) / c.len1, (c.far1.y - c.at.y) / c.len1};
    const sketch::Point2 u2{(c.far2.x - c.at.x) / c.len2, (c.far2.y - c.at.y) / c.len2};
    c.theta = std::atan2(std::abs(u1.x * u2.y - u1.y * u2.x), u1.x * u2.x + u1.y * u2.y);
    if (c.theta < 1e-6 || c.theta > std::numbers::pi - 1e-6) {
        problems = cornerProblem(
            nameOf(c.first.entity) + " and " + nameOf(c.second.entity) +
            " carry straight on at that corner (or fold back on each other), so there's nothing "
            "to round.");
        return std::nullopt;
    }
    return c;
}

}  // namespace

std::optional<double> largestCornerRadius(const sketch::Sketch& sketch, const sketch::PointRef& at) {
    Diagnostics problems;
    const auto c = findCorner(sketch, at, problems);
    if (!c) return std::nullopt;
    return std::min(c->len1, c->len2) * std::tan(c->theta / 2);
}

Diagnostics roundCorner(sketch::Sketch& s, const sketch::PointRef& at, double radius, RoundedCorner* made) {
    using sketch::ConstraintKind;
    using sketch::PointRef;
    using sketch::PointRole;
    auto nameOf = [&](const EntityId& id) { return s.find(id) ? s.find(id)->name : std::string("?"); };
    auto lineOf = [&](const EntityId& id) -> sketch::LineShape* {
        for (auto& e : s.entities) {
            if (e.id == id) return std::get_if<sketch::LineShape>(&e.shape);
        }
        return nullptr;
    };
    Diagnostics problems;
    const auto found = findCorner(s, at, problems);
    if (!found) return problems;
    if (!(radius > 0))
        return cornerProblem("A corner's radius must be more than 0 mm. Type a bigger number.");
    const auto& corner = *found;
    const auto other = std::optional<PointRef>(corner.second);
    const ConstraintId joint = corner.joint;
    auto* l1 = lineOf(at.entity);
    auto* l2 = lineOf(other->entity);
    auto cornerOf = [](sketch::LineShape& l, PointRole r) -> sketch::Point2& {
        return r == PointRole::Start ? l.start : l.end;
    };
    const sketch::Point2 p = corner.at, f1 = corner.far1, f2 = corner.far2;
    const double len1 = corner.len1, len2 = corner.len2, theta = corner.theta;
    const sketch::Point2 u1{(f1.x - p.x) / len1, (f1.y - p.y) / len1},
        u2{(f2.x - p.x) / len2, (f2.y - p.y) / len2};
    auto problem = [](std::string message) { return cornerProblem(std::move(message)); };
    const double cut = radius / std::tan(theta / 2);  // how far back each line is cut
    if (cut >= std::min(len1, len2)) {
        const double most = std::min(len1, len2) * std::tan(theta / 2);
        std::ostringstream fit;
        fit.precision(3);
        fit << "A " << radius << " mm radius doesn't fit that corner: it would cut back past the end of "
            << nameOf(len1 <= len2 ? at.entity : other->entity) << ". Use a radius under " << most << " mm.";
        return problem(fit.str());
    }
    const sketch::Point2 t1{p.x + u1.x * cut, p.y + u1.y * cut}, t2{p.x + u2.x * cut, p.y + u2.y * cut};
    const double bl = std::hypot(u1.x + u2.x, u1.y + u2.y);
    const sketch::Point2 bisector{(u1.x + u2.x) / bl, (u1.y + u2.y) / bl};
    const double out = radius / std::sin(theta / 2);
    const sketch::Point2 center{p.x + bisector.x * out, p.y + bisector.y * out};
    // Counterclockwise the short way round, from whichever tangent point that starts at.
    double a1 = std::atan2(t1.y - center.y, t1.x - center.x),
           a2 = std::atan2(t2.y - center.y, t2.x - center.x);
    double sweep = a2 - a1;
    while (sweep <= 0) sweep += 2 * std::numbers::pi;
    const bool fromFirst = sweep <= std::numbers::pi;
    const double start = fromFirst ? a1 : a2;
    double end = fromFirst ? a1 + sweep : a2 + (2 * std::numbers::pi - sweep);

    // The lines, cut back to where the arc meets them.
    cornerOf(*l1, at.role) = t1;
    cornerOf(*l2, other->role) = t2;
    const EntityId arcId = EntityId::generate(), cornerId = EntityId::generate();
    s.entities.push_back(
        {arcId, uniqueSketchName(s, "Arc"), sketch::ArcShape{center, radius, start, end}, false});
    s.entities.push_back({cornerId, uniqueSketchName(s, "Point"), sketch::PointShape{p}, true});

    // What was on the corner moves to the helper point; Lengths of the two lines
    // become distances from their far ends to it.
    const PointRef first = at, second = *other, sharp{cornerId, PointRole::Self};
    std::erase_if(s.constraints, [&](const sketch::Constraint& c) { return c.id == joint; });
    for (auto& c : s.constraints) {
        for (auto& q : c.points) {
            if (q == first || q == second) q = sharp;
        }
        if (c.kind == ConstraintKind::Length && c.entities.size() == 1 &&
            (c.entities[0] == first.entity || c.entities[0] == second.entity)) {
            const bool one = c.entities[0] == first.entity;
            const auto role = one ? first.role : second.role;
            c.kind = ConstraintKind::Distance;
            c.points = {{c.entities[0], role == PointRole::Start ? PointRole::End : PointRole::Start}, sharp};
            c.entities.clear();
        }
    }
    auto add = [&](ConstraintKind kind, std::vector<PointRef> points, std::vector<EntityId> entities,
                   double value = 0) {
        sketch::Constraint k;
        k.id = ConstraintId::generate();
        k.kind = kind;
        k.points = std::move(points);
        k.entities = std::move(entities);
        k.value = value;
        k.name = uniqueSketchName(s, constraintBaseOf(kind));
        s.constraints.push_back(std::move(k));
        return s.constraints.back().id;
    };
    add(ConstraintKind::PointOnLine, {sharp}, {first.entity});
    add(ConstraintKind::PointOnLine, {sharp}, {second.entity});
    add(ConstraintKind::Coincident, {first, {arcId, fromFirst ? PointRole::Start : PointRole::End}}, {});
    add(ConstraintKind::Coincident, {second, {arcId, fromFirst ? PointRole::End : PointRole::Start}}, {});
    add(ConstraintKind::Tangent, {}, {first.entity, arcId});
    add(ConstraintKind::Tangent, {}, {arcId, second.entity});
    const auto radiusId = add(ConstraintKind::Radius, {}, {arcId}, radius);
    if (made) *made = {arcId, cornerId, radiusId};
    return {};
}

namespace {

constexpr double kTwoPi = 2 * std::numbers::pi;

// An angle moved into [0, 2 pi).
double turn(double a) {
    a = std::fmod(a, kTwoPi);
    return a < 0 ? a + kTwoPi : a;
}

// Where an arc's sweep (from its start, counterclockwise) reaches an angle, or
// nothing if the angle isn't on the arc.
std::optional<double> onArc(const sketch::ArcShape& arc, double angle) {
    const double along = turn(angle - arc.startAngle);
    const double sweep = arc.endAngle - arc.startAngle;
    if (along > sweep + 1e-12) return std::nullopt;
    return along;
}

// The points where a line through a and b (all of it, both ways) meets a circle.
std::vector<sketch::Point2> lineMeetsCircle(sketch::Point2 a, sketch::Point2 b, sketch::Point2 c, double r) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double fx = a.x - c.x, fy = a.y - c.y;
    const double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - r * r;
    const double disc = B * B - 4 * A * C;
    if (A <= 0 || disc < 0) return {};
    std::vector<sketch::Point2> out;
    for (double sign : {-1.0, 1.0}) {
        const double t = (-B + sign * std::sqrt(disc)) / (2 * A);
        out.push_back({a.x + t * dx, a.y + t * dy});
        if (disc == 0) break;
    }
    return out;
}

// The points where two circles meet.
std::vector<sketch::Point2> circlesMeet(sketch::Point2 c0, double r0, sketch::Point2 c1, double r1) {
    const double d = std::hypot(c1.x - c0.x, c1.y - c0.y);
    if (d <= 0 || d > r0 + r1 || d < std::abs(r0 - r1)) return {};
    const double a = (r0 * r0 - r1 * r1 + d * d) / (2 * d);
    const double h = std::sqrt(std::max(0.0, r0 * r0 - a * a));
    const sketch::Point2 m{c0.x + a * (c1.x - c0.x) / d, c0.y + a * (c1.y - c0.y) / d};
    const sketch::Point2 off{-(c1.y - c0.y) * h / d, (c1.x - c0.x) * h / d};
    if (h == 0) return {m};
    return {{m.x + off.x, m.y + off.y}, {m.x - off.x, m.y - off.y}};
}

// A circle's or arc's center and radius.
bool roundOf(const sketch::Shape& s, sketch::Point2& c, double& r) {
    if (const auto* ci = std::get_if<sketch::CircleShape>(&s)) {
        c = ci->center;
        r = ci->radius;
        return true;
    }
    if (const auto* a = std::get_if<sketch::ArcShape>(&s)) {
        c = a->center;
        r = a->radius;
        return true;
    }
    return false;
}

// Whether a point found on another entity's line or circle is on its actual
// piece: between a line's ends, within an arc's sweep.
bool onPiece(const sketch::Shape& s, sketch::Point2 p) {
    if (const auto* l = std::get_if<sketch::LineShape>(&s)) {
        const double dx = l->end.x - l->start.x, dy = l->end.y - l->start.y, len2 = dx * dx + dy * dy;
        if (len2 <= 0) return false;
        const double u = ((p.x - l->start.x) * dx + (p.y - l->start.y) * dy) / len2;
        return u >= -1e-9 && u <= 1 + 1e-9;
    }
    if (const auto* a = std::get_if<sketch::ArcShape>(&s))
        return onArc(*a, std::atan2(p.y - a->center.y, p.x - a->center.x)).has_value();
    return std::holds_alternative<sketch::CircleShape>(s);
}

// Where something crosses the entity being trimmed: how far along it (a line's
// 0 to 1, an arc's angle from its start, a circle's angle) and what crosses it.
struct Crossing {
    double at;
    EntityId by;
};

// How far along an entity a point is, in the terms Crossing uses.
double alongOf(const sketch::Shape& s, sketch::Point2 p) {
    if (const auto* l = std::get_if<sketch::LineShape>(&s)) {
        const double dx = l->end.x - l->start.x, dy = l->end.y - l->start.y, len2 = dx * dx + dy * dy;
        return len2 > 0 ? ((p.x - l->start.x) * dx + (p.y - l->start.y) * dy) / len2 : 0;
    }
    sketch::Point2 c;
    double r = 0;
    roundOf(s, c, r);
    const double angle = std::atan2(p.y - c.y, p.x - c.x);
    if (const auto* a = std::get_if<sketch::ArcShape>(&s)) {
        const double sweep = a->endAngle - a->startAngle;
        const double along = turn(angle - a->startAngle);
        // Off the arc: count it from whichever end is nearer.
        return along <= sweep ? along : (along - sweep < kTwoPi - along ? sweep : 0.0);
    }
    return turn(angle);
}

std::vector<Crossing> crossings(const sketch::Sketch& s, const sketch::Entity& target) {
    std::vector<Crossing> out;
    const auto& t = target.shape;
    for (const auto& other : s.entities) {
        if (other.id == target.id) continue;
        const auto& o = other.shape;
        std::vector<sketch::Point2> points;
        sketch::Point2 tc, oc;
        double tr = 0, orr = 0;
        const auto* tl = std::get_if<sketch::LineShape>(&t);
        const auto* ol = std::get_if<sketch::LineShape>(&o);
        if (tl && ol) {
            const double dx = tl->end.x - tl->start.x, dy = tl->end.y - tl->start.y;
            const double ex = ol->end.x - ol->start.x, ey = ol->end.y - ol->start.y;
            const double den = dx * ey - dy * ex;
            if (std::abs(den) > 1e-12) {
                const double u = ((ol->start.x - tl->start.x) * dy - (ol->start.y - tl->start.y) * dx) / den;
                if (u >= -1e-9 && u <= 1 + 1e-9)
                    points.push_back({ol->start.x + u * ex, ol->start.y + u * ey});
            }
        } else if (tl && roundOf(o, oc, orr)) {
            points = lineMeetsCircle(tl->start, tl->end, oc, orr);
        } else if (ol && roundOf(t, tc, tr)) {
            points = lineMeetsCircle(ol->start, ol->end, tc, tr);
        } else if (roundOf(t, tc, tr) && roundOf(o, oc, orr)) {
            points = circlesMeet(tc, tr, oc, orr);
        }
        for (const auto& p : points) {
            if (!onPiece(o, p)) continue;
            // On the target's own piece, and not at one of its ends (nothing to cut there).
            if (std::holds_alternative<sketch::LineShape>(t)) {
                const double u = alongOf(t, p);
                if (u <= 1e-9 || u >= 1 - 1e-9) continue;
            } else if (const auto* a = std::get_if<sketch::ArcShape>(&t)) {
                const auto along = onArc(*a, std::atan2(p.y - a->center.y, p.x - a->center.x));
                if (!along || *along <= 1e-9 || *along >= a->endAngle - a->startAngle - 1e-9) continue;
            }
            out.push_back({alongOf(t, p), other.id});
        }
    }
    std::sort(out.begin(), out.end(), [](const Crossing& a, const Crossing& b) { return a.at < b.at; });
    return out;
}

// What trimming would do.
struct TrimPlan {
    enum Kind { Delete, CutStart, CutEnd, Split, CircleToArc } kind = Delete;
    double from = 0, to = 0;    // the removed piece, in Crossing terms
    EntityId byFrom{}, byTo{};  // what cuts it there
};

std::optional<TrimPlan> planTrim(const sketch::Sketch& s, const sketch::Entity& e, sketch::Point2 near,
                                 Diagnostics& problems) {
    const auto& shape = e.shape;
    const bool line = std::holds_alternative<sketch::LineShape>(shape);
    const bool arc = std::holds_alternative<sketch::ArcShape>(shape);
    const bool circle = std::holds_alternative<sketch::CircleShape>(shape);
    if (!line && !arc && !circle) {
        problems.push_back({"sketch.trim",
                            Severity::Error,
                            "Trim works on lines, circles and arcs. " + e.name + " isn't one of those.",
                            {}});
        return std::nullopt;
    }
    const auto cuts = crossings(s, e);
    TrimPlan plan;
    if (circle) {
        if (cuts.empty()) return plan;  // nothing crosses it: delete it
        if (cuts.size() == 1) {
            problems.push_back({"sketch.trim",
                                Severity::Error,
                                e.name +
                                    " is crossed in only one place, so there's no piece to cut off. Draw "
                                    "something else across it first.",
                                {}});
            return std::nullopt;
        }
        // The gap between crossings (going round) that the point is in.
        const double here = alongOf(shape, near);
        std::size_t k = 0;
        while (k < cuts.size() && cuts[k].at <= here) ++k;
        const auto& before = k == 0 ? cuts.back() : cuts[k - 1];
        const auto& after = k == cuts.size() ? cuts.front() : cuts[k];
        plan.kind = TrimPlan::CircleToArc;
        plan.from = before.at;
        plan.byFrom = before.by;
        plan.to = after.at;
        plan.byTo = after.by;
        return plan;
    }
    const double here = alongOf(shape, near);
    const double end =
        line ? 1.0
             : std::get<sketch::ArcShape>(shape).endAngle - std::get<sketch::ArcShape>(shape).startAngle;
    std::optional<Crossing> below, above;
    for (const auto& c : cuts) {
        if (c.at < here) below = c;
        if (c.at > here && !above) above = c;
    }
    if (!below && !above) return plan;  // nothing crosses it: delete it
    if (below && above) {
        plan.kind = TrimPlan::Split;
        plan.from = below->at;
        plan.byFrom = below->by;
        plan.to = above->at;
        plan.byTo = above->by;
    } else if (above) {
        plan.kind = TrimPlan::CutStart;
        plan.from = 0;
        plan.to = above->at;
        plan.byTo = above->by;
    } else {
        plan.kind = TrimPlan::CutEnd;
        plan.from = below->at;
        plan.byFrom = below->by;
        plan.to = end;
    }
    return plan;
}

// The point a distance along an entity (in Crossing terms).
sketch::Point2 pointAlong(const sketch::Shape& s, double at) {
    if (const auto* l = std::get_if<sketch::LineShape>(&s))
        return {l->start.x + at * (l->end.x - l->start.x), l->start.y + at * (l->end.y - l->start.y)};
    sketch::Point2 c;
    double r = 0;
    roundOf(s, c, r);
    const double base =
        std::holds_alternative<sketch::ArcShape>(s) ? std::get<sketch::ArcShape>(s).startAngle : 0.0;
    return {c.x + r * std::cos(base + at), c.y + r * std::sin(base + at)};
}

// The piece between two distances along an entity.
sketch::Shape pieceOf(const sketch::Shape& s, double from, double to) {
    if (std::holds_alternative<sketch::LineShape>(s))
        return sketch::LineShape{pointAlong(s, from), pointAlong(s, to)};
    sketch::Point2 c;
    double r = 0;
    roundOf(s, c, r);
    const double base =
        std::holds_alternative<sketch::ArcShape>(s) ? std::get<sketch::ArcShape>(s).startAngle : 0.0;
    double a0 = base + from, a1 = base + to;
    while (a1 <= a0) a1 += kTwoPi;  // a circle's gap can run past 0
    return sketch::ArcShape{c, r, a0, a1};
}

}  // namespace

std::optional<sketch::Shape> trimmedPiece(const sketch::Sketch& s, const EntityId& id, sketch::Point2 near) {
    const auto* e = s.find(id);
    if (!e) return std::nullopt;
    Diagnostics problems;
    const auto plan = planTrim(s, *e, near, problems);
    if (!plan) return std::nullopt;
    if (plan->kind == TrimPlan::Delete) return e->shape;
    return pieceOf(e->shape, plan->from, plan->to);
}

Diagnostics trim(sketch::Sketch& s, const EntityId& entityId, sketch::Point2 near) {
    // A copy: the caller's id may live in the sketch, which moves when a piece is added.
    const EntityId id = entityId;
    using sketch::ConstraintKind;
    using sketch::PointRef;
    using sketch::PointRole;
    const auto* found = s.find(id);
    if (!found) return {{"sketch.not_found", Severity::Error, "That isn't in the sketch anymore.", {}}};
    Diagnostics problems;
    const auto planned = planTrim(s, *found, near, problems);
    if (!planned) return problems;
    const auto plan = *planned;
    auto uses = [](const sketch::Constraint& c, const EntityId& e) {
        return std::ranges::find(c.entities, e) != c.entities.end() ||
               std::ranges::any_of(c.points, [&](const PointRef& p) { return p.entity == e; });
    };
    if (plan.kind == TrimPlan::Delete) {
        std::erase_if(s.constraints, [&](const sketch::Constraint& c) { return uses(c, id); });
        std::erase_if(s.entities, [&](const sketch::Entity& e) { return e.id == id; });
        return {};
    }
    auto entity = [&](const EntityId& e) -> sketch::Entity& {
        return *std::ranges::find_if(s.entities, [&](const sketch::Entity& x) { return x.id == e; });
    };
    auto add = [&](ConstraintKind kind, std::vector<PointRef> points, std::vector<EntityId> entities) {
        sketch::Constraint k;
        k.id = ConstraintId::generate();
        k.kind = kind;
        k.points = std::move(points);
        k.entities = std::move(entities);
        k.name = uniqueSketchName(s, constraintBaseOf(kind));
        s.constraints.push_back(std::move(k));
    };
    // A cut end stays on what cut it.
    auto keepOn = [&](const PointRef& p, const EntityId& by) {
        const auto* cutter = s.find(by);
        if (!cutter) return;
        add(std::holds_alternative<sketch::LineShape>(cutter->shape) ? ConstraintKind::PointOnLine
                                                                     : ConstraintKind::PointOnCircle,
            {p}, {by});
    };
    auto dropOn = [&](const PointRef& p) {
        std::erase_if(s.constraints, [&](const sketch::Constraint& c) {
            return std::ranges::any_of(c.points, [&](const PointRef& q) { return q == p; });
        });
    };
    const sketch::Shape before = found->shape;
    const bool line = std::holds_alternative<sketch::LineShape>(before);
    // A trimmed line's Length no longer says what it said.
    if (line) {
        std::erase_if(s.constraints, [&](const sketch::Constraint& c) {
            return c.kind == ConstraintKind::Length && c.entities.size() == 1 && c.entities[0] == id;
        });
    }
    auto setStart = [&](sketch::Shape& shape, double at) {
        if (auto* l = std::get_if<sketch::LineShape>(&shape)) l->start = pointAlong(before, at);
        if (auto* a = std::get_if<sketch::ArcShape>(&shape)) {
            const double base = std::get<sketch::ArcShape>(before).startAngle;
            a->startAngle = base + at;
        }
    };
    auto setEnd = [&](sketch::Shape& shape, double at) {
        if (auto* l = std::get_if<sketch::LineShape>(&shape)) l->end = pointAlong(before, at);
        if (auto* a = std::get_if<sketch::ArcShape>(&shape)) {
            const double base = std::get<sketch::ArcShape>(before).startAngle;
            a->endAngle = base + at;
        }
    };
    switch (plan.kind) {
        case TrimPlan::Delete:
            break;
        case TrimPlan::CutStart:
            dropOn({id, PointRole::Start});
            setStart(entity(id).shape, plan.to);
            keepOn({id, PointRole::Start}, plan.byTo);
            break;
        case TrimPlan::CutEnd:
            dropOn({id, PointRole::End});
            setEnd(entity(id).shape, plan.from);
            keepOn({id, PointRole::End}, plan.byFrom);
            break;
        case TrimPlan::Split: {
            // This one keeps the start; a new one takes the end, and what was on the end.
            const EntityId piece = EntityId::generate();
            sketch::Shape second = before;
            setStart(second, plan.to);
            const auto& original = entity(id);
            s.entities.push_back(
                {piece, uniqueSketchName(s, line ? "Line" : "Arc"), second, original.construction});
            for (auto& c : s.constraints) {
                for (auto& q : c.points) {
                    if (q == PointRef{id, PointRole::End}) q = {piece, PointRole::End};
                }
            }
            setEnd(entity(id).shape, plan.from);
            keepOn({id, PointRole::End}, plan.byFrom);
            keepOn({piece, PointRole::Start}, plan.byTo);
            if (line) {
                // Kept in line with the first piece.
                add(ConstraintKind::PointOnLine, {{piece, PointRole::End}}, {id});
                add(ConstraintKind::PointOnLine, {{piece, PointRole::Start}}, {id});
            } else {
                // On the same circle.
                add(ConstraintKind::Coincident, {{piece, PointRole::Center}, {id, PointRole::Center}}, {});
                add(ConstraintKind::Equal, {}, {id, piece});
            }
            break;
        }
        case TrimPlan::CircleToArc: {
            // What's left goes from where the gap ends round to where it starts.
            const auto& c = std::get<sketch::CircleShape>(before);
            double a0 = plan.to, a1 = plan.from;
            while (a1 <= a0) a1 += kTwoPi;
            entity(id).shape = sketch::ArcShape{c.center, c.radius, a0, a1};
            keepOn({id, PointRole::Start}, plan.byTo);
            keepOn({id, PointRole::End}, plan.byFrom);
            break;
        }
    }
    return {};
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
