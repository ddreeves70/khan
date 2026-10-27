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
    viewoptions::OptionValue value(std::string_view id) const override;
    Diagnostics set(std::string_view id, const viewoptions::OptionValue& value) override;
    void ask(std::string_view id) override;

    /// Tells everyone an option changed because its setting was changed in the
    /// store (another part of Khan, or a test). Call on SettingChanged.
    /// @param key The setting's key, which is the option's id.
    void settingChanged(const std::string& key);

    /// How ask() asks the student: given the option and its value, the typed
    /// value, or nothing if cancelled. Unset (kh_cli, tests): ask() does nothing.
    std::function<std::optional<double>(const viewoptions::OptionSpec&, double)> asker;

    /// Where ask() reports a refused value.
    std::function<void(const Diagnostics&)> report;

private:
    std::vector<viewoptions::OptionSpec> options_;
    plugin::PluginSettings& settings_;
    plugin::EventBus& bus_;
};

}  // namespace kh::plugins
