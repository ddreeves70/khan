// Tests for the Extrude step in the model: what it builds from, when it's blocked
// or fails (and what it says), and that copies and undo work. The kernel here is
// the fake one (a face's volume is its area); the real geometry is tested in
// tests/kernel and by the corpus.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;

namespace {

// A design with Sketch1, built through the registry like kh_cli does.
struct Design {
    Document doc;
    test::FakeKernel kernel;
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();

    Diagnostics run(std::string_view name, Args args) {
        auto made = CommandRegistry::builtin().create(name, args, doc);
        if (!made.ok()) return made.issues;
        return doc.execute(std::move(made.command));
    }
    const Feature& named(std::string_view name) const {
        std::size_t i = 0;
        while (i < doc.featureCount() && doc.featureAt(i).name() != name) ++i;
        INFO("no step called " << name);
        REQUIRE(i < doc.featureCount());
        return doc.featureAt(i);
    }
    FeatureId id(const char* name) const { return named(name).id(); }
    Diagnostics sk(std::string_view name, Args args) {
        args["sketch"] = id("Sketch1");
        return run(name, std::move(args));
    }
    // A closed 40 x 20 mm rectangle in Sketch1.
    void rectangle() {
        REQUIRE(run("feature.add_sketch", {}).empty());
        for (auto [a, b] : {std::pair{sketch::Point2{0, 0}, sketch::Point2{40, 0}},
                            {{40, 0}, {40, 20}},
                            {{40, 20}, {0, 20}},
                            {{0, 20}, {0, 0}}}) {
            REQUIRE(sk("sketch.add_line", {{"from", a}, {"to", b}}).empty());
        }
    }
    RebuildReport rebuild() { return doc.regenerate(kernel, solver.get()); }
};

}  // namespace

TEST_CASE("Extrude makes a body from a sketch's closed shape; the sketch isn't a body", "[model][extrude]") {
    Design d;
    d.rectangle();
    REQUIRE(d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}, {"distance", 5.0}}).empty());
    const auto& e = static_cast<const ExtrudeFeature&>(d.named("Extrude1"));
    CHECK(e.sketch() == d.id("Sketch1"));
    CHECK(e.distance() == 5);
    CHECK(e.side() == ExtrudeSide::One);
    CHECK(d.doc.undoLabel() == "Add Extrude1");
    const auto report = d.rebuild();
    REQUIRE(report.ok());
    REQUIRE(report.bodies() == std::vector<FeatureId>{e.id()});
    CHECK(d.kernel.massProps(*report.shapeOf(e.id())).volume == Approx(800 * 5));
    // The sketch's result says which plane it's on, for steps that use it.
    CHECK(report.result(d.id("Sketch1"))->plane == SketchPlane::XY);
}

TEST_CASE("Extrude's defaults, sides and names come from the command", "[model][extrude]") {
    Design d;
    d.rectangle();
    REQUIRE(d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}}).empty());
    REQUIRE(
        d.run("feature.add_extrude",
              {{"sketch", d.id("Sketch1")}, {"side", std::string("both")}, {"name", std::string("Thick")}})
            .empty());
    REQUIRE(
        d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}, {"side", std::string("flip")}}).empty());
    CHECK(static_cast<const ExtrudeFeature&>(d.named("Extrude1")).distance() == 10);  // the default
    CHECK(static_cast<const ExtrudeFeature&>(d.named("Thick")).side() == ExtrudeSide::Both);
    CHECK(static_cast<const ExtrudeFeature&>(d.named("Extrude2")).side() == ExtrudeSide::Flip);
    const auto report = d.rebuild();
    CHECK(report.ok());
    CHECK(report.bodies().size() == 3);  // one sketch can make several bodies
}

TEST_CASE("Extrude with no closed shape says which ends aren't joined", "[model][extrude]") {
    Design d;
    REQUIRE(d.run("feature.add_sketch", {}).empty());
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}}).empty());
    REQUIRE(d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}}).empty());
    const auto report = d.rebuild();
    const auto* r = report.result(d.id("Extrude1"));
    CHECK(r->status == FeatureStatus::Failed);
    REQUIRE(r->issues.size() == 1);
    CHECK(r->issues[0].code == "extrude.no_shape");
    CHECK(r->issues[0].message.find("Line1's start, Line1's end") != std::string::npos);
}

TEST_CASE("Extrude waits for its sketch: blocked if it didn't solve, is off, or isn't a sketch",
          "[model][extrude]") {
    Design d;
    d.rectangle();
    REQUIRE(d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}}).empty());
    SECTION("a sketch whose constraints conflict") {
        REQUIRE(d.sk("sketch.constrain",
                     {{"kind", std::string("length")}, {"on", std::string("Line1")}, {"value", 40.0}})
                    .empty());
        REQUIRE(d.sk("sketch.constrain",
                     {{"kind", std::string("length")}, {"on", std::string("Line1")}, {"value", 30.0}})
                    .empty());
        const auto report = d.rebuild();  // kept: results point into it
        const auto* r = report.result(d.id("Extrude1"));
        CHECK(r->status == FeatureStatus::Blocked);
        CHECK(r->issues.at(0).message == "This step uses Sketch1, which didn't solve. Fix the sketch first.");
    }
    SECTION("a sketch that's turned off") {
        REQUIRE(d.run("feature.suppress", {{"steps", std::vector<FeatureId>{d.id("Sketch1")}}, {"off", true}})
                    .empty());
        const auto report = d.rebuild();
        CHECK(report.result(d.id("Extrude1"))->status == FeatureStatus::Blocked);
        CHECK(report.result(d.id("Extrude1"))->issues.at(0).message.find("turned off") != std::string::npos);
    }
    SECTION("a box instead of a sketch") {
        REQUIRE(d.run("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}}).empty());
        REQUIRE(
            d.run("feature.add_extrude", {{"sketch", d.id("Box1")}, {"name", std::string("Odd")}}).empty());
        const auto report = d.rebuild();  // kept: results point into it
        const auto* r = report.result(d.id("Odd"));
        CHECK(r->status == FeatureStatus::Blocked);
        CHECK(r->issues.at(0).message == "This step uses Box1, which isn't a sketch. Pick a sketch instead.");
    }
}

TEST_CASE("Extrude with a distance of 0 says how to fix it", "[model][extrude]") {
    Design d;
    d.rectangle();
    REQUIRE(d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}, {"distance", 0.0}}).empty());
    const auto report = d.rebuild();
    const auto* r = report.result(d.id("Extrude1"));
    CHECK(r->status == FeatureStatus::Failed);
    CHECK(r->issues.at(0).code == "extrude.distance");
}

TEST_CASE("duplicating an Extrude copies its sketch too, and the copy uses the copy", "[model][extrude]") {
    Design d;
    d.rectangle();
    REQUIRE(d.run("feature.add_extrude", {{"sketch", d.id("Sketch1")}}).empty());
    REQUIRE(d.run("feature.duplicate", {{"feature", d.id("Extrude1")}}).empty());
    REQUIRE(d.doc.featureCount() == 4);
    const auto& copy = static_cast<const ExtrudeFeature&>(d.doc.featureAt(3));
    CHECK(copy.sketch() == d.doc.featureAt(2).id());  // the copied sketch, not Sketch1
    CHECK(d.doc.featureAt(2).type() == FeatureType::Sketch);
    CHECK(d.rebuild().ok());
    d.doc.undo();
    CHECK(d.doc.featureCount() == 2);
}
