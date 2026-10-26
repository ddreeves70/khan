#include "Tiers.h"

#include <vector>

namespace kh::plugin::detail {

namespace {

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Lower-case words joined by dots, like a plugin id.
bool wellFormed(std::string_view name) {
    if (name.empty() || name.front() == '.' || name.back() == '.') return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '.' && i + 1 < name.size() && name[i + 1] == '.') return false;
        if (name[i] != '.' && !isWordChar(name[i])) return false;
    }
    return true;
}

}  // namespace

Tiers::Tiers() {
    for (const char* t : {"foundation", "tools", "ui"}) {
        up_[t];
        names_.emplace_back(t);
    }
    up_["foundation"].insert("tools");
    up_["tools"].insert("ui");
}

bool Tiers::below(std::string_view a, std::string_view b) const {
    // Walk upwards from a; the graph is small and has no loops.
    std::vector<std::string> todo{std::string(a)};
    std::set<std::string> seen;
    while (!todo.empty()) {
        const std::string t = todo.back();
        todo.pop_back();
        auto it = up_.find(t);
        if (it == up_.end()) continue;
        for (const auto& u : it->second) {
            if (u == b) return true;
            if (seen.insert(u).second) todo.push_back(u);
        }
    }
    return false;
}

std::string Tiers::define(const PluginManifest& plugin, const TierSpec& spec, bool& unknown) {
    unknown = false;
    const std::string who = plugin.name.empty() ? plugin.id : plugin.name;
    const std::string what = who + " adds the tier \"" + spec.name + "\"";
    const std::string prefix = plugin.id + ".";
    if (spec.name.size() <= prefix.size() || spec.name.compare(0, prefix.size(), prefix) != 0 ||
        !wellFormed(spec.name)) {
        return what + ", but a new tier's name must start with the plugin's id, like \"" + prefix +
               "layer\", so two plugins can't pick the same name. Khan ignored it.";
    }
    if (known(spec.name)) return what + ", which already exists. Khan ignored the second one.";
    if (spec.after.empty() && spec.before.empty()) {
        return what +
               " without saying where it goes. Add \"after\" or \"before\" with an existing tier, "
               "like \"after\": \"tools\". Khan ignored it.";
    }
    for (const auto* ref : {&spec.after, &spec.before}) {
        if (!ref->empty() && !known(*ref)) {
            unknown = true;
            return what + (ref == &spec.after ? " after" : " before") + " the tier \"" + *ref +
                   "\", which no plugin adds. Khan ignored it.";
        }
    }
    if (!spec.after.empty() && !spec.before.empty() &&
        (spec.after == spec.before || below(spec.before, spec.after))) {
        return what + " after " + spec.after + " and before " + spec.before + ", but " + spec.after +
               " isn't below " + spec.before + ". Khan ignored it.";
    }
    up_[spec.name];
    if (!spec.after.empty()) up_[spec.after].insert(spec.name);
    if (!spec.before.empty()) up_[spec.name].insert(spec.before);
    names_.push_back(spec.name);
    return {};
}

}  // namespace kh::plugin::detail
