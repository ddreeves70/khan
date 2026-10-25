// A test plugin that loads with a warning: it sends an event whose name starts
// with another plugin's id.
#include <string_view>

#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {

struct Ping {
    static constexpr std::string_view kType = "test.other.ping";
};

void registerIt(kh::plugin::PluginContext& ctx) {
    ctx.onStart([&ctx] { ctx.publish(Ping{}); });
}

}  // namespace

KH_PLUGIN_ENTRY(registerIt)
