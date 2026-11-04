#pragma once

// Plugin tiers: ordered layers (foundation, tools, ui, and any a plugin adds),
// used only to warn about dependencies that run the wrong way. Private to
// sb_plugin; see docs/design/plugin-contracts.md, "Tiers".

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "sb/plugin/Manifest.h"

namespace sb::plugin::detail {

class Tiers {
public:
    // Starts with SketchBillet's three: foundation, below tools, below ui.
    Tiers();

    bool known(std::string_view name) const { return up_.count(std::string(name)) > 0; }

    // True if tier `a` is strictly below tier `b` (both known).
    bool below(std::string_view a, std::string_view b) const;

    // Adds a plugin's tier. Returns why it can't be added (a whole sentence
    // ending in "SketchBillet ignored it."), or an empty string when it was added.
    // A tier placed after or before a tier nobody has defined yet isn't added
    // and gives "unknown"; the caller can try again after other plugins' tiers.
    std::string define(const PluginManifest& plugin, const TierSpec& spec, bool& unknown);

    // Every tier, in the order defined.
    const std::vector<std::string>& names() const { return names_; }

private:
    std::map<std::string, std::set<std::string>> up_;  // tier -> tiers directly above it
    std::vector<std::string> names_;
};

}  // namespace sb::plugin::detail
