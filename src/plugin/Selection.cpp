#include "sb/plugin/Selection.h"

#include <algorithm>
#include <utility>

#include "sb/plugin/Events.h"

namespace sb::plugin {

Selection::Selection(EventBus& bus, const DocumentList& docs) : bus_(bus), docs_(docs) {
    subscriptions_.push_back(bus_.subscribe<DocumentChanged>([this](const DocumentChanged& e) {
        if (e.doc == doc_) prune();
    }));
    subscriptions_.push_back(bus_.subscribe<DocumentClosed>([this](const DocumentClosed& e) {
        if (e.doc == doc_) clear();
    }));
}

bool Selection::contains(const FeatureId& id) const {
    return std::find(features_.begin(), features_.end(), id) != features_.end();
}

void Selection::set(const DocId& doc, std::vector<FeatureId> features) {
    const auto* design = docs_.find(doc);
    std::vector<FeatureId> kept;
    if (design) {
        for (const auto& id : features) {
            if (design->find(id) && std::find(kept.begin(), kept.end(), id) == kept.end()) kept.push_back(id);
        }
    }
    const DocId newDoc = kept.empty() ? DocId{} : doc;
    if (newDoc == doc_ && kept == features_ && !design_ && elements_.empty()) return;
    doc_ = newDoc;
    features_ = std::move(kept);
    elements_.clear();
    design_ = false;
    bus_.publish(SelectionChanged{doc_, features_, false, {}});
}

void Selection::setElements(const DocId& doc, std::vector<PickedElement> elements) {
    const auto* design = docs_.find(doc);
    std::vector<PickedElement> kept;
    if (design) {
        for (auto& e : elements) {
            if (design->find(e.body) && !e.ref.name.empty() &&
                std::find(kept.begin(), kept.end(), e) == kept.end()) {
                kept.push_back(std::move(e));
            }
        }
    }
    if (kept.empty()) {
        if (elements_.empty()) {
            set(doc_, features_);
            return;
        }
        elements_.clear();
        set(DocId{}, {});
        return;
    }
    if (doc == doc_ && kept == elements_ && features_.empty() && !design_) return;
    doc_ = doc;
    features_.clear();
    elements_ = std::move(kept);
    design_ = false;
    bus_.publish(SelectionChanged{doc_, features_, false, elements_});
}

void Selection::setPickMode(PickMode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    if (mode == PickMode::Bodies && !elements_.empty()) clear();
    bus_.publish(PickModeChanged{mode_});
}

void Selection::pickDesign(const DocId& doc) {
    if (!docs_.find(doc)) {
        clear();
        return;
    }
    if (design_ && doc_ == doc) return;
    doc_ = doc;
    features_.clear();
    elements_.clear();
    design_ = true;
    bus_.publish(SelectionChanged{doc_, features_, true, {}});
}

void Selection::clear() {
    set(DocId{}, {});
}

void Selection::prune() {
    if (design_) return;  // the design is still there
    if (!elements_.empty()) {
        setElements(doc_, elements_);  // drops edges on steps that are gone
        return;
    }
    set(doc_, features_);  // drops steps that are gone
}

}  // namespace sb::plugin
