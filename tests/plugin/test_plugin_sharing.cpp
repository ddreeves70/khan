// Tests for plugins sharing things with each other: services offered by plugins,
// two-phase startup, "uses", loops (allowed), and the warnings Khan gives while loading
// (see docs/design/plugin-contracts.md).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "kh/plugin/PluginHost.h"

using namespace kh;
using namespace kh::plugin;

namespace {

// A contract, as a provider plugin would ship it.
struct IRuler {
    static constexpr const char* kServiceName = "test.ruler/1";
    virtual ~IRuler() = default;
    virtual double inches(double mm) const = 0;
};
struct Ruler final : IRuler {
    double inches(double mm) const override { return mm / 25.4; }
};
struct RulerReady {
    static constexpr const char* kType = "test.ruler.ready";
    double scale = 1;
};
Ruler ruler;

std::string manifest(const std::string& id, const std::string& extra = "") {
    return R"({"id": ")" + id + R"(", "name": ")" + id + R"(", "version": "1.0.0")" +
           (extra.empty() ? "" : ", " + extra) + "}";
}

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

// What the test plugins saw.
struct Seen {
    std::vector<std::string> events;
    const IRuler* atRegister = nullptr;
    const IRuler* atStart = nullptr;
};
Seen* seen = nullptr;
struct SeenGuard {
    Seen s;
    SeenGuard() { seen = &s; }
    ~SeenGuard() { seen = nullptr; }
};

void provideRuler(PluginContext& ctx) {
    seen->events.push_back("ruler registers");
    ctx.provide<IRuler>(ruler);
    ctx.onStart([&ctx] {
        seen->events.push_back("ruler starts");
        ctx.publish(RulerReady{2});
    });
}

void provideOldRuler(PluginContext& ctx) {
    ctx.provide<IRuler>(ruler, true);  // going away
}

void useRulerAtStart(PluginContext& ctx) {
    seen->events.push_back("user registers");
    ctx.listen<RulerReady>([](const RulerReady&) { seen->events.push_back("user heard ready"); });
    ctx.onStart([&ctx] {
        seen->events.push_back("user starts");
        seen->atStart = ctx.service<IRuler>();
    });
}

void useRulerAtRegister(PluginContext& ctx) {
    seen->atRegister = ctx.service<IRuler>();
}

void failAtStart(PluginContext& ctx) {
    ctx.provide<IRuler>(ruler);
    ctx.onStart([&ctx] { ctx.fail("It needs a measuring tape, and none is connected."); });
}

void sendBadlyNamedEvent(PluginContext& ctx) {
    ctx.onStart([&ctx] { ctx.publish(RulerReady{}); });  // test.ruler.ready, but this is test.liar
}

void slow(PluginContext& ctx) {
    ctx.onStart([] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); });
}

void nothing(PluginContext&) {}

}  // namespace

TEST_CASE("a plugin's service reaches other plugins when they start", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.user", R"("uses": ["test.ruler"])"), &useRulerAtStart});
    host.addBuiltin({manifest("test.ruler"), &provideRuler});
    host.loadAll();

    // Everyone registers first (providers before users), then everyone starts.
    CHECK(seen->events == std::vector<std::string>{"ruler registers", "user registers", "ruler starts",
                                                   "user heard ready", "user starts"});
    REQUIRE(seen->atStart);
    CHECK(seen->atStart->inches(25.4) == 1.0);
    CHECK(get(host, "test.ruler").services == std::vector<std::string>{"test.ruler/1"});
    // Declared properly: no warnings for either.
    CHECK(get(host, "test.user").issues.empty());
    CHECK(get(host, "test.ruler").issues.empty());
}

TEST_CASE("a missing \"uses\" plugin is fine, with a note", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.user", R"("uses": ["test.ruler"])"), &useRulerAtStart});
    host.loadAll();
    const auto& user = get(host, "test.user");
    CHECK(user.state == PluginState::Loaded);
    CHECK(seen->atStart == nullptr);  // it does without
    REQUIRE(user.issues.size() == 1);
    CHECK(user.issues[0].code == "plugin.uses_missing");
    CHECK(user.issues[0].severity == Severity::Info);
}

TEST_CASE("using a plugin without listing it is warned about", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.user"), &useRulerAtStart});  // no "uses"
    host.addBuiltin({manifest("test.ruler"), &provideRuler});
    host.loadAll();
    const auto& user = get(host, "test.user");
    CHECK(user.state == PluginState::Loaded);
    CHECK(seen->atStart != nullptr);  // it still works
    // Once for the event it listens to, once for the service.
    CHECK(codes(user) ==
          std::vector<std::string>{"plugin.undeclared_dependency", "plugin.undeclared_dependency"});
    CHECK(user.issues[0].severity == Severity::Warning);
    CHECK(user.issues[1].message.find("Add it to \"uses\"") != std::string::npos);
}

TEST_CASE("looking up another plugin's service while registering is warned about", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.ruler"), &provideRuler});
    host.addBuiltin({manifest("test.user", R"("uses": ["test.ruler"])"), &useRulerAtRegister});
    host.loadAll();
    CHECK(seen->atRegister != nullptr);  // works, because the ruler happened to load first
    CHECK(codes(get(host, "test.user")) == std::vector<std::string>{"plugin.early_lookup"});
}

