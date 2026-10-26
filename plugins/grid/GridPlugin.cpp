#include "kh/plugins/Grid.h"

#include <QApplication>
#include <QInputDialog>
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
  "tier": "foundation"
})";

bool spacingOk(double spacing) {
    return std::isfinite(spacing) && spacing >= grid::kMinSpacing && spacing <= grid::kMaxSpacing;
}

Diagnostic badSpacing(double spacing) {
    std::ostringstream detail;
    detail << "spacing " << spacing << " mm";
    return {"grid.spacing", Severity::Error,
            "The grid spacing must be between 0.01 mm and 1000 mm. Pick a size like 1, 5 or 10 mm.",
            detail.str()};
}

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

constexpr double kPresets[] = {1.0, 2.0, 5.0, 10.0, 20.0, 50.0};  // mm

bool isPreset(double mm) {
    return std::find(std::begin(kPresets), std::end(kPresets), mm) != std::end(kPresets);
}

void registerGrid(plugin::PluginContext& ctx) {
    // Shared by the actions and listeners below, which Khan keeps while the
    // plugin is loaded, so the grid lives exactly as long as they do.
    auto grid = std::make_shared<GridService>(ctx.settings(), ctx.events());
    ctx.provide<grid::IGrid>(*grid);
    auto* ui = ctx.service<ui::UiRegistry>();

    // Drawing: one lattice overlay, redrawn whenever the grid changes.
    auto& overlays = ctx.overlays();
    overlays.set("grid", gridOverlay(grid->settings()));
    ctx.listen<GridChanged>([&overlays](const GridChanged& e) { overlays.set("grid", gridOverlay(e.grid)); });
    // Someone changed a setting in the store (another copy of the app, a test).
    ctx.listen<plugin::SettingChanged>([grid](const plugin::SettingChanged& e) {
        if (e.plugin == "kh.grid") grid->reload();
    });

    // The View menu.
    auto change = [grid](auto edit) {
        return [grid, edit] {
            auto g = grid->settings();
            edit(g);
            grid->set(g);
        };
    };
    ctx.addAction({.id = "kh.grid.show",
                   .text = "Show grid",
                   .menu = "View",
                   .shortcut = "Ctrl+G",
                   .tip = "Show or hide the grid in the views.",
                   .run = change([](GridSettings& g) { g.visible = !g.visible; }),
                   .checked = [grid] { return grid->settings().visible; }});
    ctx.addAction({.id = "kh.grid.snap",
                   .text = "Snap to grid",
                   .menu = "View",
                   .shortcut = "Ctrl+Shift+G",
                   .tip = "Make points you pick with the mouse jump to the nearest grid crossing.",
                   .run = change([](GridSettings& g) { g.snap = !g.snap; }),
                   .checked = [grid] { return grid->settings().snap; }});
    ctx.addAction({.id = "kh.grid.lines",
                   .text = "Lines",
                   .menu = "View/Grid style",
                   .tip = "Draw the grid as lines, like graph paper.",
                   .run = change([](GridSettings& g) { g.style = GridStyle::Lines; }),
                   .checked = [grid] { return grid->settings().style == GridStyle::Lines; }});
    ctx.addAction({.id = "kh.grid.dots",
                   .text = "Dots",
                   .menu = "View/Grid style",
                   .tip = "Draw the grid as a dot at every crossing.",
                   .run = change([](GridSettings& g) { g.style = GridStyle::Dots; }),
                   .checked = [grid] { return grid->settings().style == GridStyle::Dots; }});
    for (double mm : kPresets) {
        ctx.addAction({.id = "kh.grid.spacing." + mmText(mm),
                       .text = mmText(mm) + " mm",
                       .menu = "View/Grid spacing",
                       .tip = "Put the grid lines " + mmText(mm) + " mm apart.",
                       .run = [grid, mm] { grid->setSpacing(mm); },
                       .checked = [grid, mm] { return grid->settings().spacing == mm; }});
    }
    ctx.addAction({.id = "kh.grid.spacing.other",
                   .text = "Other...",
                   .menu = "View/Grid spacing",
                   .tip = "Type any distance between grid lines, from 0.01 mm to 1000 mm.",
                   .run =
                       [grid, ui] {
                           if (!qApp) return;
                           bool ok = false;
                           const double mm = QInputDialog::getDouble(
                               QApplication::activeWindow(), QObject::tr("Grid spacing"),
                               QObject::tr("Distance between grid lines (mm):"), grid->settings().spacing,
                               grid::kMinSpacing, grid::kMaxSpacing, 2, &ok);
                           if (!ok) return;
                           if (auto problems = grid->setSpacing(mm); !problems.empty() && ui)
                               ui->showProblems(problems);
                       },
                   .currentText =
                       [grid] {
                           const double mm = grid->settings().spacing;
                           return isPreset(mm) ? std::string("Other...") : "Other (" + mmText(mm) + " mm)...";
                       }});

    // The status bar, in the desktop app.
    if (ui) {
        auto& bus = ctx.events();
        ui->addStatusItem({"kh.grid.status", [grid, &bus] { return new GridLabel(bus, *grid); }});
    }
}

}  // namespace

GridService::GridService(plugin::PluginSettings& settings, plugin::EventBus& bus)
    : store_(settings), bus_(bus), settings_(read()) {}

GridSettings GridService::read() const {
    GridSettings g;
    g.visible = store_.flag("visible", g.visible);
    const double spacing = store_.number("spacing", g.spacing);
    if (spacingOk(spacing)) g.spacing = spacing;  // a damaged spacing keeps the default
    g.snap = store_.flag("snap", g.snap);
    g.style = store_.text("style", "lines") == "dots" ? GridStyle::Dots : GridStyle::Lines;
    return g;
}

Diagnostics GridService::set(const GridSettings& next) {
    if (!spacingOk(next.spacing)) return {badSpacing(next.spacing)};
    if (next == settings_) return {};
    settings_ = next;
    writing_ = true;  // one GridChanged for all four settings, not one each
    store_.set("visible", next.visible);
    store_.set("spacing", next.spacing);
    store_.set("snap", next.snap);
    store_.set("style", next.style == GridStyle::Dots ? "dots" : "lines");
    writing_ = false;
    bus_.publish(GridChanged{settings_});
    return {};
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
