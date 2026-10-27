// Tests for the kh.axes plugin: what it draws, and its view options.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Axes.h"
#include "kh/plugins/ViewOptions.h"

using namespace kh;

TEST_CASE("the axes are three 25 mm lines from the origin, with their letters past the ends", "[axes]") {
    const auto all = plugins::axesOverlays(true, true);
    REQUIRE(all.size() == 6);
    const char* letters[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        const auto& line = all[static_cast<std::size_t>(i)].overlay;
        const auto& label = all[static_cast<std::size_t>(i + 3)].overlay;
        CHECK(line.shape == plugin::OverlayShape::Lines);
        REQUIRE(line.points.size() == 2);
        const auto& end = line.points[1];
        CHECK(end.x + end.y + end.z == plugins::kAxisLength);  // along one axis only
        CHECK(label.shape == plugin::OverlayShape::Labels);
        REQUIRE(label.texts.size() == 1);
        CHECK(label.texts[0] == letters[i]);
        CHECK(label.color == line.color);
        CHECK(label.points[0].x + label.points[0].y + label.points[0].z > plugins::kAxisLength);
        CHECK(line.visible);
        CHECK(label.visible);
    }
    for (const auto& a : plugins::axesOverlays(true, false)) {
        CHECK(a.overlay.visible == (a.overlay.shape == plugin::OverlayShape::Lines));
    }
    for (const auto& a : plugins::axesOverlays(false, true)) CHECK_FALSE(a.overlay.visible);
}

TEST_CASE("kh.axes follows its view options, and Khan remembers them", "[axes]") {
    plugin::MemorySettings store;
    store.write("plugins/kh.viewoptions/kh.axes.labels", "false");  // turned off last time
    plugin::PluginHost host;
    host.setSettings(store);
    host.addBuiltin(plugins::viewOptionsPlugin());
    host.addBuiltin(plugins::axesPlugin());
    host.loadAll();
    REQUIRE(host.find("kh.axes")->state == plugin::PluginState::Loaded);
    CHECK(host.find("kh.axes")->issues.empty());
    auto visible = [&](const char* id) { return host.overlays().find(id)->visible; };
    CHECK(visible("kh.axes.x"));
    CHECK_FALSE(visible("kh.axes.x_label"));

    const plugin::ActionSpec* show = nullptr;
    for (const auto& a : host.actions()) {
        if (a.id == "kh.axes.show") show = &a;
    }
    REQUIRE(show);
    CHECK(show->plugin == "kh.axes");  // the menu item is the axes' own
    show->run();                       // View > Show axes: off
    CHECK_FALSE(visible("kh.axes.z"));
    CHECK(store.read("plugins/kh.viewoptions/kh.axes.show") == "false");
}
