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
  "description": "The x, y and z axes on the view cube (and at the origin if you like), with their letters.",
  "tier": "ui",
  "requires": ["kh.viewoptions"]
})";

constexpr const char* kIds[] = {"kh.axes.show", "kh.axes.labels", "kh.axes.origin"};

// The axes as the options say, for every pane (an empty key) or for one.
std::vector<AxesOverlay> overlaysFor(const viewoptions::IViewOptions& options, const std::string& pane) {
    return axesOverlays(options.flag(kIds[0], pane), options.flag(kIds[1], pane),
                        options.flag(kIds[2], pane));
}

// Draws (or hides) the axes as the options say: for every pane, and again for
// each pane that has its own axes options. `drawn` lists those panes.
void show(plugin::PluginContext& ctx, const viewoptions::IViewOptions& options,
          std::set<std::string>& drawn) {
    for (auto& a : overlaysFor(options, {})) ctx.overlays().set(a.name, std::move(a.overlay));
    std::set<std::string> now;
    for (const auto& p : options.panes()) {
        bool own = false;
        for (const char* id : kIds) own = own || options.hasOwn(id, p.key);
        if (!own) continue;
        for (auto& a : overlaysFor(options, p.key)) ctx.overlays().set(a.name, std::move(a.overlay), p.key);
        now.insert(p.key);
    }
    for (const auto& pane : drawn) {
        if (now.count(pane)) continue;
        for (const auto& a : axesOverlays(true, true, true)) ctx.overlays().remove(a.name, pane);
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

std::vector<AxesOverlay> axesOverlays(bool onCube, bool labels, bool atOrigin) {
    struct Axis {
        const char* name;
        const char* letter;
        kernel::Vec3 along;
        plugin::OverlayColor color;
    };
    const Axis list[] = {{"x", "X", {1, 0, 0}, {209, 69, 59}},
                         {"y", "Y", {0, 1, 0}, {46, 155, 79}},
                         {"z", "Z", {0, 0, 1}, {52, 101, 196}}};
    // On the cube, in pixels: from the corner nearest (-x, -y, -z), along its
    // edges just outside their bevels, and a little past them, like Fusion's.
    const double corner = -(plugin::kViewCubeHalfSize - plugin::kViewCubeBevel / 2 + kCubeAxisGap);
    const double cubeEnd = plugin::kViewCubeHalfSize + kCubeAxisOvershoot;
    struct Set {
        const char* prefix;
        bool shown;
        kernel::Vec3 start;
        double end;        // how far along the axis the line ends
        double labelAt;    // and where its letter goes
        double labelSize;  // pixels
        bool onViewCube;
    };
    const Set sets[] = {
        {"", onCube, {corner, corner, corner}, cubeEnd, cubeEnd + 9, 12, true},
        {"origin_", atOrigin, {0, 0, 0}, kAxisLength, kAxisLength * 1.2, 14, false},
    };
    std::vector<AxesOverlay> out;
    for (const auto& set : sets) {
        auto at = [&set](const Axis& a, double t) {
            // The start, moved to `t` along the axis.
            return kernel::Vec3{a.along.x ? t : set.start.x, a.along.y ? t : set.start.y,
                                a.along.z ? t : set.start.z};
        };
        for (const auto& a : list) {
            plugin::Overlay line;
            line.points = {set.start, at(a, set.end)};
            line.color = a.color;
            line.size = 2;
            line.visible = set.shown;
            line.onViewCube = set.onViewCube;
            out.push_back({std::string(set.prefix) + a.name, std::move(line)});
        }
        for (const auto& a : list) {
            plugin::Overlay label;
            label.shape = plugin::OverlayShape::Labels;
            label.points = {at(a, set.labelAt)};  // just past the end
            label.texts = {a.letter};
            label.color = a.color;
            label.size = set.labelSize;
            label.visible = set.shown && labels;
            label.onViewCube = set.onViewCube;
            out.push_back({std::string(set.prefix) + a.name + "_label", std::move(label)});
        }
    }
    return out;
}

std::vector<viewoptions::OptionSpec> axesOptions() {
    return {{.id = "kh.axes.show",
             .text = "Show axes",
             .group = "Axes",
             .tip = "Show or hide the x, y and z axes on the view cube.",
             .kind = viewoptions::OptionKind::Flag,
             .initial = true},
            {.id = "kh.axes.labels",
             .text = "Show axis labels",
             .group = "Axes",
             .tip = "Write X, Y and Z at the ends of the axes.",
             .kind = viewoptions::OptionKind::Flag,
             .initial = true},
            {.id = "kh.axes.origin",
             .text = "Show axes at the origin",
             .group = "Axes",
             .tip = "Also draw the axes at the origin (0, 0, 0), 25 mm long.",
             .kind = viewoptions::OptionKind::Flag,
             .initial = false}};
}

plugin::BuiltinPlugin axesPlugin() {
    return {kManifest, &registerAxes};
}

}  // namespace kh::plugins
