// Fillet and Chamfer steps with real geometry: they round or bevel the edges
// picked, keep finding them when earlier steps change, and say plainly when
// they can't, so the student picks again.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <numbers>

#include "sb/io/SketchBilletFile.h"
#include "sb/kernel_occt/OcctKernel.h"
#include "sb/model/CommandRegistry.h"
#include "sb/model/Commands.h"
#include "sb/model/Document.h"
#include "sb/sketch_planegcs/PlaneGcsSolver.h"

using namespace sb;
using namespace sb::model;
using Catch::Approx;
using kernel::TopoKind;

namespace {

constexpr double kPi = std::numbers::pi;

struct Design {
    Document doc;
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();

    Diagnostics tryRun(std::string_view name, Args args) {
        auto made = CommandRegistry::builtin().create(name, args, doc);
        if (!made.ok()) return made.issues;
        return doc.execute(std::move(made.command));
    }
    void run(std::string_view name, Args args) {
        const auto issues = tryRun(name, std::move(args));
        INFO(name << ": " << (issues.empty() ? std::string() : issues.front().message));
        REQUIRE(issues.empty());
    }
    FeatureId last() const { return doc.featureAt(doc.featureCount() - 1).id(); }
    RebuildReport rebuild() { return doc.regenerate(*kernel, solver.get()); }

