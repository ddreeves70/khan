// The Tape plugin: measures with the Ruler plugin's ruler, if it's there.
#include <cstdio>
#include <string>

#include "example/ruler/Ruler.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {

using example::ruler::IRuler;
using example::ruler::UnitChanged;

std::string twoDecimals(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", v);
    return text;
}

void registerTape(kh::plugin::PluginContext& ctx) {
    // Listening needs no ruler yet: the event arrives when the ruler starts.
    ctx.listen<UnitChanged>(
        [&ctx](const UnitChanged& e) { ctx.log(std::string("The ruler shows lengths in ") + e.unit + "."); });

    // Look up other plugins' services when starting, not while registering.
    ctx.onStart([&ctx] {
        const auto* ruler = ctx.service<IRuler>();
        if (!ruler) {
            ctx.log("There's no ruler, so lengths stay in mm.");
            return;
        }
        ctx.log("A 25.4 mm box is " + twoDecimals(ruler->fromMm(25.4)) + " " + ruler->unit() + " long.");
    });
}

}  // namespace

KH_PLUGIN_ENTRY(registerTape)
