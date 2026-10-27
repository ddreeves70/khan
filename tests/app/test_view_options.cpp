// Tests for the kh.viewoptions plugin's service: declaring options, values,
// checking them, remembering them, events, and the menu items addOption() adds.

#include <catch2/catch_test_macros.hpp>

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
    s.options.ask("test.size");  // no way to ask (kh_cli): nothing happens
    CHECK(s.options.number("test.size") == 10.0);
    double offered = 0;
    s.options.asker = [&](const OptionSpec&, double current) -> std::optional<double> {
        offered = current;
        return 12.5;
    };
    s.options.ask("test.size");
    CHECK(offered == 10.0);
    CHECK(s.options.number("test.size") == 12.5);
    Diagnostics reported;
    s.options.report = [&](const Diagnostics& d) { reported = d; };
    s.options.asker = [](const OptionSpec&, double) -> std::optional<double> { return 0.0; };
    s.options.ask("test.size");
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

    auto find = [&](const std::string& id) -> const plugin::ActionSpec& {
        for (const auto& a : host.actions()) {
            if (a.id == id) return a;
        }
        FAIL("no action " << id);
        return host.actions().front();
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
