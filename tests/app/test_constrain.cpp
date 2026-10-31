// Tests for the sketcher's constraint rules: which buttons fit what's picked, what
// they add, and that a new dimension starts at today's size (nothing moves).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "Constrain.h"

using namespace kh;
using namespace kh::sketch;
using namespace kh::plugins::sketcher;
using Catch::Approx;
using K = ConstraintKind;

namespace {

struct Parts {
    Sketch s;
    EntityId line1 = add(LineShape{{0, 0}, {30, 0}}, "Line1");
    EntityId line2 = add(LineShape{{30, 0}, {30, 40}}, "Line2");
    EntityId circle = add(CircleShape{{10, 10}, 4}, "Circle1");
    EntityId arc = add(ArcShape{{20, 20}, 5, 0, std::numbers::pi / 2}, "Arc1");
    EntityId point = add(PointShape{{50, 50}}, "Point1");
    EntityId add(Shape shape, const char* name) {
        const auto id = EntityId::generate();
        s.entities.push_back({id, name, shape, false});
        return id;
    }
    PointRef start(EntityId e) const { return {e, PointRole::Start}; }
    PointRef end(EntityId e) const { return {e, PointRole::End}; }
};

std::vector<Rule> fitting(const Picks& p, const Sketch& s) {
    std::vector<Rule> out;
    for (auto r : allRules()) {
        if (!plan(r, p, s).empty()) out.push_back(r);
    }
    return out;
}

}  // namespace

TEST_CASE("which buttons fit what's picked", "[sketcher][constrain]") {
    Parts d;
    using R = Rule;
    SECTION("nothing picked: nothing fits") {
        CHECK(fitting({}, d.s).empty());
    }
    SECTION("one line") {
        CHECK(fitting({{}, {d.line1}, {}}, d.s) == std::vector<Rule>{R::Horizontal, R::Vertical, R::Dimension,
                                                                     R::HorizontalDimension,
                                                                     R::VerticalDimension});
    }
    SECTION("two lines") {
        CHECK(fitting({{}, {d.line1, d.line2}, {}}, d.s) == std::vector<Rule>{R::Horizontal, R::Vertical,
                                                                              R::Parallel, R::Perpendicular,
                                                                              R::Equal, R::Dimension});
    }
    SECTION("two points") {
        CHECK(fitting({{d.end(d.line1), d.start(d.line2)}, {}, {}}, d.s) ==
              std::vector<Rule>{R::Coincident, R::Fix, R::Dimension, R::HorizontalDimension,
                                R::VerticalDimension});
    }
    SECTION("a point and a line") {
        CHECK(fitting({{{d.point, PointRole::Self}}, {d.line1}, {}}, d.s) ==
              std::vector<Rule>{R::On, R::Midpoint});
    }
    SECTION("a line and a circle") {
        CHECK(fitting({{}, {d.line1}, {d.circle}}, d.s) == std::vector<Rule>{R::Tangent});
    }
    SECTION("a circle and an arc") {
        CHECK(fitting({{}, {}, {d.circle, d.arc}}, d.s) == std::vector<Rule>{R::Equal, R::Tangent});
    }
    SECTION("a picked label fits nothing") {
        Picks p{{}, {d.line1}, {}};
        p.other = 1;
        CHECK(fitting(p, d.s).empty());
    }
}

TEST_CASE("dimensions start at today's size, so nothing moves", "[sketcher][constrain]") {
    Parts d;
    auto one = [&](Rule r, const Picks& p) {
        const auto c = plan(r, p, d.s);
        REQUIRE(c.size() == 1);
        CHECK(error(Sketch{d.s.entities, {c[0]}}, c[0]) < 1e-9);  // already true
        return c[0];
    };
    CHECK(one(Rule::Dimension, {{}, {d.line1}, {}}).kind == K::Length);
    CHECK(one(Rule::Dimension, {{}, {d.line1}, {}}).value == Approx(30));
    CHECK(one(Rule::Dimension, {{}, {}, {d.circle}}).kind == K::Diameter);
    CHECK(one(Rule::Dimension, {{}, {}, {d.circle}}).value == Approx(8));
    CHECK(one(Rule::Dimension, {{}, {}, {d.arc}}).kind == K::Radius);
    CHECK(one(Rule::Dimension, {{d.start(d.line1), d.end(d.line2)}, {}, {}}).value == Approx(50));
    const auto angle = one(Rule::Dimension, {{}, {d.line1, d.line2}, {}});
    CHECK(angle.kind == K::Angle);
    CHECK(angle.value == Approx(std::numbers::pi / 2));
    CHECK(one(Rule::HorizontalDimension, {{}, {d.line1}, {}}).value == Approx(30));
    CHECK(one(Rule::VerticalDimension, {{}, {d.line2}, {}}).value == Approx(40));
    const auto fix = one(Rule::Fix, {{d.end(d.line2)}, {}, {}});
    CHECK(fix.at.x == Approx(30));
    CHECK(fix.at.y == Approx(40));
}

TEST_CASE("several lines get one constraint each, or one pair each with the first", "[sketcher][constrain]") {
    Parts d;
    const auto third = d.add(LineShape{{0, 10}, {20, 12}}, "Line3");
    CHECK(plan(Rule::Horizontal, {{}, {d.line1, d.line2, third}, {}}, d.s).size() == 3);
    const auto equal = plan(Rule::Equal, {{}, {d.line1, d.line2, third}, {}}, d.s);
    REQUIRE(equal.size() == 2);
    CHECK(equal[1].entities == std::vector<EntityId>{d.line1, third});
}

