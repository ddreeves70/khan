#pragma once

/// @file
/// kh.viewoptions: the built-in plugin that keeps every view option (the grid's,
/// the axes', and any plugin's), with the View options panel.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "kh/plugin/EventBus.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugin/Settings.h"
#include "kh/viewoptions/ViewOptions.h"

namespace kh::plugins {

/// Gives the kh.viewoptions plugin. It offers kh.viewoptions/1 to other
/// plugins, remembers their options, and in the desktop app adds the View
/// options panel (closed at first) and View > View options... to open it.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin viewOptionsPlugin();

/// The view options themselves, inside kh.viewoptions. Values are kept in the
/// plugin's settings under each option's id, so they last between runs.
class ViewOptionsService final : public viewoptions::IViewOptions {
public:
    /// Makes an empty set of options.
    /// @param settings kh.viewoptions's settings. Must outlive this.
    /// @param bus Where ViewOptionChanged is sent. Must outlive this.
    ViewOptionsService(plugin::PluginSettings& settings, plugin::EventBus& bus);

    Diagnostics add(viewoptions::OptionSpec option) override;
    const viewoptions::OptionSpec* find(std::string_view id) const override;
    std::vector<const viewoptions::OptionSpec*> options() const override;
    viewoptions::OptionValue value(std::string_view id, std::string_view pane) const override;
    Diagnostics set(std::string_view id, const viewoptions::OptionValue& value) override;
    Diagnostics setInPane(std::string_view id, const viewoptions::OptionValue& value,
                          std::string_view pane) override;
    bool hasOwn(std::string_view id, std::string_view pane) const override;
    std::vector<std::string> ownOptions(std::string_view pane) const override;
    void reset(std::string_view id, std::string_view pane) override;
    void resetPane(std::string_view pane) override;
    void ask(std::string_view id, std::string_view pane) override;
    void addPane(viewoptions::PaneInfo pane) override;
    void removePane(std::string_view key, bool forget) override;
    std::vector<viewoptions::PaneInfo> panes() const override { return panes_; }
    void setActivePane(std::string_view key) override;
    std::string activePane() const override { return active_; }
    void showPaneMenu(std::string_view pane, int x, int y) override;

    /// Tells everyone an option changed because its setting was changed in the
    /// store (another part of Khan, or a test). Call on SettingChanged.
    /// @param key The setting's key: an option's id, or "panes/<pane>/<option id>".
    void settingChanged(const std::string& key);

    /// Finds a pane.
    /// @param key Its key.
    /// @return Its name, or an empty string.
    std::string paneName(std::string_view key) const;

    /// How ask() asks the student: given the option and its value, the typed
    /// value, or nothing if cancelled. Unset (kh_cli, tests): ask() does nothing.
    std::function<std::optional<double>(const viewoptions::OptionSpec&, double)> asker;

    /// Where ask() reports a refused value.
    std::function<void(const Diagnostics&)> report;

    /// How showPaneMenu() shows the menu. Unset (kh_cli, tests): nothing is shown.
    std::function<void(const std::string& pane, int x, int y)> paneMenu;

private:
    std::optional<viewoptions::OptionValue> stored(const viewoptions::OptionSpec& o,
                                                   const std::string& key) const;
    std::vector<std::string> knownPanes() const;  // panes that have ever had their own values

    std::vector<viewoptions::OptionSpec> options_;
    std::vector<viewoptions::PaneInfo> panes_;
    std::string active_;
    plugin::PluginSettings& settings_;
    plugin::EventBus& bus_;
};

}  // namespace kh::plugins
