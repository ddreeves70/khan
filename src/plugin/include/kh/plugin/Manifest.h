#pragma once

/// @file
/// The plugin.json manifest that describes a plugin.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"

namespace kh::plugin {

/// What a plugin's `plugin.json` says about it. See docs/plugin-guide/manifest.md.
///
/// @code
/// {
///   "id": "example.hello",
///   "name": "Hello",
///   "version": "1.0.0",
///   "apiVersion": 1,
///   "description": "Adds a command that makes a cube.",
///   "library": "hello",
///   "requires": ["kh.modeling"]
/// }
/// @endcode
struct PluginManifest {
    std::string id;           ///< Stable id, lower case with dots, e.g. "example.hello". Never renamed.
    std::string name;         ///< Shown to people, e.g. "Hello".
    std::string version;      ///< The plugin's own version, e.g. "1.0.0".
    int apiVersion = 0;       ///< The plugin API it was written for; must equal kPluginApiVersion.
    std::string description;  ///< One plain sentence about what it adds. May be empty.
    std::string library;  ///< Shared library file name without its extension (.dll, .dylib, .so); empty for
                          ///< built-ins.
    std::vector<std::string>
        dependencies;  ///< Ids of plugins that must load first ("requires" in plugin.json).
    std::vector<std::string> permissions;  ///< Declared permissions. Stored, not yet enforced.
    std::filesystem::path folder;          ///< The folder the manifest was read from; empty for built-ins.
};

/// What parseManifest() returns.
struct ManifestResult {
    std::optional<PluginManifest> manifest;  ///< The manifest; empty if it could not be read.
    Diagnostics issues;                      ///< Every problem found, in plain words.

    /// Checks whether the manifest was read.
    /// @return True if @ref manifest is set.
    bool ok() const { return manifest.has_value(); }
};

/// Reads a manifest from its JSON text.
///
/// Checks that `id`, `name`, `version` and `apiVersion` are present and have the
/// right types, and that the id is lower case words joined by dots. Unknown keys
/// are ignored, so newer manifests still load. Does not check `apiVersion` against
/// this Khan; PluginHost does that, so the plugin still shows in the Plugin manager.
/// @param json The text of plugin.json.
/// @param folder The plugin's folder, stored in the result and used in messages.
/// @return The manifest, or every problem found.
ManifestResult parseManifest(std::string_view json, const std::filesystem::path& folder = {});

/// Reads the manifest of a built-in plugin (one compiled into Khan).
///
/// Like parseManifest(), except `apiVersion` may be left out: a built-in plugin
/// is always built with this Khan, so it gets kPluginApiVersion and nobody has
/// to update it by hand. If a built-in manifest does give `apiVersion`, it is
/// kept, and PluginHost checks it like any other.
/// @param json The manifest text.
/// @return The manifest, or every problem found.
ManifestResult parseBuiltinManifest(std::string_view json);

/// Reads `plugin.json` from a plugin's folder.
/// @param folder The plugin's folder.
/// @return The manifest, or why it could not be read.
ManifestResult readManifest(const std::filesystem::path& folder);

/// Checks a plugin or contribution id: lower-case letters, digits and `_`, in two
/// or more parts joined by dots, e.g. "kh.modeling" or "example.hello.add_cube".
/// @param id The id to check.
/// @return True if it is well formed.
bool isValidId(std::string_view id);

}  // namespace kh::plugin
