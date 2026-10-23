// Tests for the event bus (kh_plugin).

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "kh/plugin/EventBus.h"
#include "kh/plugin/Events.h"

using namespace kh;
using namespace kh::plugin;

namespace {

DocumentChanged changed(std::string label) {
    return {DocId::generate(), ChangeKind::Executed, std::move(label)};
}

}  // namespace

TEST_CASE("listeners get events of their type only, in subscription order", "[plugin][bus]") {
    EventBus bus;
    std::vector<std::string> seen;
    auto a =
        bus.subscribe<DocumentChanged>([&](const DocumentChanged& e) { seen.push_back("a:" + e.label); });
    auto b =
        bus.subscribe<DocumentChanged>([&](const DocumentChanged& e) { seen.push_back("b:" + e.label); });
    auto c = bus.subscribe<RebuildFinished>([&](const RebuildFinished&) { seen.push_back("rebuild"); });

    bus.publish(changed("Add Box1"));
    CHECK(seen == std::vector<std::string>{"a:Add Box1", "b:Add Box1"});
    bus.publish(RebuildFinished{DocId::generate(), true});
    CHECK(seen.back() == "rebuild");
    CHECK(bus.listenerCount(DocumentChanged::kType) == 2);
}

TEST_CASE("a subscription disconnects when destroyed or reset", "[plugin][bus]") {
    EventBus bus;
    int calls = 0;
    {
        auto s = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) { ++calls; });
        bus.publish(changed("x"));
        CHECK(s.connected());
    }
    bus.publish(changed("y"));
    CHECK(calls == 1);

    auto s = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) { ++calls; });
    s.reset();
    CHECK_FALSE(s.connected());
    bus.publish(changed("z"));
    CHECK(calls == 1);
    CHECK(bus.listenerCount(DocumentChanged::kType) == 0);
}

TEST_CASE("moving a subscription keeps one connection", "[plugin][bus]") {
    EventBus bus;
    int calls = 0;
    Subscription kept;
    {
        auto s = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) { ++calls; });
        kept = std::move(s);
        CHECK_FALSE(s.connected());
    }
    bus.publish(changed("x"));
    CHECK(calls == 1);
    CHECK(bus.listenerCount(DocumentChanged::kType) == 1);
}

TEST_CASE("an event published by a listener waits for the current one", "[plugin][bus]") {
    EventBus bus;
    std::vector<std::string> seen;
    auto first = bus.subscribe<DocumentChanged>([&](const DocumentChanged& e) {
        seen.push_back("1:" + e.label);
        if (e.label == "outer") bus.publish(changed("inner"));
    });
    auto second =
        bus.subscribe<DocumentChanged>([&](const DocumentChanged& e) { seen.push_back("2:" + e.label); });

    bus.publish(changed("outer"));
    // Every listener sees "outer" before anyone sees "inner".
    CHECK(seen == std::vector<std::string>{"1:outer", "2:outer", "1:inner", "2:inner"});
}

TEST_CASE("a listener that disconnects another stops it hearing the current event", "[plugin][bus]") {
    EventBus bus;
    int laterCalls = 0;
    Subscription later;
    auto first = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) { later.reset(); });
    later = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) { ++laterCalls; });
    bus.publish(changed("x"));
    CHECK(laterCalls == 0);
}

TEST_CASE("a listener can disconnect itself while it runs", "[plugin][bus]") {
    EventBus bus;
    int calls = 0;
    Subscription self;
    self = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) {
        ++calls;
        self.reset();  // destroys this listener's own function object
    });
    bus.publish(changed("x"));
    bus.publish(changed("y"));
    CHECK(calls == 1);
}

TEST_CASE("a listener that throws is reported, and others still get the event", "[plugin][bus]") {
    EventBus bus;
    Diagnostics problems;
    bus.setErrorHandler([&](const Diagnostic& d) { problems.push_back(d); });
    int calls = 0;
    auto bad =
        bus.subscribe<DocumentChanged>([](const DocumentChanged&) { throw std::runtime_error("oops"); });
    auto good = bus.subscribe<DocumentChanged>([&](const DocumentChanged&) { ++calls; });

    bus.publish(changed("x"));
    CHECK(calls == 1);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "event.listener_failed");
    CHECK(problems[0].detail.find("kh.DocumentChanged") != std::string::npos);
    CHECK(problems[0].detail.find("oops") != std::string::npos);
}

TEST_CASE("a subscription can outlive its bus", "[plugin][bus]") {
    Subscription s;
    {
        EventBus bus;
        s = bus.subscribe<DocumentChanged>([](const DocumentChanged&) {});
    }
    CHECK_FALSE(s.connected());
    s.reset();  // must not crash
}
