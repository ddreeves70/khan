#pragma once

/// @file
/// PluginHost: finds plugins, checks them, and loads them in order.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/kernel/IKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/EventBus.h"
#include "kh/plugin/Manifest.h"
#include "kh/plugin/PluginContext.h"

namespace kh::plugin {

namespace detail {
class Library;
}

/// A plugin compiled into Khan itself, such as kh.modeling.
struct BuiltinPlugin {
    std::string manifestJson;  ///< The text of its plugin.json.
    RegisterFn registerFn;     ///< Its register function.
};

/// Whether a plugin loaded.
enum class PluginState {
    Loaded,   ///< Running; its commands and listeners are in place.
    Failed,   ///< Not loaded because of a problem with the plugin itself. See PluginInfo::issues.
    Skipped,  ///< Not loaded because a plugin it needs didn't load.
};

/// One plugin as the Plugin manager shows it.
struct PluginInfo {
    PluginManifest manifest;  ///< Its manifest. For a damaged plugin.json, only `folder` and `name` are set.
    bool builtin = false;     ///< True for plugins compiled into Khan.
    PluginState state = PluginState::Failed;  ///< Whether it loaded.
    Diagnostics issues;                       ///< Why it didn't load, in plain words.
    std::vector<std::string> messages;        ///< What it wrote with PluginContext::log().
    std::vector<std::string> commands;        ///< The commands it added, sorted.
};

/// Finds plugins, checks their manifests, and loads them in dependency order.
///
/// A plugin that can't load is listed with a plain message and the rest still
/// load, so Khan always starts. Plugins stay loaded until the host is destroyed.
///
/// @code
/// PluginHost host(kernel.get());
/// host.addBuiltin(kh::plugins::modelingPlugin());
/// host.addFolder(userPluginFolder);
/// host.loadAll();
/// for (const auto& p : host.plugins()) { ... show p.manifest.name and p.state ... }
/// DocId doc = host.documents().add(std::make_unique<model::Document>());
/// host.documents().run(doc, "feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
/// @endcode
class PluginHost {
public:
    /// Makes a host with no plugins.
    /// @param kernel The kernel used to rebuild open designs; may be null (no rebuilds).
    ///        Must outlive the host.
    explicit PluginHost(kernel::IKernel* kernel = nullptr);

    /// Closes every design, disconnects every listener, then unloads the plugins.
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    /// Adds a plugin that is compiled into Khan. Call before loadAll().
    /// @param plugin Its manifest text and register function.
    void addBuiltin(BuiltinPlugin plugin);

    /// Adds a folder to search for plugins. Each subfolder with a plugin.json is
    /// one plugin. A folder that doesn't exist is skipped. Call before loadAll().
    /// @param folder The folder of plugin folders.
    void addFolder(std::filesystem::path folder);

    /// Reads every manifest, checks it, and loads the plugins in dependency
    /// order. Does nothing if called again.
    void loadAll();

    /// Lists every plugin found, loaded or not.
    /// @return The plugins, in the order they were loaded (problems at the end).
    const std::vector<PluginInfo>& plugins() const { return infos_; }

    /// Finds a plugin by id.
    /// @param id The plugin id, e.g. "kh.modeling".
    /// @return The plugin, or nullptr if none has that id.
    const PluginInfo* find(std::string_view id) const;

    /// Gives every command of every loaded plugin.
    /// @return The command registry.
    const model::CommandRegistry& commands() const { return registry_; }

    /// Gives the event bus.
    /// @return The bus.
    EventBus& events() { return bus_; }

    /// Gives the open designs.
    /// @return The document list.
    DocumentList& documents() { return documents_; }

    /// Lists problems that happened while plugins reacted to events.
    /// @return The problems, oldest first.
    const Diagnostics& eventProblems() const { return eventProblems_; }

private:
    struct Candidate;
    void loadOne(Candidate& c, PluginInfo& info);

    // Declared so they are destroyed in the safe order: listeners and designs
    // (which hold plugin code) go before the libraries that contain that code.
    std::vector<std::unique_ptr<detail::Library>> libraries_;
    Diagnostics eventProblems_;
    EventBus bus_;
    model::CommandRegistry registry_;
    DocumentList documents_;
    std::vector<std::unique_ptr<PluginContext>> contexts_;
    std::vector<BuiltinPlugin> builtins_;
    std::vector<std::filesystem::path> folders_;
    std::vector<PluginInfo> infos_;
    bool loaded_ = false;
};

/// Gives the file name of a plugin's shared library on this system.
/// @param library The `library` value from plugin.json, e.g. "hello".
/// @return "hello.dll" on Windows, "hello.dylib" on macOS, "hello.so" on Linux.
std::string libraryFileName(std::string_view library);

}  // namespace kh::plugin
