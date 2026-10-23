#pragma once

/// @file
/// kh.featuretree: the built-in plugin with the feature tree panel.

#include "kh/plugin/PluginHost.h"

namespace kh::plugins {

/// Gives the kh.featuretree plugin: a panel listing the active design's steps in
/// order, with how each one built. Picking rows selects steps everywhere; a row
/// can be renamed in place or deleted.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin featureTreePlugin();

}  // namespace kh::plugins
