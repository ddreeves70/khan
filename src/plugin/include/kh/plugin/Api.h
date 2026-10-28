#pragma once

/// @file
/// The plugin API version and the macros an outside plugin uses to export itself.

namespace kh::plugin {

class PluginContext;

/// Version of the plugin API in this build of Khan.
///
/// A plugin's `plugin.json` gives the `apiVersion` it was written for, and an
/// outside C++ plugin also reports the version it was compiled against. Both
/// must equal this number, or Khan refuses to load the plugin with a message.
/// The number goes up whenever a change could break an existing plugin.
/// Version 2 added actions, the shared selection and services. Version 3 added
/// panels the student can open more than once (PanelSpec::allowMultiple).
/// Version 4 lets plugins share things: services offered by plugins, onStart(),
/// publish(), and "uses" in plugin.json. Version 5 added settings(), actions
/// that run plugin code with a check mark, submenus, and status-bar items.
/// Version 6 added overlays() and moved the grid out of kh_plugin into the
/// kh.grid plugin (its kh.grid/1 service). Version 7 added tiers ("tier" and
/// "tiers" in plugin.json, and in PluginManifest). Version 8 added Labels
/// overlays (words drawn in the viewers). Version 9 added panels that start
/// closed (PanelSpec::startHidden) and UiRegistry::showPanel(). Version 10
/// added overlays for one pane (Overlay::pane, PluginOverlays::forPane()).
/// Version 11 added command arguments that take several steps
/// (model::ArgType::FeatureList) and actions that use every selected step
/// (ActionSpec::fromSelectionList). Version 12 added folders: command
/// arguments that name a folder (model::ArgType::FolderRef).
inline constexpr int kPluginApiVersion = 12;

/// The function every plugin provides. Khan calls it once, at start, to let the
/// plugin add its commands and start listening to events.
/// @param ctx The plugin's door into Khan. It stays valid until Khan closes.
using RegisterFn = void (*)(PluginContext& ctx);

}  // namespace kh::plugin

/// Marks a function as visible from outside a plugin's shared library.
#if defined(_WIN32)
#define KH_PLUGIN_EXPORT __declspec(dllexport)
#else
#define KH_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/// Turns a function into the entry point of an outside C++ plugin.
///
/// Put this once in the plugin's code, after the function. It exports the two
/// functions Khan looks for in the shared library: `kh_plugin_api_version`, so
/// Khan can check the plugin was built for this API, and `kh_plugin_register`,
/// which calls @p fn.
///
/// @code
/// void registerHello(kh::plugin::PluginContext& ctx) { ... }
/// KH_PLUGIN_ENTRY(registerHello)
/// @endcode
/// @param fn A function `void fn(kh::plugin::PluginContext&)`.
#define KH_PLUGIN_ENTRY(fn)                                                                 \
    extern "C" KH_PLUGIN_EXPORT int kh_plugin_api_version() {                               \
        return ::kh::plugin::kPluginApiVersion;                                             \
    }                                                                                       \
    extern "C" KH_PLUGIN_EXPORT void kh_plugin_register(::kh::plugin::PluginContext& ctx) { \
        fn(ctx);                                                                            \
    }
