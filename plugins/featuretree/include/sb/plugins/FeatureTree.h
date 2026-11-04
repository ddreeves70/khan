#pragma once

/// @file
/// sb.featuretree: the built-in plugin with the feature tree panel.

#include "sb/plugin/PluginHost.h"

namespace sb::plugins {

/// Gives the sb.featuretree plugin: a panel listing the active design's steps in
/// order, with how each one built. Picking rows selects steps everywhere; a row
/// can be renamed in place or deleted.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin featureTreePlugin();

}  // namespace sb::plugins
