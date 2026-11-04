#pragma once

/// @file
/// sb.modeling: the built-in plugin with SketchBillet's modeling commands.

#include "sb/plugin/PluginHost.h"

/// SketchBillet's own built-in plugins.
namespace sb::plugins {

/// Gives the sb.modeling plugin: the box, cylinder and Boolean commands, plus
/// delete and rename.
///
/// @code
/// plugin::PluginHost host;
/// host.addBuiltin(sb::plugins::modelingPlugin());
/// host.loadAll();
/// @endcode
/// @return Its manifest and register function, for PluginHost::addBuiltin().
///         The manifest leaves out `apiVersion`; built-ins always get this SketchBillet's.
plugin::BuiltinPlugin modelingPlugin();

}  // namespace sb::plugins