    // A 40 x 20 mm rectangle with its width as Length1, extruded 10 mm up.
    FeatureId plate() {
        run("feature.add_sketch", {});
        const auto sketch = last();
        run("sketch.add_rectangle",
            {{"sketch", sketch}, {"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{40, 20}}});
        run("sketch.constrain", {{"sketch", sketch},
                                 {"kind", std::string("length")},
                                 {"on", std::string("Line1")},
                                 {"value", 40.0}});
        run("feature.add_extrude", {{"sketch", sketch}, {"distance", 10.0}});
        return last();
    }

    // The edge of a body's result whose middle is at `p`, as a reference.
    GeometryRef edgeAt(const FeatureId& body, kernel::Point3 p) {
        const auto report = rebuild();
        const auto& names = *report.result(body)->names;
        for (std::size_t i = 0; i < names.elements.edges.size(); ++i) {
            const auto& c = names.elements.edges[i].center;
            if (std::abs(c.x - p.x) < 1e-6 && std::abs(c.y - p.y) < 1e-6 && std::abs(c.z - p.z) < 1e-6) {
                return referTo(names, TopoKind::Edge, static_cast<int>(i));
            }
        }
        FAIL("no edge there");
        return {};
    }

    double volume(const FeatureId& id) {
        const auto report = rebuild();
        const auto* r = report.result(id);
        REQUIRE(r);
        INFO((r->issues.empty() ? std::string() : r->issues.front().message));
        REQUIRE(r->status == FeatureStatus::Ok);
        return kernel->massProps(r->shape).volume;
    }
};

// What rounding an edge of length `l` by radius `r` takes away: a square less a quarter circle, all along.
double rounded(double r, double l) {
    return (r * r - kPi * r * r / 4) * l;
}

}  // namespace

TEST_CASE("a fillet rounds the picked edge, and keeps it when sizes change", "[model][fillet][occt]") {
    Design d;
    const auto plate = d.plate();
    // The top front edge, along x: as long as the rectangle is wide.
    const auto edge = d.edgeAt(plate, {20, 0, 10});
    d.run("feature.add_fillet", {{"body", plate}, {"edges", std::vector{edge}}, {"radius", 3.0}});
    const auto fillet = d.last();
    CHECK(d.doc.find(fillet)->name() == "Fillet1");
    CHECK(d.volume(fillet) == Approx(8000 - rounded(3, 40)).epsilon(1e-9));

    // Wider, taller, and a bigger radius: the same edge, worked out again.
    d.run("sketch.set_value",
          {{"sketch", d.doc.featureAt(0).id()}, {"constraint", std::string("Length1")}, {"value", 65.0}});
    d.run("feature.set_value", {{"feature", plate}, {"setting", std::string("distance")}, {"value", 15.0}});
    d.run("feature.set_value", {{"feature", fillet}, {"setting", std::string("radius")}, {"value", 5.0}});
    CHECK(d.volume(fillet) == Approx(65 * 20 * 15 - rounded(5, 65)).epsilon(1e-9));
    const auto report = d.rebuild();
    CHECK(report.ok());
    CHECK(report.result(fillet)->issues.empty());  // found by its exact name, so nothing to say
    CHECK(d.kernel->validate(report.result(fillet)->shape).empty());
}

TEST_CASE("a chamfer bevels the picked edges by its distance", "[model][chamfer][occt]") {
    Design d;
    const auto plate = d.plate();
    // Both upright edges at the front, each 10 mm tall.
    const auto left = d.edgeAt(plate, {0, 0, 5});
    const auto right = d.edgeAt(plate, {40, 0, 5});
    d.run("feature.add_chamfer", {{"body", plate}, {"edges", std::vector{left, right}}, {"distance", 2.0}});
    const auto chamfer = d.last();
    CHECK(d.volume(chamfer) == Approx(8000 - 2 * (2.0 * 2 / 2) * 10).epsilon(1e-9));
    d.run("feature.set_value", {{"feature", plate}, {"setting", std::string("distance")}, {"value", 30.0}});
    CHECK(d.volume(chamfer) == Approx(40 * 20 * 30 - 2 * (2.0 * 2 / 2) * 30).epsilon(1e-9));
}

TEST_CASE("a fillet whose edge is gone says so, and works again when its edges are picked again",
          "[model][fillet][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{40, 30, 10}}});
    const auto box = d.last();
    d.run("feature.add_cylinder", {{"base", kernel::Vec3{20, 15, -1}}, {"radius", 4.0}, {"height", 12.0}});
    const auto pin = d.last();
    d.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", box}, {"b", pin}});
    const auto cut = d.last();
    // The hole's top rim: a circle at z = 10.
    const auto rim = d.edgeAt(cut, {20, 15, 10});
    d.run("feature.add_fillet", {{"body", cut}, {"edges", std::vector{rim}}, {"radius", 1.0}});
    const auto fillet = d.last();
    REQUIRE(d.rebuild().ok());

    // The pin no longer reaches the top: the rim is gone.
    d.run("feature.set_value", {{"feature", pin}, {"setting", std::string("height")}, {"value", 6.0}});
    auto report = d.rebuild();
    const auto* r = report.result(fillet);
    REQUIRE(r->status == FeatureStatus::Failed);
    REQUIRE(r->issues.size() == 1);
    CHECK(r->issues[0].code == "edges.missing");
    CHECK(r->issues[0].message ==
          "Fillet1 can't find an edge it rounds: an earlier change took it away. Edit Fillet1 and pick its "
          "edges "
          "again.");

    // Pick the top's front edge instead: the fillet works again, as one undo step.
    const auto front = d.edgeAt(cut, {20, 0, 10});
    d.run("feature.set_edges", {{"feature", fillet}, {"edges", std::vector{front}}});
    CHECK(d.doc.undoLabel() == "Pick edges of Fillet1");
    CHECK(d.rebuild().result(fillet)->status == FeatureStatus::Ok);
    REQUIRE(d.doc.undo());
    CHECK(d.rebuild().result(fillet)->status == FeatureStatus::Failed);
}

TEST_CASE("a fillet that found its edge by where it is says to check it", "[model][fillet][occt]") {
    Design d;
    d.run("feature.add_box", {{"size", kernel::Vec3{40, 30, 10}}});
    const auto box = d.last();
    d.run("feature.add_box", {{"corner", kernel::Vec3{5, -1, 2}}, {"size", kernel::Vec3{5, 32, 10}}});
    const auto slot = d.last();
    d.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", box}, {"b", slot}});
    const auto cut = d.last();
    // The long piece of the top's front edge, from x = 10 to 40.
    const auto edge = d.edgeAt(cut, {25, 0, 10});
    REQUIRE(edge.name.find("#2") != std::string::npos);  // between the top's and front's second pieces
    d.run("feature.add_fillet", {{"body", cut}, {"edges", std::vector{edge}}, {"radius", 1.0}});
    const auto fillet = d.last();
    // Move the slot off the top: the edge is whole again, with no "#2".
    d.run("feature.set_value", {{"feature", slot}, {"setting", std::string("corner_z")}, {"value", -5.0}});
    d.run("feature.set_value", {{"feature", slot}, {"setting", std::string("size_z")}, {"value", 8.0}});
    const auto report = d.rebuild();
    const auto* r = report.result(fillet);
    REQUIRE(r->status == FeatureStatus::Ok);
    REQUIRE(r->issues.size() == 1);
    CHECK(r->issues[0].code == "edges.matched");
    CHECK(r->issues[0].severity == Severity::Info);
    CHECK(d.kernel->massProps(r->shape).volume ==
          Approx(40 * 30 * 10 - 5 * 30 * 3 - rounded(1, 40)).epsilon(1e-9));
}

TEST_CASE("fillets and chamfers that can't be made say how to fix them", "[model][fillet][chamfer][occt]") {
    Design d;
    const auto plate = d.plate();
    const auto edge = d.edgeAt(plate, {20, 0, 10});

    // No edges.
    auto issues = d.tryRun("feature.add_fillet", {{"body", plate}, {"edges", std::vector<GeometryRef>{}}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "command.arg_geometry");

    // A radius too big for the 10 mm tall plate.
    d.run("feature.add_fillet", {{"body", plate}, {"edges", std::vector{edge}}, {"radius", 12.0}});
    auto report = d.rebuild();
    CHECK(report.result(d.last())->issues.at(0).code == "kernel.fillet.failed");

    // A size of 0.
    d.run("feature.add_chamfer", {{"body", plate}, {"edges", std::vector{edge}}, {"distance", 0.0}});
    report = d.rebuild();
    CHECK(report.result(d.last())->issues.at(0).message ==
          "A chamfer needs a distance greater than 0 mm. Type one in Properties, like 1.");

    // Edges can only be picked again on a Fillet or Chamfer.
    issues = d.tryRun("feature.set_edges", {{"feature", plate}, {"edges", std::vector{edge}}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].code == "edges.not_edge_step");
}

TEST_CASE("a fillet saves and opens with its edges, and copies follow their copied body",
          "[model][fillet][occt][file]") {
    Design d;
    const auto plate = d.plate();
    const auto edge = d.edgeAt(plate, {20, 0, 10});
    d.run("feature.add_fillet", {{"body", plate}, {"edges", std::vector{edge}}, {"radius", 3.0}});
    const auto fillet = d.last();
    const double before = d.volume(fillet);

    const auto path = std::filesystem::temp_directory_path() / "sb-test-fillet.sb";
    REQUIRE(io::saveFile(d.doc, path).empty());
    auto opened = io::openFile(path);
    std::filesystem::remove(path);
    REQUIRE(opened.ok());
    const auto* again = dynamic_cast<const FilletFeature*>(opened.document->find(fillet));
    REQUIRE(again);
    REQUIRE(again->edges().size() == 1);
    CHECK(again->edges()[0].name == edge.name);
    CHECK(again->edges()[0].signature.size == Approx(40));
    CHECK(again->edges()[0].signature.geom == kernel::GeomKind::Line);
    const auto report = opened.document->regenerate(*d.kernel, d.solver.get());
    REQUIRE(report.ok());
    CHECK(d.kernel->massProps(report.result(fillet)->shape).volume == Approx(before));

    // Duplicating the fillet copies its extrude and sketch too; the copy rounds
    // the copied plate's edge.
    d.run("feature.duplicate", {{"feature", fillet}});
    const auto copy = d.last();
    CHECK(copy != fillet);
    CHECK(d.volume(copy) == Approx(before));
}
