// Tests for the list of open designs (kh_plugin).

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "../model/FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/Events.h"

using namespace kh;
using namespace kh::plugin;

namespace {

// Records every event, as "Type:detail".
struct Recorder {
    explicit Recorder(EventBus& bus) {
        subs.push_back(bus.subscribe<DocumentOpened>([this](const auto&) { log.push_back("Opened"); }));
        subs.push_back(bus.subscribe<DocumentClosed>([this](const auto&) { log.push_back("Closed"); }));
        subs.push_back(bus.subscribe<ActiveDocumentChanged>([this](const ActiveDocumentChanged& e) {
            log.push_back(e.doc.isNil() ? "Active:none" : "Active");
        }));
        subs.push_back(bus.subscribe<DocumentChanged>([this](const DocumentChanged& e) {
            const char* kind = e.kind == ChangeKind::Executed ? "Executed"
                               : e.kind == ChangeKind::Undone ? "Undone"
                                                              : "Redone";
            log.push_back(std::string("Changed:") + kind + ":" + e.label);
        }));
        subs.push_back(bus.subscribe<RebuildFinished>(
            [this](const RebuildFinished& e) { log.push_back(e.ok ? "Rebuilt:ok" : "Rebuilt:problem"); }));
    }
    std::vector<std::string> log;
    std::vector<Subscription> subs;
};

}  // namespace

TEST_CASE("changes go through the list and every listener hears about them", "[plugin][documents]") {
    EventBus bus;
    test::FakeKernel kernel;
    DocumentList docs(bus, model::CommandRegistry::builtin(), &kernel);
    Recorder rec(bus);

    const DocId id = docs.add(std::make_unique<model::Document>());
    CHECK(rec.log == std::vector<std::string>{"Opened", "Rebuilt:ok", "Active"});
    CHECK(docs.active() == id);
    rec.log.clear();

    REQUIRE(docs.run(id, "feature.add_box", {{"size", kernel::Vec3{10, 20, 30}}}).empty());
    CHECK(rec.log == std::vector<std::string>{"Changed:Executed:Add Box1", "Rebuilt:ok"});

    // The rebuild result is there for listeners to read.
    const auto* report = docs.lastRebuild(id);
    REQUIRE(report);
    const auto box = docs.find(id)->featureAt(0).id();
    CHECK(kernel.massProps(*report->shapeOf(box)).volume == 6000.0);  // 10 * 20 * 30 mm^3

    rec.log.clear();
    CHECK(docs.undo(id));
    CHECK(docs.redo(id));
    CHECK(rec.log == std::vector<std::string>{"Changed:Undone:Add Box1", "Rebuilt:ok",
                                              "Changed:Redone:Add Box1", "Rebuilt:ok"});
}

TEST_CASE("a failed command changes nothing and sends nothing", "[plugin][documents]") {
    EventBus bus;
    DocumentList docs(bus, model::CommandRegistry::builtin());
    const DocId id = docs.add(std::make_unique<model::Document>());
    Recorder rec(bus);

    const auto issues = docs.run(id, "feature.add_box", {});  // size is required
    CHECK_FALSE(issues.empty());
    CHECK(docs.find(id)->featureCount() == 0);
    CHECK(rec.log.empty());
    CHECK_FALSE(docs.undo(id));  // nothing to undo
}

TEST_CASE("without a kernel, designs are not rebuilt", "[plugin][documents]") {
    EventBus bus;
    DocumentList docs(bus, model::CommandRegistry::builtin());
    Recorder rec(bus);
    const DocId id = docs.add(std::make_unique<model::Document>());
    CHECK(docs.lastRebuild(id) == nullptr);
    CHECK(rec.log == std::vector<std::string>{"Opened", "Active"});
}

TEST_CASE("closing designs moves the active one along", "[plugin][documents]") {
    EventBus bus;
    DocumentList docs(bus, model::CommandRegistry::builtin());
    const DocId a = docs.add(std::make_unique<model::Document>());
    const DocId b = docs.add(std::make_unique<model::Document>());
    CHECK(docs.active() == a);  // the first one stays active
    CHECK(docs.ids() == std::vector<DocId>{a, b});

    Recorder rec(bus);
    CHECK(docs.close(a));
    CHECK(docs.active() == b);
    CHECK(docs.close(b));
    CHECK(docs.active().isNil());
    CHECK(rec.log == std::vector<std::string>{"Closed", "Active", "Closed", "Active:none"});
    CHECK_FALSE(docs.close(b));
}

TEST_CASE("working on a design that isn't open explains the problem", "[plugin][documents]") {
    EventBus bus;
    DocumentList docs(bus, model::CommandRegistry::builtin());
    const auto issues = docs.run(DocId::generate(), "feature.add_box", {{"size", kernel::Vec3{1, 1, 1}}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "document.not_open");
    CHECK_FALSE(docs.setActive(DocId::generate()));
}

TEST_CASE("a listener may close the design that just changed", "[plugin][documents]") {
    EventBus bus;
    test::FakeKernel kernel;
    DocumentList docs(bus, model::CommandRegistry::builtin(), &kernel);
    const DocId id = docs.add(std::make_unique<model::Document>());
    auto closer = bus.subscribe<DocumentChanged>([&](const DocumentChanged& e) { docs.close(e.doc); });
    CHECK(docs.run(id, "feature.add_box", {{"size", kernel::Vec3{1, 1, 1}}}).empty());
    CHECK(docs.find(id) == nullptr);
}
