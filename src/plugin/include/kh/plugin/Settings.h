#pragma once

/// @file
/// Settings Khan remembers for each plugin, such as a ruler's unit.

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "kh/plugin/EventBus.h"

namespace kh::plugin {

/// Where settings are kept. The program running Khan provides one: the desktop
/// app keeps them with its own settings, so they last between runs; kh_cli and
/// tests use MemorySettings, which forgets them on exit.
///
/// Keys look like "plugins/example.ruler/unit". Values are text.
class SettingsBackend {
public:
    /// Destroys the backend.
    virtual ~SettingsBackend() = default;

    /// Reads a value.
    /// @param key The full key, e.g. "plugins/example.ruler/unit".
    /// @return The value, or nothing if it was never set.
    virtual std::optional<std::string> read(std::string_view key) const = 0;

    /// Writes a value.
    /// @param key The full key.
    /// @param value The value, as text.
    virtual void write(std::string_view key, std::string_view value) = 0;

    /// Forgets a value, so reading it gives nothing again.
    /// @param key The full key.
    virtual void remove(std::string_view key) = 0;
};

/// Settings kept in memory only, forgotten when the program ends.
class MemorySettings final : public SettingsBackend {
public:
    std::optional<std::string> read(std::string_view key) const override;
    void write(std::string_view key, std::string_view value) override;
    void remove(std::string_view key) override;

private:
    std::map<std::string, std::string, std::less<>> values_;
};

/// One plugin's remembered settings. A plugin gets its own from
/// PluginContext::settings(); keys can't clash with other plugins' keys.
///
/// Every change sends SettingChanged, so menus with check marks and panels can
/// show the new value.
///
/// @code
/// auto& settings = ctx.settings();
/// const bool inches = settings.text("unit", "in") == "in";  // "in" until changed
/// settings.set("unit", "mm");
/// @endcode
class PluginSettings {
public:
    /// Makes the settings of one plugin.
    /// @param backend Where they're kept. Must outlive this object.
    /// @param bus Where SettingChanged is sent. Must outlive this object.
    /// @param plugin The plugin's id.
    PluginSettings(SettingsBackend& backend, EventBus& bus, std::string plugin);

    /// Reads a text setting.
    /// @param key The setting's name, e.g. "unit".
    /// @param fallback What to give if it was never set.
    /// @return The value.
    std::string text(std::string_view key, std::string_view fallback = {}) const;

    /// Reads a number setting.
    /// @param key The setting's name.
    /// @param fallback What to give if it was never set, or isn't a number.
    /// @return The value.
    double number(std::string_view key, double fallback = 0) const;

    /// Reads an on/off setting.
    /// @param key The setting's name.
    /// @param fallback What to give if it was never set, or isn't true or false.
    /// @return The value.
    bool flag(std::string_view key, bool fallback = false) const;

    /// Checks whether a setting was ever set.
    /// @param key The setting's name.
    /// @return True if it has a value.
    bool has(std::string_view key) const;

    /// Changes a text setting, and sends SettingChanged if the value is new.
    /// @param key The setting's name.
    /// @param value The new value.
    void set(std::string_view key, std::string_view value);

    /// Changes a text setting (so a string literal isn't taken as a flag).
    /// @param key The setting's name.
    /// @param value The new value.
    void set(std::string_view key, const char* value) { set(key, std::string_view(value)); }

    /// Changes a number setting, and sends SettingChanged if the value is new.
    /// @param key The setting's name.
    /// @param value The new value. Saved the same way on every computer.
    void set(std::string_view key, double value);

    /// Changes an on/off setting, and sends SettingChanged if the value is new.
    /// @param key The setting's name.
    /// @param value The new value.
    void set(std::string_view key, bool value);

    /// Forgets a setting, so reading it gives the fallback again.
    /// @param key The setting's name.
    void remove(std::string_view key);

    /// Gives the full key a setting is kept under.
    /// @param key The setting's name, e.g. "unit".
    /// @return "plugins/<plugin id>/<key>".
    std::string fullKey(std::string_view key) const;

private:
    void store(std::string_view key, std::string_view value);

    SettingsBackend& backend_;
    EventBus& bus_;
    std::string plugin_;
};

}  // namespace kh::plugin
