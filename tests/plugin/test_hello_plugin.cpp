// Builds and loads the plugin guide's Hello plugin (docs/plugin-guide/hello),
// so the tutorial can't go stale.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "../model/FakeKernel.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Modeling.h"

using namespace kh;
using namespace kh::plugin;

TEST_CASE("the guide's Hello plugin loads and adds a working command", "[plugin][hello]") {
    test::FakeKernel kernel;
    PluginHost host(&kernel);
    host.addBuiltin(plugins::modelingPlugin());
    host.addFolder(KH_PLUGIN_FOLDER);
    host.loadAll();

    const auto* hello = host.find("example.hello");
    REQUIRE(hello);
    INFO((hello->issues.empty() ? std::string() : hello->issues[0].message + " " + hello->issues[0].detail));
    REQUIRE(hello->state == PluginState::Loaded);
    CHECK_FALSE(hello->builtin);
    CHECK(hello->manifest.name == "Hello");
    CHECK(hello->commands == std::vector<std::string>{"example.hello.add_cube"});
    CHECK(hello->actions == std::vector<std::string>{"example.hello.cube"});

    // It needs kh.modeling, so it loads after it.
    CHECK(host.plugins().at(0).manifest.id == "kh.modeling");
    CHECK(host.plugins().at(1).manifest.id == "example.hello");

    // Run its command: a 20 mm cube by default, and 5 mm when asked.
    auto& docs = host.documents();
    const DocId doc = docs.add(std::make_unique<model::Document>());
    REQUIRE(docs.run(doc, "example.hello.add_cube", {}).empty());
    REQUIRE(docs.run(doc, "example.hello.add_cube", {{"size", 5.0}}).empty());

    const auto& design = *docs.find(doc);
    REQUIRE(design.featureCount() == 2);
    CHECK(design.featureAt(0).name() == "Cube1");
    CHECK(design.featureAt(1).name() == "Cube2");
    const auto* report = docs.lastRebuild(doc);
    REQUIRE(report);
    CHECK(kernel.massProps(*report->shapeOf(design.featureAt(0).id())).volume == 8000.0);  // 20^3 mm^3
    CHECK(kernel.massProps(*report->shapeOf(design.featureAt(1).id())).volume == 125.0);   // 5^3 mm^3

    // Undo works on a plugin's step like any other.
    CHECK(docs.undo(doc));
    CHECK(design.featureCount() == 1);

    // Its listener heard every change.
    const auto& log = host.find("example.hello")->messages;
    CHECK(log == std::vector<std::string>{"Hello saw a change: Add Cube1", "Hello saw a change: Add Cube2",
                                          "Hello saw a change: Add Cube2"});
    CHECK(host.eventProblems().empty());
}
