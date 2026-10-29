// Tests for the kh.viewoptions plugin's service: declaring options, values,
// checking them, remembering them, events, and the menu items addOption() adds.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "kh/plugin/PluginHost.h"
#include "kh/plugins/ViewOptions.h"

using namespace kh;
using namespace kh::viewoptions;

namespace {

struct Store {
    plugin::MemorySettings memory;
    plugin::EventBus bus;
    plugin::PluginSettings settings{memory, bus, "kh.viewoptions"};
    plugins::ViewOptionsService options{settings, bus};
    plugin::Subscription relay =
        bus.subscribe<plugin::SettingChanged>([this](const plugin::SettingChanged& e) {
            if (e.plugin == "kh.viewoptions") options.settingChanged(e.key);
        });
};

OptionSpec size() {
    return {.id = "test.size",
            .text = "Size",
            .group = "Test",
            .kind = OptionKind::Length,
            .initial = 10.0,
            .min = 1,
            .max = 100,
            .presets = {1, 2.5, 5, 10}};
}

OptionSpec style() {
    return {.id = "test.style",
            .text = "Style",
            .kind = OptionKind::Choice,
            .initial = std::string("a"),
            .choices = {{"a", "Ay"}, {"b", "Bee"}}};
}

}  // namespace

TEST_CASE("options start at their first value, and change when set", "[viewoptions]") {
    Store s;
    REQUIRE(s.options.add({.id = "test.on", .text = "On", .initial = true}).empty());
    REQUIRE(s.options.add(size()).empty());
    REQUIRE(s.options.add(style()).empty());
    CHECK(s.options.flag("test.on"));
    CHECK(s.options.number("test.size") == 10.0);
    CHECK(s.options.choice("test.style") == "a");
    CHECK(s.options.options().size() == 3);

    std::vector<std::string> heard;
    auto sub =
        s.bus.subscribe<ViewOptionChanged>([&](const ViewOptionChanged& e) { heard.push_back(e.option); });
    REQUIRE(s.options.set("test.on", false).empty());
    REQUIRE(s.options.set("test.on", false).empty());  // the same: no event
    REQUIRE(s.options.set("test.size", 2.5).empty());
    REQUIRE(s.options.set("test.style", std::string("b")).empty());
    CHECK(heard == std::vector<std::string>{"test.on", "test.size", "test.style"});
    CHECK_FALSE(s.options.flag("test.on"));
    CHECK(s.memory.read("plugins/kh.viewoptions/test.size") == "2.5");

    // The next run reads them back.
    plugins::ViewOptionsService again(s.settings, s.bus);
    REQUIRE(again.add(size()).empty());
    CHECK(again.number("test.size") == 2.5);
}

TEST_CASE("values outside the limits, of the wrong kind, or not a choice are refused", "[viewoptions]") {
    Store s;
    REQUIRE(s.options.add(size()).empty());
    REQUIRE(s.options.add(style()).empty());
    auto problems = s.options.set("test.size", 500.0);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "viewoptions.value");
    CHECK(problems[0].message == "Size must be between 1 mm and 100 mm. Pick one like 1, 5 or 10 mm.");
    CHECK(s.options.set("test.size", true)[0].message == "Size can't take that kind of value.");
    CHECK(s.options.set("test.style", std::string("c"))[0].message == "Style has no choice called \"c\".");
    CHECK(s.options.set("test.nothing", true)[0].code == "viewoptions.unknown");
    CHECK(s.options.number("test.size") == 10.0);  // unchanged

    // Declaring the same id twice, or with a bad first value, is refused too.
    CHECK(s.options.add(size())[0].code == "viewoptions.taken");
    auto bad = size();
    bad.id = "test.bad";
    bad.initial = 0.5;
    CHECK(s.options.add(bad)[0].code == "viewoptions.initial");
}

TEST_CASE("a damaged saved value falls back to the first value", "[viewoptions]") {
    Store s;
    s.memory.write("plugins/kh.viewoptions/test.size", "huge");
    s.memory.write("plugins/kh.viewoptions/test.style", "zebra");
    REQUIRE(s.options.add(size()).empty());
    REQUIRE(s.options.add(style()).empty());
    CHECK(s.options.number("test.size") == 10.0);
    CHECK(s.options.choice("test.style") == "a");
}

