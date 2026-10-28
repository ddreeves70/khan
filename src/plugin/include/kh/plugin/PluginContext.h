#pragma once

/// @file
/// PluginContext: everything a plugin can reach in Khan.

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "kh/model/CommandRegistry.h"
#include "kh/plugin/Actions.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/EventBus.h"
#include "kh/plugin/Manifest.h"
#include "kh/plugin/Overlays.h"
#include "kh/plugin/Selection.h"
#include "kh/plugin/Settings.h"

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

    /// Adds a menu item or toolbar button that runs a command by name.
    ///
    /// Like addCommand(), only works while the register function runs. The
    /// command may be the plugin's own or one from a plugin it requires. An
    /// outside plugin's action ids must start with its id and a dot.
    /// @param action What it shows and which command it runs.
    virtual void addAction(ActionSpec action) = 0;

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

    /// Gives this plugin's remembered settings. In the desktop app they last
    /// between runs; in kh_cli they're forgotten when it ends. Usable any time,
    /// including in the register function.
    /// @return The settings; keys can't clash with other plugins'.
    virtual PluginSettings& settings() = 0;

    /// Gives this plugin's view overlays: lines, dots and lattices every viewer
    /// draws over the design. Usable any time. If the plugin fails to start, Khan
    /// removes the overlays it added.
    /// @return The overlays; names can't clash with other plugins'.
    virtual PluginOverlays& overlays() = 0;

    /// Gives the shared selection: the steps the student has picked.
    /// @return The selection.
    virtual Selection& selection() = 0;

    /// Switches the toolbar to a mode, so only the buttons for what the student is
    /// doing show: while a sketch is being edited, the sketcher turns on
    /// "kh.sketcher.editing", and only buttons with that `toolbarMode` show (plus
    /// Khan's own New, Open, Save, Undo and Redo). Sends ToolbarModeChanged.
    ///
    /// The name should start with the plugin's id and a dot; otherwise Khan adds
    /// a warning (and still switches).
    /// @param mode The mode to turn on, or empty to go back to the usual toolbar.
    virtual void setToolbarMode(std::string mode) = 0;

    /// Gives the toolbar mode that is on.
    /// @return The mode, or empty for the usual toolbar.
    virtual const std::string& toolbarMode() const = 0;

    /// Finds a service by name: one the program running Khan provides (the
    /// desktop app provides kh::ui::UiRegistry for adding panels; kh_cli provides
    /// none), or one another plugin provides with provide().
    ///
    /// Look up other plugins' services in an onStart() callback, not while
    /// registering, and list the plugin in "requires" or "uses" in plugin.json;
    /// otherwise Khan adds a warning. Prefer the typed service<T>().
    /// @param name The service's name, e.g. "kh.ui" or "example.ruler/1".
    /// @return The service, or nullptr if nobody provides it.
    virtual void* service(std::string_view name) = 0;

    /// Finds a service by its type.
    /// @code
    /// if (auto* ui = ctx.service<kh::ui::UiRegistry>()) ui->addPanel(...);
    /// @endcode
    /// @tparam T The service type; it names itself with a static `kServiceName`.
    /// @return The service, or nullptr if nobody provides it.
    template <typename T>
    T* service() {
        return static_cast<T*>(service(T::kServiceName));
    }

    /// Offers a service to other plugins (see "Share something with other
    /// plugins" in the plugin guide).
    ///
    /// Only works while the register function runs; the service becomes
    /// available when it returns without problems. The name should start with
    /// the plugin's id and end with a version, like "example.ruler/1". If
    /// another plugin already provides that name, theirs is kept and this
    /// plugin gets a warning.
    /// @param name The service's name.
    /// @param service The service. It must stay valid until Khan closes.
    /// @param deprecated True if the service is going away: plugins that use it
    ///        get a warning telling them to move to a newer version.
    virtual void provide(std::string name, void* service, bool deprecated) = 0;

    /// Offers a service, named by its type's `kServiceName`.
    /// @code
    /// static Ruler ruler;  // lives until Khan closes
    /// ctx.provide<example::IRuler>(ruler);
    /// @endcode
    /// @tparam T The service type (usually an abstract class from a contract header).
    /// @param service The service. It must stay valid until Khan closes.
    /// @param deprecated True if the service is going away (see the other provide()).
    template <typename T>
    void provide(T& service, bool deprecated = false) {
        provide(std::string(T::kServiceName), static_cast<void*>(&service), deprecated);
    }

    /// Runs @p start once every plugin has registered, so every service exists.
    /// This is where a plugin looks up and connects to other plugins' services.
    ///
    /// Only works while the register function runs. Callbacks run in load order.
    /// Calling fail() in one stops the plugin, like failing while registering.
    /// @param start What to do when Khan starts.
    virtual void onStart(std::function<void()> start) = 0;

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
    ///
    /// Listening to another plugin's event (its type starts with that plugin's
    /// id) without listing the plugin in "requires" or "uses" adds a warning.
    /// @tparam E The event type, e.g. DocumentChanged.
    /// @param listener Called with each event of type @p E.
    template <typename E>
    void listen(std::function<void(const E&)> listener) {
        noteListen(E::kType);
        keep(events().subscribe<E>(std::move(listener)));
    }

    /// Sends one of this plugin's own events to every listener.
    ///
    /// The event type's name must start with the plugin's id and a dot, like
    /// "example.ruler.ready", so two plugins' events never clash; otherwise
    /// Khan adds a warning (and still sends it).
    /// @tparam E The event type, a struct with a `kType` name.
    /// @param event The event.
    template <typename E>
    void publish(const E& event) {
        notePublish(E::kType);
        events().publish(event);
    }

    /// Checks an event this plugin is about to send (used by publish()).
    /// @param type The event's type name.
    virtual void notePublish(std::string_view type) = 0;

    /// Checks an event this plugin is about to listen to (used by listen()).
    /// @param type The event's type name.
    virtual void noteListen(std::string_view type) = 0;
};

}  // namespace kh::plugin
