// Tests for plugin tiers: which way dependencies should run, tiers plugins add,
// and the warnings (see docs/design/plugin-contracts.md, "Tiers").

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "kh/plugin/Manifest.h"
#include "kh/plugin/PluginHost.h"

using namespace kh;
using namespace kh::plugin;

namespace {

std::string manifest(const std::string& id, const std::string& extra = "") {
    return R"({"id": ")" + id + R"(", "name": ")" + id + R"(", "version": "1.0.0")" +
           (extra.empty() ? "" : ", " + extra) + "}";
}

void nothing(PluginContext&) {}

const PluginInfo& get(const PluginHost& host, const char* id) {
    const auto* p = host.find(id);
    REQUIRE(p);
    return *p;
}

std::vector<std::string> codes(const PluginInfo& p) {
    std::vector<std::string> out;
    for (const auto& d : p.issues) out.push_back(d.code);
    return out;
}

}  // namespace

TEST_CASE("plugin.json can say a plugin's tier and add tiers", "[plugin][tiers]") {
    const auto r = parseManifest(R"({"id": "sam.mill", "name": "Mill", "version": "1", "apiVersion": 6,
        "tier": "sam.mill.machining",
        "tiers": [{"name": "sam.mill.machining", "after": "tools", "before": "ui"},
                  {"name": "sam.mill.posts", "after": "sam.mill.machining"}]})");
    REQUIRE(r.ok());
    CHECK(r.manifest->tier == "sam.mill.machining");
    REQUIRE(r.manifest->tiers.size() == 2);
    CHECK(r.manifest->tiers[0].name == "sam.mill.machining");
    CHECK(r.manifest->tiers[0].after == "tools");
    CHECK(r.manifest->tiers[0].before == "ui");
    CHECK(r.manifest->tiers[1].before.empty());

    for (const char* bad : {R"("tiers": "tools")", R"("tiers": [{"after": "tools"}])",
                            R"("tiers": [{"name": "sam.mill.x", "after": 3}])", R"("tier": 2)"}) {
        const auto b =
            parseManifest(R"({"id": "sam.mill", "name": "Mill", "version": "1", "apiVersion": 6, )" +
                          std::string(bad) + "}");
        INFO(bad);
        CHECK_FALSE(b.ok());
    }
}

TEST_CASE("using a plugin in a lower or the same tier is fine; a higher one is warned about",
          "[plugin][tiers]") {
    PluginHost host;
    host.addBuiltin({manifest("test.base", R"("tier": "foundation")"), &nothing});
    host.addBuiltin({manifest("test.base2", R"("tier": "foundation", "uses": ["test.base"])"), &nothing});
    host.addBuiltin({manifest("test.tool", R"("tier": "tools", "requires": ["test.base"])"), &nothing});
    host.addBuiltin({manifest("test.view", R"("tier": "ui", "uses": ["test.tool", "test.base"])"), &nothing});
    host.addBuiltin({manifest("test.wrong", R"("tier": "foundation", "uses": ["test.view"])"), &nothing});
    host.addBuiltin({manifest("test.free", R"("uses": ["test.view"])"), &nothing});  // no tier: not checked
    host.loadAll();
    for (const char* id : {"test.base", "test.base2", "test.tool", "test.view", "test.free"})
        CHECK(get(host, id).issues.empty());
    const auto& wrong = get(host, "test.wrong");
    CHECK(wrong.state == PluginState::Loaded);  // a warning never stops a plugin
    REQUIRE(codes(wrong) == std::vector<std::string>{"plugin.tier_order"});
    CHECK(wrong.issues[0].severity == Severity::Warning);
    CHECK(wrong.issues[0].message ==
          "test.wrong (tier foundation) uses test.view (tier ui), which is in a higher tier. A plugin should "
          "only use plugins in its own tier or lower ones: move what they share into a lower plugin, or have "
          "test.view send events that test.wrong listens to.");
}

TEST_CASE("a plugin can add a tier between others", "[plugin][tiers]") {
    PluginHost host;
    host.addBuiltin(
        {manifest("sam.mill", R"("tiers": [{"name": "sam.mill.machining", "after": "tools", "before": "ui"}],
                                          "tier": "sam.mill.machining", "uses": ["test.tool"])"),
         &nothing});
    // Another plugin's tier, placed next to sam.mill's (added in any order).
    host.addBuiltin(
        {manifest("ann.lathe", R"("tiers": [{"name": "ann.lathe.turning", "after": "sam.mill.machining"}],
                                           "tier": "ann.lathe.turning", "uses": ["sam.mill"])"),
         &nothing});
    // Side by side with sam.mill's tier: neither is above the other.
    host.addBuiltin(
        {manifest("bo.cut", R"("tiers": [{"name": "bo.cut.cutting", "after": "tools", "before": "ui"}],
                                        "tier": "bo.cut.cutting", "uses": ["sam.mill"])"),
         &nothing});
    host.addBuiltin({manifest("test.tool", R"("tier": "tools")"), &nothing});
    host.addBuiltin({manifest("test.view", R"("tier": "ui", "uses": ["sam.mill"])"), &nothing});
    host.addBuiltin({manifest("test.low", R"("tier": "tools", "uses": ["sam.mill"])"), &nothing});
    host.loadAll();
    for (const char* id : {"sam.mill", "ann.lathe", "bo.cut", "test.tool", "test.view"})
        CHECK(get(host, id).issues.empty());
    CHECK(codes(get(host, "test.low")) == std::vector<std::string>{"plugin.tier_order"});
}

TEST_CASE("tiers that can't be added are warned about and ignored", "[plugin][tiers]") {
    PluginHost host;
    host.addBuiltin({manifest("test.a", R"("tiers": [{"name": "layer", "after": "tools"}])"), &nothing});
    host.addBuiltin(
        {manifest("test.b", R"("tiers": [{"name": "test.b.x", "after": "ui", "before": "tools"}])"),
         &nothing});
    host.addBuiltin({manifest("test.c", R"("tiers": [{"name": "test.c.x"}])"), &nothing});
    host.addBuiltin({manifest("test.d", R"("tiers": [{"name": "test.d.x", "after": "nowhere"}])"), &nothing});
    host.addBuiltin({manifest("test.e", R"("tiers": [{"name": "test.e.x", "after": "tools"},
                                                     {"name": "test.e.x", "before": "ui"}])"),
                     &nothing});
    host.addBuiltin({manifest("test.f", R"("tier": "test.b.x")"), &nothing});  // was ignored
    host.loadAll();
    auto message = [&](const char* id) {
        const auto& p = get(host, id);
        REQUIRE(p.issues.size() == 1);
        return p.issues[0].message;
    };
    CHECK(codes(get(host, "test.a")) == std::vector<std::string>{"plugin.tier_conflict"});
    CHECK(message("test.a").find("must start with the plugin's id, like \"test.a.layer\"") !=
          std::string::npos);
    CHECK(message("test.b").find("after ui and before tools, but ui isn't below tools") != std::string::npos);
    CHECK(message("test.c").find("without saying where it goes") != std::string::npos);
    CHECK(message("test.d").find("after the tier \"nowhere\", which no plugin adds") != std::string::npos);
    CHECK(message("test.e").find("which already exists") != std::string::npos);
    CHECK(codes(get(host, "test.f")) == std::vector<std::string>{"plugin.tier_unknown"});
    CHECK(message("test.f").find("Use one of these: foundation, tools, ui, test.e.x.") != std::string::npos);
    for (const char* id : {"test.a", "test.b", "test.c", "test.d", "test.e", "test.f"})
        CHECK(get(host, id).state == PluginState::Loaded);
}