TEST_CASE("Other... asks the student, and a refused answer is reported", "[viewoptions]") {
    Store s;
    REQUIRE(s.options.add(size()).empty());
    s.options.ask("test.size", {});  // no way to ask (kh_cli): nothing happens
    CHECK(s.options.number("test.size") == 10.0);
    double offered = 0;
    s.options.asker = [&](const OptionSpec&, double current) -> std::optional<double> {
        offered = current;
        return 12.5;
    };
    s.options.ask("test.size", {});
    CHECK(offered == 10.0);
    CHECK(s.options.number("test.size") == 12.5);
    Diagnostics reported;
    s.options.report = [&](const Diagnostics& d) { reported = d; };
    s.options.asker = [](const OptionSpec&, double) -> std::optional<double> { return 0.0; };
    s.options.ask("test.size", {});
    REQUIRE(reported.size() == 1);
    CHECK(s.options.number("test.size") == 12.5);
}

namespace {

void declares(plugin::PluginContext& ctx) {
    auto* options = ctx.service<IViewOptions>();
    REQUIRE(options);
    REQUIRE(addOption(ctx, *options, {.id = "test.views.on", .text = "Show it", .initial = true}).empty());
    auto length = size();
    length.id = "test.views.size";
    REQUIRE(addOption(ctx, *options, length).empty());
    auto choice = style();
    choice.id = "test.views.style";
    REQUIRE(addOption(ctx, *options, choice).empty());
    // Another plugin's id is refused.
    CHECK(addOption(ctx, *options, {.id = "kh.grid.mine", .text = "Mine"})[0].code == "viewoptions.id");
}

}  // namespace

TEST_CASE("addOption gives each option its View menu items", "[viewoptions]") {
    plugin::PluginHost host;
    host.addBuiltin(plugins::viewOptionsPlugin());
    host.addBuiltin(
        {R"({"id": "test.views", "name": "Views", "version": "1", "requires": ["kh.viewoptions"]})",
         &declares});
    host.loadAll();
    REQUIRE(host.find("test.views")->state == plugin::PluginState::Loaded);
    CHECK(host.find("test.views")->issues.empty());  // looking it up while registering is fine: it's required

    std::vector<std::string> ids, menus;
    for (const auto& a : host.actions()) {
        if (a.plugin != "test.views") continue;
        ids.push_back(a.id);
        menus.push_back(a.menu);
    }
    CHECK(ids == std::vector<std::string>{"test.views.on", "test.views.size.1", "test.views.size.2_5",
                                          "test.views.size.5", "test.views.size.10", "test.views.size.other",
                                          "test.views.style.a", "test.views.style.b"});
    CHECK(menus[0] == "View");
    CHECK(menus[1] == "View/Size");
    CHECK(menus[6] == "View/Style");

    // (find_if and REQUIRE, not a loop then FAIL: see test_grid_plugin.cpp.)
    auto find = [&](const std::string& id) -> const plugin::ActionSpec& {
        const auto& all = host.actions();
        const auto it =
            std::find_if(all.begin(), all.end(), [&](const plugin::ActionSpec& a) { return a.id == id; });
        INFO("no action " << id);
        REQUIRE(it != all.end());
        return *it;
    };
    CHECK(find("test.views.on").checked());
    find("test.views.on").run();
    CHECK_FALSE(find("test.views.on").checked());
    CHECK(find("test.views.size.10").checked());
    CHECK(find("test.views.size.2_5").text == "2.5 mm");
    find("test.views.size.2_5").run();
    CHECK(find("test.views.size.2_5").checked());
    CHECK(find("test.views.size.other").currentText() == "Other...");
    find("test.views.style.b").run();
    CHECK(find("test.views.style.b").checked());
    CHECK_FALSE(find("test.views.style.a").checked());
}

