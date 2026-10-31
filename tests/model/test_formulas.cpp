// Tests for sketch dimensions whose numbers come from formulas that use the
// design's parameters: setting them, following parameter changes, renames,
// undo, and rebuilding.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <memory>
#include <numbers>

#include "FakeKernel.h"
#include "kh/model/CommandRegistry.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/model/Formulas.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::model;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

// A design with parameters wall = 3 mm and width = 40 mm, and a sketch with a
// 40 x 20 mm rectangle in it (Line1 along the bottom, Line2 up the right side).
struct Drawing {
    Document doc;
    test::FakeKernel kernel;
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();
    FeatureId sketchId;

    Drawing() {
        REQUIRE(
            run("parameter.set", {{"name", std::string("wall")}, {"value", std::string("3 mm")}}).empty());
        REQUIRE(
            run("parameter.set", {{"name", std::string("width")}, {"value", std::string("40 mm")}}).empty());
        REQUIRE(run("feature.add_sketch", {}).empty());
        sketchId = doc.featureAt(0).id();
        REQUIRE(sk("sketch.add_rectangle", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{40, 20}}})
                    .empty());
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

    Diagnostics setParameter(std::string name, std::string value) {
        return doc.execute(std::make_unique<SetParameterCommand>(std::move(name), std::move(value)));
    }

    const sketch::Sketch& sketch() const {
        return static_cast<const SketchFeature&>(*doc.find(sketchId)).sketch();
    }

    const sketch::Constraint& dimension(std::string_view name) const {
        const auto* c = sketch().findConstraint(name);
        REQUIRE(c);
        return *c;
    }

    // How long a line is once the sketch is solved (mm).
    double solvedLength(std::string_view line) {
        const auto report = doc.regenerate(kernel, solver.get());
        const auto* r = report.result(sketchId);
        REQUIRE(r);
        REQUIRE(r->status == FeatureStatus::Ok);
        const auto* e = sketch().findEntity(line);
        REQUIRE(e);
        for (const auto& solved : r->sketch->entities) {
            if (solved.id != e->id) continue;
            const auto& l = std::get<sketch::LineShape>(solved.shape);
            return std::hypot(l.end.x - l.start.x, l.end.y - l.start.y);
        }
        FAIL("line not solved");
        return 0;
    }
};

}  // namespace

TEST_CASE("a dimension's formula must give the right kind of value", "[model][formulas]") {
    ParameterTable table;
    table.define({ParamId::generate(), "wall", "3 mm", ""});
    table.define({ParamId::generate(), "count", "4", ""});
    table.define({ParamId::generate(), "tilt", "15 deg", ""});
    const auto values = table.evaluate();
    using K = sketch::ConstraintKind;

    CHECK(evalDimension(K::Length, "wall * count", values, "Length1").value->value == 12);
    CHECK(evalDimension(K::Radius, "1 in", values, "Radius1").value->value == 25.4);
    CHECK(evalDimension(K::Angle, "tilt * 2", values, "Angle1").value->value ==
          Approx(30 * std::numbers::pi / 180));
    CHECK(evalDimension(K::HorizontalDistance, "-wall", values, "HDistance1").value->value ==
          -3);  // may be < 0

    auto r = evalDimension(K::Length, "count", values, "Length1");
    REQUIRE_FALSE(r.ok());
    CHECK(r.issues.front().code == "dimension.units");
    CHECK(r.issues.front().message ==
          "Length1 needs a length, like 20 mm or wall * 2, but count is a plain number.");
    r = evalDimension(K::Length, "20", values, "Length1");
    CHECK(r.issues.front().message ==
          "Length1 needs a length, like 20 mm or wall * 2, but 20 is a plain number. Give it a unit, like 20 "
          "mm.");
    r = evalDimension(K::Angle, "wall", values, "Angle1");
    CHECK_THAT(r.issues.front().message,
               StartsWith("Angle1 needs an angle, like 30 deg, but wall is a length."));
    r = evalDimension(K::Diameter, "wall - 5 mm", values, "Diameter1");
    CHECK(r.issues.front().code == "dimension.size");
    CHECK(r.issues.front().message ==
          "Diameter1 would be -2 mm (wall - 5 mm), but it must be more than 0 mm.");
    r = evalDimension(K::Length, "wal * 2", values, "Length1 in Sketch1");
    CHECK(r.issues.front().code == "expression.unknown_name");
    CHECK_THAT(r.issues.front().message, StartsWith("Length1 in Sketch1 uses wal * 2. There's no parameter"));
}

