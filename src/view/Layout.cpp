#include "kh/view/Layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "kh/view/Scene.h"

namespace kh::view {

int paneCount(PaneLayout layout) {
    switch (layout) {
        case PaneLayout::Single:
            return 1;
        case PaneLayout::SideBySide:
        case PaneLayout::Stacked:
            return 2;
        case PaneLayout::Four:
            return 4;
    }
    return 1;
}

std::vector<StandardView> defaultViews(PaneLayout layout) {
    switch (layout) {
        case PaneLayout::Single:
            return {StandardView::Iso};
        case PaneLayout::SideBySide:
            return {StandardView::Front, StandardView::Iso};
        case PaneLayout::Stacked:
            return {StandardView::Top, StandardView::Front};
        case PaneLayout::Four:
            return {StandardView::Top, StandardView::Iso, StandardView::Front, StandardView::Right};
    }
    return {StandardView::Iso};
}

ViewDirection direction(StandardView view) {
    const double s = 1.0 / std::sqrt(3.0);
    switch (view) {
        case StandardView::Front:
            return {{0, -1, 0}, {0, 0, 1}};
        case StandardView::Back:
            return {{0, 1, 0}, {0, 0, 1}};
        case StandardView::Left:
            return {{-1, 0, 0}, {0, 0, 1}};
        case StandardView::Right:
            return {{1, 0, 0}, {0, 0, 1}};
        case StandardView::Top:
            return {{0, 0, 1}, {0, 1, 0}};
        case StandardView::Bottom:
            return {{0, 0, -1}, {0, -1, 0}};
        case StandardView::Iso: {
            // Up is Z, tipped back so it is at right angles to the line of sight.
            const double u = 1.0 / std::sqrt(6.0);
            return {{s, -s, s}, {-u, u, 2 * u}};
        }
    }
    return {{0, -1, 0}, {0, 0, 1}};
}

const std::vector<StandardView>& allViews() {
    static const std::vector<StandardView> views{
        StandardView::Front, StandardView::Back,   StandardView::Left, StandardView::Right,
        StandardView::Top,   StandardView::Bottom, StandardView::Iso};
    return views;
}

std::string_view viewName(StandardView view) {
    switch (view) {
        case StandardView::Front:
            return "Front";
        case StandardView::Back:
            return "Back";
        case StandardView::Left:
            return "Left";
        case StandardView::Right:
            return "Right";
        case StandardView::Top:
            return "Top";
        case StandardView::Bottom:
            return "Bottom";
        case StandardView::Iso:
            return "Iso";
    }
    return "Front";
}

std::optional<StandardView> parseView(std::string_view name) {
    auto lower = [](std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    const std::string wanted = lower(name);
    for (auto v : allViews()) {
        if (lower(viewName(v)) == wanted) return v;
    }
    return std::nullopt;
}

const std::vector<PaneLayout>& allLayouts() {
    static const std::vector<PaneLayout> layouts{PaneLayout::Single, PaneLayout::SideBySide,
                                                 PaneLayout::Stacked, PaneLayout::Four};
    return layouts;
}

std::string_view layoutName(PaneLayout layout) {
    switch (layout) {
        case PaneLayout::Single:
            return "One pane";
        case PaneLayout::SideBySide:
            return "Two side by side";
        case PaneLayout::Stacked:
            return "Two stacked";
        case PaneLayout::Four:
            return "Four panes";
    }
    return "One pane";
}

namespace {

// How many lines of a lattice each way from its origin. A spacing of 0 or less
// draws nothing.
int latticeCount(const ViewOverlay& o) {
    if (!(o.spacing > 0) || !(o.halfSize >= 0)) return -1;
    return static_cast<int>(std::floor(o.halfSize / o.spacing + 1e-9));
}

kernel::Vec3 at(const ViewOverlay& o, double u, double v) {
    return {o.origin.x + o.xAxis.x * u + o.yAxis.x * v, o.origin.y + o.xAxis.y * u + o.yAxis.y * v,
            o.origin.z + o.xAxis.z * u + o.yAxis.z * v};
}

}  // namespace

std::vector<kernel::Vec3> latticeLines(const ViewOverlay& o) {
    std::vector<kernel::Vec3> out;
    const int n = latticeCount(o);
    if (n < 0) return out;
    const double h = o.halfSize;
    out.reserve(static_cast<std::size_t>(2 * n + 1) * 4);
    for (int i = -n; i <= n; ++i) {
        const double t = i * o.spacing;
        out.push_back(at(o, t, -h));  // along y
        out.push_back(at(o, t, h));
        out.push_back(at(o, -h, t));  // along x
        out.push_back(at(o, h, t));
    }
    return out;
}

std::vector<kernel::Vec3> latticePoints(const ViewOverlay& o) {
    std::vector<kernel::Vec3> out;
    const int n = latticeCount(o);
    if (n < 0) return out;
    out.reserve(static_cast<std::size_t>(2 * n + 1) * static_cast<std::size_t>(2 * n + 1));
    for (int i = -n; i <= n; ++i) {
        for (int j = -n; j <= n; ++j) out.push_back(at(o, i * o.spacing, j * o.spacing));
    }
    return out;
}

std::vector<FeatureId> mergePick(const std::vector<FeatureId>& before, const std::vector<FeatureId>& after) {
    auto in = [](const std::vector<FeatureId>& list, const FeatureId& id) {
        return std::find(list.begin(), list.end(), id) != list.end();
    };
    std::vector<FeatureId> out;
    for (const auto& id : before) {
        if (in(after, id) && !in(out, id)) out.push_back(id);
    }
    for (const auto& id : after) {
        if (!in(out, id)) out.push_back(id);
    }
    return out;
}

std::optional<kernel::Vec3> rayHitsPlane(const kernel::Vec3& from, const kernel::Vec3& direction,
                                         const DrawingPlane& plane) {
    const auto& n = plane.normal;
    const double along = direction.x * n.x + direction.y * n.y + direction.z * n.z;
    const double len =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z) *
        std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len == 0 || std::abs(along) < 1e-9 * len) return std::nullopt;  // edge-on: no single point
    const double t = ((plane.origin.x - from.x) * n.x + (plane.origin.y - from.y) * n.y +
                      (plane.origin.z - from.z) * n.z) /
                     along;
    return kernel::Vec3{from.x + t * direction.x, from.y + t * direction.y, from.z + t * direction.z};
}

}  // namespace kh::view