TEST_CASE("a pane can have its own value; a change for all panes clears it", "[viewoptions][panes]") {
    Store s;
    REQUIRE(s.options.add(size()).empty());
    REQUIRE(s.options.add({.id = "test.on", .text = "On", .initial = true}).empty());
    s.options.addPane({"v1.p1", "Viewer 1, left pane"});
    s.options.addPane({"v1.p2", "Viewer 1, right pane"});
    std::vector<std::string> heard;
    auto sub = s.bus.subscribe<ViewOptionChanged>(
        [&](const ViewOptionChanged& e) { heard.push_back(e.option + "@" + e.pane); });

    REQUIRE(s.options.setInPane("test.size", 5.0, "v1.p1").empty());  // This pane only
    CHECK(s.options.number("test.size", "v1.p1") == 5.0);
    CHECK(s.options.number("test.size", "v1.p2") == 10.0);  // other panes follow all panes
    CHECK(s.options.number("test.size") == 10.0);
    CHECK(s.options.hasOwn("test.size", "v1.p1"));
    CHECK_FALSE(s.options.hasOwn("test.size", "v1.p2"));
    CHECK(s.options.ownOptions("v1.p1") == std::vector<std::string>{"test.size"});
    CHECK(heard == std::vector<std::string>{"test.size@v1.p1"});

    // A change for all panes really applies to all: the pane's own value goes.
    heard.clear();
    REQUIRE(s.options.set("test.size", 20.0).empty());
    CHECK(s.options.number("test.size", "v1.p1") == 20.0);
    CHECK_FALSE(s.options.hasOwn("test.size", "v1.p1"));
    CHECK(heard == std::vector<std::string>{"test.size@v1.p1", "test.size@"});

    // Even when the all-panes value was already that value.
    REQUIRE(s.options.setInPane("test.on", false, "v1.p2").empty());
    REQUIRE(s.options.set("test.on", true).empty());
    CHECK(s.options.flag("test.on", "v1.p2"));

    // Putting one option, or a whole pane, back.
    REQUIRE(s.options.setInPane("test.size", 5.0, "v1.p2").empty());
    REQUIRE(s.options.setInPane("test.on", false, "v1.p2").empty());
    s.options.reset("test.size", "v1.p2");
    CHECK(s.options.ownOptions("v1.p2") == std::vector<std::string>{"test.on"});
    s.options.resetPane("v1.p2");
    CHECK(s.options.ownOptions("v1.p2").empty());

    // A value a pane can't take is refused, as for all panes.
    CHECK(s.options.setInPane("test.size", 500.0, "v1.p1")[0].code == "viewoptions.value");
    // A damaged saved pane value is ignored.
    s.memory.write("plugins/kh.viewoptions/panes/v1.p1/test.size", "huge");
    CHECK_FALSE(s.options.hasOwn("test.size", "v1.p1"));
    CHECK(s.options.number("test.size", "v1.p1") == 20.0);
}

TEST_CASE("panes come and go; their own values are kept only when the window closes",
          "[viewoptions][panes]") {
    Store s;
    REQUIRE(s.options.add(size()).empty());
    int changes = 0;
    auto sub = s.bus.subscribe<PanesChanged>([&](const PanesChanged&) { ++changes; });
    s.options.addPane({"v1.p1", "Viewer 1"});
    s.options.addPane({"v1.p2", "Viewer 1, right pane"});
    s.options.addPane({"v1.p1", "Viewer 1, left pane"});  // renamed
    REQUIRE(s.options.panes().size() == 2);
    CHECK(s.options.panes()[0].name == "Viewer 1, left pane");
    CHECK(s.options.activePane() == "v1.p1");  // the first pane
    s.options.setActivePane("v1.p2");
    CHECK(s.options.activePane() == "v1.p2");
    CHECK(changes == 4);

    REQUIRE(s.options.setInPane("test.size", 5.0, "v1.p1").empty());
    REQUIRE(s.options.setInPane("test.size", 2.5, "v1.p2").empty());
    s.options.removePane("v1.p2", true);  // back to one pane: gone for good
    CHECK_FALSE(s.options.hasOwn("test.size", "v1.p2"));
    CHECK(s.options.activePane() == "v1.p1");
    s.options.removePane("v1.p1", false);  // the window closes: kept for next time
    CHECK(s.options.panes().empty());
    CHECK(s.options.hasOwn("test.size", "v1.p1"));

    // Next time: the same pane is back with its own value, and "Other..." asks for that pane.
    plugins::ViewOptionsService again(s.settings, s.bus);
    REQUIRE(again.add(size()).empty());
    again.addPane({"v1.p1", "Viewer 1"});
    CHECK(again.number("test.size", "v1.p1") == 5.0);
    again.asker = [](const OptionSpec&, double current) -> std::optional<double> { return current + 1; };
    again.ask("test.size", "v1.p1");
    CHECK(again.number("test.size", "v1.p1") == 6.0);
    CHECK(again.number("test.size") == 10.0);
}