TEST_CASE("sketch.constrain and sketch.set_value take a formula, and the sketch solves to it",
          "[model][formulas]") {
    Drawing d;
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("length")},
                                      {"on", std::string("Line1")},
                                      {"formula", std::string("width + wall * 2")}})
                .empty());
    const auto& c = d.dimension("Length1");
    CHECK(c.value == 46);
    CHECK(c.expression == "width + wall * 2");
    CHECK(d.solvedLength("Line1") == Approx(46));

    REQUIRE(d.sk("sketch.constrain",
                 {{"kind", std::string("length")}, {"on", std::string("Line2")}, {"value", 20.0}})
                .empty());
    REQUIRE(d.sk("sketch.set_value",
                 {{"constraint", std::string("Length2")}, {"formula", std::string("wall * 5")}})
                .empty());
    CHECK(d.dimension("Length2").value == 15);
    CHECK(d.dimension("Length2").expression == "wall * 5");
    CHECK(d.solvedLength("Line2") == Approx(15));

    // Typing a number replaces the formula.
    REQUIRE(d.sk("sketch.set_value", {{"constraint", std::string("Length2")}, {"value", 12.0}}).empty());
    CHECK(d.dimension("Length2").value == 12);
    CHECK(d.dimension("Length2").expression.empty());
    d.doc.undo();
    CHECK(d.dimension("Length2").expression == "wall * 5");
}

TEST_CASE("a formula that can't be used for a dimension is refused, and nothing changes",
          "[model][formulas]") {
    Drawing d;
    const auto before = d.sketch().constraints.size();
    auto first = [&](Args args) {
        args["kind"] = std::string("length");
        args["on"] = std::string("Line1");
        auto issues = d.sk("sketch.constrain", std::move(args));
        REQUIRE_FALSE(issues.empty());
        return issues.front();
    };
    const auto plain = first({{"formula", std::string("5")}});
    CHECK(plain.code == "dimension.units");
    CHECK_THAT(plain.message, StartsWith("Length1 needs a length"));  // named as it would be
    CHECK(first({{"formula", std::string("walls")}}).code == "expression.unknown_name");
    CHECK(first({{"formula", std::string("wall - 1 in")}}).code == "dimension.size");
    CHECK(first({{"formula", std::string("wall")}, {"value", 3.0}}).code == "sketch.value");
    CHECK(first({}).code == "sketch.value");
    auto issues = d.sk("sketch.constrain", {{"kind", std::string("horizontal")},
                                            {"on", std::string("Line1")},
                                            {"formula", std::string("wall")}});
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().message == "Horizontal has no number. Leave out formula.");
    CHECK(d.sketch().constraints.size() == before);
}

