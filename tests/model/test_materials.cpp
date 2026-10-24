// Tests for materials: the built-in list, mass and cost, what a body is made
// of, and the commands that pick and add materials.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>

#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/model/Material.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;

namespace {

struct Tag {
    Document doc{DocKind::Part, "Tag"};
    FeatureId plate, hole, tag;

    Tag() {
        auto p = std::make_unique<BoxFeature>("Plate", kernel::Vec3{0, 0, 0}, kernel::Vec3{60, 30, 4});
        auto h =
            std::make_unique<CylinderFeature>("Hole", kernel::Vec3{9, 15, -1}, kernel::Vec3{0, 0, 1}, 2.5, 6);
        auto t = std::make_unique<BooleanFeature>("Tag", kernel::BoolOp::Cut, p->id(), h->id());
        plate = p->id();
        hole = h->id();
        tag = t->id();
        REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(p))).empty());
        REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(h))).empty());
        REQUIRE(doc.execute(std::make_unique<AddFeatureCommand>(std::move(t))).empty());
    }

    Diagnostics run(const std::string& name, const Args& args) {
        auto made = CommandRegistry::builtin().create(name, args, doc);
        if (!made.ok()) return made.issues;
        return doc.execute(std::move(made.command));
    }
};

}  // namespace

TEST_CASE("the built-in materials have sensible, unique entries", "[model][material]") {
    const auto& list = builtinMaterials();
    REQUIRE(list.size() >= 15);
    std::set<std::string> keys, names;
    for (const auto& m : list) {
        INFO(m.name);
        CHECK(m.builtin);
        CHECK(checkMaterial(m).empty());
        CHECK(m.youngsModulus > 0);
        CHECK(m.poissonsRatio > 0);
        CHECK(m.yieldStrength > 0);
        CHECK(m.costPerKg > 0);
        keys.insert(m.key);
        names.insert(m.name);
        CHECK(findBuiltinMaterial(m.key) == &m);
        CHECK(findBuiltinMaterial(m.name) == &m);
    }
    CHECK(keys.size() == list.size());
    CHECK(names.size() == list.size());
    CHECK(defaultMaterial().key == "pla");
    CHECK(findBuiltinMaterial("ALUMINUM 6061")->density == Approx(2.70));
    CHECK(findBuiltinMaterial("unobtainium") == nullptr);
}

TEST_CASE("mass, weight and cost follow from volume and material", "[model][material]") {
    const auto& pla = defaultMaterial();
    CHECK(massGrams(1000, pla) == Approx(1.24));  // 1 cm^3 of PLA
    CHECK(massGrams(0, pla) == 0);
    CHECK(weightNewtons(1000) == Approx(9.81));     // 1 kg
    CHECK(materialCost(500, pla) == Approx(10.0));  // half a kilo at 20 per kg
    Material free = pla;
    free.costPerKg = 0;
    CHECK(materialCost(500, free) == 0);
}

TEST_CASE("colors are read and written as #rrggbb", "[model][material]") {
    CHECK(parseColor("#3366cc") == Color{0x33, 0x66, 0xcc});
    CHECK(parseColor("#FFFFFF") == Color{255, 255, 255});
    CHECK_FALSE(parseColor("blue").has_value());
    CHECK_FALSE(parseColor("#12345").has_value());
    CHECK_FALSE(parseColor("#12345g").has_value());
    CHECK(colorText({0x33, 0x66, 0xcc}) == "#3366cc");
    CHECK(parseColor(colorText({1, 2, 3})) == Color{1, 2, 3});
}

TEST_CASE("a material's numbers are checked", "[model][material]") {
    Material m{"", "Mine", 1.0, {}, 0, 0, 0, 0, false};
    CHECK(checkMaterial(m).empty());  // unknown strength and price are fine
    m.density = 0;
    CHECK(checkMaterial(m).front().code == "material.density");
    m.density = 1;
    m.poissonsRatio = 0.7;
    CHECK(checkMaterial(m).front().code == "material.poisson");
    m.poissonsRatio = 0.3;
    m.costPerKg = -1;
    CHECK(checkMaterial(m).front().code == "material.negative");
    m.costPerKg = 0;
    m.name.clear();
    CHECK(checkMaterial(m).front().code == "material.name_empty");
}

TEST_CASE("a body is PLA until a material is picked", "[model][material]") {
    Tag t;
    for (const auto& id : {t.plate, t.hole, t.tag}) {
        CHECK(&t.doc.materialOf(id) == &defaultMaterial());
        CHECK(t.doc.materialSource(id).isNil());
    }
    CHECK(&t.doc.materialOf(FeatureId::generate()) == &defaultMaterial());
}

