#pragma once

/// @file
/// PluginContext: everything a plugin can reach in Khan.

#include <functional>
#include <string>
#include <utility>

#include "kh/model/CommandRegistry.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/EventBus.h"
#include "kh/plugin/Manifest.h"

namespace kh::plugin {

/// A plugin's only door into Khan. Khan passes one to the plugin's register
/// function; it stays valid until Khan closes, so a plugin may keep a reference.
///
/// @code
/// void registerHello(kh::plugin::PluginContext& ctx) {
///     ctx.addCommand({"example.hello.add_cube", "Add cube", "Adds a cube.", {}},
///                    [](const kh::model::Document&, const kh::model::Args&) { ... });
///     ctx.listen<kh::plugin::DocumentChanged>([&ctx](const auto& e) { ctx.log("Changed: " + e.label); });
/// }
/// @endcode
class PluginContext {
public:
    /// Destroys the context.
    virtual ~PluginContext() = default;

    /// Gives the plugin's own manifest.
    /// @return What its plugin.json says.
    virtual const PluginManifest& manifest() const = 0;

    /// Adds a command that menus, scripts and the assistant can run by name.
    ///
    /// Only works while the register function runs; the commands become
    /// available when it returns without problems. An outside plugin's command
    /// names must start with its id and a dot, e.g. "example.hello.add_cube",
    /// so two plugins can never pick the same name.
    /// @param info The command's name, label, description and arguments.
    /// @param factory Builds the command once its arguments have been checked.
    virtual void addCommand(model::CommandInfo info, model::CommandFactory factory) = 0;

    /// Gives every command of every loaded plugin, for looking them up.
    /// @return The shared command registry.
    virtual const model::CommandRegistry& commands() const = 0;

    /// Gives the event bus, to send events or to listen with your own Subscription.
    /// @return The shared bus.
    virtual EventBus& events() = 0;

    /// Gives the open designs. Change them only through its execute(), run(),
    /// undo() and redo(), so undo and events always work.
    /// @return The shared document list.
    virtual DocumentList& documents() = 0;

    /// Adds a line to the plugin's log, shown in the Plugin manager and by
    /// `kh_cli plugins`.
    /// @param message Plain words about what happened.
    virtual void log(std::string message) = 0;

    /// Reports that the plugin can't work, for example because something it
    /// needs is missing. Call it from the register function: when that returns,
    /// Khan drops everything the plugin added and shows @p message.
    /// @param message Plain words for the student: what is wrong and how to fix it.
    virtual void fail(std::string message) = 0;

    /// Keeps a subscription connected until Khan closes (or the plugin fails to load).
    /// @param subscription The subscription to keep.
    virtual void keep(Subscription subscription) = 0;

    /// Listens for one type of event until Khan closes.
    /// @tparam E The event type, e.g. DocumentChanged.
    /// @param listener Called with each event of type @p E.
    template <typename E>
    void listen(std::function<void(const E&)> listener) {
        keep(events().subscribe<E>(std::move(listener)));
    }
};

}  // namespace kh::plugin
