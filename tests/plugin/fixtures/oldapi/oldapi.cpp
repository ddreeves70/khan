// Says its plugin.json API version is current, but was "built" for API 0.
#include "kh/plugin/Api.h"

extern "C" KH_PLUGIN_EXPORT int kh_plugin_api_version() {
    return 0;
}
extern "C" KH_PLUGIN_EXPORT void kh_plugin_register(kh::plugin::PluginContext&) {}
