// Tests for the sb.axes plugin: what it draws, and its view options.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "sb/plugin/PluginHost.h"
#include "sb/plugins/Axes.h"
#include "sb/plugins/ViewOptions.h"

using namespace sb;

TEST_CASE("the axes run along the view cube's edges, with their letters past the ends", "[axes]") {
    const auto all = plugins::axesOverlays(true, true, false);
    REQUIRE(all.size() == 12);
    const char* letters[] = {"X", "Y", "Z"};
    const char* names[] = {"x", "y", "z"};
    const double h = plugin::kViewCubeHalfSize;  // its sides
    for (int i = 0; i < 3; ++i) {
        const auto& line = all[static_cast<std::size_t>(i)];
        const auto& label = all[static_cast<std::size_t>(i + 3)];
        CHECK(line.name == names[i]);
        CHECK(label.name == std::string(names[i]) + "_label");
        CHECK(line.overlay.shape == plugin::OverlayShape::Lines);
        CHECK(line.overlay.onViewCube);
        CHECK(label.overlay.onViewCube);
        REQUIRE(line.overlay.points.size() == 2);
        const auto& from = line.overlay.points[0];
        const auto& to = line.overlay.points[1];
        // From the cube's (-x, -y, -z) corner, just outside it...
        // Outside its sides, so seen straight on (Top, Front, Right) they're beside it, not behind.
        for (double c : {from.x, from.y, from.z}) CHECK(c < -h);
        // ...along one axis only, to past the cube's far side.
        const double along[] = {to.x, to.y, to.z};
        const double side[] = {from.x, from.y, from.z};
        for (int k = 0; k < 3; ++k) {
            if (k == i)
                CHECK(along[k] > h);
            else
                CHECK(along[k] == side[k]);
        }
        CHECK(label.overlay.shape == plugin::OverlayShape::Labels);
        REQUIRE(label.overlay.texts.size() == 1);
        CHECK(label.overlay.texts[0] == letters[i]);
        CHECK(label.overlay.color == line.overlay.color);
        const auto& p = label.overlay.points[0];
        CHECK((i == 0 ? p.x : i == 1 ? p.y : p.z) > along[i]);
        CHECK(line.overlay.visible);
        CHECK(label.overlay.visible);
    }
    // The origin's are off unless asked for.
    for (std::size_t i = 6; i < 12; ++i) {
        CHECK(all[i].name.rfind("origin_", 0) == 0);
        CHECK_FALSE(all[i].overlay.onViewCube);
        CHECK_FALSE(all[i].overlay.visible);
    }
    for (const auto& a : plugins::axesOverlays(true, false, false)) {
        CHECK(a.overlay.visible == (a.overlay.shape == plugin::OverlayShape::Lines && a.overlay.onViewCube));
    }
    for (const auto& a : plugins::axesOverlays(false, true, false)) CHECK_FALSE(a.overlay.visible);
}

TEST_CASE("the axes at the origin are three 25 mm lines, drawn as well as the cube's when asked", "[axes]") {
    const auto all = plugins::axesOverlays(false, true, true);
    for (int i = 0; i < 3; ++i) {
        const auto& line = all[static_cast<std::size_t>(6 + i)].overlay;
        const auto& label = all[static_cast<std::size_t>(9 + i)].overlay;
        REQUIRE(line.points.size() == 2);
        const auto& from = line.points[0];
        const auto& end = line.points[1];
        CHECK(from.x == 0);
        CHECK(from.y == 0);
        CHECK(from.z == 0);
        CHECK(end.x + end.y + end.z == plugins::kAxisLength);  // along one axis only
        CHECK(label.points[0].x + label.points[0].y + label.points[0].z > plugins::kAxisLength);
        CHECK(line.visible);
        CHECK(label.visible);
    }
    for (std::size_t i = 0; i < 6; ++i) CHECK_FALSE(all[i].overlay.visible);  // the cube's are off
}

TEST_CASE("sb.axes follows its view options, and SketchBillet remembers them", "[axes]") {
    plugin::MemorySettings store;
    store.write("plugins/sb.viewoptions/sb.axes.labels", "false");  // turned off last time
    plugin::PluginHost host;
    host.setSettings(store);
    host.addBuiltin(plugins::viewOptionsPlugin());
    host.addBuiltin(plugins::axesPlugin());
    host.loadAll();
    REQUIRE(host.find("sb.axes")->state == plugin::PluginState::Loaded);
    CHECK(host.find("sb.axes")->issues.empty());
    auto visible = [&](const char* id) { return host.overlays().find(id)->visible; };
    CHECK(visible("sb.axes.x"));
    CHECK_FALSE(visible("sb.axes.x_label"));

    const plugin::ActionSpec* show = nullptr;
    for (const auto& a : host.actions()) {
        if (a.id == "sb.axes.show") show = &a;
    }
    REQUIRE(show);
    CHECK(show->plugin == "sb.axes");  // the menu item is the axes' own
    show->run();                       // View > Show axes: off
    CHECK_FALSE(visible("sb.axes.z"));
    CHECK(store.read("plugins/sb.viewoptions/sb.axes.show") == "false");

    CHECK_FALSE(visible("sb.axes.origin_x"));  // off until the student asks
    const plugin::ActionSpec* origin = nullptr;
    for (const auto& a : host.actions()) {
        if (a.id == "sb.axes.origin") origin = &a;
    }
    REQUIRE(origin);
    origin->run();  // View > Show axes at the origin: on, even with the cube's off
    CHECK(visible("sb.axes.origin_x"));
    CHECK_FALSE(visible("sb.axes.origin_x_label"));  // the letters are still off
    CHECK(store.read("plugins/sb.viewoptions/sb.axes.origin") == "true");
}

namespace {

viewoptions::IViewOptions* peeked = nullptr;
void peek(plugin::PluginContext& ctx) {
    ctx.onStart([&ctx] { peeked = ctx.service<viewoptions::IViewOptions>(); });
}

}  // namespace

TEST_CASE("a pane can hide its axis labels on its own", "[axes][panes]") {
    plugin::MemorySettings store;
    plugin::PluginHost host;
    host.setSettings(store);
    host.addBuiltin(plugins::viewOptionsPlugin());
    host.addBuiltin(plugins::axesPlugin());
    host.addBuiltin(
        {R"({"id": "test.peek", "name": "Peek", "version": "1", "requires": ["sb.viewoptions"]})", &peek});
    host.loadAll();
    viewoptions::IViewOptions* options = peeked;
    REQUIRE(options);
    options->addPane({"v1.p1", "Viewer 1, left pane"});
    options->addPane({"v1.p2", "Viewer 1, right pane"});
    auto labelsShown = [&](const char* pane) {
        for (const auto& o : host.overlays().forPane(pane)) {
            if (o.id == "sb.axes.x_label") return o.visible;
        }
        return false;
    };
    REQUIRE(options->setInPane("sb.axes.labels", false, "v1.p1").empty());
    CHECK_FALSE(labelsShown("v1.p1"));
    CHECK(labelsShown("v1.p2"));
    REQUIRE(options->set("sb.axes.labels", true).empty());  // a change for all panes
    CHECK(labelsShown("v1.p1"));
    CHECK(host.overlays().find("sb.axes.x_label", "v1.p1") == nullptr);
}
