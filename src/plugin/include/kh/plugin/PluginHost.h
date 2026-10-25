#pragma once

/// @file
/// PluginHost: finds plugins, checks them, and loads them in order.

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/kernel/IKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/plugin/Actions.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/EventBus.h"
#include "kh/plugin/Manifest.h"
#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginContext.h"
#include "kh/plugin/Selection.h"
#include "kh/plugin/Settings.h"

namespace kh::plugin {

namespace detail {
class Library;
struct HostShared;
}  // namespace detail

/// A plugin compiled into Khan itself, such as kh.modeling.
struct BuiltinPlugin {
    std::string
        manifestJson;  ///< Its manifest text. Leave out `apiVersion`: built-ins always get kPluginApiVersion.
    RegisterFn registerFn;  ///< Its register function.
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
    /// Why it didn't load (errors), things that work but are probably a mistake
    /// (warnings, see "Warnings" in the plugin guide), and notes (info), in plain words.
    Diagnostics issues;
    std::vector<std::string> messages;  ///< What it wrote with PluginContext::log().
    std::vector<std::string> commands;  ///< The commands it added, sorted.
    std::vector<std::string> actions;   ///< The ids of the actions it added, in the order added.
    std::vector<std::string> services;  ///< The services it provides to other plugins, sorted.
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

    /// Offers a service to plugins, such as the desktop app's kh::ui::UiRegistry.
    /// Call before loadAll(). The service must outlive the host.
    /// @param name The service's name, e.g. "kh.ui".
    /// @param service The service.
    void provide(std::string name, void* service);

    /// Offers a service to plugins, named by its type's `kServiceName`.
    /// @tparam T The service type.
    /// @param service The service. Must outlive the host.
    template <typename T>
    void provide(T& service) {
        provide(std::string(T::kServiceName), static_cast<void*>(&service));
    }

    /// Sets where plugins' settings are kept (see PluginContext::settings()).
    /// Call before loadAll(). Without it, settings are kept in memory only.
    /// @param backend The settings store. Must outlive the host.
    void setSettings(SettingsBackend& backend) { settingsBackend_ = &backend; }

    /// Sets how long a plugin may take to register and start before it gets the
    /// plugin.slow_start warning. Call before loadAll().
    /// @param limit The limit; 0 turns the warning off. The default is 500 ms.
    void setSlowStartLimit(std::chrono::milliseconds limit) { slowStartLimit_ = limit; }

    /// Reads every manifest, checks it, and loads the plugins in dependency
    /// order (see the class description). Does nothing if called again.
    ///
    /// Loading has two phases. First every plugin registers, in dependency order
    /// ("requires" and "uses"; plugins ready at the same time go in id order).
    /// Plugins that depend on each other in a loop are loaded anyway, in id
    /// order, with a warning. Then every loaded plugin's onStart() callbacks run,
    /// in the same order. A plugin whose "requires" plugin didn't load doesn't
    /// load either.
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

    /// Gives every action of every loaded plugin.
    /// @return The actions, in the order the plugins loaded and added them.
    const std::vector<ActionSpec>& actions() const { return actions_; }

    /// Gives the shared selection.
    /// @return The selection.
    Selection& selection() { return selection_; }

    /// Gives every plugin's view overlays.
    /// @return The overlays.
    OverlayList& overlays() { return overlays_; }

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
    void withdraw(Candidate& c, PluginInfo& info);

    // Declared so they are destroyed in the safe order: listeners and designs
    // (which hold plugin code) go before the libraries that contain that code.
    std::vector<std::unique_ptr<detail::Library>> libraries_;
    Diagnostics eventProblems_;
    EventBus bus_;
    model::CommandRegistry registry_;
    DocumentList documents_;
    Selection selection_;
    OverlayList overlays_{bus_};
    std::vector<ActionSpec> actions_;
    std::unique_ptr<detail::HostShared> shared_;  // services and loading state, shared with contexts
    std::chrono::milliseconds slowStartLimit_{500};
    MemorySettings memorySettings_;
    SettingsBackend* settingsBackend_ = &memorySettings_;
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
