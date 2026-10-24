#include "kh/plugin/Grid.h"

#include <cmath>
#include <sstream>

namespace kh::plugin {

namespace {

Diagnostic badSpacing(double spacing) {
    std::ostringstream detail;
    detail << "spacing " << spacing << " mm";
    return {"grid.spacing", Severity::Error,
            "The grid spacing must be between 0.01 mm and 1000 mm. Pick a size like 1, 5 or 10 mm.",
            detail.str()};
}

bool spacingOk(double spacing) {
    return std::isfinite(spacing) && spacing >= Grid::kMinSpacing && spacing <= Grid::kMaxSpacing;
}

}  // namespace

void Grid::changed(const GridSettings& next) {
    if (next == settings_) return;
    settings_ = next;
    bus_.publish(GridChanged{settings_});
}

Diagnostics Grid::set(const GridSettings& settings) {
    if (!spacingOk(settings.spacing)) return {badSpacing(settings.spacing)};
    changed(settings);
    return {};
}

void Grid::setVisible(bool visible) {
    auto next = settings_;
    next.visible = visible;
    changed(next);
}

Diagnostics Grid::setSpacing(double spacing) {
    if (!spacingOk(spacing)) return {badSpacing(spacing)};
    auto next = settings_;
    next.spacing = spacing;
    changed(next);
    return {};
}

void Grid::setSnap(bool snap) {
    auto next = settings_;
    next.snap = snap;
    changed(next);
}

void Grid::setStyle(GridStyle style) {
    auto next = settings_;
    next.style = style;
    changed(next);
}

double Grid::snap(double value) const {
    if (!settings_.snap) return value;
    const double snapped = std::round(value / settings_.spacing) * settings_.spacing;
    return snapped == 0.0 ? 0.0 : snapped;  // no -0
}

kernel::Vec3 Grid::snap(const kernel::Vec3& point) const {
    return {snap(point.x), snap(point.y), snap(point.z)};
}

}  // namespace kh::plugin
