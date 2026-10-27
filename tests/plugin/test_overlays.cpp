// Tests for view overlays (kh_plugin): each plugin's names, OverlaysChanged
// events, and cleaning up after a plugin that fails.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginHost.h"

using namespace kh;
using namespace kh::plugin;

namespace {

Overlay line(double length) {
    Overlay o;
    o.points = {{0, 0, 0}, {length, 0, 0}};
    o.color = {230, 120, 20};
    return o;
}

std::string manifest(const std::string& id) {
    return R"({"id": ")" + id + R"(", "name": ")" + id + R"(", "version": "1.0.0"})";
}

void drawsThenFails(PluginContext& ctx) {
    ctx.overlays().set("guide", line(50));
    ctx.fail("Testing a plugin that fails after drawing.");
}

void draws(PluginContext& ctx) {
    ctx.overlays().set("guide", line(20));
}

}  // namespace

TEST_CASE("overlays are kept in order under each plugin's own names", "[plugin][overlays]") {
    EventBus bus;
    OverlayList list(bus);
    std::vector<std::string> heard;
    auto sub = bus.subscribe<OverlaysChanged>([&](const OverlaysChanged& e) { heard.push_back(e.id); });
    PluginOverlays a(list, "test.a");
    PluginOverlays b(list, "test.b");

    a.set("guide", line(50));
    b.set("guide", line(10));  // the same name, another plugin: no clash
    REQUIRE(list.all().size() == 2);
    CHECK(list.all()[0].id == "test.a.guide");
    CHECK(list.all()[1].id == "test.b.guide");
    CHECK(list.find("test.b.guide")->points[1].x == 10);

    a.set("guide", line(50));  // nothing changed: no event
    a.set("guide", line(60));  // replaced, keeps its place
    CHECK(list.all()[0].points[1].x == 60);
    CHECK(list.all().size() == 2);

    b.remove("guide");
    b.remove("guide");  // already gone: no event
    CHECK(list.find("test.b.guide") == nullptr);
    CHECK(heard == std::vector<std::string>{"test.a.guide", "test.b.guide", "test.a.guide", "test.b.guide"});
}

TEST_CASE("a plugin's overlays are reachable from its context and removed if it fails",
          "[plugin][overlays]") {
    PluginHost host;
    host.addBuiltin({manifest("test.broken"), &drawsThenFails});
    host.addBuiltin({manifest("test.fine"), &draws});
    host.loadAll();
    CHECK(host.find("test.broken")->state == PluginState::Failed);
    REQUIRE(host.overlays().all().size() == 1);
    CHECK(host.overlays().all()[0].id == "test.fine.guide");
}

TEST_CASE("an overlay for one pane is drawn there instead of the every-pane one", "[plugin][overlays]") {
    EventBus bus;
    OverlayList list(bus);
    PluginOverlays grid(list, "test.grid");
    grid.set("lines", line(10));
    grid.set("lines", line(5), "v1.p2");  // the right pane's own
    grid.set("marks", line(1), "v1.p2");  // only in the right pane
    auto lengths = [&](const char* pane) {
        std::vector<double> out;
        for (const auto& o : list.forPane(pane)) out.push_back(o.points[1].x);
        return out;
    };
    CHECK(lengths("v1.p1") == std::vector<double>{10});
    CHECK(lengths("v1.p2") == std::vector<double>{5, 1});
    CHECK(lengths("") == std::vector<double>{10});
    CHECK(list.find("test.grid.lines", "v1.p2")->pane == "v1.p2");
    grid.remove("lines", "v1.p2");
    CHECK(lengths("v1.p2") == std::vector<double>{10, 1});
    list.removeOwner("test.grid");  // every pane's
    CHECK(list.all().empty());
}
