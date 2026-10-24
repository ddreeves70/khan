#pragma once

/// @file
/// The shared selection: which steps the student has picked.

#include <vector>

#include "kh/core/Id.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/EventBus.h"

namespace kh::plugin {

/// What the student has picked, shared by every panel and view.
///
/// Picking a step in the feature tree selects it here, and every other panel
/// and view hears SelectionChanged and highlights it too. Until faces and edges
/// can be picked (Phase 2), a selection is a list of steps in one design.
///
/// The selection keeps itself valid: steps that are deleted or undone away drop
/// out of it, and closing the design clears it.
///
/// @code
/// ctx.selection().set(doc, {boxId});
/// if (ctx.selection().contains(boxId)) { ... }
/// @endcode
class Selection {
public:
    /// Makes an empty selection.
    /// @param bus Where SelectionChanged is sent. Must outlive the selection.
    /// @param docs The open designs, to drop steps that no longer exist. Must outlive the selection.
    Selection(EventBus& bus, const DocumentList& docs);

    Selection(const Selection&) = delete;
    Selection& operator=(const Selection&) = delete;

    /// Gives the design the selection is in.
    /// @return Its id, or a nil id when nothing is selected.
    DocId doc() const { return doc_; }

    /// Gives the selected steps.
    /// @return Their ids, in the order they were picked.
    const std::vector<FeatureId>& features() const { return features_; }

    /// Checks whether a step is selected.
    /// @param id The step.
    /// @return True if it is selected.
    bool contains(const FeatureId& id) const;

    /// Replaces the selection. Sends SelectionChanged if it differs. Ids that
    /// aren't steps of @p doc, and repeats, are left out.
    /// @param doc The design.
    /// @param features The steps to select, in order.
    void set(const DocId& doc, std::vector<FeatureId> features);

    /// Selects nothing. Sends SelectionChanged if something was selected.
    void clear();

private:
    void prune();

    EventBus& bus_;
    const DocumentList& docs_;
    DocId doc_;
    std::vector<FeatureId> features_;
    std::vector<Subscription> subscriptions_;
};

}  // namespace kh::plugin
