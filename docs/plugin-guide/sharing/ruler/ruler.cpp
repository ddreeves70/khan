// The Ruler plugin: shares a ruler that shows lengths in inches.
#include "example/ruler/Ruler.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {

using example::ruler::IRuler;
using example::ruler::UnitChanged;

class InchRuler final : public IRuler {
public:
    const char* unit() const override { return "in"; }
    double fromMm(double mm) const override { return mm / 25.4; }  // 25.4 mm in an inch
};

InchRuler ruler;  // lives until Khan closes

void registerRuler(kh::plugin::PluginContext& ctx) {
    // Step 1, registering: offer the ruler to every other plugin.
    ctx.provide<IRuler>(ruler);

    // Step 2, starting: every plugin has registered. Tell them the unit.
    ctx.onStart([&ctx] { ctx.publish(UnitChanged{ruler.unit()}); });
}

}  // namespace

KH_PLUGIN_ENTRY(registerRuler)
