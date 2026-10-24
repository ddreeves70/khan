#pragma once

/// @file
/// kh.modeling: the built-in plugin with Khan's modeling commands.

#include "kh/plugin/PluginHost.h"

/// Khan's own built-in plugins.
namespace kh::plugins {

/// Gives the kh.modeling plugin: the box, cylinder and Boolean commands, plus
/// delete and rename.
///
/// @code
/// plugin::PluginHost host;
/// host.addBuiltin(kh::plugins::modelingPlugin());
/// host.loadAll();
/// @endcode
/// @return Its manifest and register function, for PluginHost::addBuiltin().
///         The manifest leaves out `apiVersion`; built-ins always get this Khan's.
plugin::BuiltinPlugin modelingPlugin();

}  // namespace kh::plugins
