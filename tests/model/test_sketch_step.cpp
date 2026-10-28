// Tests for the Sketch step: SketchFeature, the sketch.* commands, and solving
// sketches when a design rebuilds.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;

namespace {

// A design with a sketch, built through the registry like kh_cli does.
struct Drawing {
    Document doc;
    test::FakeKernel kernel;
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();
    FeatureId sketchId;

    Drawing() {
        REQUIRE(run("feature.add_sketch", {}).empty());
        sketchId = doc.featureAt(0).id();
    }

    Diagnostics run(std::string_view name, Args args) {
        auto made = CommandRegistry::builtin().create(name, args, doc);
        if (!made.ok()) return made.issues;
        return doc.execute(std::move(made.command));
    }

    Diagnostics sk(std::string_view name, Args args) {
        args["sketch"] = sketchId;
        return run(name, std::move(args));
    }

    const SketchFeature& step() const { return static_cast<const SketchFeature&>(*doc.find(sketchId)); }

    RebuildReport rebuild() { return doc.regenerate(kernel, solver.get()); }
};

}  // namespace

TEST_CASE("feature.add_sketch adds an empty sketch on a plane; it isn't a body", "[model][sketch]") {
    Drawing d;
    CHECK(d.step().name() == "Sketch1");
    CHECK(d.step().plane() == SketchPlane::XY);
    CHECK(d.doc.undoLabel() == "Add Sketch1");
    REQUIRE(
        d.run("feature.add_sketch", {{"plane", std::string("xz")}, {"name", std::string("Side")}}).empty());
    CHECK(static_cast<const SketchFeature&>(d.doc.featureAt(1)).plane() == SketchPlane::XZ);

    const auto report = d.rebuild();
    const auto* r = report.result(d.sketchId);
    REQUIRE(r);
    CHECK(r->status == FeatureStatus::Ok);
    REQUIRE(r->sketch);
    CHECK(r->sketch->freedom == 0);  // nothing drawn yet
    CHECK_FALSE(report.shapeOf(d.sketchId));
    CHECK(report.bodies().empty());
}

TEST_CASE("drawing and constraining a sketch, then solving it on rebuild", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{38, 3}}}).empty());
    CHECK(d.doc.undoLabel() == "Add Line1");
    REQUIRE(
        d.sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Line1.start")}}).empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("horizontal")}, {"on", std::string("Line1")}})
                .empty());
    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("length")}, {"on", std::string("Line1")}, {"value", 40.0}})
                .empty());
    CHECK(d.doc.undoLabel() == "Add Length1");
    const auto& s = d.step().sketch();
    REQUIRE(s.constraints.size() == 3);
    CHECK(s.constraints[0].name == "Fix1");
    CHECK(s.constraints[1].name == "Horizontal1");
    // The step keeps what was drawn; the rebuild has the solved line.
    CHECK(std::get<sketch::LineShape>(s.entities[0].shape).end.x == 38);

    const auto report = d.rebuild();
    const auto* r = report.result(d.sketchId);
    REQUIRE(r->status == FeatureStatus::Ok);
    CHECK(r->sketch->freedom == 0);
    const auto& line = std::get<sketch::LineShape>(r->sketch->entities[0].shape);
    CHECK(line.end.x == Approx(40));
    CHECK(line.end.y == Approx(0).margin(1e-9));
}

TEST_CASE("undo and redo of sketch changes keep the same ids", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_circle", {{"center", sketch::Point2{5, 5}}, {"radius", 3.0}}).empty());
    const EntityId id = d.step().sketch().entities.at(0).id;
    REQUIRE(d.doc.undo());
    CHECK(d.step().sketch().entities.empty());
    REQUIRE(d.doc.redo());
    REQUIRE(d.step().sketch().entities.size() == 1);
    CHECK(d.step().sketch().entities[0].id == id);
    CHECK(d.step().sketch().entities[0].name == "Circle1");
}

