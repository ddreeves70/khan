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

GridExtent gridExtent(double spacing) {
    constexpr double kMinHalf = 100.0;    // mm
    constexpr double kUsualLines = 50.0;  // each way from the origin
    constexpr double kMaxLines = 100.0;   // each way, for fine grids
    GridExtent e;
    e.step = spacing > 0 ? spacing : 10.0;
    e.halfSize = std::min(std::max(kMinHalf, e.step * kUsualLines), e.step * kMaxLines);
    return e;
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

}  // namespace kh::view
