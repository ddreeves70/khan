#include "kh/plugins/Grid.h"

#include <QLabel>

#include <algorithm>
#include <cmath>
#include <locale>
#include <memory>
#include <sstream>
#include <string>

#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins {

namespace {

using grid::GridChanged;
using grid::GridSettings;
using grid::GridStyle;

constexpr const char* kManifest = R"({
  "id": "kh.grid",
  "name": "Grid",
  "version": "0.1.0",
  "description": "The grid: its View menu items, its drawing in the viewers, and snapping to it.",
  "tier": "foundation",
  "requires": ["kh.viewoptions"]
})";

// "12.5", "10": the spacing as a student would write it.
std::string mmText(double mm) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << mm;
    return out.str();
}

std::string statusText(const GridSettings& g) {
    const std::string snap = g.snap ? "on" : "off";
    if (!g.visible) return "Grid off, snap " + snap;
    return "Grid " + mmText(g.spacing) + " mm, " + (g.style == GridStyle::Dots ? "dots" : "lines") +
           ", snap " + snap;
}

// The status-bar text, kept up to date by listening to the grid.
class GridLabel final : public QLabel {
public:
    GridLabel(plugin::EventBus& bus, const GridService& grid) {
        setText(QString::fromStdString(statusText(grid.settings())));
        sub_ = bus.subscribe<GridChanged>(
            [this](const GridChanged& e) { setText(QString::fromStdString(statusText(e.grid))); });
    }

private:
    plugin::Subscription sub_;
};

void registerGrid(plugin::PluginContext& ctx) {
    // The grid's settings are view options: kh.viewoptions remembers them and
    // builds their View menu items. It's in "requires", so it's here already.
    auto* options = ctx.service<viewoptions::IViewOptions>();
    if (!options) {
        ctx.fail("The grid needs the View options plugin (kh.viewoptions). Reinstall Khan to get it back.");
        return;
    }
    for (auto& spec : gridOptions()) {
        if (auto problems = viewoptions::addOption(ctx, *options, std::move(spec)); !problems.empty()) {
            ctx.fail(problems.front().message);
            return;
        }
    }

    // Shared by the listener below, which Khan keeps while the plugin is loaded,
    // so the grid lives exactly as long as it does.
    auto grid = std::make_shared<GridService>(*options, ctx.events());
    ctx.provide<grid::IGrid>(*grid);

    // Drawing: one lattice overlay, redrawn whenever the grid changes.
    auto& overlays = ctx.overlays();
    overlays.set("grid", gridOverlay(grid->settings()));
    ctx.listen<GridChanged>(
        [grid, &overlays](const GridChanged& e) { overlays.set("grid", gridOverlay(e.grid)); });

    // The status bar, in the desktop app.
    if (auto* ui = ctx.service<ui::UiRegistry>()) {
        auto& bus = ctx.events();
        ui->addStatusItem({"kh.grid.status", [grid, &bus] { return new GridLabel(bus, *grid); }});
    }
}

}  // namespace

std::vector<viewoptions::OptionSpec> gridOptions() {
    using viewoptions::OptionKind;
    return {
        {.id = "kh.grid.show",
         .text = "Show grid",
         .group = "Grid",
         .tip = "Show or hide the grid in the views.",
         .kind = OptionKind::Flag,
         .initial = true,
         .shortcut = "Ctrl+G"},
        {.id = "kh.grid.snap",
         .text = "Snap to grid",
         .group = "Grid",
         .tip = "Make points you pick with the mouse jump to the nearest grid crossing.",
         .kind = OptionKind::Flag,
         .initial = false,
         .shortcut = "Ctrl+Shift+G"},
        {.id = "kh.grid.style",
         .text = "Grid style",
         .group = "Grid",
         .tip = "Lines look like graph paper; dots are lighter and keep the part easy to see.",
         .kind = OptionKind::Choice,
         .initial = std::string("lines"),
         .choices = {{"lines", "Lines"}, {"dots", "Dots"}}},
        {.id = "kh.grid.spacing",
         .text = "Grid spacing",
         .group = "Grid",
         .tip = "The distance between grid lines, from 0.01 mm to 1000 mm.",
         .kind = OptionKind::Length,
         .initial = 10.0,
         .min = grid::kMinSpacing,
         .max = grid::kMaxSpacing,
         .presets = {1, 2, 5, 10, 20, 50}},
    };
}

GridService::GridService(viewoptions::IViewOptions& options, plugin::EventBus& bus)
    : options_(options), bus_(bus), settings_(read()) {
    sub_ = bus.subscribe<viewoptions::ViewOptionChanged>([this](const viewoptions::ViewOptionChanged& e) {
        if (e.option.rfind("kh.grid.", 0) == 0) reload();
    });
}

GridSettings GridService::read() const {
    GridSettings g;
    g.visible = options_.flag("kh.grid.show");
    g.snap = options_.flag("kh.grid.snap");
    g.style = options_.choice("kh.grid.style") == "dots" ? GridStyle::Dots : GridStyle::Lines;
    if (const double spacing = options_.number("kh.grid.spacing"); spacing > 0) g.spacing = spacing;
    return g;
}

Diagnostics GridService::set(const GridSettings& next) {
    writing_ = true;  // one GridChanged for all four options, not one each
    // The spacing first: if it's refused, nothing changes.
    auto problems = options_.set("kh.grid.spacing", next.spacing);
    if (problems.empty()) {
        options_.set("kh.grid.show", next.visible);
        options_.set("kh.grid.snap", next.snap);
        options_.set("kh.grid.style", std::string(next.style == GridStyle::Dots ? "dots" : "lines"));
    }
    writing_ = false;
    reload();
    return problems;
}

Diagnostics GridService::setSpacing(double spacing) {
    auto next = settings_;
    next.spacing = spacing;
    return set(next);
}

void GridService::reload() {
    if (writing_) return;
    const auto next = read();
    if (next == settings_) return;
    settings_ = next;
    bus_.publish(GridChanged{settings_});
}

double GridService::snap(double value) const {
    if (!settings_.snap) return value;
    const double snapped = std::round(value / settings_.spacing) * settings_.spacing;
    return snapped == 0.0 ? 0.0 : snapped;  // no -0
}

kernel::Vec3 GridService::snap(const kernel::Vec3& point) const {
    return {snap(point.x), snap(point.y), snap(point.z)};
}

plugin::Overlay gridOverlay(const GridSettings& g) {
    constexpr double kMinHalf = 100.0;    // mm
    constexpr double kUsualLines = 50.0;  // each way from the origin
    constexpr double kMaxLines = 100.0;   // each way, for fine grids
    plugin::Overlay o;
    o.shape = plugin::OverlayShape::Lattice;
    o.visible = g.visible;
    o.lattice.spacing = g.spacing > 0 ? g.spacing : 10.0;
    o.lattice.halfSize =
        std::min(std::max(kMinHalf, o.lattice.spacing * kUsualLines), o.lattice.spacing * kMaxLines);
    o.lattice.dots = g.style == GridStyle::Dots;
    if (o.lattice.dots) {
        o.color = {132, 140, 154};
        o.size = 3;
    } else {
        o.color = {204, 209, 218};
        o.size = 1;
    }
    return o;
}

plugin::BuiltinPlugin gridPlugin() {
    return {kManifest, &registerGrid};
}

}  // namespace kh::plugins