TEST_CASE("a cut takes its first body's material, unless it has its own", "[model][material]") {
    Tag t;
    REQUIRE(t.run("feature.set_material", {{"feature", t.plate}, {"material", std::string("aluminum 6061")}})
                .empty());
    CHECK(t.doc.find(t.plate)->material() == "aluminum_6061");  // stored by key
    CHECK(t.doc.materialOf(t.tag).name == "Aluminum 6061");
    CHECK(t.doc.materialSource(t.tag) == t.plate);
    CHECK(&t.doc.materialOf(t.hole) == &defaultMaterial());  // the tool's material doesn't spread

    REQUIRE(t.run("feature.set_material", {{"feature", t.tag}, {"material", std::string("Brass (C360)")}})
                .empty());
    CHECK(t.doc.materialOf(t.tag).key == "brass_360");
    CHECK(t.doc.materialSource(t.tag) == t.tag);
    CHECK(t.doc.undoLabel() == "Make Tag Brass (C360)");

    // Undo goes back to the plate's material; clearing works too.
    REQUIRE(t.doc.undo());
    CHECK(t.doc.materialOf(t.tag).key == "aluminum_6061");
    REQUIRE(t.run("feature.set_material", {{"feature", t.plate}, {"material", std::string()}}).empty());
    CHECK(&t.doc.materialOf(t.tag) == &defaultMaterial());
}

TEST_CASE("an unknown material is refused with a plain message", "[model][material]") {
    Tag t;
    const auto issues =
        t.run("feature.set_material", {{"feature", t.plate}, {"material", std::string("wood")}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "material.unknown");
    CHECK(issues[0].message.find("kh_cli materials") != std::string::npos);
    CHECK(t.doc.find(t.plate)->material().empty());
}

TEST_CASE("editing a step keeps its material", "[model][material]") {
    Tag t;
    REQUIRE(t.run("feature.set_material", {{"feature", t.plate}, {"material", std::string("oak")}}).empty());
    auto copy = t.doc.find(t.plate)->clone();
    static_cast<BoxFeature&>(*copy).setSize({70, 30, 4});
    REQUIRE(t.doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy))).empty());
    CHECK(t.doc.materialOf(t.tag).key == "oak");
}

TEST_CASE("a design can add, change and delete its own materials", "[model][material]") {
    Tag t;
    REQUIRE(t.run("material.define", {{"name", std::string("Blue PETG")},
                                      {"density", 1.27},
                                      {"color", std::string("#3366cc")},
                                      {"cost_per_kg", 25.0}})
                .empty());
    REQUIRE(t.doc.customMaterials().size() == 1);
    const auto key = t.doc.customMaterials()[0].key;
    CHECK(MaterialId::parse(key).has_value());  // a UUID, like every saved object
    CHECK_FALSE(t.doc.customMaterials()[0].builtin);
    CHECK(t.doc.findMaterial("blue petg")->color == Color{0x33, 0x66, 0xcc});

    REQUIRE(t.run("feature.set_material", {{"feature", t.plate}, {"material", std::string("Blue PETG")}})
                .empty());
    CHECK(t.doc.materialOf(t.tag).name == "Blue PETG");

    // Defining it again changes it in place: same id, new numbers.
    REQUIRE(t.run("material.define", {{"name", std::string("blue PETG")}, {"density", 1.30}}).empty());
    REQUIRE(t.doc.customMaterials().size() == 1);
    CHECK(t.doc.customMaterials()[0].key == key);
    CHECK(t.doc.materialOf(t.tag).density == Approx(1.30));
    CHECK(t.doc.undoLabel() == "Change material blue PETG");
    REQUIRE(t.doc.undo());
    CHECK(t.doc.materialOf(t.tag).density == Approx(1.27));

    // It can't be deleted while a step uses it.
    auto issues = t.run("material.delete", {{"name", std::string("Blue PETG")}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "material.in_use");
    REQUIRE(t.run("feature.set_material", {{"feature", t.plate}, {"material", std::string()}}).empty());
    REQUIRE(t.run("material.delete", {{"name", std::string("Blue PETG")}}).empty());
    CHECK(t.doc.customMaterials().empty());
    REQUIRE(t.doc.undo());
    REQUIRE(t.doc.customMaterials().size() == 1);
    CHECK(t.doc.customMaterials()[0].key == key);

    // Undoing the first define removes it, and redo brings back the same id.
    while (t.doc.canUndo() && !t.doc.customMaterials().empty()) t.doc.undo();
    CHECK(t.doc.customMaterials().empty());
    REQUIRE(t.doc.redo());
    REQUIRE(t.doc.customMaterials().size() == 1);
    CHECK(t.doc.customMaterials()[0].key == key);
}

TEST_CASE("built-in materials can't be changed or deleted", "[model][material]") {
    Tag t;
    auto issues = t.run("material.define", {{"name", std::string("PLA")}, {"density", 2.0}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "material.builtin");
    CHECK(issues[0].message.find("My PLA") != std::string::npos);
    issues = t.run("material.delete", {{"name", std::string("pla")}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "material.builtin");
    issues = t.run("material.delete", {{"name", std::string("nothing")}});
    CHECK(issues[0].code == "material.unknown");
}

TEST_CASE("bad material numbers and colors are refused", "[model][material]") {
    Tag t;
    auto issues = t.run("material.define", {{"name", std::string("Fog")}, {"density", 0.0}});
    CHECK(issues[0].code == "material.density");
    issues = t.run("material.define",
                   {{"name", std::string("Fog")}, {"density", 1.0}, {"color", std::string("grey")}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "material.color");
    CHECK(t.doc.customMaterials().empty());
}
