// Builds and loads the plugin guide's sharing example (docs/plugin-guide/sharing):
// Ruler offers a service and an event, Tape uses them. So the page can't go stale.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Modeling.h"

using namespace kh;
using namespace kh::plugin;

TEST_CASE("the guide's Tape plugin uses the Ruler plugin's ruler", "[plugin][sharing][guide]") {
    PluginHost host;
    host.addBuiltin(plugins::modelingPlugin());
    host.addFolder(KH_PLUGIN_FOLDER);
    host.loadAll();

    const auto* ruler = host.find("example.ruler");
    const auto* tape = host.find("example.tape");
    REQUIRE(ruler);
    REQUIRE(tape);
    REQUIRE(ruler->state == PluginState::Loaded);
    REQUIRE(tape->state == PluginState::Loaded);
    CHECK(ruler->services == std::vector<std::string>{"example.ruler/1"});
    // The event arrives when Ruler starts, then Tape starts and measures.
    CHECK(tape->messages ==
          std::vector<std::string>{"The ruler shows lengths in in.", "A 25.4 mm box is 1.00 in long."});
    // Both follow the rules, so neither gets a warning.
    CHECK(ruler->issues.empty());
    CHECK(tape->issues.empty());
}
