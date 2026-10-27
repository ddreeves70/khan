#include "kh/plugins/Axes.h"

#include <string>

#include "kh/plugin/Events.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.axes",
  "name": "Axes",
  "version": "0.1.0",
  "description": "The x, y and z axes at the origin, with their letters.",
  "tier": "ui"
})";

// Draws (or hides) the axes as the settings say.
void show(plugin::PluginContext& ctx) {
    const auto& s = ctx.settings();
    for (auto& a : axesOverlays(s.flag("visible", true), s.flag("labels", true)))
        ctx.overlays().set(a.name, std::move(a.overlay));
}

void registerAxes(plugin::PluginContext& ctx) {
    show(ctx);
    ctx.listen<plugin::SettingChanged>([&ctx](const plugin::SettingChanged& e) {
        if (e.plugin == "kh.axes") show(ctx);
    });
    auto& settings = ctx.settings();
    ctx.addAction({.id = "kh.axes.show",
                   .text = "Show axes",
                   .menu = "View",
                   .tip = "Show or hide the x, y and z axes at the origin.",
                   .run = [&settings] { settings.set("visible", !settings.flag("visible", true)); },
                   .checked = [&settings] { return settings.flag("visible", true); }});
    ctx.addAction({.id = "kh.axes.labels",
                   .text = "Show axis labels",
                   .menu = "View",
                   .tip = "Write X, Y and Z at the ends of the axes.",
                   .run = [&settings] { settings.set("labels", !settings.flag("labels", true)); },
                   .checked = [&settings] { return settings.flag("labels", true); }});
}

}  // namespace

std::vector<AxesOverlay> axesOverlays(bool axes, bool labels) {
    struct Axis {
        const char* line;
        const char* label;
        const char* letter;
        kernel::Vec3 end;
        plugin::OverlayColor color;
    };
    const double L = kAxisLength;
    const Axis list[] = {{"x", "x_label", "X", {L, 0, 0}, {209, 69, 59}},
                         {"y", "y_label", "Y", {0, L, 0}, {46, 155, 79}},
                         {"z", "z_label", "Z", {0, 0, L}, {52, 101, 196}}};
    std::vector<AxesOverlay> out;
    for (const auto& a : list) {
        plugin::Overlay line;
        line.points = {{0, 0, 0}, a.end};
        line.color = a.color;
        line.size = 2;
        line.visible = axes;
        out.push_back({a.line, std::move(line)});
    }
    for (const auto& a : list) {
        plugin::Overlay label;
        label.shape = plugin::OverlayShape::Labels;
        label.points = {{a.end.x * 1.2, a.end.y * 1.2, a.end.z * 1.2}};  // just past the end
        label.texts = {a.letter};
        label.color = a.color;
        label.size = 14;
        label.visible = axes && labels;
        out.push_back({a.label, std::move(label)});
    }
    return out;
}

plugin::BuiltinPlugin axesPlugin() {
    return {kManifest, &registerAxes};
}

}  // namespace kh::plugins
