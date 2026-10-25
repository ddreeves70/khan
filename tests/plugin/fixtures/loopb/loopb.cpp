// A test plugin in a loop with test.loopa: Khan loads both, with a warning.
#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {
void registerIt(kh::plugin::PluginContext&) {}
}  // namespace

KH_PLUGIN_ENTRY(registerIt)
