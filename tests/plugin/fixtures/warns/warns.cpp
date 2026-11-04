// A test plugin that loads with a warning: it sends an event whose name starts
// with another plugin's id.
#include <string_view>

#include "sb/plugin/Api.h"
#include "sb/plugin/PluginContext.h"

namespace {

struct Ping {
    static constexpr std::string_view kType = "test.other.ping";
};

void registerIt(sb::plugin::PluginContext& ctx) {
    ctx.onStart([&ctx] { ctx.publish(Ping{}); });
}

}  // namespace

SB_PLUGIN_ENTRY(registerIt)
