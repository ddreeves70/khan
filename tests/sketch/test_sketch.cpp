// Tests for kh_sketch: lookups, check() and Khan's own error() measures.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "SketchHelpers.h"

using namespace kh;
using namespace kh::sketch;
using Catch::Approx;
using kh::test::SketchBuilder;

TEST_CASE("pointOf finds each kind of point, and only the ones an entity has", "[sketch]") {
    SketchBuilder b;
    const auto p = b.point(1, 2);
    const auto l = b.line(0, 0, 10, 0);
    const auto c = b.circle(5, 5, 2);
    const auto a = b.arc(0, 0, 10, 0, std::numbers::pi / 2);
    CHECK(pointOf(b.sketch, test::self(p))->y == 2);
    CHECK(pointOf(b.sketch, test::end(l))->x == 10);
    CHECK(pointOf(b.sketch, test::center(c))->x == 5);
    CHECK(pointOf(b.sketch, test::start(a))->x == Approx(10));
    CHECK(pointOf(b.sketch, test::end(a))->y == Approx(10));
    CHECK(pointOf(b.sketch, test::end(a))->x == Approx(0).margin(1e-12));
    CHECK_FALSE(pointOf(b.sketch, test::center(l)));  // a line has no center
    CHECK_FALSE(pointOf(b.sketch, test::start(c)));   // a circle has no ends
    CHECK_FALSE(pointOf(b.sketch, test::self(EntityId::generate())));
}

TEST_CASE("error() measures how far each constraint is from true", "[sketch]") {
    SketchBuilder b;
    const auto l1 = b.line(0, 0, 10, 3);
    const auto l2 = b.line(0, 5, 10, 5);
    const auto c1 = b.circle(0, 0, 5);
    const auto c2 = b.circle(12, 0, 4);
    const auto p = b.point(3, 4);
    auto err = [&](ConstraintKind k, std::vector<PointRef> pts, std::vector<EntityId> ents, double v = 0,
                   Point2 at = {}) {
        return error(b.sketch,
                     Constraint{ConstraintId::generate(), {}, k, std::move(pts), std::move(ents), v, at});
    };
    CHECK(err(ConstraintKind::Horizontal, {}, {l1}) == Approx(3));
    CHECK(err(ConstraintKind::Vertical, {}, {l2}) == Approx(10));
    CHECK(err(ConstraintKind::Horizontal, {}, {l2}) == 0);
    CHECK(err(ConstraintKind::Length, {}, {l2}, 10) == 0);
    CHECK(err(ConstraintKind::Length, {}, {l2}, 12) == Approx(2));
    CHECK(err(ConstraintKind::Fix, {test::self(p)}, {}, 0, {3, 0}) == Approx(4));
    CHECK(err(ConstraintKind::Distance, {test::self(p), test::center(c1)}, {}, 5) == Approx(0).margin(1e-12));
    CHECK(err(ConstraintKind::PointOnCircle, {test::self(p)}, {c1}) == Approx(0).margin(1e-12));
    CHECK(err(ConstraintKind::PointOnLine, {test::self(p)}, {l2}) == Approx(1));
    CHECK(err(ConstraintKind::Midpoint, {test::self(p)}, {l2}) == Approx(std::hypot(2.0, 1.0)));
    CHECK(err(ConstraintKind::Tangent, {}, {c1, c2}) ==
          Approx(3));  // 12 mm apart; touching from outside needs 5 + 4 = 9
    CHECK(err(ConstraintKind::Tangent, {}, {l2, c1}) ==
          Approx(0).margin(1e-12));  // y = 5 touches the radius-5 circle
    CHECK(err(ConstraintKind::Equal, {}, {c1, c2}) == Approx(1));
    CHECK(err(ConstraintKind::Radius, {}, {c1}, 5) == 0);
    CHECK(err(ConstraintKind::Diameter, {}, {c2}, 8) == 0);
    CHECK(err(ConstraintKind::HorizontalDistance, {test::start(l1), test::end(l1)}, {}, 10) == 0);
    CHECK(err(ConstraintKind::HorizontalDistance, {test::end(l1), test::start(l1)}, {}, 10) ==
          Approx(20));  // signed
    CHECK(err(ConstraintKind::VerticalDistance, {test::start(l1), test::end(l1)}, {}, 3) == 0);
    CHECK(err(ConstraintKind::Parallel, {}, {l2, l2}) == 0);
    CHECK(err(ConstraintKind::Angle, {}, {l2, l1}, std::atan2(3.0, 10.0)) == Approx(0).margin(1e-12));
    // Angles wrap around: -350 degrees is the same as +10.
    CHECK(err(ConstraintKind::Angle, {}, {l2, l1}, std::atan2(3.0, 10.0) - 2 * std::numbers::pi) ==
          Approx(0).margin(1e-12));
}

TEST_CASE("Tangent circles count as touching from outside or inside", "[sketch]") {
    SketchBuilder b;
    const auto big = b.circle(0, 0, 10);
    const auto inside = b.circle(6, 0, 4);  // touches the big circle from inside at x = 10
    const auto outside = b.circle(14, 0, 4);
    auto tangent = [&](EntityId a, EntityId c) {
        return error(b.sketch,
                     Constraint{ConstraintId::generate(), {}, ConstraintKind::Tangent, {}, {a, c}, 0, {}});
    };
    CHECK(tangent(big, inside) == Approx(0).margin(1e-12));
    CHECK(tangent(big, outside) == Approx(0).margin(1e-12));
}

TEST_CASE("check() explains constraints that use the wrong things", "[sketch]") {
    SketchBuilder b;
    const auto l = b.line(0, 0, 10, 0);
    const auto c = b.circle(0, 0, 5);
    b.constrain(ConstraintKind::Horizontal, {}, {c});   // a circle isn't a line
    b.constrain(ConstraintKind::Parallel, {}, {l, l});  // the same line twice
    b.constrain(ConstraintKind::Tangent, {}, {l, l});   // two lines can't be tangent
    b.constrain(ConstraintKind::Length, {}, {l}, 0);    // 0 mm
    b.constrain(ConstraintKind::Coincident, {test::start(l), test::start(l)}, {});  // one point twice
    b.constrain(ConstraintKind::Radius, {}, {EntityId::generate()}, 5);             // not in the sketch
    b.constrain(ConstraintKind::Fix, {test::center(l)}, {});                        // a line has no center
    const auto problems = check(b.sketch);
    REQUIRE(problems.size() == 7);
    CHECK(problems[0].message == "Horizontal needs one line.");
    CHECK(problems[1].message == "Parallel needs two different lines.");
    CHECK(problems[2].message ==
          "Tangent needs a line and a circle or arc, two circles or arcs, or a spline and a curve joined to it.");
    CHECK(problems[3].message == "A Length must be more than 0 mm. Type a bigger number.");
    CHECK(problems[4].message == "Coincident needs two different points.");
    CHECK(problems[5].code == "sketch.missing");
    CHECK(problems[6].code == "sketch.point");
}

TEST_CASE("check() finds geometry that can't be solved", "[sketch]") {
    SketchBuilder b;
    b.line(1, 1, 1, 1);  // no length
    b.circle(0, 0, 0);   // no radius
    b.arc(0, 0, -2, 0, 1);
    const auto problems = check(b.sketch);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0].code == "sketch.line_length");
    CHECK(problems[1].code == "sketch.radius");
    CHECK(problems[2].code == "sketch.radius");

    SketchBuilder ok;
    ok.line(0, 0, 1, 0);
    ok.arc(0, 0, 2, 0, 1);
    CHECK(check(ok.sketch).empty());
}