TEST_CASE("the same constraint is recognized either way round", "[sketcher][constrain]") {
    Parts d;
    const auto a = plan(Rule::Parallel, {{}, {d.line1, d.line2}, {}}, d.s)[0];
    const auto b = plan(Rule::Parallel, {{}, {d.line2, d.line1}, {}}, d.s)[0];
    CHECK(sameConstraint(a, b));
    const auto h = plan(Rule::Horizontal, {{}, {d.line1}, {}}, d.s)[0];
    CHECK_FALSE(sameConstraint(a, h));
}

TEST_CASE("labels say what a constraint is, with units", "[sketcher][constrain]") {
    Parts d;
    auto label = [&](Rule r, const Picks& p) { return labelText(plan(r, p, d.s)[0]); };
    CHECK(label(Rule::Horizontal, {{}, {d.line1}, {}}) == "H");
    CHECK(label(Rule::Perpendicular, {{}, {d.line1, d.line2}, {}}) == "90°");
    CHECK(label(Rule::Dimension, {{}, {d.line1}, {}}) == "30 mm");
    CHECK(label(Rule::Dimension, {{}, {}, {d.circle}}) == "Ø8 mm");
    CHECK(label(Rule::Dimension, {{}, {}, {d.arc}}) == "R5 mm");
    CHECK(label(Rule::Dimension, {{}, {d.line1, d.line2}, {}}) == "90°");
    CHECK(label(Rule::HorizontalDimension, {{}, {d.line1}, {}}) == "x 30 mm");
    CHECK(labelText(plan(Rule::Coincident, {{d.end(d.line1), d.start(d.line2)}, {}, {}}, d.s)[0]).empty());
    // Every labeled constraint gets a place beside what it's on.
    const auto len = plan(Rule::Dimension, {{}, {d.line1}, {}}, d.s)[0];
    auto at = labelPlace(len, d.s, 2, {15, 20});  // the sketch's middle is above Line1
    REQUIRE(at);
    CHECK(at->at.x == Approx(15));
    CHECK(at->at.y == Approx(-2));  // 2 mm below the line: outside the shape
    CHECK(at->away.y == Approx(-1));
    at = labelPlace(len, d.s, 2, {15, -20});  // ... or below it
    CHECK(at->at.y == Approx(2));
}

TEST_CASE("spline buttons: On for a point, Tangent and Smooth where a curve is joined to its end",
          "[sketcher][constrain][spline]") {
    Parts d;
    using R = Rule;
    const auto spline = d.add(SplineShape{{{30, 40}, {30, 60}, {60, 60}, {60, 40}}, {}, 3, false}, "Spline1");
    const auto other = d.add(SplineShape{{{90, 0}, {95, 9}, {99, 1}, {105, 8}}, {}, 3, false}, "Spline2");
    Picks pointAndSpline;
    pointAndSpline.points = {{d.point, PointRole::Self}};
    pointAndSpline.splines = {spline};
    CHECK(fitting(pointAndSpline, d.s) == std::vector<Rule>{R::On});
    const auto on = plan(R::On, pointAndSpline, d.s);
    REQUIRE(on.size() == 1);
    CHECK(on[0].kind == K::PointOnSpline);
    // Its own control point isn't "on" it: control points pull the curve.
    Picks ownPole = pointAndSpline;
    ownPole.points = {{spline, PointRole::Pole, 1}};
    CHECK(fitting(ownPole, d.s).empty());

    // Line2 ends where Spline1 starts, but they aren't joined yet: no Tangent or Smooth.
    Picks splineAndLine;
    splineAndLine.lines = {d.line2};
    splineAndLine.splines = {spline};
    CHECK(fitting(splineAndLine, d.s).empty());
    Constraint join{ConstraintId::generate(),
                    "Coincident1",
                    K::Coincident,
                    {{d.line2, PointRole::End}, {spline, PointRole::Pole, 0}},
                    {},
                    0,
                    {}};
    d.s.constraints.push_back(join);
    CHECK(fitting(splineAndLine, d.s) == std::vector<Rule>{R::Tangent, R::Smooth});
    const auto smooth = plan(R::Smooth, splineAndLine, d.s);
    REQUIRE(smooth.size() == 1);
    CHECK(smooth[0].kind == K::Smooth);
    CHECK(smooth[0].entities == std::vector<EntityId>{spline, d.line2});  // the spline first
    // Two splines, not joined; a spline and a circle (no ends) never.
    Picks twoSplines;
    twoSplines.splines = {spline, other};
    CHECK(fitting(twoSplines, d.s).empty());
    Picks splineAndCircle;
    splineAndCircle.rounds = {d.circle};
    splineAndCircle.splines = {spline};
    CHECK(fitting(splineAndCircle, d.s).empty());
    CHECK(labelText(smooth[0]) == "S");
    CHECK(labelText(on[0]) == "On");
}

TEST_CASE("Symmetric fits two points and a line, but not the line's own ends",
          "[sketcher][constrain][symmetric]") {
    Parts d;
    Picks p;
    p.points = {{d.point, PointRole::Self}, {d.circle, PointRole::Center}};
    p.lines = {d.line1};
    CHECK(fitting(p, d.s) == std::vector<Rule>{Rule::Symmetric});
    const auto made = plan(Rule::Symmetric, p, d.s);
    REQUIRE(made.size() == 1);
    CHECK(made[0].kind == K::Symmetric);
    CHECK(made[0].entities == std::vector<EntityId>{d.line1});
    CHECK(labelText(made[0]) == "Sym");
    p.points[1] = d.end(d.line1);
    CHECK(fitting(p, d.s).empty());
}
