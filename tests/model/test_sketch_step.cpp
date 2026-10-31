// Tests for the Sketch step: SketchFeature, the sketch.* commands, and solving
// sketches when a design rebuilds.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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
    CHECK(message("fix", "Line1.middle") ==
          "Line1.middle isn't a point: use .start, .end, .center, or .pole1, .pole2, ... for a spline's "
          "control points. Fix needs one point, like on=Line1.start.");

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

namespace {

using Points = std::vector<sketch::Point2>;

const sketch::SplineShape& splineIn(const Drawing& d, const char* name) {
    return std::get<sketch::SplineShape>(d.step().sketch().findEntity(name)->shape);
}

}  // namespace

TEST_CASE("sketch.add_spline through points works out control points the curve passes through",
          "[model][sketch][spline]") {
    Drawing d;
    const Points through{{0, 0}, {10, 18}, {30, 22}, {40, 0}};
    REQUIRE(d.sk("sketch.add_spline", {{"through", through}}).empty());
    const auto& sp = splineIn(d, "Spline1");
    CHECK(sp.degree == 3);
    REQUIRE(sp.poles.size() == 4);
    const auto at = sketch::splineThroughAt(4, 3, false);
    for (std::size_t k = 0; k < 4; ++k) {
        const auto p = sketch::splinePoint(sp, at[k]);
        CHECK(p.x == Approx(through[k].x).margin(1e-9));
        CHECK(p.y == Approx(through[k].y).margin(1e-9));
    }
    // Both, or neither: say which to give.
    auto problems = d.sk("sketch.add_spline", {{"through", through}, {"poles", through}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "sketch.spline_points");
    problems = d.sk("sketch.add_spline", {});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "sketch.spline_points");
    // Too few points for the degree: the usual check explains.
    problems = d.sk("sketch.add_spline", {{"through", Points{{0, 0}, {5, 5}}}, {"degree", std::int64_t{3}}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "sketch.spline_poles");
}

TEST_CASE("sketch.set_spline changes a spline's degree and closes it; set_weight changes one pull",
          "[model][sketch][spline]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_spline", {{"poles", Points{{0, 0}, {0, 30}, {40, 30}, {40, 0}}}}).empty());
    REQUIRE(
        d.sk("sketch.set_spline", {{"spline", std::string("Spline1")}, {"degree", std::int64_t{2}}}).empty());
    CHECK(splineIn(d, "Spline1").degree == 2);
    REQUIRE(d.sk("sketch.set_spline", {{"spline", std::string("Spline1")}, {"closed", true}}).empty());
    CHECK(splineIn(d, "Spline1").closed);
    CHECK(splineIn(d, "Spline1").degree == 2);
    CHECK(d.doc.undoLabel() == "Change Spline1");

    auto problems = d.sk("sketch.set_spline", {{"spline", std::string("Spline1")}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message == "Say what to change: degree=2, closed=true, or both.");
    problems = d.sk("sketch.set_spline", {{"spline", std::string("Spline1")}, {"degree", std::int64_t{4}}});
    REQUIRE(problems.size() == 1);  // 4 control points can't make degree 4
    CHECK(problems[0].code == "sketch.spline_poles");

    REQUIRE(d.sk("sketch.set_weight", {{"point", std::string("Spline1.pole2")}, {"weight", 3.0}}).empty());
    CHECK(splineIn(d, "Spline1").weights == std::vector<double>{1, 3, 1, 1});
    REQUIRE(d.sk("sketch.set_weight", {{"point", std::string("Spline1.pole2")}, {"weight", 1.0}}).empty());
    CHECK(splineIn(d, "Spline1").weights.empty());  // all 1 again: none saved
    problems = d.sk("sketch.set_weight", {{"point", std::string("Spline1.pole2")}, {"weight", 0.0}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "sketch.spline_weights");
    problems = d.sk("sketch.set_weight", {{"point", std::string("Spline1.pole9")}, {"weight", 2.0}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message == "Spline1 has 4 control points: use Spline1.pole1 to Spline1.pole4.");
    REQUIRE(d.sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}}).empty());
    problems = d.sk("sketch.set_spline", {{"spline", std::string("Line1")}, {"closed", true}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message == "Line1 isn't a spline. Pick a spline, like Spline1.");
}