TEST_CASE("changing a parameter changes the dimensions that use it, as one undo step", "[model][formulas]") {
    Drawing d;
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("length")},
                                      {"on", std::string("Line1")},
                                      {"formula", std::string("width")}})
                .empty());
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("length")},
                                      {"on", std::string("Line2")},
                                      {"formula", std::string("width / 2")}})
                .empty());
    const auto id = d.dimension("Length1").id;

    REQUIRE(d.setParameter("width", "60 mm").empty());
    CHECK(d.doc.undoLabel() == "Change width to 60 mm");
    CHECK(d.dimension("Length1").value == 60);
    CHECK(d.dimension("Length2").value == 30);
    CHECK(d.dimension("Length1").id == id);
    CHECK(d.solvedLength("Line1") == Approx(60));
    CHECK(d.solvedLength("Line2") == Approx(30));

    d.doc.undo();
    CHECK(d.dimension("Length1").value == 40);
    CHECK(d.dimension("Length2").value == 20);
    CHECK(d.doc.parameters().find("width")->expression == "40 mm");
    CHECK(d.solvedLength("Line1") == Approx(40));
    d.doc.redo();
    CHECK(d.dimension("Length1").value == 60);

    // A parameter the dimension uses through another parameter counts too.
    REQUIRE(d.setParameter("width", "wall * 10").empty());
    REQUIRE(d.setParameter("wall", "5 mm").empty());
    CHECK(d.dimension("Length1").value == 50);
    CHECK(d.dimension("Length2").value == 25);

    // A property: for any width, the solved lines are the width and half of it.
    for (double w : {10.0, 33.3, 120.0}) {
        REQUIRE(d.setParameter("wall", std::to_string(w / 10) + " mm").empty());
        CHECK(d.solvedLength("Line1") == Approx(w));
        CHECK(d.solvedLength("Line2") == Approx(w / 2));
    }
}

TEST_CASE("a parameter change that would break a dimension is refused", "[model][formulas]") {
    Drawing d;
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("length")},
                                      {"on", std::string("Line1")},
                                      {"formula", std::string("width - 10 mm")}})
                .empty());
    auto issues = d.setParameter("width", "5 mm");  // Length1 would be -5 mm
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().code == "parameter.breaks");
    CHECK_THAT(issues.front().message,
               StartsWith("Changing width to 5 mm would stop Length1 in Sketch1 working: Length1 in Sketch1 "
                          "would be -5 mm"));
    issues = d.setParameter("width", "40");  // a plain number
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().code == "parameter.breaks");
    CHECK(d.doc.parameters().find("width")->expression == "40 mm");
    CHECK(d.dimension("Length1").value == 30);
    CHECK(d.doc.undoLabel() != "Change width to 40");
}

TEST_CASE(
    "a dimension whose formula already didn't work doesn't block parameter changes, and warns on rebuild",
    "[model][formulas]") {
    Drawing d;
    // As if pasted from another design: a formula using a name this design doesn't have.
    REQUIRE(d.doc
                .execute(std::make_unique<EditSketchCommand>(
                    d.sketchId,
                    [](sketch::Sketch& s, std::string& label) {
                        sketch::Constraint c{ConstraintId::generate(),
                                             "Length1",
                                             sketch::ConstraintKind::Length,
                                             {},
                                             {},
                                             25,
                                             {},
                                             "gap * 2"};
                        c.entities.push_back(s.findEntity("Line1")->id);
                        s.constraints.push_back(c);
                        label = "Paste";
                        return Diagnostics{};
                    }))
                .empty());
    REQUIRE(d.setParameter("wall", "4 mm").empty());
    CHECK(d.dimension("Length1").value == 25);  // kept

    const auto report = d.doc.regenerate(d.kernel, d.solver.get());
    const auto* r = report.result(d.sketchId);
    REQUIRE(r);
    CHECK(r->status == FeatureStatus::Ok);  // it still builds, with its last value
    REQUIRE(r->issues.size() == 1);
    CHECK(r->issues.front().code == "dimension.formula");
    CHECK(r->issues.front().severity == Severity::Warning);
    CHECK_THAT(r->issues.front().message, ContainsSubstring("There's no parameter called gap"));
    CHECK_THAT(r->issues.front().message, ContainsSubstring("It keeps its last value, 25 mm."));

    // Adding the parameter fixes it, and the dimension follows.
    REQUIRE(d.setParameter("gap", "15 mm").empty());
    CHECK(d.dimension("Length1").value == 30);
}

