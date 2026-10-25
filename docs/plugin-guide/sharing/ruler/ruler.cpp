// The Ruler plugin: shares a ruler that shows lengths in inches or mm. The
// student picks with View > Ruler shows inches, and Khan remembers it.
#include <string>

#include "example/ruler/Ruler.h"
#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {

using example::ruler::IRuler;
using example::ruler::UnitChanged;

class Ruler final : public IRuler {
public:
    bool inches = true;
    const char* unit() const override { return inches ? "in" : "mm"; }
    double fromMm(double mm) const override { return inches ? mm / 25.4 : mm; }  // 25.4 mm in an inch
};

Ruler ruler;  // lives until Khan closes

void registerRuler(kh::plugin::PluginContext& ctx) {
    // The unit the student picked last time ("in" the first time).
    auto& settings = ctx.settings();
    ruler.inches = settings.text("unit", "in") == "in";

    // Step 1, registering: offer the ruler to every other plugin.
    ctx.provide<IRuler>(ruler);

    // A View menu item with a check mark. Clicking it changes the setting.
    ctx.addAction({.id = "example.ruler.inches",
                   .text = "Ruler shows inches",
                   .menu = "View",
                   .tip = "Show lengths in inches instead of mm.",
                   .run = [&settings] { settings.set("unit", ruler.inches ? "mm" : "in"); },
                   .checked = [] { return ruler.inches; }});

    // When the setting changes, change the ruler and tell the other plugins.
    ctx.listen<kh::plugin::SettingChanged>([&ctx, &settings](const kh::plugin::SettingChanged& e) {
        if (e.plugin != "example.ruler" || e.key != "unit") return;
        ruler.inches = settings.text("unit", "in") == "in";
        ctx.publish(UnitChanged{ruler.unit()});
    });

    // Step 2, starting: every plugin has registered. Tell them the unit.
    ctx.onStart([&ctx] { ctx.publish(UnitChanged{ruler.unit()}); });
}

}  // namespace

KH_PLUGIN_ENTRY(registerRuler)