TEST_CASE("adding and removing control points keeps constraints on the points they were on",
          "[model][sketch][spline]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_spline", {{"poles", Points{{0, 0}, {0, 30}, {40, 30}, {40, 0}}}}).empty());
    REQUIRE(d.sk("sketch.set_weight", {{"point", std::string("Spline1.pole4")}, {"weight", 2.0}}).empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Spline1.pole3")}})
                .empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Spline1.pole1")}})
                .empty());
    auto fixedPole = [&](const char* name) {
        return d.step().sketch().findConstraint(name)->points[0].index;
    };

    // Between pole1 and pole2: halfway, unless told where.
    REQUIRE(
        d.sk("sketch.add_pole", {{"spline", std::string("Spline1")}, {"after", std::int64_t{1}}}).empty());
    auto sp = splineIn(d, "Spline1");
    REQUIRE(sp.poles.size() == 5);
    CHECK(sp.poles[1].x == 0);
    CHECK(sp.poles[1].y == 15);
    CHECK(sp.weights == std::vector<double>{1, 1, 1, 1, 2});  // the weight stayed with its point
    CHECK(fixedPole("Fix1") == 3);                            // the old pole3 is pole4 now
    CHECK(fixedPole("Fix2") == 0);
    REQUIRE(
        d.sk("sketch.add_pole",
             {{"spline", std::string("Spline1")}, {"after", std::int64_t{4}}, {"at", sketch::Point2{45, 20}}})
            .empty());
    CHECK(splineIn(d, "Spline1").poles[4].x == 45);
    CHECK(d.doc.undoLabel() == "Add a control point to Spline1");
    auto problems = d.sk("sketch.add_pole", {{"spline", std::string("Spline1")}, {"after", std::int64_t{6}}});
    REQUIRE(problems.size() == 1);  // an open spline's last point has nothing after it
    CHECK(problems[0].code == "sketch.pole_place");

    // Removing pole4 (the fixed one) takes its Fix away.
    REQUIRE(d.sk("sketch.remove_pole", {{"point", std::string("Spline1.pole4")}}).empty());
    sp = splineIn(d, "Spline1");
    REQUIRE(sp.poles.size() == 5);
    CHECK(sp.poles[3].x == 45);  // the one added after it moved down
    CHECK(d.step().sketch().findConstraint("Fix1") == nullptr);
    CHECK(fixedPole("Fix2") == 0);
    CHECK(d.doc.undoLabel() == "Remove Spline1.pole4");
    REQUIRE(d.rebuild().ok());

    // Down to the fewest for degree 3, then no more.
    REQUIRE(d.sk("sketch.remove_pole", {{"point", std::string("Spline1.pole2")}}).empty());
    problems = d.sk("sketch.remove_pole", {{"point", std::string("Spline1.pole2")}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message ==
          "Spline1 has only 4 control points, the fewest a spline of degree 3 can have. Lower its degree "
          "first (sketch.set_spline degree=2), or delete the whole spline.");

    // A closed spline can take one after its last point.
    REQUIRE(d.sk("sketch.set_spline", {{"spline", std::string("Spline1")}, {"closed", true}}).empty());
    REQUIRE(
        d.sk("sketch.add_pole", {{"spline", std::string("Spline1")}, {"after", std::int64_t{4}}}).empty());
    CHECK(splineIn(d, "Spline1").poles.size() == 5);
    // Undo puts it all back.
    REQUIRE(d.doc.undo());
    CHECK(splineIn(d, "Spline1").poles.size() == 4);
}

namespace {

// A file of points in a fresh folder, removed afterwards.
struct PointsFile {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("kh_points_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = dir / "wave.csv";
    PointsFile() { std::filesystem::create_directories(dir); }
    ~PointsFile() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const std::string& text) const { std::ofstream(path, std::ios::binary) << text; }
    std::string name() const {
        const auto u = path.u8string();
        return std::string(u.begin(), u.end());
    }
};

}  // namespace

TEST_CASE("sketch.import_spline makes a spline from a file, and remembers it; reload follows the file",
          "[model][sketch][spline][import]") {
    Drawing d;
    PointsFile file;
    file.write("x,y\n0,0\n10,18\n30,22\n40,0\n");
    REQUIRE(d.sk("sketch.import_spline", {{"file", file.name()}}).empty());
    auto spline = [&] { return splineIn(d, "Spline1"); };
    REQUIRE(spline().poles.size() == 4);
    REQUIRE(spline().source);
    CHECK(spline().source->through);
    const std::string remembered = spline().source->file;
    CHECK(std::filesystem::path(std::u8string(remembered.begin(), remembered.end())) == file.path);
    // Through the points.
    const auto at = sketch::splineThroughAt(4, 3, false);
    CHECK(sketch::splinePoint(spline(), at[1]).y == Approx(18).margin(1e-9));

    // Constrain the last control point, then the file loses a point: reloading takes the
    // constraint on the point that's gone, and keeps the one on the first.
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Spline1.pole4")}})
                .empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Spline1.pole1")}})
                .empty());
    file.write("0,0\n15,25\n40,0\n");
    REQUIRE(
        d.sk("sketch.set_spline", {{"spline", std::string("Spline1")}, {"degree", std::int64_t{2}}}).empty());
    REQUIRE(d.sk("sketch.reload_spline", {{"spline", std::string("Spline1")}}).empty());
    CHECK(spline().poles.size() == 3);
    CHECK(spline().degree == 2);  // kept
    CHECK(d.step().sketch().findConstraint("Fix1") == nullptr);
    CHECK(d.step().sketch().findConstraint("Fix2") != nullptr);
    CHECK(d.doc.undoLabel() == "Reload Spline1 from wave.csv");
    const auto top = sketch::splinePoint(spline(), sketch::splineThroughAt(3, 2, false)[1]);
    CHECK(top.y == Approx(25).margin(1e-9));
    // Undo puts the old points (and the constraint) back, without reading the file.
    REQUIRE(d.doc.undo());
    CHECK(spline().poles.size() == 4);
    CHECK(d.step().sketch().findConstraint("Fix1") != nullptr);
    REQUIRE(d.doc.redo());
    CHECK(spline().poles.size() == 3);

    // In inches, as control points.
    file.write("0 0\n1 1\n2 0\n");
    REQUIRE(d.sk("sketch.import_spline", {{"file", file.name()},
                                          {"through", false},
                                          {"unit", std::string("in")},
                                          {"name", std::string("Inches")}})
                .empty());
    CHECK(splineIn(d, "Inches").poles[1].x == Approx(25.4));
    CHECK(splineIn(d, "Inches").source->unit == LengthUnit::Inch);
    CHECK_FALSE(splineIn(d, "Inches").source->through);
}

