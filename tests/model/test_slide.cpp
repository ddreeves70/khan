// Tests for sliding a body (SlideBodyCommand, feature.slide): what dragging a
// body's arrows in the 3D view does. The bounds checks use OCCT.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>

#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;
using Catch::Matchers::StartsWith;

namespace {

struct Design {
    Document doc;
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();

    FeatureId add(const std::string& name, const Args& args) {
        auto made = CommandRegistry::builtin().create(name, args, doc);
        REQUIRE(made.ok());
        REQUIRE(doc.execute(std::move(made.command)).empty());
        return doc.featureAt(doc.featureCount() - 1).id();
    }

    Diagnostics slide(const FeatureId& id, kernel::Vec3 by, bool continues = false,
                      FeatureId moveId = FeatureId::generate()) {
        return doc.execute(std::make_unique<SlideBodyCommand>(id, by, continues, moveId));
    }

    // The bounds of a step's body after a rebuild (mm).
    kernel::Box3 bounds(const FeatureId& id) {
        const auto report = doc.regenerate(*kernel, solver.get());
        const auto* shape = report.shapeOf(id);
        REQUIRE(shape);
        return kernel->bounds(*shape);
    }
};

void checkMovedBy(const kernel::Box3& before, const kernel::Box3& after, kernel::Vec3 by) {
    CHECK(after.min.x == Approx(before.min.x + by.x).margin(1e-6));
    CHECK(after.min.y == Approx(before.min.y + by.y).margin(1e-6));
    CHECK(after.min.z == Approx(before.min.z + by.z).margin(1e-6));
    CHECK(after.max.x == Approx(before.max.x + by.x).margin(1e-6));
    CHECK(after.max.y == Approx(before.max.y + by.y).margin(1e-6));
    CHECK(after.max.z == Approx(before.max.z + by.z).margin(1e-6));
}

}  // namespace

TEST_CASE("sliding a box or cylinder moves where it starts, and its body moves by exactly that",
          "[model][slide][occt]") {
    Design d;
    const auto box = d.add("feature.add_box", {{"size", kernel::Vec3{10, 20, 30}}});
    const auto cyl =
        d.add("feature.add_cylinder", {{"base", kernel::Vec3{50, 0, 0}}, {"radius", 4.0}, {"height", 8.0}});
    // A property: for several slides, the body's bounds move by exactly the slide.
    for (const kernel::Vec3 by : {kernel::Vec3{10, 0, 0}, kernel::Vec3{0, -5.5, 0}, kernel::Vec3{1, 2, 3}}) {
        for (const auto& id : {box, cyl}) {
            const auto before = d.bounds(id);
            REQUIRE(d.slide(id, by).empty());
            checkMovedBy(before, d.bounds(id), by);
        }
    }
    CHECK(d.doc.featureCount() == 2);  // no Move steps
    CHECK(static_cast<const BoxFeature&>(*d.doc.find(box)).corner().x == 11);
    CHECK(static_cast<const CylinderFeature&>(*d.doc.find(cyl)).base().z == 3);
    CHECK(d.doc.undoLabel() == "Move Cylinder1");
    d.doc.undo();
    CHECK(static_cast<const CylinderFeature&>(*d.doc.find(cyl)).base().z == 0);
}

