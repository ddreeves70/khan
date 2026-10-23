#pragma once

/// @file
/// kh.properties: the built-in plugin with the Properties panel.

#include "kh/plugin/PluginHost.h"

namespace kh::plugins {

/// Gives the kh.properties plugin: a panel showing the selected step's name and
/// numbers. Changing a number edits the step through a command, so it can be
/// undone, and the design rebuilds.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin propertiesPlugin();

}  // namespace kh::plugins