TEST_CASE("renaming a parameter renames it in dimensions' formulas too", "[model][formulas]") {
    Drawing d;
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("length")},
                                      {"on", std::string("Line1")},
                                      {"formula", std::string("width + wall")}})
                .empty());
    REQUIRE(d.doc.execute(std::make_unique<RenameParameterCommand>("wall", "thickness")).empty());
    CHECK(d.dimension("Length1").expression == "width + thickness");
    CHECK(d.dimension("Length1").value == 43);
    d.doc.undo();
    CHECK(d.dimension("Length1").expression == "width + wall");
    d.doc.redo();
    CHECK(d.dimension("Length1").expression == "width + thickness");
}

TEST_CASE("a parameter a dimension uses can't be deleted", "[model][formulas]") {
    Drawing d;
    REQUIRE(d.sk("sketch.constrain", {{"kind", std::string("length")},
                                      {"on", std::string("Line1")},
                                      {"formula", std::string("width")}})
                .empty());
    auto issues = d.doc.execute(std::make_unique<DeleteParameterCommand>("width"));
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().message ==
          "Length1 in Sketch1 uses width, so width can't be deleted. Change Length1 in Sketch1 so it doesn't "
          "use "
          "width first, or delete Length1 in Sketch1 too.");
    CHECK(dimensionsUsing(d.sketch(), "width") == std::vector<std::string>{"Length1"});
    CHECK(dimensionsUsing(d.sketch(), "wall").empty());
    REQUIRE(d.doc.execute(std::make_unique<DeleteParameterCommand>("wall")).empty());  // nothing uses wall
}

// ---------------------------------------------------------------------------
// Steps' numbers
// ---------------------------------------------------------------------------

TEST_CASE("every step's numbers can be read and changed by key", "[model][formulas]") {
    BoxFeature box("Box1", {1, 2, 3}, {10, 20, 30});
    CylinderFeature cyl("Cyl1", {1, 2, 3}, {0, 0, 1}, 5, 8);
    MoveFeature move("Move1", box.id(), kernel::Motion{{1, 2, 3}, {4, 5, 6}, {0, 0, 1}, 0.5});
    ExtrudeFeature extrude("Extrude1", FeatureId::generate(), 7, ExtrudeSide::One);
    CHECK(box.number("size_y") == 20);
    CHECK(box.number("corner_z") == 3);
    CHECK(cyl.number("radius") == 5);
    CHECK(cyl.number("base_x") == 1);
    CHECK(move.number("center_y") == 5);
    CHECK(move.number("angle") == 0.5);
    CHECK(extrude.number("distance") == 7);
    CHECK_FALSE(box.number("radius"));
    CHECK_FALSE(box.number("size_w"));
    CHECK_FALSE(box.setNumber("sizes_x", 1));
    BooleanFeature cut("Cut1", kernel::BoolOp::Cut, box.id(), cyl.id());
    CHECK(cut.numbers().empty());

    // A property: setting each number by key and reading it back gives the
    // number, and changes nothing else.
    std::vector<Feature*> steps{&box, &cyl, &move, &extrude};
    for (Feature* f : steps) {
        const auto list = f->numbers();
        REQUIRE_FALSE(list.empty());
        for (std::size_t i = 0; i < list.size(); ++i) {
            INFO(f->name() << " " << list[i].key);
            std::vector<double> others;
            for (const auto& n : list) others.push_back(*f->number(n.key));
            REQUIRE(f->setNumber(list[i].key, 42.5 + static_cast<double>(i)));
            CHECK(f->number(list[i].key) == 42.5 + static_cast<double>(i));
            for (std::size_t j = 0; j < list.size(); ++j) {
                if (j != i) CHECK(f->number(list[j].key) == others[j]);
            }
        }
    }
    CHECK(box.size().x == 45.5);  // size_x is the 4th number
    CHECK(move.numbers().back().dim == kAngle);
    CHECK(extrude.numbers().front().positive);
}

