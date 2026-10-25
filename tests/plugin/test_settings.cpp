// Tests for plugin settings (kh_plugin): typed values, each plugin's own keys,
// SettingChanged events, and number text that's the same on every computer.

#include <catch2/catch_test_macros.hpp>

#include <locale>
#include <string>
#include <vector>

#include "kh/plugin/Events.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugin/Settings.h"

using namespace kh;
using namespace kh::plugin;

TEST_CASE("settings keep text, numbers and yes/no values", "[plugin][settings]") {
    MemorySettings store;
    EventBus bus;
    PluginSettings s(store, bus, "test.ruler");

    CHECK_FALSE(s.has("unit"));
    CHECK(s.text("unit", "mm") == "mm");  // the fallback until it's set
    CHECK(s.number("spacing", 10) == 10);
    CHECK(s.flag("inches", true));

    s.set("unit", "inch");
    s.set("spacing", 12.5);
    s.set("inches", false);
    CHECK(s.has("unit"));
    CHECK(s.text("unit") == "inch");
    CHECK(s.number("spacing") == 12.5);
    CHECK_FALSE(s.flag("inches", true));

    s.remove("unit");
    CHECK_FALSE(s.has("unit"));
    CHECK(s.text("unit", "mm") == "mm");
}

TEST_CASE("each plugin's settings are kept under its own id", "[plugin][settings]") {
    MemorySettings store;
    EventBus bus;
    PluginSettings ruler(store, bus, "test.ruler");
    PluginSettings tape(store, bus, "test.tape");
    ruler.set("unit", "inch");
    CHECK(ruler.fullKey("unit") == "plugins/test.ruler/unit");
    CHECK(store.read("plugins/test.ruler/unit") == "inch");
    CHECK_FALSE(tape.has("unit"));
}

TEST_CASE("changing a setting tells everyone, once", "[plugin][settings]") {
    MemorySettings store;
    EventBus bus;
    PluginSettings s(store, bus, "test.ruler");
    std::vector<std::string> heard;
    auto sub = bus.subscribe<SettingChanged>(
        [&](const SettingChanged& e) { heard.push_back(e.plugin + "/" + e.key); });

    s.set("unit", "inch");
    s.set("unit", "inch");  // the same value: nothing changed, no event
    s.set("spacing", 2.0);
    s.remove("spacing");
    s.remove("spacing");  // already gone
    CHECK(heard == std::vector<std::string>{"test.ruler/unit", "test.ruler/spacing", "test.ruler/spacing"});
}

TEST_CASE("numbers are stored as short text with a decimal point", "[plugin][settings]") {
    MemorySettings store;
    EventBus bus;
    PluginSettings s(store, bus, "test.n");
    s.set("a", 0.1);
    s.set("b", 25.4);
    s.set("c", 1.0 / 3.0);
    s.set("d", 500.0);
    CHECK(store.read("plugins/test.n/a") == "0.1");
    CHECK(store.read("plugins/test.n/b") == "25.4");
    CHECK(s.number("c") == 1.0 / 3.0);  // reads back exactly
    CHECK(store.read("plugins/test.n/d") == "500");

    // Text that isn't a number gives the fallback.
    s.set("e", "twelve");
    CHECK(s.number("e", 7) == 7);
    s.set("f", "3.5mm");
    CHECK(s.number("f", 7) == 7);
    s.set("g", "maybe");
    CHECK(s.flag("g", true));
}

TEST_CASE("numbers don't depend on the computer's language", "[plugin][settings]") {
    // A comma for the decimal point, as in German, if this computer has it.
    struct Comma : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };
    const std::locale old = std::locale::global(std::locale(std::locale::classic(), new Comma));
    MemorySettings store;
    EventBus bus;
    PluginSettings s(store, bus, "test.n");
    s.set("spacing", 1234.5);
    const auto text = store.read("plugins/test.n/spacing");
    const double back = s.number("spacing");
    std::locale::global(old);
    CHECK(text == "1234.5");
    CHECK(back == 1234.5);
}

namespace {

std::string manifest(const std::string& id) {
    return R"({"id": ")" + id + R"(", "name": ")" + id + R"(", "version": "1.0.0"})";
}

void remembers(PluginContext& ctx) {
    ctx.settings().set("count", ctx.settings().number("count") + 1);
}

int runs = 0;
void runAction(PluginContext& ctx) {
    ctx.addAction({.id = "test.run.go",
                   .text = "Inches",
                   .menu = "View/Ruler",
                   .run = [] { ++runs; },
                   .checked = [] { return runs % 2 == 1; }});
}
void emptyAction(PluginContext& ctx) {
    ctx.addAction({.id = "test.empty.go", .text = "Go", .menu = "View"});
}
void bothAction(PluginContext& ctx) {
    ctx.addAction(
        {.id = "test.both.go", .text = "Go", .menu = "View", .command = "feature.add_box", .run = [] {}});
}

}  // namespace

TEST_CASE("plugins keep settings in the store the program gives", "[plugin][settings]") {
    MemorySettings store;
    for (int i = 0; i < 2; ++i) {  // two runs of the program
        PluginHost host;
        host.setSettings(store);
        host.addBuiltin({manifest("test.counter"), &remembers});
        host.loadAll();
    }
    CHECK(store.read("plugins/test.counter/count") == "2");
}

TEST_CASE("an action can run plugin code and show a check mark", "[plugin][settings]") {
    PluginHost host;
    host.addBuiltin({manifest("test.run"), &runAction});
    host.addBuiltin({manifest("test.empty"), &emptyAction});
    host.addBuiltin({manifest("test.both"), &bothAction});
    host.loadAll();

    REQUIRE(host.find("test.run")->state == PluginState::Loaded);
    REQUIRE(host.actions().size() == 1);
    const auto& a = host.actions()[0];
    CHECK(a.menu == "View/Ruler");
    CHECK_FALSE(a.checked());
    a.run();
    CHECK(a.checked());

    const auto* empty = host.find("test.empty");
    CHECK(empty->state == PluginState::Failed);
    CHECK(empty->issues.at(0).message.find("no command and no run function") != std::string::npos);
    const auto* both = host.find("test.both");
    CHECK(both->state == PluginState::Failed);
    CHECK(both->issues.at(0).message.find("both a command and a run function") != std::string::npos);
}
