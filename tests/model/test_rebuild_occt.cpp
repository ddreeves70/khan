#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Document.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;

// The same document path as the fake-kernel tests, with real geometry.
TEST_CASE("A box with a hole cut through it has the right volume", "[model][rebuild][occt]") {
    Document doc;
    const auto& reg = CommandRegistry::builtin();
    auto add = [&](const std::string& name, const Args& args) {
        auto made = reg.create(name, args, doc);
        REQUIRE(made.ok());
        REQUIRE(doc.execute(std::move(made.command)).empty());
        return doc.featureAt(doc.featureCount() - 1).id();
    };

    const auto block = add("feature.add_box", {{"size", kernel::Vec3{40, 30, 10}}});
    // Starts below the block and ends above it, so it cuts all the way through.
    const auto pin =
        add("feature.add_cylinder", {{"base", kernel::Vec3{20, 15, -1}}, {"radius", 5.0}, {"height", 12.0}});
    const auto cut =
        add("feature.add_boolean", {{"operation", std::string("cut")}, {"a", block}, {"b", pin}});

    auto kernelPtr = kernel::makeOcctKernel();
    auto& k = *kernelPtr;
    const auto report = doc.regenerate(k);
    REQUIRE(report.ok());
    REQUIRE(report.bodies() == std::vector<FeatureId>{cut});

    const auto& body = *report.shapeOf(cut);
    const double expected = 40.0 * 30.0 * 10.0 - std::numbers::pi * 5.0 * 5.0 * 10.0;  // 10 214.6 mm^3
    REQUIRE(k.massProps(body).volume == Approx(expected).epsilon(1e-6));
    REQUIRE(k.validate(body).empty());
    REQUIRE(k.topology(body).solids == 1);

    // Undo the cut: the block and the pin are separate bodies again.
    REQUIRE(doc.undo());
    const auto before = doc.regenerate(k);
    REQUIRE(before.bodies() == std::vector<FeatureId>{block, pin});
    REQUIRE(k.massProps(*before.shapeOf(block)).volume == Approx(12000.0));
}

TEST_CASE("a body's mass is its volume times its material's density", "[model][material][occt]") {
    auto kernel = kernel::makeOcctKernel();
    // A 10 mm cube: 1 cm^3.
    const auto cube = kernel->makeBox({0, 0, 0}, 10, 10, 10).shape;
    const auto pla = bodyMass(*kernel, cube, defaultMaterial());
    CHECK(pla.volume == Approx(1000));
    CHECK(pla.area == Approx(600));
    CHECK(pla.grams == Approx(1.24));
    CHECK(pla.newtons == Approx(1.24 / 1000 * 9.81));
    CHECK(pla.cost == Approx(1.24 / 1000 * 20));
    CHECK(pla.center.x == Approx(5));
    CHECK(pla.center.z == Approx(5));
    // Property: mass scales with density, and nothing else changes.
    const auto steel = bodyMass(*kernel, cube, *findBuiltinMaterial("steel_1018"));
    CHECK(steel.grams == Approx(7.87));
    CHECK(steel.volume == Approx(pla.volume));
    // An empty shape weighs nothing.
    CHECK(bodyMass(*kernel, kernel::ShapeHandle{}, defaultMaterial()).grams == 0);
}

TEST_CASE("a sketched spline arch extrudes to the area under the curve", "[model][rebuild][occt][spline]") {
    Document doc;
    const auto& reg = CommandRegistry::builtin();
    auto run = [&](const std::string& name, Args args) {
        auto made = reg.create(name, args, doc);
        INFO(name << ": " << (made.issues.empty() ? std::string() : made.issues.front().message));
        REQUIRE(made.ok());
        const auto issues = doc.execute(std::move(made.command));
        INFO((issues.empty() ? std::string() : issues.front().message));
        REQUIRE(issues.empty());
    };
    run("feature.add_sketch", {});
    const auto sketch = doc.featureAt(0).id();
    run("sketch.add_spline",
        {{"sketch", sketch}, {"poles", std::vector<sketch::Point2>{{0, 0}, {0, 30}, {40, 30}, {40, 0}}}});
    run("sketch.add_line",
        {{"sketch", sketch}, {"from", sketch::Point2{40, 0}}, {"to", sketch::Point2{0, 0}}});
    // Its control points are named for constraints, like Spline1.pole2.
    run("sketch.constrain",
        {{"sketch", sketch}, {"kind", std::string("fix")}, {"on", std::string("Spline1.pole2")}});
    run("feature.add_extrude", {{"sketch", sketch}, {"distance", 5.0}});
    const auto& s = static_cast<const SketchFeature&>(doc.featureAt(0)).sketch();
    REQUIRE(s.constraints.size() == 1);
    CHECK(s.constraints[0].points[0] == sketch::PointRef{s.entities[0].id, sketch::PointRole::Pole, 1});
    CHECK(std::get<sketch::SplineShape>(s.entities[0].shape).degree == 3);  // the default

    auto k = kernel::makeOcctKernel();
    auto solver = sketch::makePlaneGcsSolver();
    const auto report = doc.regenerate(*k, solver.get());
    REQUIRE(report.ok());
    const auto* body = report.shapeOf(doc.featureAt(1).id());
    REQUIRE(body);
    // A Bezier arch's area is 3/5 of the 40 x 30 box around its poles: 720 mm^2.
    CHECK(k->massProps(*body).volume == Approx(720 * 5).epsilon(1e-9));
    CHECK(k->validate(*body).empty());
}
