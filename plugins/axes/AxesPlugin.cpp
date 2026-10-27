#include "kh/plugins/Axes.h"

#include <memory>
#include <set>
#include <string>

#include "kh/plugin/Events.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.axes",
  "name": "Axes",
  "version": "0.1.0",
  "description": "The x, y and z axes at the origin, with their letters.",
  "tier": "ui",
  "requires": ["kh.viewoptions"]
})";

// Draws (or hides) the axes as the options say: for every pane, and again for
// each pane that has its own axes options. `drawn` lists those panes.
void show(plugin::PluginContext& ctx, const viewoptions::IViewOptions& options,
          std::set<std::string>& drawn) {
    for (auto& a : axesOverlays(options.flag("kh.axes.show"), options.flag("kh.axes.labels")))
        ctx.overlays().set(a.name, std::move(a.overlay));
    std::set<std::string> now;
    for (const auto& p : options.panes()) {
        if (!options.hasOwn("kh.axes.show", p.key) && !options.hasOwn("kh.axes.labels", p.key)) continue;
        for (auto& a :
             axesOverlays(options.flag("kh.axes.show", p.key), options.flag("kh.axes.labels", p.key)))
            ctx.overlays().set(a.name, std::move(a.overlay), p.key);
        now.insert(p.key);
    }
    for (const auto& pane : drawn) {
        if (now.count(pane)) continue;
        for (const auto& a : axesOverlays(true, true)) ctx.overlays().remove(a.name, pane);
    }
    drawn = std::move(now);
}

void registerAxes(plugin::PluginContext& ctx) {
    // Its two settings are view options: kh.viewoptions remembers them and builds
    // their View menu items. It's in "requires", so it's here already.
    auto* options = ctx.service<viewoptions::IViewOptions>();
    if (!options) {
        ctx.fail("The axes need the View options plugin (kh.viewoptions). Reinstall Khan to get it back.");
        return;
    }
    for (auto& spec : axesOptions()) {
        if (auto problems = viewoptions::addOption(ctx, *options, std::move(spec)); !problems.empty()) {
            ctx.fail(problems.front().message);
            return;
        }
    }
    auto drawn = std::make_shared<std::set<std::string>>();
    show(ctx, *options, *drawn);
    ctx.listen<viewoptions::ViewOptionChanged>(
        [&ctx, options, drawn](const viewoptions::ViewOptionChanged& e) {
            if (e.option.rfind("kh.axes.", 0) == 0) show(ctx, *options, *drawn);
        });
    ctx.listen<viewoptions::PanesChanged>(
        [&ctx, options, drawn](const viewoptions::PanesChanged&) { show(ctx, *options, *drawn); });
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

std::vector<viewoptions::OptionSpec> axesOptions() {
    return {{.id = "kh.axes.show",
             .text = "Show axes",
             .group = "Axes",
             .tip = "Show or hide the x, y and z axes at the origin.",
             .kind = viewoptions::OptionKind::Flag,
             .initial = true},
            {.id = "kh.axes.labels",
             .text = "Show axis labels",
             .group = "Axes",
             .tip = "Write X, Y and Z at the ends of the axes.",
             .kind = viewoptions::OptionKind::Flag,
             .initial = true}};
}

plugin::BuiltinPlugin axesPlugin() {
    return {kManifest, &registerAxes};
}

}  // namespace kh::plugins
