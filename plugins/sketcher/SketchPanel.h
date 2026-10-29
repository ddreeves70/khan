#pragma once

// The Sketch panel: while a sketch is edited, every constraint and dimension in
// it, with each dimension's number ready to type in.

#include <QPointer>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "kh/core/Id.h"
#include "kh/plugin/PluginContext.h"

class QLabel;
class QTableWidget;

namespace kh::plugins::sketcher {

// What the panel asks the sketcher.
struct PanelHooks {
    std::function<std::optional<FeatureId>()> editing;  // the sketch being edited
    std::function<std::vector<ConstraintId>()> picked;  // constraints picked in the viewer
    std::function<void(const ConstraintId&)> pick;      // pick one (from a click on its row)
    std::function<ConstraintId()> takeJustAdded;        // a new dimension to put the cursor in
};

class SketchPanel final : public QWidget {
public:
    SketchPanel(plugin::PluginContext& ctx, PanelHooks hooks, QWidget* parent = nullptr);

    // Shows the edited sketch's constraints again (rebuilding the list only if
    // they changed, so a number being typed isn't lost).
    void refresh();

private:
    void rebuild();
    void setValue(const std::string& name, double value);

    plugin::PluginContext& ctx_;
    PanelHooks hooks_;
    QLabel* status_ = nullptr;
    QTableWidget* table_ = nullptr;
    std::string shown_;  // what the list shows now, to tell when it needs making again
    std::vector<ConstraintId> rows_;
    bool updating_ = false;
};

}  // namespace kh::plugins::sketcher
