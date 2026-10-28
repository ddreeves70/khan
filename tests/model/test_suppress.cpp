// Tests for turning steps off and on: SuppressFeaturesCommand and feature.suppress.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"

using namespace kh;
using namespace kh::model;

namespace {

FeatureId add(Document& doc, std::unique_ptr<Feature> f) {
    const auto id = f->id();
    REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(f))).empty());
    return id;
}

// Plate, Hole, Tag = Plate cut Hole, and Spare.
struct Tag {
    Document doc;
    FeatureId plate =
        add(doc, std::make_unique<BoxFeature>("Plate", kernel::Vec3{0, 0, 0}, kernel::Vec3{60, 30, 4}));
    FeatureId hole = add(doc, std::make_unique<CylinderFeature>("Hole", kernel::Vec3{9, 15, -1},
                                                                kernel::Vec3{0, 0, 1}, 2.5, 6));
    FeatureId tag = add(doc, std::make_unique<BooleanFeature>("Tag", kernel::BoolOp::Cut, plate, hole));
    FeatureId spare =
        add(doc, std::make_unique<BoxFeature>("Spare", kernel::Vec3{100, 0, 0}, kernel::Vec3{1, 2, 3}));

    Diagnostics turn(std::vector<FeatureId> ids, bool off) {
        return doc.execute(std::make_unique<SuppressFeaturesCommand>(std::move(ids), off));
    }
};

}  // namespace

TEST_CASE("turning several steps off is one undo step, and nothing is deleted", "[model][suppress]") {
    Tag t;
    REQUIRE(t.turn({t.hole, t.spare}, true).empty());
    CHECK(t.doc.undoLabel() == "Turn off 2 steps");
    CHECK(t.doc.featureCount() == 4);
    CHECK(t.doc.find(t.hole)->suppressed());
    CHECK(t.doc.find(t.spare)->suppressed());
    CHECK_FALSE(t.doc.find(t.plate)->suppressed());

    // Rebuilt: the steps that are off have no shape, and Tag, which uses Hole, is blocked.
    test::FakeKernel k;
    const auto report = t.doc.regenerate(k);
    CHECK(report.result(t.hole)->status == FeatureStatus::Suppressed);
    CHECK(report.result(t.spare)->status == FeatureStatus::Suppressed);
    CHECK(report.result(t.tag)->status == FeatureStatus::Blocked);
    CHECK(report.result(t.plate)->status == FeatureStatus::Ok);
    CHECK_FALSE(report.shapeOf(t.hole));

    REQUIRE(t.doc.undo());
    CHECK_FALSE(t.doc.find(t.hole)->suppressed());
    CHECK_FALSE(t.doc.find(t.spare)->suppressed());
    REQUIRE(t.doc.redo());
    CHECK(t.doc.find(t.hole)->suppressed());
}

TEST_CASE("turning on brings back only the steps that were off", "[model][suppress]") {
    Tag t;
    REQUIRE(t.turn({t.hole}, true).empty());
    CHECK(t.doc.undoLabel() == "Turn off Hole");
    REQUIRE(t.turn({t.plate, t.hole}, false).empty());  // Plate was already on
    CHECK(t.doc.undoLabel() == "Turn on Hole");
    CHECK_FALSE(t.doc.find(t.hole)->suppressed());

    REQUIRE(t.doc.undo());  // Hole goes back off; Plate stays on
    CHECK(t.doc.find(t.hole)->suppressed());
    CHECK_FALSE(t.doc.find(t.plate)->suppressed());
    test::FakeKernel k;
    CHECK(t.doc.regenerate(k).result(t.tag)->status == FeatureStatus::Blocked);
}

TEST_CASE("turning off steps that are already off, or none, explains why", "[model][suppress]") {
    Tag t;
    REQUIRE(t.turn({t.spare}, true).empty());
    auto problems = t.turn({t.spare}, true);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "feature.already_off");
    CHECK(problems[0].message == "Spare is already turned off.");
    problems = t.turn({t.plate, t.hole}, false);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "feature.already_on");
    CHECK(problems[0].message == "Those steps are already turned on.");
    CHECK(t.turn({}, true).front().code == "feature.nothing");
    CHECK(t.turn({FeatureId::generate()}, true).front().code == "feature.not_found");
    CHECK(t.doc.undoLabel() == "Turn off Spare");  // none of these changed anything
}

TEST_CASE("feature.suppress takes a list of steps, and is off unless told otherwise",
          "[model][suppress][registry]") {
    Tag t;
    const auto& reg = CommandRegistry::builtin();
    auto made = reg.create("feature.suppress", {{"steps", std::vector<FeatureId>{t.hole, t.spare}}}, t.doc);
    REQUIRE(made.ok());
    REQUIRE(t.doc.execute(std::move(made.command)).empty());
    CHECK(t.doc.find(t.hole)->suppressed());

    made = reg.create("feature.suppress", {{"steps", std::vector<FeatureId>{t.hole}}, {"off", false}}, t.doc);
    REQUIRE(made.ok());
    REQUIRE(t.doc.execute(std::move(made.command)).empty());
    CHECK_FALSE(t.doc.find(t.hole)->suppressed());

    // The registry checks the list before making a command.
    made = reg.create("feature.suppress", {{"steps", std::vector<FeatureId>{}}}, t.doc);
    REQUIRE_FALSE(made.ok());
    CHECK(made.issues[0].code == "command.arg_feature");
    made = reg.create("feature.suppress", {{"steps", std::vector<FeatureId>{t.hole, FeatureId::generate()}}},
                      t.doc);
    REQUIRE_FALSE(made.ok());
    CHECK(made.issues[0].code == "command.arg_feature");
    made = reg.create("feature.suppress", {{"steps", t.hole}}, t.doc);  // one id isn't a list
    REQUIRE_FALSE(made.ok());
    CHECK(made.issues[0].code == "command.arg_type");
}

TEST_CASE("argFeatures and argBool read their arguments", "[model][registry]") {
    const FeatureId a = FeatureId::generate();
    const Args args{{"list", std::vector<FeatureId>{a}}, {"one", a}, {"flag", false}};
    CHECK(argFeatures(args, "list") == std::vector<FeatureId>{a});
    CHECK(argFeatures(args, "one") == std::vector<FeatureId>{a});
    CHECK(argFeatures(args, "missing").empty());
    CHECK_FALSE(argBool(args, "flag", true));
    CHECK(argBool(args, "missing", true));
}
