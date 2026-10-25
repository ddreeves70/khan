#include "kh/plugin/Settings.h"

#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

#include "kh/plugin/Events.h"

namespace kh::plugin {

std::optional<std::string> MemorySettings::read(std::string_view key) const {
    auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

void MemorySettings::write(std::string_view key, std::string_view value) {
    values_.insert_or_assign(std::string(key), std::string(value));
}

void MemorySettings::remove(std::string_view key) {
    if (auto it = values_.find(key); it != values_.end()) values_.erase(it);
}

PluginSettings::PluginSettings(SettingsBackend& backend, EventBus& bus, std::string plugin)
    : backend_(backend), bus_(bus), plugin_(std::move(plugin)) {}

std::string PluginSettings::fullKey(std::string_view key) const {
    return "plugins/" + plugin_ + "/" + std::string(key);
}

std::string PluginSettings::text(std::string_view key, std::string_view fallback) const {
    return backend_.read(fullKey(key)).value_or(std::string(fallback));
}

double PluginSettings::number(std::string_view key, double fallback) const {
    const auto value = backend_.read(fullKey(key));
    if (!value) return fallback;
    // Always "." for the decimal point, whatever language the computer uses.
    std::istringstream in(*value);
    in.imbue(std::locale::classic());
    double d = 0;
    in >> d;
    if (in.fail() || !(in >> std::ws).eof()) return fallback;
    return d;
}

bool PluginSettings::flag(std::string_view key, bool fallback) const {
    const auto value = backend_.read(fullKey(key));
    if (value == "true") return true;
    if (value == "false") return false;
    return fallback;
}

bool PluginSettings::has(std::string_view key) const {
    return backend_.read(fullKey(key)).has_value();
}

void PluginSettings::store(std::string_view key, std::string_view value) {
    const auto full = fullKey(key);
    if (backend_.read(full) == value) return;
    backend_.write(full, value);
    bus_.publish(SettingChanged{plugin_, std::string(key)});
}

void PluginSettings::set(std::string_view key, std::string_view value) {
    store(key, value);
}

void PluginSettings::set(std::string_view key, double value) {
    // The shortest text that reads back as exactly the same number ("0.1", not
    // "0.10000000000000001"), with "." for the decimal point on every computer.
    std::string text;
    for (int digits : {15, 17}) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(digits) << value;
        text = out.str();
        std::istringstream back(text);
        back.imbue(std::locale::classic());
        double check = 0;
        back >> check;
        if (check == value) break;
    }
    store(key, text);
}

void PluginSettings::set(std::string_view key, bool value) {
    store(key, value ? "true" : "false");
}

void PluginSettings::remove(std::string_view key) {
    const auto full = fullKey(key);
    if (!backend_.read(full)) return;
    backend_.remove(full);
    bus_.publish(SettingChanged{plugin_, std::string(key)});
}

}  // namespace kh::plugin
