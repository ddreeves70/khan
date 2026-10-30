// Tests for the 3D view's plain logic: panes, standard views, overlays and picking.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

#include "kh/view/Layout.h"
#include "kh/view/Scene.h"

using namespace kh;
using namespace kh::view;
using Catch::Approx;

namespace {
double dot(const kernel::Vec3& a, const kernel::Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double length(const kernel::Vec3& v) {
    return std::sqrt(dot(v, v));
}
}  // namespace

TEST_CASE("each layout has the right number of panes and starting views", "[view]") {
    CHECK(paneCount(PaneLayout::Single) == 1);
    CHECK(paneCount(PaneLayout::SideBySide) == 2);
    CHECK(paneCount(PaneLayout::Stacked) == 2);
    CHECK(paneCount(PaneLayout::Four) == 4);
    for (auto layout : allLayouts()) {
        CHECK(static_cast<int>(defaultViews(layout).size()) == paneCount(layout));
    }
    CHECK(defaultViews(PaneLayout::Single) == std::vector{StandardView::Iso});
    // Like a drawing sheet: Top above Front, Right beside Front.
    CHECK(defaultViews(PaneLayout::Four) ==
          std::vector{StandardView::Top, StandardView::Iso, StandardView::Front, StandardView::Right});
}

TEST_CASE("standard views look from the right side, with sensible up directions", "[view]") {
    for (auto v : allViews()) {
        const auto d = direction(v);
        INFO(viewName(v));
        CHECK(length(d.toEye) == Approx(1.0));
        CHECK(length(d.up) == Approx(1.0));
        CHECK(dot(d.toEye, d.up) == Approx(0.0).margin(1e-12));
    }
    CHECK(direction(StandardView::Front).toEye.y == Approx(-1.0));
    CHECK(direction(StandardView::Top).toEye.z == Approx(1.0));
    CHECK(direction(StandardView::Right).toEye.x == Approx(1.0));
    // Iso looks from the front, right and above, with Z pointing up the screen.
    const auto iso = direction(StandardView::Iso);
    CHECK(iso.toEye.x > 0);
    CHECK(iso.toEye.y < 0);
    CHECK(iso.toEye.z > 0);
    CHECK(iso.up.z > 0.8);
    // Every side view keeps Z up.
    for (auto v : {StandardView::Front, StandardView::Back, StandardView::Left, StandardView::Right}) {
        CHECK(direction(v).up.z == Approx(1.0));
    }
}

TEST_CASE("views and layouts have names the toolbar shows", "[view]") {
    std::set<std::string_view> names;
    for (auto v : allViews()) {
        names.insert(viewName(v));
        CHECK(parseView(viewName(v)) == v);
    }
    CHECK(names.size() == allViews().size());
    CHECK(parseView("TOP") == StandardView::Top);
    CHECK_FALSE(parseView("sideways").has_value());
    CHECK(layoutName(PaneLayout::Four) == "Four panes");
}

TEST_CASE("a lattice is lines every spacing, reaching half its size each way", "[view]") {
    ViewOverlay o;
    o.shape = OverlayShape::Lattice;
    o.spacing = 10;
    o.halfSize = 50;
    const auto lines = latticeLines(o);
    REQUIRE(lines.size() == 11 * 4);  // 11 lines each way (-50 to 50), 2 points each
    double minX = 1e9, maxX = -1e9;
    for (const auto& p : lines) {
        CHECK(p.z == 0);
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        // Every point is on a line: x or y is a multiple of 10 mm.
        CHECK((std::fmod(std::abs(p.x), 10.0) == Approx(0).margin(1e-9) ||
               std::fmod(std::abs(p.y), 10.0) == Approx(0).margin(1e-9)));
    }
    CHECK(minX == -50);
    CHECK(maxX == 50);

    // Dots at the crossings, and a lattice on another plane, moved.
    o.dots = true;
    o.origin = {0, 0, 5};
    o.xAxis = {1, 0, 0};
    o.yAxis = {0, 0, 1};  // the XZ plane, 5 mm up
    const auto dots = latticePoints(o);
    REQUIRE(dots.size() == 11 * 11);
    for (const auto& p : dots) CHECK(p.y == 0);
    CHECK(dots.front().z == -45);
    CHECK(dots.back().z == 55);

    // A spacing that isn't positive draws nothing, instead of looping forever.
    o.spacing = 0;
    CHECK(latticeLines(o).empty());
    CHECK(latticePoints(o).empty());
}

TEST_CASE("a click keeps the picking order", "[view]") {
    const auto a = FeatureId::generate(), b = FeatureId::generate(), c = FeatureId::generate();
    // A plain click on b replaces the selection.
    CHECK(mergePick({a, c}, {b}) == std::vector{b});
    // Ctrl-click on b adds it at the end, whatever order the view lists them in.
    CHECK(mergePick({c, a}, {a, b, c}) == std::vector{c, a, b});
    // Ctrl-click on a selected step removes it, and the rest keep their order.
    CHECK(mergePick({c, a, b}, {b, c}) == std::vector{c, b});
    // Clicking empty space clears it.
    CHECK(mergePick({a}, {}).empty());
    // Duplicates collapse.
    CHECK(mergePick({}, {a, a}) == std::vector{a});
}

TEST_CASE("display styles have names the view bar shows, and are found by them", "[view]") {
    REQUIRE(view::allDisplayStyles().size() == 3);
    CHECK(view::allDisplayStyles().front() == view::DisplayStyle::ShadedWithEdges);  // the usual one first
    for (auto s : view::allDisplayStyles()) {
        CHECK(view::parseDisplayStyle(view::displayStyleName(s)) == s);
    }
    CHECK(view::displayStyleName(view::DisplayStyle::Wireframe) == "Wireframe");
    CHECK(view::parseDisplayStyle("SHADED") == view::DisplayStyle::Shaded);
    CHECK_FALSE(view::parseDisplayStyle("x-ray"));
}
