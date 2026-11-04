#include "sb/plugins/Grid.h"

#include <QLabel>

#include <algorithm>
#include <cmath>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "sb/plugin/Events.h"
#include "sb/ui/UiRegistry.h"

namespace sb::plugins {

namespace {

using grid::GridChanged;
using grid::GridSettings;
using grid::GridStyle;

constexpr const char* kManifest = R"({
  "id": "sb.grid",
  "name": "Grid",
  "version": "0.1.0",
  "description": "The grid: its View menu items, its drawing in the viewers, and snapping to it.",
  "tier": "foundation",
  "requires": ["sb.viewoptions"]
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

// The status-bar text: the grid in the pane the student is working in.
class GridLabel final : public QLabel {
public:
    GridLabel(plugin::EventBus& bus, const GridService& grid, const viewoptions::IViewOptions& options)
        : grid_(grid), options_(options) {
        update();
        subs_.push_back(bus.subscribe<GridChanged>([this](const GridChanged&) { update(); }));
        subs_.push_back(bus.subscribe<viewoptions::PanesChanged>([this](const auto&) { update(); }));
    }

private:
    void update() { setText(QString::fromStdString(statusText(grid_.settings(options_.activePane())))); }

    const GridService& grid_;
    const viewoptions::IViewOptions& options_;
    std::vector<plugin::Subscription> subs_;
};

void registerGrid(plugin::PluginContext& ctx) {
    // The grid's settings are view options: sb.viewoptions remembers them and
    // builds their View menu items. It's in "requires", so it's here already.
    auto* options = ctx.service<viewoptions::IViewOptions>();
    if (!options) {
        ctx.fail(
            "The grid needs the View options plugin (sb.viewoptions). Reinstall SketchBillet to get it "
            "back.");
        return;
    }
    for (auto& spec : gridOptions()) {
        if (auto problems = viewoptions::addOption(ctx, *options, std::move(spec)); !problems.empty()) {
            ctx.fail(problems.front().message);
            return;
        }
    }

    // Shared by the listener below, which SketchBillet keeps while the plugin is loaded,
    // so the grid lives exactly as long as it does.
    auto grid = std::make_shared<GridService>(*options, ctx.events());
    ctx.provide<grid::IGrid>(*grid);

    // Drawing: one lattice overlay for every pane, plus one for each pane that
    // has its own grid. Redrawn whenever the grid or the panes change.
    auto& overlays = ctx.overlays();
    auto drawn = std::make_shared<std::set<std::string>>();  // panes with their own lattice
    auto redraw = [grid, options, &overlays, drawn] {
        overlays.set("grid", gridOverlay(grid->settings()));
        std::set<std::string> now;
        for (const auto& p : options->panes()) {
            if (!grid->hasOwn(p.key)) continue;
            overlays.set("grid", gridOverlay(grid->settings(p.key)), p.key);
            now.insert(p.key);
        }
        for (const auto& pane : *drawn) {
            if (!now.count(pane)) overlays.remove("grid", pane);
        }
        *drawn = std::move(now);
    };
    redraw();
    ctx.listen<GridChanged>([redraw](const GridChanged&) { redraw(); });
    ctx.listen<viewoptions::PanesChanged>([redraw](const viewoptions::PanesChanged&) { redraw(); });

    // The status bar, in the desktop app.
    if (auto* ui = ctx.service<ui::UiRegistry>()) {
        auto& bus = ctx.events();
        ui->addStatusItem(
            {"sb.grid.status", [grid, options, &bus] { return new GridLabel(bus, *grid, *options); }});
    }
}

}  // namespace

std::vector<viewoptions::OptionSpec> gridOptions() {
    using viewoptions::OptionKind;
    return {
        {.id = "sb.grid.show",
         .text = "Show grid",
         .group = "Grid",
         .tip = "Show or hide the grid in the views.",
         .kind = OptionKind::Flag,
         .initial = true,
         .shortcut = "Ctrl+G"},
        {.id = "sb.grid.snap",
         .text = "Snap to grid",
         .group = "Grid",
         .tip = "Make points you pick with the mouse jump to the nearest grid crossing.",
         .kind = OptionKind::Flag,
         .initial = false,
         .shortcut = "Ctrl+Shift+G"},
        {.id = "sb.grid.style",
         .text = "Grid style",
         .group = "Grid",
         .tip = "Lines look like graph paper; dots are lighter and keep the part easy to see.",
         .kind = OptionKind::Choice,
         .initial = std::string("lines"),
         .choices = {{"lines", "Lines"}, {"dots", "Dots"}}},
        {.id = "sb.grid.spacing",
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
    : options_(options), bus_(bus), settings_(read({})) {
    sub_ = bus.subscribe<viewoptions::ViewOptionChanged>(
        [this](const viewoptions::ViewOptionChanged& e) { changed(e); });
}

namespace {
const char* const kGridOptions[] = {"sb.grid.show", "sb.grid.snap", "sb.grid.style", "sb.grid.spacing"};
}  // namespace

GridSettings GridService::read(std::string_view pane) const {
    GridSettings g;
    g.visible = options_.flag("sb.grid.show", pane);
    g.snap = options_.flag("sb.grid.snap", pane);
    g.style = options_.choice("sb.grid.style", pane) == "dots" ? GridStyle::Dots : GridStyle::Lines;
    if (const double spacing = options_.number("sb.grid.spacing", pane); spacing > 0) g.spacing = spacing;
    return g;
}

GridSettings GridService::settings(std::string_view pane) const {
    return pane.empty() ? settings_ : read(pane);
}

bool GridService::hasOwn(std::string_view pane) const {
    for (const char* id : kGridOptions) {
        if (options_.hasOwn(id, pane)) return true;
    }
    return false;
}

Diagnostics GridService::set(const GridSettings& next) {
    writing_ = true;  // one GridChanged for all four options, not one each
    // The spacing first: if it's refused, nothing changes.
    auto problems = options_.set("sb.grid.spacing", next.spacing);
    if (problems.empty()) {
        options_.set("sb.grid.show", next.visible);
        options_.set("sb.grid.snap", next.snap);
        options_.set("sb.grid.style", std::string(next.style == GridStyle::Dots ? "dots" : "lines"));
    }
    writing_ = false;
    changed({"sb.grid.spacing", {}});
    return problems;
}

Diagnostics GridService::setSpacing(double spacing) {
    auto next = settings_;
    next.spacing = spacing;
    return set(next);
}

void GridService::changed(const viewoptions::ViewOptionChanged& e) {
    if (writing_ || e.option.rfind("sb.grid.", 0) != 0) return;
    if (!e.pane.empty()) {
        bus_.publish(GridChanged{read(e.pane), e.pane});
        return;
    }
    const auto next = read({});
    if (next == settings_) return;
    settings_ = next;
    bus_.publish(GridChanged{settings_, {}});
}

kernel::Vec3 GridService::snap(const kernel::Vec3& point, std::string_view pane) const {
    const auto g = settings(pane);
    if (!g.snap) return point;
    auto one = [&](double v) {
        const double snapped = std::round(v / g.spacing) * g.spacing;
        return snapped == 0.0 ? 0.0 : snapped;  // no -0
    };
    return {one(point.x), one(point.y), one(point.z)};
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

}  // namespace sb::plugins
