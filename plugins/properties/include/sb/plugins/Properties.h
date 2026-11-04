#pragma once

/// @file
/// sb.properties: the built-in plugin with the Properties panel.

#include "sb/plugin/PluginHost.h"

namespace sb::plugins {

/// Gives the sb.properties plugin: a panel showing the selected step's name and
/// numbers. Changing a number edits the step through a command, so it can be
/// undone, and the design rebuilds.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin propertiesPlugin();

}  // namespace sb::plugins
