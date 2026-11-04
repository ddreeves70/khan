#pragma once

/// @file
/// sb.parameters: the built-in plugin with the Parameters panel.

#include "sb/plugin/PluginHost.h"

namespace sb::plugins {

/// Gives the sb.parameters plugin: a panel listing the active design's
/// parameters (name, value or formula, what it works out to, and a note), where
/// they're added, changed, renamed and deleted in place, through the
/// parameter.set, parameter.rename and parameter.delete commands. Modify >
/// Parameters opens it.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin parametersPlugin();

}  // namespace sb::plugins
