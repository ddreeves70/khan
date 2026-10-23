#include "kh/model/Document.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace kh::model {

// ---------------------------------------------------------------------------
// DocumentEditor
// ---------------------------------------------------------------------------

Feature* DocumentEditor::find(const FeatureId& id) {
    for (auto& f : doc_.features_) {
        if (f->id() == id) return f.get();
    }
    return nullptr;
}

void DocumentEditor::insert(std::size_t index, std::unique_ptr<Feature> feature) {
    index = std::min(index, doc_.features_.size());
    doc_.features_.insert(doc_.features_.begin() + static_cast<std::ptrdiff_t>(index), std::move(feature));
}

std::unique_ptr<Feature> DocumentEditor::remove(const FeatureId& id) {
    auto& fs = doc_.features_;
    auto it = std::find_if(fs.begin(), fs.end(), [&](const auto& f) { return f->id() == id; });
    if (it == fs.end()) return nullptr;
    auto out = std::move(*it);
    fs.erase(it);
    return out;
}

std::unique_ptr<Feature> DocumentEditor::replace(std::unique_ptr<Feature> f) {
    for (auto& slot : doc_.features_) {
        if (slot->id() == f->id()) {
            std::swap(slot, f);
            return f;  // now holds the old feature
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// RebuildReport
// ---------------------------------------------------------------------------

const FeatureResult* RebuildReport::result(const FeatureId& id) const {
    auto it = results_.find(id);
    return it == results_.end() ? nullptr : &it->second;
}

const kernel::ShapeHandle* RebuildReport::shapeOf(const FeatureId& id) const {
    const auto* r = result(id);
    return (r && r->status == FeatureStatus::Ok) ? &r->shape : nullptr;
}

bool RebuildReport::ok() const {
    return std::all_of(results_.begin(), results_.end(), [](const auto& kv) {
        return kv.second.status == FeatureStatus::Ok || kv.second.status == FeatureStatus::Suppressed;
    });
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

Document::Document(DocKind kind, std::string name) : Document(DocId::generate(), kind, std::move(name)) {}

Document::Document(DocId id, DocKind kind, std::string name) : id_(id), kind_(kind), name_(std::move(name)) {}

Document::~Document() = default;

const Feature* Document::find(const FeatureId& id) const {
    for (const auto& f : features_) {
        if (f->id() == id) return f.get();
    }
    return nullptr;
}

std::optional<std::size_t> Document::indexOf(const FeatureId& id) const {
    for (std::size_t i = 0; i < features_.size(); ++i) {
        if (features_[i]->id() == id) return i;
    }
    return std::nullopt;
}

std::vector<FeatureId> Document::dependents(const FeatureId& id) const {
    std::vector<FeatureId> out;
    for (const auto& f : features_) {
        const auto deps = f->dependencies();
        if (std::find(deps.begin(), deps.end(), id) != deps.end()) out.push_back(f->id());
    }
    return out;
}

std::string Document::uniqueName(const std::string& base) const {
    for (int n = 1;; ++n) {
        std::string candidate = base + std::to_string(n);
        const bool taken = std::any_of(features_.begin(), features_.end(),
                                       [&](const auto& f) { return f->name() == candidate; });
        if (!taken) return candidate;
    }
}

Diagnostics Document::execute(std::unique_ptr<Command> command) {
    if (!command) return {{"command.missing", Severity::Error, "There was nothing to do.", {}}};

    DocumentEditor editor(*this);
    auto issues = command->apply(editor);
    if (!issues.empty()) return issues;  // contract: the document is unchanged

    undone_.clear();
    const std::uint64_t serial = nextSerial_++;
    if (!done_.empty() && done_.back().command->mergeWith(*command)) {
        done_.back().serial = serial;  // merged: same undo step, new state
    } else {
        done_.push_back({std::move(command), serial});
        if (done_.size() > undoLimit_) done_.erase(done_.begin());
    }
    return {};
}

bool Document::undo() {
    if (done_.empty()) return false;
    DocumentEditor editor(*this);
    done_.back().command->revert(editor);
    undone_.push_back(std::move(done_.back()));
    done_.pop_back();
    return true;
}

bool Document::redo() {
    if (undone_.empty()) return false;
    DocumentEditor editor(*this);
    auto issues = undone_.back().command->apply(editor);
    if (!issues.empty()) return false;  // should not happen: it applied cleanly before
    done_.push_back(std::move(undone_.back()));
    undone_.pop_back();
    return true;
}

std::string Document::undoLabel() const {
    return done_.empty() ? "" : done_.back().command->label();
}

std::string Document::redoLabel() const {
    return undone_.empty() ? "" : undone_.back().command->label();
}

void Document::clearHistory() {
    // The current state becomes the base state: serial 0. A document that was
    // saved in this state stays unmodified.
    const bool wasSaved = !isModified();
    done_.clear();
    undone_.clear();
    savedSerial_ = wasSaved ? 0 : std::numeric_limits<std::uint64_t>::max();
}

void Document::setUndoLimit(std::size_t limit) {
    undoLimit_ = std::max<std::size_t>(limit, 1);
    while (done_.size() > undoLimit_) done_.erase(done_.begin());
}

void Document::markSaved() {
    savedSerial_ = currentSerial();
}

bool Document::isModified() const {
    return currentSerial() != savedSerial_;
}

RebuildReport Document::regenerate(kernel::IKernel& kernel) const {
    RebuildReport report;
    std::set<FeatureId> usedAsInput;

    for (const auto& f : features_) {
        FeatureResult r;
        if (f->suppressed()) {
            r.status = FeatureStatus::Suppressed;
        } else {
            // A feature is blocked if any input has no shape (failed, blocked,
            // suppressed or missing). Name the first such input for the message.
            std::optional<std::string> blocker;  // the message, once one input is found
            for (const auto& dep : f->dependencies()) {
                usedAsInput.insert(dep);
                if (blocker || report.shapeOf(dep)) continue;
                const Feature* d = find(dep);
                if (!d) {
                    blocker = "This step uses a step that no longer exists. Change or delete this step.";
                } else if (d->suppressed()) {
                    blocker = "This step uses " + d->name() + ", which is turned off. Turn " + d->name() +
                              " back on, or change this step.";
                } else {
                    blocker = "This step uses " + d->name() + ", which has a problem. Fix that step first.";
                }
            }
            if (blocker) {
                r.status = FeatureStatus::Blocked;
                r.issues.push_back({"feature.blocked", Severity::Error, *blocker, {}});
            } else {
                auto op = f->execute(kernel, report);
                r.status = op.ok() ? FeatureStatus::Ok : FeatureStatus::Failed;
                r.shape = op.shape;
                r.issues = std::move(op.issues);
            }
        }
        report.order_.push_back(f->id());
        report.results_.emplace(f->id(), std::move(r));
    }

    for (const auto& id : report.order_) {
        if (report.shapeOf(id) && !usedAsInput.count(id)) report.bodies_.push_back(id);
    }
    return report;
}

}  // namespace kh::model
