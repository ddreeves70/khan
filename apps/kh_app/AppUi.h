#pragma once

#include <QString>

#include <functional>
#include <vector>

#include "kh/ui/UiRegistry.h"

namespace kh::app {

// The desktop app's UiRegistry: collects the panels plugins add while they
// load, and forwards their messages to the main window once it exists.
class AppUi final : public ui::UiRegistry {
public:
    void addPanel(ui::PanelSpec panel) override { panels_.push_back(std::move(panel)); }
    void showProblems(const Diagnostics& problems) override;
    void showMessage(const QString& message) override;

    const std::vector<ui::PanelSpec>& panels() const { return panels_; }

    // Where messages go. Until set (while plugins load), they are kept and
    // shown when it is.
    void setSinks(std::function<void(const Diagnostics&)> problems,
                  std::function<void(const QString&)> message);

private:
    std::vector<ui::PanelSpec> panels_;
    std::function<void(const Diagnostics&)> problemSink_;
    std::function<void(const QString&)> messageSink_;
    Diagnostics heldProblems_;
};

}  // namespace kh::app
