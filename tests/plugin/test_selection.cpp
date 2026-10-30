// Tests for the shared selection (kh_plugin).

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "kh/model/CommandRegistry.h"
#include "kh/plugin/DocumentList.h"
#include "kh/plugin/Events.h"
#include "kh/plugin/Selection.h"

using namespace kh;
using namespace kh::plugin;

namespace {

struct Fixture {
    EventBus bus;
    DocumentList docs{bus, model::CommandRegistry::builtin()};
    Selection selection{bus, docs};
    std::vector<SelectionChanged> heard;
    Subscription sub =
        bus.subscribe<SelectionChanged>([this](const SelectionChanged& e) { heard.push_back(e); });
    DocId doc = docs.add(std::make_unique<model::Document>());

    FeatureId addBox() {
        REQUIRE(docs.run(doc, "feature.add_box", {{"size", kernel::Vec3{1, 1, 1}}}).empty());
        const auto* d = docs.find(doc);
        return d->featureAt(d->featureCount() - 1).id();
    }
};

}  // namespace

TEST_CASE("selecting steps tells every listener once", "[plugin][selection]") {
    Fixture f;
    const auto a = f.addBox();
    const auto b = f.addBox();

    f.selection.set(f.doc, {b, a});
    REQUIRE(f.heard.size() == 1);
    CHECK(f.heard[0].doc == f.doc);
    CHECK(f.heard[0].features == std::vector<FeatureId>{b, a});  // picking order kept
    CHECK(f.selection.contains(a));

    f.selection.set(f.doc, {b, a});  // the same again: no event
    CHECK(f.heard.size() == 1);

    f.selection.clear();
    REQUIRE(f.heard.size() == 2);
    CHECK(f.heard[1].doc.isNil());
    CHECK(f.selection.features().empty());
}

TEST_CASE("only real steps of the design can be selected", "[plugin][selection]") {
    Fixture f;
    const auto a = f.addBox();
    f.selection.set(f.doc, {a, FeatureId::generate(), a});
    CHECK(f.selection.features() == std::vector<FeatureId>{a});  // unknown and repeated ids dropped
    f.selection.set(DocId::generate(), {a});                     // not an open design
    CHECK(f.selection.features().empty());
    CHECK(f.selection.doc().isNil());
}

TEST_CASE("deleting or undoing a selected step drops it from the selection", "[plugin][selection]") {
    Fixture f;
    const auto a = f.addBox();
    const auto b = f.addBox();
    f.selection.set(f.doc, {a, b});

    REQUIRE(f.docs.undo(f.doc));  // b is undone away
    CHECK(f.selection.features() == std::vector<FeatureId>{a});
    REQUIRE(f.docs.run(f.doc, "feature.delete", {{"feature", a}}).empty());
    CHECK(f.selection.features().empty());
    CHECK(f.heard.back().features.empty());
}

TEST_CASE("closing the design clears the selection", "[plugin][selection]") {
    Fixture f;
    const auto a = f.addBox();
    f.selection.set(f.doc, {a});
    f.docs.close(f.doc);
    CHECK(f.selection.features().empty());
    CHECK(f.selection.doc().isNil());
}

TEST_CASE("the design itself can be picked, for its settings; picking steps un-picks it",
          "[plugin][selection]") {
    Fixture f;
    const auto a = f.addBox();
    f.selection.set(f.doc, {a});
    f.selection.pickDesign(f.doc);
    CHECK(f.selection.designPicked());
    CHECK(f.selection.doc() == f.doc);
    CHECK(f.selection.features().empty());
    CHECK(f.heard.back().design);
    const auto heard = f.heard.size();
    f.selection.pickDesign(f.doc);  // no change, no event
    CHECK(f.heard.size() == heard);

    // A change to the design keeps it picked.
    REQUIRE(f.docs.run(f.doc, "feature.delete", {{"feature", a}}).empty());
    CHECK(f.selection.designPicked());

    f.selection.set(f.doc, {f.addBox()});
    CHECK_FALSE(f.selection.designPicked());
    CHECK_FALSE(f.heard.back().design);

    f.selection.pickDesign(DocId::generate());  // not an open design
    CHECK_FALSE(f.selection.designPicked());
    CHECK(f.selection.doc().isNil());

    f.selection.pickDesign(f.doc);
    f.docs.close(f.doc);
    CHECK_FALSE(f.selection.designPicked());
}