namespace {

// A design with wall = 3 mm, a 40 x 20 mm rectangle and Extrude1, 5 mm thick.
struct Plate : Drawing {
    FeatureId extrudeId;

    Plate() {
        REQUIRE(run("feature.add_extrude", {{"sketch", sketchId}, {"distance", 5.0}}).empty());
        extrudeId = doc.featureAt(1).id();
    }

    const Feature& extrude() const { return *doc.find(extrudeId); }

    Diagnostics setValue(std::string key, Args args) {
        args["feature"] = extrudeId;
        args["setting"] = std::move(key);
        return run("feature.set_value", std::move(args));
    }
};

}  // namespace

TEST_CASE("feature.set_value sets a step's number to a number or a formula, and undoes",
          "[model][formulas]") {
    Plate d;
    REQUIRE(d.setValue("distance", {{"formula", std::string("wall * 4")}}).empty());
    CHECK(d.doc.undoLabel() == "Change distance of Extrude1");
    CHECK(d.extrude().number("distance") == 12);
    CHECK(d.extrude().formula("distance") == "wall * 4");

    REQUIRE(d.setValue("distance", {{"value", 6.0}}).empty());
    CHECK(d.extrude().number("distance") == 6);
    CHECK(d.extrude().formula("distance").empty());  // typed: no formula
    d.doc.undo();
    CHECK(d.extrude().formula("distance") == "wall * 4");
    d.doc.undo();
    CHECK(d.extrude().number("distance") == 5);
    CHECK(d.extrude().formulas().empty());
    d.doc.redo();
    CHECK(d.extrude().number("distance") == 12);

    // A Move's angle is typed in degrees, like everywhere else.
    REQUIRE(d.run("feature.add_move", {{"body", d.extrudeId}}).empty());
    const auto moveId = d.doc.featureAt(2).id();
    REQUIRE(
        d.run("feature.set_value", {{"feature", moveId}, {"setting", std::string("angle")}, {"value", 90.0}})
            .empty());
    CHECK(*d.doc.find(moveId)->number("angle") == Approx(std::numbers::pi / 2));
    REQUIRE(
        d.run("feature.set_value",
              {{"feature", moveId}, {"setting", std::string("angle")}, {"formula", std::string("45 deg")}})
            .empty());
    CHECK(*d.doc.find(moveId)->number("angle") == Approx(std::numbers::pi / 4));
}

TEST_CASE("feature.set_value refuses what can't be used, and changes nothing", "[model][formulas]") {
    Plate d;
    auto first = [&](std::string key, Args args) {
        auto issues = d.setValue(std::move(key), std::move(args));
        REQUIRE_FALSE(issues.empty());
        return issues.front();
    };
    CHECK(first("distanse", {{"value", 3.0}}).message ==
          "Extrude1 has no number called distanse. Its numbers are: distance.");
    CHECK(first("distance", {{"value", -3.0}}).code == "dimension.size");
    CHECK(first("distance", {{"value", 0.0}}).code == "dimension.size");
    CHECK(first("distance", {}).code == "feature.number");
    CHECK(first("distance", {{"value", 3.0}, {"formula", std::string("wall")}}).code == "feature.number");
    CHECK(first("distance", {{"formula", std::string("12")}}).code == "dimension.units");
    CHECK(first("distance", {{"formula", std::string("wall - 1 in")}}).code == "dimension.size");
    CHECK(first("distance", {{"formula", std::string("wal")}}).code == "expression.unknown_name");
    auto issues = d.run("feature.set_value",
                        {{"feature", d.sketchId}, {"setting", std::string("distance")}, {"value", 3.0}});
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().message == "Sketch1 has no numbers that can be changed this way.");
    CHECK(d.extrude().number("distance") == 5);
    CHECK(d.doc.undoLabel() == "Add Extrude1");
}

