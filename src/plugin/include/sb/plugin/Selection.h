#pragma once

/// @file
/// The shared selection: which steps, edges (or which design's settings) the student has picked.

#include <vector>

#include "sb/core/Id.h"
#include "sb/plugin/DocumentList.h"
#include "sb/plugin/EventBus.h"
#include "sb/plugin/Picks.h"

namespace sb::plugin {

/// What the student has picked, shared by every panel and view.
///
/// Picking a step in the feature tree selects it here, and every other panel
/// and view hears SelectionChanged and highlights it too. A selection is a list of
/// steps in one design, or, while viewers pick edges (PickMode::Edges), a list of
/// edges of its bodies.
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

    /// Gives the picked edges, while the selection is edges instead of steps.
    /// @return The edges, in the order they were picked; empty when steps are selected.
    const std::vector<PickedElement>& elements() const { return elements_; }

    /// Selects edges instead of steps. Sends SelectionChanged if it differs.
    /// Edges on steps that aren't in @p doc, and repeats, are left out.
    /// @param doc The design.
    /// @param elements The edges, in order.
    void setElements(const DocId& doc, std::vector<PickedElement> elements);

    /// Gives what clicking a body in a viewer picks.
    /// @return Bodies (steps), or edges.
    PickMode pickMode() const { return mode_; }

    /// Changes what clicking a body picks. Sends PickModeChanged if it differs,
    /// and clears picked edges when going back to bodies.
    /// @param mode Bodies or edges.
    void setPickMode(PickMode mode);

    /// Picks the design itself instead of any of its steps, as the feature
    /// tree's **Document settings** row does: Properties then shows the design's
    /// settings. Sends SelectionChanged if it differs. Picking steps with set()
    /// (or clear()) un-picks it.
    /// @code
    /// ctx.selection().pickDesign(ctx.documents().active());
    /// @endcode
    /// @param doc The design. An id that isn't an open design selects nothing.
    void pickDesign(const DocId& doc);

    /// Checks whether the design itself is picked (see pickDesign()).
    /// @return True if it is; doc() says which design, and features() is empty.
    bool designPicked() const { return design_; }

private:
    void prune();

    EventBus& bus_;
    const DocumentList& docs_;
    DocId doc_;
    std::vector<FeatureId> features_;
    std::vector<PickedElement> elements_;
    PickMode mode_ = PickMode::Bodies;
    bool design_ = false;
    std::vector<Subscription> subscriptions_;
};

}  // namespace sb::plugin