TEST_CASE("sliding another body adds a Move step at the end; the rest of the drag joins it",
          "[model][slide][occt]") {
    Design d;
    const auto sketch = d.add("feature.add_sketch", {});
    REQUIRE(d.doc
                .execute(std::make_unique<EditSketchCommand>(
                    sketch,
                    [](sketch::Sketch& s, std::string& label) {
                        // A 20 x 10 mm rectangle.
                        const sketch::Point2 p[4] = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
                        for (int i = 0; i < 4; ++i) {
                            s.entities.push_back({EntityId::generate(), "Line" + std::to_string(i + 1),
                                                  sketch::LineShape{p[i], p[(i + 1) % 4]}, false});
                        }
                        label = "Draw";
                        return Diagnostics{};
                    }))
                .empty());
    const auto extrude = d.add("feature.add_extrude", {{"sketch", sketch}, {"distance", 5.0}});
    const auto before = d.bounds(extrude);

    // A drag: three small slides, then one undo puts it all back.
    const auto moveId = FeatureId::generate();
    REQUIRE(d.slide(extrude, {4, 0, 0}, false, moveId).empty());
    REQUIRE(d.doc.find(moveId));
    CHECK(d.doc.find(moveId)->name() == "Move1");
    CHECK(d.doc.undoLabel() == "Move Extrude1");
    REQUIRE(d.slide(moveId, {3, 0, 0}, true).empty());  // the rest of the drag slides the new Move
    REQUIRE(d.slide(moveId, {0, 2, 0}, true).empty());
    CHECK(d.doc.featureCount() == 3);
    const auto& move = static_cast<const MoveFeature&>(*d.doc.find(moveId));
    CHECK(move.motion().offset.x == 7);
    CHECK(move.motion().offset.y == 2);
    checkMovedBy(before, d.bounds(moveId), {7, 2, 0});

    d.doc.undo();  // the whole drag
    CHECK(d.doc.featureCount() == 2);
    CHECK_FALSE(d.doc.find(moveId));
    d.doc.redo();
    REQUIRE(d.doc.find(moveId));
    CHECK(static_cast<const MoveFeature&>(*d.doc.find(moveId)).motion().offset.x == 7);
    CHECK(d.doc.find(moveId)->id() == moveId);  // the same id on redo

    // The next drag (not continuing) is its own undo step, and slides the Move.
    REQUIRE(d.slide(moveId, {0, 0, 1}).empty());
    CHECK(d.doc.featureCount() == 3);
    CHECK(static_cast<const MoveFeature&>(*d.doc.find(moveId)).motion().offset.z == 1);
    d.doc.undo();
    CHECK(static_cast<const MoveFeature&>(*d.doc.find(moveId)).motion().offset.z == 0);
}

TEST_CASE("sliding is refused for a sketch, and for a body a later step uses", "[model][slide]") {
    Design d;
    const auto sketch = d.add("feature.add_sketch", {});
    const auto box = d.add("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
    const auto pin = d.add("feature.add_cylinder", {{"radius", 2.0}, {"height", 20.0}});
    const auto cut =
        d.add("feature.add_boolean", {{"operation", std::string("cut")}, {"a", box}, {"b", pin}});
    const auto twice = d.add("feature.add_move", {{"body", cut}});
    (void)twice;

    auto issues = d.slide(sketch, {1, 0, 0});
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().code == "slide.sketch");
    issues = d.slide(cut, {1, 0, 0});
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().message ==
          "Cut1 is used by Move1, so moving it alone would leave Move1 behind. Move Move1 instead.");
    CHECK(SlideBodyCommand::check(d.doc, cut).front().code == "slide.used");
    CHECK(SlideBodyCommand::check(d.doc, box).empty());  // a used box can still move: the cut follows
    CHECK(SlideBodyCommand::check(d.doc, FeatureId::generate()).front().code == "feature.not_found");
    CHECK(d.doc.featureCount() == 5);
}

TEST_CASE("a slid number stops coming from its formula; the others keep theirs", "[model][slide]") {
    Design d;
    REQUIRE(d.doc.execute(std::make_unique<SetParameterCommand>("gap", "5 mm")).empty());
    const auto box = d.add("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
    for (const char* key : {"corner_x", "corner_y"}) {
        REQUIRE(d.doc.execute(std::make_unique<SetNumberCommand>(box, key, std::string("gap"))).empty());
    }
    REQUIRE(d.slide(box, {2, 0, 0}).empty());
    const auto& f = *d.doc.find(box);
    CHECK(f.number("corner_x") == 7);
    CHECK(f.formula("corner_x").empty());
    CHECK(f.formula("corner_y") == "gap");
}

TEST_CASE("feature.slide slides a body by name", "[model][slide]") {
    Design d;
    const auto box = d.add("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
    d.add("feature.slide", {{"feature", box}, {"by", kernel::Vec3{0, 0, 5}}});
    CHECK(static_cast<const BoxFeature&>(*d.doc.find(box)).corner().z == 5);
}
