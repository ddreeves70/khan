// Tests for the Move step: MoveFeature and feature.add_move.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;

namespace {

FeatureId add(Document& doc, std::unique_ptr<Feature> f) {
    const auto id = f->id();
    REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(f))).empty());
    return id;
}

kernel::Motion slide(double x, double y, double z) {
    kernel::Motion m;
    m.offset = {x, y, z};
    return m;
}

}  // namespace

TEST_CASE("a Move step uses up the body it moves, and keeps its volume", "[model][move]") {
    Document doc;
    const auto box = add(doc, std::make_unique<BoxFeature>("Box1", kernel::Vec3{}, kernel::Vec3{10, 10, 10}));
    const auto move = add(doc, std::make_unique<MoveFeature>("Move1", box, slide(20, 0, 0)));
    CHECK(doc.find(move)->dependencies() == std::vector<FeatureId>{box});

    test::FakeKernel k;
    const auto report = doc.regenerate(k);
    REQUIRE(report.ok());
    CHECK(test::FakeKernel::volumeOf(*report.shapeOf(move)) == 1000.0);
    CHECK(report.bodies() == std::vector<FeatureId>{move});  // Box1 isn't a finished body anymore
}

TEST_CASE("a Move of a step that's off is blocked", "[model][move]") {
    Document doc;
    const auto box = add(doc, std::make_unique<BoxFeature>("Box1", kernel::Vec3{}, kernel::Vec3{10, 10, 10}));
    const auto move = add(doc, std::make_unique<MoveFeature>("Move1", box, slide(1, 0, 0)));
    REQUIRE(
        doc.execute(std::make_unique<SuppressFeaturesCommand>(std::vector<FeatureId>{box}, true)).empty());
    test::FakeKernel k;
    CHECK(doc.regenerate(k).result(move)->status == FeatureStatus::Blocked);
}

TEST_CASE("a moved body is made of what the body it moves is made of", "[model][move][material]") {
    Document doc;
    const auto box = add(doc, std::make_unique<BoxFeature>("Box1", kernel::Vec3{}, kernel::Vec3{10, 10, 10}));
    const auto move = add(doc, std::make_unique<MoveFeature>("Move1", box, slide(1, 0, 0)));
    REQUIRE(doc.execute(std::make_unique<SetMaterialCommand>(box, "Brass (C360)")).empty());
    CHECK(doc.materialOf(move).name == "Brass (C360)");
    CHECK(doc.materialSource(move) == box);
}

TEST_CASE("copying a Move step copies the body it moves too", "[model][move][copy]") {
    Document doc;
    const auto box = add(doc, std::make_unique<BoxFeature>("Box1", kernel::Vec3{}, kernel::Vec3{10, 10, 10}));
    const auto move = add(doc, std::make_unique<MoveFeature>("Move1", box, slide(1, 2, 3)));
    const auto steps = copySteps(doc, {move});
    REQUIRE(steps.size() == 2);
    const auto& copy = dynamic_cast<const MoveFeature&>(*steps[1]);
    CHECK(copy.body() == steps[0]->id());
    CHECK(copy.motion().offset.y == 2.0);
}

TEST_CASE("feature.add_move takes degrees and stores radians", "[model][move][registry]") {
    Document doc;
    const auto box = add(doc, std::make_unique<BoxFeature>("Box1", kernel::Vec3{}, kernel::Vec3{10, 10, 10}));
    auto made = CommandRegistry::builtin().create(
        "feature.add_move", {{"body", box}, {"turn", 90.0}, {"by", kernel::Vec3{5, 0, 0}}}, doc);
    REQUIRE(made.ok());
    REQUIRE(doc.execute(std::move(made.command)).empty());
    REQUIRE(doc.featureCount() == 2);
    const auto& m = dynamic_cast<const MoveFeature&>(doc.featureAt(1));
    CHECK(m.name() == "Move1");
    CHECK(m.body() == box);
    CHECK(m.motion().angle == Approx(std::numbers::pi / 2));
    CHECK(m.motion().axis.z == 1.0);  // up, unless told otherwise
    CHECK(m.motion().offset.x == 5.0);
    CHECK(m.motion().center.x == 0.0);

    made = CommandRegistry::builtin().create("feature.add_move", {{"by", kernel::Vec3{5, 0, 0}}}, doc);
    REQUIRE_FALSE(made.ok());
    CHECK(made.issues[0].code == "command.arg_missing");
}