TEST_CASE("sketch commands explain what's wrong in plain words", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}}).empty());
    REQUIRE(d.sk("sketch.add_circle", {{"center", sketch::Point2{5, 5}}, {"radius", 3.0}}).empty());
    auto message = [&](std::string kind, std::string on, std::optional<double> value = {}) {
        Args a{{"kind", kind}, {"on", on}};
        if (value) a["value"] = *value;
        const auto problems = d.sk("sketch.constrain", a);
        REQUIRE(problems.size() == 1);
        return problems[0].message;
    };
    CHECK(message("coincident", "Line1,Line1.end") ==
          "Line1 isn't a single point: say which of its points, like Line1.start or Line1.end.");
    CHECK(message("horizontal", "Line1.start") ==
          "Use Line1 here, without .start. Horizontal needs one line, like on=Line1.");
    CHECK(message("parallel", "Line1") == "Parallel needs two lines, like on=Line1,Line2.");
    CHECK(message("length", "Line9") ==
          "There's nothing called Line9 in this sketch. Length needs one line, like on=Line1.");
    CHECK(message("length", "Line1") == "Length needs a value, like value=40 (mm).");
    CHECK(message("angle", "Line1,Line1", 30.0) == "Angle needs two different lines.");
    CHECK(message("horizontal", "Line1", 3.0) == "Horizontal has no number. Leave out value.");
    CHECK(message("horizontal", "Circle1") == "Horizontal needs one line.");
    CHECK(message("radius", "Circle1", 0.0) == "A Radius must be more than 0 mm. Type a bigger number.");
    CHECK(
        message("fix", "Line1.middle") ==
        "Line1.middle isn't a point: use .start, .end or .center. Fix needs one point, like on=Line1.start.");

    const auto taken =
        d.sk("sketch.add_point", {{"at", sketch::Point2{1, 1}}, {"name", std::string("Line1")}});
    REQUIRE(taken.size() == 1);
    CHECK(taken[0].code == "sketch.name_taken");

    // Only sketches can be drawn in.
    REQUIRE(d.run("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}}).empty());
    const auto notSketch =
        d.run("sketch.add_point", {{"sketch", d.doc.featureAt(1).id()}, {"at", sketch::Point2{}}});
    REQUIRE(notSketch.size() == 1);
    CHECK(notSketch[0].message == "Box1 isn't a sketch. Pick a sketch, like Sketch1.");
    CHECK(d.step().sketch().constraints.empty());  // nothing above changed the sketch
}

TEST_CASE("a sketch whose constraints conflict fails to rebuild, and says which", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{38, 3}}}).empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("horizontal")}, {"on", std::string("Line1")}})
                .empty());
    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("length")}, {"on", std::string("Line1")}, {"value", 40.0}})
                .empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("horizontal_distance")},
                                      {"on", std::string("Line1.start,Line1.end")},
                                      {"value", 50.0}})
                .empty());
    auto report = d.rebuild();
    const auto* r = report.result(d.sketchId);
    CHECK(r->status == FeatureStatus::Failed);
    REQUIRE_FALSE(r->issues.empty());
    CHECK(r->issues[0].message ==
          "These constraints can't all be true at once: Length1, HorizontalDistance1. Delete one of them, or "
          "change a "
          "dimension.");

    // Changing the distance to match fixes it, with a repeat noted.
    REQUIRE(d.sk("sketch.set_value", {{"constraint", std::string("HorizontalDistance1")}, {"value", 40.0}})
                .empty());
    CHECK(d.doc.undoLabel() == "Change HorizontalDistance1");
    report = d.rebuild();
    r = report.result(d.sketchId);
    CHECK(r->status == FeatureStatus::Ok);
    REQUIRE(r->issues.size() == 1);
    CHECK(r->issues[0].severity == Severity::Warning);
}

TEST_CASE("an angle is typed in degrees and kept in radians", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}}).empty());
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 4}}}).empty());
    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("angle")}, {"on", std::string("Line1,Line2")}, {"value", 30.0}})
                .empty());
    CHECK(d.step().sketch().constraints[0].value == Approx(std::numbers::pi / 6));
    REQUIRE(d.sk("sketch.set_value", {{"constraint", std::string("Angle1")}, {"value", 45.0}}).empty());
    CHECK(d.step().sketch().constraints[0].value == Approx(std::numbers::pi / 4));
    REQUIRE(
        d.sk("sketch.add_arc", {{"center", sketch::Point2{0, 0}}, {"radius", 5.0}, {"end", 180.0}}).empty());
    const auto& arc = std::get<sketch::ArcShape>(d.step().sketch().entities[2].shape);
    CHECK(arc.startAngle == 0);
    CHECK(arc.endAngle == Approx(std::numbers::pi));
}

TEST_CASE("deleting an entity deletes the constraints on it", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}}).empty());
    REQUIRE(
        d.sk("sketch.add_line", {{"from", sketch::Point2{10, 0}}, {"to", sketch::Point2{10, 10}}}).empty());
    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("coincident")}, {"on", std::string("Line1.end,Line2.start")}})
                .empty());
    REQUIRE(
        d.sk("sketch.constrain", {{"kind", std::string("vertical")}, {"on", std::string("Line2")}}).empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("horizontal")}, {"on", std::string("Line1")}})
                .empty());
    REQUIRE(d.sk("sketch.delete", {{"item", std::string("Line2")}}).empty());
    const auto& s = d.step().sketch();
    REQUIRE(s.entities.size() == 1);
    REQUIRE(s.constraints.size() == 1);
    CHECK(s.constraints[0].name == "Horizontal1");
    REQUIRE(d.sk("sketch.delete", {{"item", std::string("Horizontal1")}}).empty());
    CHECK(d.step().sketch().constraints.empty());
    CHECK(d.sk("sketch.delete", {{"item", std::string("Nope")}}).at(0).code == "sketch.not_found");
    REQUIRE(d.doc.undo());
    REQUIRE(d.doc.undo());
    CHECK(d.step().sketch().constraints.size() == 3);  // everything back
}

TEST_CASE("without a solver, a sketch fails to rebuild with a plain message", "[model][sketch]") {
    Drawing d;
    const auto report = d.doc.regenerate(d.kernel);
    const auto* r = report.result(d.sketchId);
    CHECK(r->status == FeatureStatus::Failed);
    CHECK(r->issues.at(0).code == "sketch.no_solver");
}

TEST_CASE("a Boolean that uses a sketch is blocked, and says a sketch isn't a body", "[model][sketch]") {
    Drawing d;
    REQUIRE(d.run("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}}).empty());
    REQUIRE(d.run("feature.add_boolean",
                  {{"operation", std::string("union")}, {"a", d.doc.featureAt(1).id()}, {"b", d.sketchId}})
                .empty());
    const auto report = d.rebuild();
    const auto* r = report.result(d.doc.featureAt(2).id());
    CHECK(r->status == FeatureStatus::Blocked);
    CHECK(r->issues.at(0).message ==
          "This step uses Sketch1, which is a sketch, not a body. Pick a body instead.");
}

TEST_CASE("copying a sketch step copies its drawing", "[model][sketch][copy]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}}).empty());
    REQUIRE(d.run("feature.duplicate", {{"feature", d.sketchId}}).empty());
    const auto& copy = static_cast<const SketchFeature&>(d.doc.featureAt(1));
    CHECK(copy.name() == "Sketch2");
    CHECK(copy.sketch().entities.at(0).name == "Line1");
}
