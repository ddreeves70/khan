// Says its plugin.json API version is current, but was "built" for API 0.
#include "sb/plugin/Api.h"

extern "C" SB_PLUGIN_EXPORT int sb_plugin_api_version() {
    return 0;
}
extern "C" SB_PLUGIN_EXPORT void sb_plugin_register(sb::plugin::PluginContext&) {}
