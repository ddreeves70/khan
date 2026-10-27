// Tests for copying steps: copySteps(), freshIds() and AddFeaturesCommand.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>

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

// Plate, Hole, Tag = Plate cut Hole, and an unrelated Spare.
struct Tag {
    Document doc;
    FeatureId plate =
        add(doc, std::make_unique<BoxFeature>("Plate", kernel::Vec3{0, 0, 0}, kernel::Vec3{60, 30, 4}));
    FeatureId hole = add(doc, std::make_unique<CylinderFeature>("Hole", kernel::Vec3{9, 15, -1},
                                                                kernel::Vec3{0, 0, 1}, 2.5, 6));
    FeatureId tag = add(doc, std::make_unique<BooleanFeature>("Tag", kernel::BoolOp::Cut, plate, hole));
    FeatureId spare =
        add(doc, std::make_unique<BoxFeature>("Spare", kernel::Vec3{100, 0, 0}, kernel::Vec3{1, 2, 3}));
};

}  // namespace

TEST_CASE("copySteps brings the steps a step uses, in design order, with new ids", "[model][copy]") {
    Tag t;
    const auto steps = copySteps(t.doc, {t.tag});
    REQUIRE(steps.size() == 3);  // Plate, Hole, Tag; not Spare
    CHECK(steps[0]->name() == "Plate");
    CHECK(steps[1]->name() == "Hole");
    CHECK(steps[2]->name() == "Tag");
    std::set<FeatureId> ids;
    for (const auto& f : steps) {
        CHECK_FALSE(t.doc.find(f->id()));  // every id is new
        ids.insert(f->id());
    }
    CHECK(ids.size() == 3);
    // The copied Tag uses the copied Plate and Hole, not the originals.
    const auto deps = steps[2]->dependencies();
    REQUIRE(deps.size() == 2);
    CHECK(deps[0] == steps[0]->id());
    CHECK(deps[1] == steps[1]->id());
    // The numbers are the same.
    const auto& plate = dynamic_cast<const BoxFeature&>(*steps[0]);
    CHECK(plate.size().x == 60);
    CHECK(plate.size().z == 4);
}

TEST_CASE("copySteps of nothing, or of steps not in the design, is empty", "[model][copy]") {
    Tag t;
    CHECK(copySteps(t.doc, {}).empty());
    CHECK(copySteps(t.doc, {FeatureId::generate()}).empty());
}

TEST_CASE("pasting copies at the end renames taken names, and undo takes them all away", "[model][copy]") {
    Tag t;
    REQUIRE(t.doc
                .execute(std::make_unique<AddFeaturesCommand>(copySteps(t.doc, {t.tag}),
                                                              std::vector<Material>{}, "Paste 3 steps"))
                .empty());
    REQUIRE(t.doc.featureCount() == 7);
    CHECK(t.doc.featureAt(4).name() == "Plate1");
    CHECK(t.doc.featureAt(5).name() == "Hole1");
    CHECK(t.doc.featureAt(6).name() == "Tag1");
    CHECK(t.doc.undoLabel() == "Paste 3 steps");
    const auto inputs = t.doc.featureAt(6).dependencies();
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[0] == t.doc.featureAt(4).id());  // the copied Plate and Hole
    CHECK(inputs[1] == t.doc.featureAt(5).id());

    REQUIRE(t.doc.undo());  // one undo for the whole paste
    CHECK(t.doc.featureCount() == 4);
    REQUIRE(t.doc.redo());
    REQUIRE(t.doc.featureCount() == 7);
    CHECK(t.doc.featureAt(6).name() == "Tag1");

    // Pasting the same steps twice can't happen: the second copy needs new ids.
    std::vector<std::unique_ptr<Feature>> again;
    again.push_back(t.doc.featureAt(6).clone());
    const auto problems = t.doc.execute(
        std::make_unique<AddFeaturesCommand>(std::move(again), std::vector<Material>{}, "Paste"));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "feature.duplicate");
    CHECK(t.doc.featureCount() == 7);
}

