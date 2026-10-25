// Tests for the kh.grid plugin's grid: its settings, snapping, what it saves and
// what it draws. (The View menu and status bar are tested in test_shell.cpp.)

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "kh/grid/Grid.h"
#include "kh/plugin/Settings.h"
#include "kh/plugins/Grid.h"

using namespace kh;
using grid::GridChanged;
using grid::GridSettings;
using grid::GridStyle;
using plugins::GridService;

namespace {

struct Store {
    plugin::MemorySettings memory;
    plugin::EventBus bus;
    plugin::PluginSettings settings{memory, bus, "kh.grid"};
};

}  // namespace

TEST_CASE("the grid starts shown, 10 mm, no snapping, lines", "[grid]") {
    Store s;
    GridService g(s.settings, s.bus);
    CHECK(g.settings() == GridSettings{});
    CHECK(g.settings().visible);
    CHECK(g.settings().spacing == 10.0);
    CHECK_FALSE(g.settings().snap);
    CHECK(g.settings().style == GridStyle::Lines);
}

TEST_CASE("each change tells listeners once, and is saved", "[grid]") {
    Store s;
    GridService g(s.settings, s.bus);
    std::vector<GridSettings> heard;
    auto sub = s.bus.subscribe<GridChanged>([&](const GridChanged& e) { heard.push_back(e.grid); });

    auto next = g.settings();
    next.style = GridStyle::Dots;
    REQUIRE(g.set(next).empty());
    REQUIRE(g.set(next).empty());  // no change, no event
    next.snap = true;
    next.visible = false;
    REQUIRE(g.set(next).empty());  // two settings at once: one event
    REQUIRE(g.setSpacing(2.5).empty());
    REQUIRE(heard.size() == 3);
    CHECK(heard.back().spacing == 2.5);
    CHECK(heard.back().style == GridStyle::Dots);
    CHECK(heard.back().snap);
    CHECK_FALSE(heard.back().visible);

    CHECK(s.memory.read("plugins/kh.grid/style") == "dots");
    CHECK(s.memory.read("plugins/kh.grid/spacing") == "2.5");
    CHECK(s.memory.read("plugins/kh.grid/snap") == "true");
    CHECK(s.memory.read("plugins/kh.grid/visible") == "false");

    // The next run reads it back.
    GridService again(s.settings, s.bus);
    CHECK(again.settings() == heard.back());
}

TEST_CASE("a spacing outside 0.01 to 1000 mm is refused with a plain message", "[grid]") {
    Store s;
    GridService g(s.settings, s.bus);
    for (double bad : {0.0, -5.0, 0.001, 5000.0}) {
        const auto problems = g.setSpacing(bad);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].code == "grid.spacing");
        CHECK(problems[0].message.find("between 0.01 mm and 1000 mm") != std::string::npos);
    }
    CHECK(g.settings().spacing == 10.0);  // unchanged
    CHECK_FALSE(s.memory.read("plugins/kh.grid/spacing"));
}

TEST_CASE("a damaged saved spacing falls back to the default", "[grid]") {
    Store s;
    for (const char* bad : {"-3", "0", "huge", "1e9"}) {
        s.memory.write("plugins/kh.grid/spacing", bad);
        GridService g(s.settings, s.bus);
        CHECK(g.settings().spacing == 10.0);
    }
}

TEST_CASE("changes made in the store are picked up when the grid reloads", "[grid]") {
    Store s;
    GridService g(s.settings, s.bus);
    int heard = 0;
    auto sub = s.bus.subscribe<GridChanged>([&](const GridChanged&) { ++heard; });
    s.memory.write("plugins/kh.grid/style", "dots");
    g.reload();
    CHECK(g.settings().style == GridStyle::Dots);
    g.reload();  // nothing new
    CHECK(heard == 1);
}

TEST_CASE("snapping moves points to the nearest crossing, only when it's on", "[grid]") {
    Store s;
    GridService g(s.settings, s.bus);
    REQUIRE(g.setSpacing(5.0).empty());
    CHECK(g.snap(12.4) == 12.4);  // off: unchanged

    auto on = g.settings();
    on.snap = true;
    REQUIRE(g.set(on).empty());
    CHECK(g.snap(12.4) == 10.0);
    CHECK(g.snap(12.6) == 15.0);
    CHECK(g.snap(-7.6) == -10.0);
    CHECK(g.snap(-1.0) == 0.0);
    const auto p = g.snap(kernel::Vec3{2.6, -2.4, 7.4});
    CHECK(p.x == 5.0);
    CHECK(p.y == 0.0);
    CHECK(p.z == 5.0);

    REQUIRE(g.setSpacing(0.25).empty());
    CHECK(g.snap(1.13) == 1.25);
}

TEST_CASE("the grid is drawn as a lattice of about 50 lines each way, within limits", "[grid]") {
    GridSettings g;
    auto o = plugins::gridOverlay(g);
    CHECK(o.shape == plugin::OverlayShape::Lattice);
    CHECK(o.visible);
    CHECK_FALSE(o.lattice.dots);
    CHECK(o.lattice.spacing == 10);
    CHECK(o.lattice.halfSize == 500);  // 50 lines of 10 mm
    g.spacing = 1;
    CHECK(plugins::gridOverlay(g).lattice.halfSize == 100);  // at least 100 mm
    g.spacing = 1000;
    CHECK(plugins::gridOverlay(g).lattice.halfSize == 50000);
    for (double spacing : {0.01, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 1000.0}) {
        g.spacing = spacing;
        const auto x = plugins::gridOverlay(g).lattice;
        CHECK(x.halfSize / x.spacing <= 100.0 + 1e-9);  // never more than 100 lines each way
        CHECK(x.halfSize > 0);
    }
    g.style = GridStyle::Dots;
    g.visible = false;
    o = plugins::gridOverlay(g);
    CHECK(o.lattice.dots);
    CHECK_FALSE(o.visible);
}
