#include "kh/plugin/DocumentList.h"

#include <algorithm>
#include <utility>

#include "kh/plugin/Events.h"

namespace kh::plugin {

namespace {

Diagnostic notOpen() {
    return {"document.not_open",
            Severity::Error,
            "That design isn't open any more. Open it again and retry.",
            {}};
}

}  // namespace

DocumentList::DocumentList(EventBus& bus, const model::CommandRegistry& commands, kernel::IKernel* kernel)
    : bus_(bus), commands_(commands), kernel_(kernel) {}

DocumentList::~DocumentList() = default;

DocumentList::Entry* DocumentList::entry(const DocId& id) {
    auto it = docs_.find(id);
    return it == docs_.end() ? nullptr : &it->second;
}

DocId DocumentList::add(std::unique_ptr<model::Document> doc) {
    if (!doc) return {};
    const DocId id = doc->id();
    if (docs_.count(id)) return id;  // already open: the same design, opened twice
    order_.push_back(id);
    docs_[id].doc = std::move(doc);
    bus_.publish(DocumentOpened{id});
    // Look it up again after each event: a listener may have closed it.
    if (auto* e = entry(id)) rebuild(*e);
    if (active_.isNil() && docs_.count(id)) setActive(id);
    return id;
}

bool DocumentList::close(const DocId& id) {
    auto it = docs_.find(id);
    if (it == docs_.end()) return false;
    // Take it out first, so listeners never see a closed design in the list.
    auto closing = std::move(it->second);
    docs_.erase(it);
    order_.erase(std::find(order_.begin(), order_.end(), id));
    bus_.publish(DocumentClosed{id});
    if (active_ == id) {
        active_ = order_.empty() ? DocId{} : order_.front();
        bus_.publish(ActiveDocumentChanged{active_});
    }
    return true;
}

std::vector<DocId> DocumentList::ids() const {
    return order_;
}

const model::Document* DocumentList::find(const DocId& id) const {
    auto it = docs_.find(id);
    return it == docs_.end() ? nullptr : it->second.doc.get();
}

bool DocumentList::setActive(const DocId& id) {
    if (!docs_.count(id)) return false;
    if (active_ == id) return true;
    active_ = id;
    bus_.publish(ActiveDocumentChanged{id});
    return true;
}

Diagnostics DocumentList::execute(const DocId& id, std::unique_ptr<model::Command> command) {
    auto* e = entry(id);
    if (!e) return {notOpen()};
    const std::string label = command ? command->label() : std::string();
    auto issues = e->doc->execute(std::move(command));
    if (issues.empty()) changed(*e, ChangeKind::Executed, label);
    return issues;
}

Diagnostics DocumentList::run(const DocId& id, std::string_view name, const model::Args& args) {
    auto* e = entry(id);
    if (!e) return {notOpen()};
    auto made = commands_.create(name, args, *e->doc);
    if (!made.ok()) return std::move(made.issues);
    return execute(id, std::move(made.command));
}

bool DocumentList::undo(const DocId& id) {
    auto* e = entry(id);
    if (!e) return false;
    const auto label = e->doc->undoLabel();
    if (!e->doc->undo()) return false;
    changed(*e, ChangeKind::Undone, label);
    return true;
}

bool DocumentList::redo(const DocId& id) {
    auto* e = entry(id);
    if (!e) return false;
    const auto label = e->doc->redoLabel();
    if (!e->doc->redo()) return false;
    changed(*e, ChangeKind::Redone, label);
    return true;
}

bool DocumentList::markSaved(const DocId& id) {
    auto* e = entry(id);
    if (!e) return false;
    e->doc->markSaved();
    return true;
}

const model::RebuildReport* DocumentList::lastRebuild(const DocId& id) const {
    auto it = docs_.find(id);
    if (it == docs_.end() || !it->second.rebuild) return nullptr;
    return &*it->second.rebuild;
}

void DocumentList::changed(Entry& e, ChangeKind kind, std::string label) {
    const DocId id = e.doc->id();
    bus_.publish(DocumentChanged{id, kind, std::move(label)});
    // A DocumentChanged listener may have closed the design.
    if (auto* still = entry(id)) rebuild(*still);
}

void DocumentList::rebuild(Entry& e) {
    if (!kernel_) return;
    e.rebuild = e.doc->regenerate(*kernel_);
    bus_.publish(RebuildFinished{e.doc->id(), e.rebuild->ok()});
}

}  // namespace kh::plugin