TEST_CASE("steps that use a step not being added are refused, and the design is unchanged", "[model][copy]") {
    Tag t;
    std::vector<std::unique_ptr<Feature>> steps;
    steps.push_back(std::make_unique<BoxFeature>("Extra", kernel::Vec3{0, 0, 0}, kernel::Vec3{1, 1, 1}));
    steps.push_back(std::make_unique<BooleanFeature>("Bad", kernel::BoolOp::Union, steps[0]->id(),
                                                     FeatureId::generate()));
    const auto problems = t.doc.execute(
        std::make_unique<AddFeaturesCommand>(std::move(steps), std::vector<Material>{}, "Paste"));
    CHECK_FALSE(problems.empty());
    CHECK(t.doc.featureCount() == 4);
    CHECK_FALSE(t.doc.canRedo());
    CHECK(t.doc.undoLabel() == "Add Spare");

    const auto none = t.doc.execute(std::make_unique<AddFeaturesCommand>(
        std::vector<std::unique_ptr<Feature>>{}, std::vector<Material>{}, "Paste"));
    REQUIRE(none.size() == 1);
    CHECK(none[0].code == "feature.nothing");
}

TEST_CASE("pasted steps bring their own materials into another design", "[model][copy][material]") {
    Tag t;
    Material blue{"", "Blue PETG", 1.27, {0x33, 0x66, 0xcc}, 2.1, 0.38, 50, 25, false};
    REQUIRE(t.doc.execute(std::make_unique<DefineMaterialCommand>(blue)).empty());
    REQUIRE(t.doc.execute(std::make_unique<SetMaterialCommand>(t.plate, "Blue PETG")).empty());
    const Material mine = t.doc.customMaterials().at(0);

    Document other;
    REQUIRE(other
                .execute(std::make_unique<AddFeaturesCommand>(copySteps(t.doc, {t.plate}),
                                                              std::vector<Material>{mine}, "Paste 1 step"))
                .empty());
    REQUIRE(other.customMaterials().size() == 1);
    CHECK(other.customMaterials()[0] == mine);
    CHECK(other.materialOf(other.featureAt(0).id()).name == "Blue PETG");
    CHECK(other.featureAt(0).name() == "Plate");  // not taken here, so not renamed

    // Pasting again doesn't add the material twice.
    REQUIRE(other
                .execute(std::make_unique<AddFeaturesCommand>(copySteps(t.doc, {t.plate}),
                                                              std::vector<Material>{mine}, "Paste 1 step"))
                .empty());
    CHECK(other.customMaterials().size() == 1);
    CHECK(other.featureAt(1).name() == "Plate1");

    // Undo takes the material away with the first paste.
    REQUIRE(other.undo());
    REQUIRE(other.undo());
    CHECK(other.featureCount() == 0);
    CHECK(other.customMaterials().empty());
}

TEST_CASE("a pasted material uses the design's own material of the same name", "[model][copy][material]") {
    Tag t;
    REQUIRE(t.doc
                .execute(std::make_unique<DefineMaterialCommand>(
                    Material{"", "Blue PETG", 1.27, {0x33, 0x66, 0xcc}, 2.1, 0.38, 50, 25, false}))
                .empty());
    REQUIRE(t.doc.execute(std::make_unique<SetMaterialCommand>(t.plate, "Blue PETG")).empty());

    Document other;
    REQUIRE(other
                .execute(std::make_unique<DefineMaterialCommand>(
                    Material{"", "Blue PETG", 1.25, {0x30, 0x60, 0xc0}, 2.0, 0.38, 50, 20, false}))
                .empty());
    const std::string theirs = other.customMaterials().at(0).key;
    REQUIRE(other
                .execute(std::make_unique<AddFeaturesCommand>(copySteps(t.doc, {t.plate}),
                                                              t.doc.customMaterials(), "Paste 1 step"))
                .empty());
    CHECK(other.customMaterials().size() == 1);
    CHECK(other.featureAt(0).material() == theirs);
}

TEST_CASE("feature.duplicate copies a step and the steps it uses", "[model][copy][registry]") {
    Tag t;
    const auto& registry = CommandRegistry::builtin();
    auto made = registry.create("feature.duplicate", {{"feature", t.tag}}, t.doc);
    REQUIRE(made.ok());
    REQUIRE(t.doc.execute(std::move(made.command)).empty());
    REQUIRE(t.doc.featureCount() == 7);
    CHECK(t.doc.featureAt(6).name() == "Tag1");
    CHECK(t.doc.undoLabel() == "Duplicate Tag");
}