TEST_CASE("importing and reloading say what's wrong with the file, in plain words",
          "[model][sketch][spline][import]") {
    Drawing d;
    PointsFile file;
    auto problems = d.sk("sketch.import_spline", {{"file", file.name()}});  // not written yet
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message == "Khan can't open wave.csv (looked for it at " + file.name() +
                                     "). Check the file is there, and try again.");
    file.write("0,0\n1,x\n");
    problems = d.sk("sketch.import_spline", {{"file", file.name()}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "sketch.points_file");
    CHECK(d.step().sketch().entities.empty());

    file.write("0,0\n10,10\n20,0\n30,10\n");
    REQUIRE(d.sk("sketch.import_spline", {{"file", file.name()}}).empty());
    file.write("0,0\n10,10\n");  // too few for degree 3 now
    problems = d.sk("sketch.reload_spline", {{"spline", std::string("Spline1")}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message ==
          "wave.csv has only 2 points now, too few for Spline1's degree 3. Add points to the file, or lower "
          "its "
          "degree first.");
    std::filesystem::remove(file.path);  // moved away
    problems = d.sk("sketch.reload_spline", {{"spline", std::string("Spline1")}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "sketch.points_file");
    REQUIRE(d.sk("sketch.add_spline", {{"poles", Points{{0, 0}, {5, 5}, {10, 0}}}}).empty());
    problems = d.sk("sketch.reload_spline", {{"spline", std::string("Spline2")}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message ==
          "Spline2 wasn't imported from a file, so there's nothing to reload. Make one from a file with "
          "sketch.import_spline.");
    problems = d.sk("sketch.reload_spline", {{"spline", std::string("Spline9")}});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message == "There's no spline called Spline9 in this sketch.");
}

TEST_CASE("sketch.add_rectangle adds four joined lines, level and upright, free to move 4 ways",
          "[model][sketch][rectangle]") {
    Drawing d;
    REQUIRE(d.sk("sketch.add_rectangle", {{"from", sketch::Point2{40, 20}}, {"to", sketch::Point2{0, 0}}})
                .empty());
    const auto& s = d.step().sketch();
    REQUIRE(s.entities.size() == 4);
    CHECK(s.constraints.size() == 8);
    CHECK(d.doc.undoLabel() == "Add rectangle (Line1 to Line4)");
    // Each line starts where the one before ends, round the corners (40,20) (0,20) (0,0) (40,0).
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& a = std::get<sketch::LineShape>(s.entities[i].shape);
        const auto& b = std::get<sketch::LineShape>(s.entities[(i + 1) % 4].shape);
        CHECK(a.end.x == b.start.x);
        CHECK(a.end.y == b.start.y);
    }
    CHECK(std::get<sketch::LineShape>(s.entities[1].shape).end.x == 0);
    CHECK(std::get<sketch::LineShape>(s.entities[1].shape).end.y == 0);
    const auto report = d.rebuild();
    REQUIRE(report.ok());
    const auto* r = report.result(d.sketchId);
    REQUIRE(r);
    REQUIRE(r->sketch);
    CHECK(r->sketch->freedom == 4);  // where it is, and how wide and tall
    CHECK(r->sketch->redundant.empty());
    // Two more dimensions and a fix make it fully constrained.
    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("length")}, {"on", std::string("Line1")}, {"value", 50.0}})
                .empty());
    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("length")}, {"on", std::string("Line2")}, {"value", 30.0}})
                .empty());
    REQUIRE(
        d.sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Line3.end")}}).empty());
    const auto again = d.rebuild();
    REQUIRE(again.ok());
    CHECK(again.result(d.sketchId)->sketch->freedom == 0);

    const auto flat =
        d.sk("sketch.add_rectangle", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{10, 0}}});
    REQUIRE(flat.size() == 1);
    CHECK(flat[0].message ==
          "A rectangle needs a width and a height: put the corners apart both left to right and up and down, "
          "like "
          "from=0,0 to=40,20.");
}