TEST_CASE("looking up a required plugin's service while registering is fine", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.ruler"), &provideRuler});
    host.addBuiltin({manifest("test.user", R"("requires": ["test.ruler"])"), &useRulerAtRegister});
    host.loadAll();
    CHECK(seen->atRegister != nullptr);  // "requires" makes Khan load the ruler first, always
    CHECK(get(host, "test.user").issues.empty());
}

TEST_CASE("the app's own services never give warnings", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.provide<IRuler>(ruler);  // provided by the program, like kh.ui
    host.addBuiltin({manifest("test.user"), &useRulerAtRegister});
    host.loadAll();
    CHECK(seen->atRegister != nullptr);
    CHECK(get(host, "test.user").issues.empty());
}

TEST_CASE("two plugins can't provide the same service", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.ruler"), &provideRuler});
    host.addBuiltin({manifest("test.ruler_pro"), &provideOldRuler});
    host.loadAll();
    const auto& pro = get(host, "test.ruler_pro");
    CHECK(pro.state == PluginState::Loaded);
    CHECK(pro.services.empty());
    CHECK(codes(pro) == std::vector<std::string>{"plugin.service_taken"});
    CHECK(pro.issues[0].message.find("Khan kept test.ruler's") != std::string::npos);
}

TEST_CASE("using a service that's going away is warned about", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.ruler"), &provideOldRuler});
    host.addBuiltin({manifest("test.user", R"("uses": ["test.ruler"])"), &useRulerAtStart});
    host.loadAll();
    CHECK(codes(get(host, "test.user")) == std::vector<std::string>{"plugin.old_contract"});
}

TEST_CASE("a plugin that fails while starting takes back its service", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.ruler"), &failAtStart});
    host.addBuiltin({manifest("test.needs", R"("requires": ["test.ruler"])"), &nothing});
    host.addBuiltin({manifest("test.user", R"("uses": ["test.ruler"])"), &useRulerAtStart});
    host.loadAll();
    const auto& r = get(host, "test.ruler");
    CHECK(r.state == PluginState::Failed);
    CHECK(codes(r) == std::vector<std::string>{"plugin.start_failed"});
    CHECK(r.issues[0].message.find("measuring tape") != std::string::npos);
    CHECK(r.services.empty());
    CHECK(get(host, "test.needs").state == PluginState::Skipped);  // it required the ruler
    CHECK(get(host, "test.user").state == PluginState::Loaded);    // it only used it
}

TEST_CASE("a loop where one plugin fails stops the plugins that require it", "[plugin][sharing]") {
    SeenGuard guard;
    PluginHost host;
    host.addBuiltin({manifest("test.a", R"("requires": ["test.b"])"), &nothing});
    host.addBuiltin({manifest("test.b", R"("requires": ["test.a"])"), &failAtStart});
    host.loadAll();
    CHECK(get(host, "test.b").state == PluginState::Failed);
    CHECK(get(host, "test.a").state == PluginState::Skipped);
    CHECK(codes(get(host, "test.a")) == std::vector<std::string>{"plugin.dependency_failed"});
}

TEST_CASE("longer loops, and a plugin that lists itself, load with no warning", "[plugin][sharing]") {
    PluginHost host;
    host.addBuiltin({manifest("test.a", R"("uses": ["test.b"])"), &nothing});
    host.addBuiltin({manifest("test.b", R"("requires": ["test.c"])"), &nothing});
    host.addBuiltin({manifest("test.c", R"("uses": ["test.a"])"), &nothing});
    host.addBuiltin({manifest("test.d", R"("requires": ["test.a"])"), &nothing});  // outside the loop
    host.addBuiltin({manifest("test.self", R"("uses": ["test.self"])"), &nothing});
    host.loadAll();
    for (const char* id : {"test.a", "test.b", "test.c", "test.d", "test.self"}) {
        CHECK(get(host, id).state == PluginState::Loaded);
        CHECK(get(host, id).issues.empty());
    }
}

TEST_CASE("an event named after another plugin is warned about", "[plugin][sharing]") {
    PluginHost host;
    host.addBuiltin({manifest("test.liar"), &sendBadlyNamedEvent});
    host.loadAll();
    const auto& liar = get(host, "test.liar");
    CHECK(codes(liar) == std::vector<std::string>{"plugin.event_name"});
    CHECK(liar.issues[0].message.find("test.liar.something") != std::string::npos);
}

TEST_CASE("a slow plugin is warned about, with a limit the program sets", "[plugin][sharing]") {
    {
        PluginHost host;
        host.setSlowStartLimit(std::chrono::milliseconds(10));
        host.addBuiltin({manifest("test.slow"), &slow});
        host.loadAll();
        CHECK(codes(get(host, "test.slow")) == std::vector<std::string>{"plugin.slow_start"});
        CHECK(get(host, "test.slow").issues[0].detail == "limit 10 ms");
    }
    {
        PluginHost host;
        host.setSlowStartLimit(std::chrono::milliseconds(0));  // off
        host.addBuiltin({manifest("test.slow"), &slow});
        host.loadAll();
        CHECK(get(host, "test.slow").issues.empty());
    }
    {
        PluginHost host;  // the default, 500 ms
        host.addBuiltin({manifest("test.slow"), &slow});
        host.loadAll();
        CHECK(get(host, "test.slow").issues.empty());
    }
}
