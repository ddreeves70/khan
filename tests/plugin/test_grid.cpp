// Tests for the grid settings and snapping (kh_plugin).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "kh/plugin/Grid.h"

using namespace kh;
using namespace kh::plugin;

TEST_CASE("the grid starts shown, 10 mm, no snapping, dots", "[plugin][grid]") {
    EventBus bus;
    Grid grid(bus);
    CHECK(grid.settings().visible);
    CHECK(grid.settings().spacing == 10.0);
    CHECK_FALSE(grid.settings().snap);
    CHECK(grid.settings().style == GridStyle::Dots);
}

TEST_CASE("each change tells listeners once", "[plugin][grid]") {
    EventBus bus;
    Grid grid(bus);
    std::vector<GridSettings> heard;
    auto sub = bus.subscribe<GridChanged>([&](const GridChanged& e) { heard.push_back(e.grid); });

    grid.setStyle(GridStyle::Lines);
    grid.setStyle(GridStyle::Lines);  // no change, no event
    grid.setSnap(true);
    grid.setVisible(false);
    REQUIRE(grid.setSpacing(2.5).empty());
    REQUIRE(heard.size() == 4);
    CHECK(heard.back().spacing == 2.5);
    CHECK(heard.back().style == GridStyle::Lines);
    CHECK(heard.back().snap);
    CHECK_FALSE(heard.back().visible);
}

TEST_CASE("a spacing outside 0.01 to 1000 mm is refused with a plain message", "[plugin][grid]") {
    EventBus bus;
    Grid grid(bus);
    for (double bad : {0.0, -5.0, 0.001, 5000.0}) {
        const auto problems = grid.setSpacing(bad);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].code == "grid.spacing");
        CHECK(problems[0].message.find("between 0.01 mm and 1000 mm") != std::string::npos);
    }
    CHECK(grid.settings().spacing == 10.0);  // unchanged
    GridSettings s;
    s.spacing = -1;
    CHECK_FALSE(grid.set(s).empty());
}

TEST_CASE("snapping moves points to the nearest crossing, only when it's on", "[plugin][grid]") {
    EventBus bus;
    Grid grid(bus);
    REQUIRE(grid.setSpacing(5.0).empty());
    CHECK(grid.snap(12.4) == 12.4);  // off: unchanged

    grid.setSnap(true);
    CHECK(grid.snap(12.4) == 10.0);
    CHECK(grid.snap(12.6) == 15.0);
    CHECK(grid.snap(-7.6) == -10.0);
    CHECK(grid.snap(-1.0) == 0.0);
    const auto p = grid.snap(kernel::Vec3{2.6, -2.4, 7.4});
    CHECK(p.x == 5.0);
    CHECK(p.y == 0.0);
    CHECK(p.z == 5.0);

    REQUIRE(grid.setSpacing(0.25).empty());
    CHECK(grid.snap(1.13) == 1.25);
}
