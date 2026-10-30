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
    void addStatusItem(ui::StatusItemSpec item) override { statusItems_.push_back(std::move(item)); }
    void showProblems(const Diagnostics& problems) override;
    void showMessage(const QString& message) override;
    void showPanel(const std::string& id) override {
        if (panelSink_) panelSink_(id, true);
    }
    void hidePanel(const std::string& id) override {
        if (panelSink_) panelSink_(id, false);
    }

    QIcon icon(std::string_view svg) const override;

    const std::vector<ui::PanelSpec>& panels() const { return panels_; }
    const std::vector<ui::StatusItemSpec>& statusItems() const { return statusItems_; }

    // Where messages go. Until set (while plugins load), they are kept and
    // shown when it is.
    void setSinks(std::function<void(const Diagnostics&)> problems,
                  std::function<void(const QString&)> message);

private:
    std::vector<ui::PanelSpec> panels_;
    std::vector<ui::StatusItemSpec> statusItems_;
    std::function<void(const Diagnostics&)> problemSink_;
    std::function<void(const QString&)> messageSink_;
    std::function<void(const std::string&, bool)> panelSink_;

public:
    // Where showPanel() and hidePanel() go (true to show): the main window, once it exists.
    void setPanelSink(std::function<void(const std::string&, bool)> sink) { panelSink_ = std::move(sink); }

private:
    Diagnostics heldProblems_;
};

}  // namespace kh::app