TEST_CASE("changing a parameter changes the step numbers that use it", "[model][formulas]") {
    Plate d;
    REQUIRE(d.setValue("distance", {{"formula", std::string("wall * 2")}}).empty());
    REQUIRE(d.setParameter("wall", "4 mm").empty());
    CHECK(d.extrude().number("distance") == 8);
    d.doc.undo();
    CHECK(d.extrude().number("distance") == 6);
    d.doc.redo();

    // A change that would make it 0 mm or less, or not a length, is refused.
    auto issues = d.setParameter("wall", "0 mm");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().code == "parameter.breaks");
    CHECK_THAT(issues.front().message,
               StartsWith("Changing wall to 0 mm would stop distance in Extrude1 working"));
    CHECK(d.extrude().number("distance") == 8);

    // Renaming follows; deleting is refused while it's used.
    REQUIRE(d.doc.execute(std::make_unique<RenameParameterCommand>("wall", "t")).empty());
    CHECK(d.extrude().formula("distance") == "t * 2");
    issues = d.doc.execute(std::make_unique<DeleteParameterCommand>("t"));
    REQUIRE_FALSE(issues.empty());
    CHECK_THAT(issues.front().message, StartsWith("distance in Extrude1 uses t, so t can't be deleted."));
    CHECK(numbersUsing(d.extrude(), "t") == std::vector<std::string>{"distance"});
}

TEST_CASE("typing over a number in Properties drops its formula; changing another number keeps it",
          "[model][formulas]") {
    Drawing d;
    REQUIRE(d.run("feature.add_box", {{"size", kernel::Vec3{10, 20, 30}}}).empty());
    const auto boxId = d.doc.featureAt(1).id();
    for (const char* key : {"size_x", "size_y"}) {
        REQUIRE(
            d.run("feature.set_value",
                  {{"feature", boxId}, {"setting", std::string(key)}, {"formula", std::string("wall * 10")}})
                .empty());
    }
    // As Properties does: a copy with one number changed.
    auto copy = d.doc.find(boxId)->clone();
    static_cast<BoxFeature&>(*copy).setSize({25, 30, 30});  // x typed over; y and z as they were
    REQUIRE(d.doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy))).empty());
    const auto& box = *d.doc.find(boxId);
    CHECK(box.formula("size_x").empty());
    CHECK(box.formula("size_y") == "wall * 10");
    REQUIRE(d.setParameter("wall", "2 mm").empty());
    const auto& after = *d.doc.find(boxId);  // the step was swapped for a new version
    CHECK(after.number("size_x") == 25);     // typed: stays
    CHECK(after.number("size_y") == 20);     // follows
    d.doc.undo();
    d.doc.undo();
    CHECK(d.doc.find(boxId)->formula("size_x") == "wall * 10");
}

TEST_CASE("a step number whose formula doesn't work keeps its value, and the rebuild warns",
          "[model][formulas]") {
    Plate d;
    auto copy = d.extrude().clone();
    copy->setFormula("distance", "gap * 2");  // as if pasted from another design
    REQUIRE(d.doc.execute(std::make_unique<EditFeatureCommand>(std::move(copy))).empty());
    REQUIRE(d.setParameter("wall", "4 mm").empty());  // not blocked by it

    const auto report = d.doc.regenerate(d.kernel, d.solver.get());
    const auto* r = report.result(d.extrudeId);
    REQUIRE(r);
    CHECK(r->status == FeatureStatus::Ok);
    REQUIRE(r->issues.size() == 1);
    CHECK(r->issues.front().code == "dimension.formula");
    CHECK(r->issues.front().severity == Severity::Warning);
    CHECK_THAT(r->issues.front().message,
               StartsWith("distance in Extrude1 uses gap * 2. There's no parameter"));
    CHECK_THAT(r->issues.front().message, ContainsSubstring("It keeps its last value, 5 mm."));

    REQUIRE(d.setParameter("gap", "1.5 mm").empty());
    CHECK(d.extrude().number("distance") == 3);
}
