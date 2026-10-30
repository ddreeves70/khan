// Tests for splines in sketches: where the curve goes (checked against the
// formulas it must match), naming their control points, checks, solving them,
// and closed shapes made with them.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "SketchHelpers.h"
#include "kh/sketch/Profiles.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::sketch;
using Catch::Approx;
using kh::test::SketchBuilder;
using K = ConstraintKind;

namespace {

void same(Point2 a, Point2 b) {
    CHECK(a.x == Approx(b.x).margin(1e-9));
    CHECK(a.y == Approx(b.y).margin(1e-9));
}

}  // namespace

TEST_CASE("an open cubic spline with 4 poles is a Bezier curve", "[sketch][spline]") {
    const SplineShape s{{{0, 0}, {10, 20}, {30, 20}, {40, 0}}, {}, 3, false};
    same(splinePoint(s, 0), {0, 0});  // it starts and ends at its first and last pole
    same(splinePoint(s, 1), {40, 0});
    // Bezier: B(t) = (1-t)^3 P0 + 3t(1-t)^2 P1 + 3t^2(1-t) P2 + t^3 P3.
    for (double t : {0.1, 0.25, 0.5, 0.8}) {
        const double a = std::pow(1 - t, 3), b = 3 * t * (1 - t) * (1 - t), c = 3 * t * t * (1 - t),
                     d = t * t * t;
        same(splinePoint(s, t), {a * 0 + b * 10 + c * 30 + d * 40, a * 0 + b * 20 + c * 20 + d * 0});
    }
}

TEST_CASE("a degree 1 spline is straight pieces through its poles", "[sketch][spline]") {
    const SplineShape s{{{0, 0}, {10, 0}, {10, 10}}, {}, 1, false};
    same(splinePoint(s, 0.5), {10, 0});   // two pieces: halfway is the middle pole
    same(splinePoint(s, 0.25), {5, 0});   // halfway along the first piece
    same(splinePoint(s, 0.75), {10, 5});  // and the second
}

TEST_CASE("a bigger weight pulls the curve towards its pole; weights of 1 change nothing",
          "[sketch][spline]") {
    const std::vector<Point2> poles{{0, 0}, {10, 20}, {30, 20}, {40, 0}};
    const SplineShape plain{poles, {}, 3, false};
    const SplineShape ones{poles, {1, 1, 1, 1}, 3, false};
    const SplineShape pulled{poles, {1, 5, 1, 1}, 3, false};
    same(splinePoint(ones, 0.3), splinePoint(plain, 0.3));
    const auto p = splinePoint(pulled, 0.3);
    const auto q = splinePoint(plain, 0.3);
    CHECK(std::hypot(p.x - 10, p.y - 20) < std::hypot(q.x - 10, q.y - 20));
    same(splinePoint(pulled, 0), {0, 0});  // the ends stay on the end poles
    same(splinePoint(pulled, 1), {40, 0});
}

TEST_CASE("a closed spline is a smooth loop: it ends where it starts", "[sketch][spline]") {
    // A uniform cubic B-spline round a square: symmetric, inside the square.
    const SplineShape s{{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, {}, 3, true};
    same(splinePoint(s, 0), splinePoint(s, 1));
    const auto pts = splineSample(s, 16);
    REQUIRE(pts.size() == 4 * 16 + 1);
    for (const auto& p : pts) {
        CHECK(std::abs(p.x) < 10);
        CHECK(std::abs(p.y) < 10);
    }
    // A uniform cubic B-spline's knot point is (P0 + 4 P1 + P2) / 6, and the middle
    // of a piece is (P0 + 23 P1 + 23 P2 + P3) / 48: on the right side, between
    // (10, -10) and (10, 10), that's x = (-10 + 230 + 230 - 10) / 48 = 9.17 mm,
    // the farthest right it goes.
    same(splinePoint(s, 0), {(-10 + 40 + 10) / 6.0, (10 - 40 - 10) / 6.0});
    double most = 0;
    for (const auto& p : pts) most = std::max(most, p.x);
    CHECK(most == Approx(440.0 / 48).margin(1e-9));
}

TEST_CASE("a spline's control points have names: pole1, pole2, ...", "[sketch][spline]") {
    SketchBuilder b;
    const auto s = b.spline({{0, 0}, {10, 20}, {30, 20}, {40, 0}});
    CHECK(pointKey({s, PointRole::Pole, 1}) == "pole2");
    CHECK(pointKey({s, PointRole::Start}) == "start");
    CHECK(pointFromKey(s, "pole2") == PointRef{s, PointRole::Pole, 1});
    CHECK(pointFromKey(s, "end") == PointRef{s, PointRole::End});
    CHECK_FALSE(pointFromKey(s, "pole"));
    CHECK_FALSE(pointFromKey(s, "pole0"));
    CHECK_FALSE(pointFromKey(s, "polex"));
    const auto points = pointsOf(*b.sketch.find(s));
    REQUIRE(points.size() == 4);
    CHECK(points[3] == PointRef{s, PointRole::Pole, 3});
    // Its start and end are its first and last pole; a pole it doesn't have isn't there.
    CHECK(pointOf(b.sketch, {s, PointRole::Pole, 2})->x == 30);
    CHECK(pointOf(b.sketch, {s, PointRole::End})->x == 40);
    CHECK_FALSE(pointOf(b.sketch, {s, PointRole::Pole, 4}));
    CHECK_FALSE(pointOf(b.sketch, {s, PointRole::Center}));
    // A closed spline has no ends.
    const auto loop = b.spline({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 3, true);
    CHECK_FALSE(pointOf(b.sketch, {loop, PointRole::Start}));
}

TEST_CASE("check() explains splines it can't use", "[sketch][spline]") {
    auto code = [](SplineShape s) {
        SketchBuilder b;
        b.add(std::move(s));
        const auto issues = check(b.sketch);
        return issues.empty() ? std::string() : issues.front().code;
    };
    CHECK(code({{{0, 0}, {10, 0}, {20, 5}, {30, 0}}, {}, 3, false}).empty());
    CHECK(code({{{0, 0}, {10, 0}, {20, 5}}, {}, 3, false}) == "sketch.spline_poles");  // a cubic needs 4
    CHECK(code({{{0, 0}, {10, 0}, {20, 5}, {30, 0}}, {}, 0, false}) == "sketch.spline_degree");
    CHECK(code({{{0, 0}, {10, 0}, {20, 5}, {30, 0}}, {}, 6, false}) == "sketch.spline_degree");
    CHECK(code({{{0, 0}, {10, 0}, {20, 5}, {30, 0}}, {1, 1}, 3, false}) == "sketch.spline_weights");
    CHECK(code({{{0, 0}, {10, 0}, {20, 5}, {30, 0}}, {1, 0, 1, 1}, 3, false}) == "sketch.spline_weights");
    CHECK(code({{{0, 0}, {10, 0}, {20, NAN}, {30, 0}}, {}, 3, false}) == "sketch.number");
    // A constraint on a pole it doesn't have.
    SketchBuilder b;
    const auto s = b.spline({{0, 0}, {10, 20}, {30, 20}, {40, 0}});
    b.constrain(K::Fix, {kh::test::pole(s, 7)}, {});
    REQUIRE_FALSE(check(b.sketch).empty());
    CHECK(check(b.sketch).front().code == "sketch.point");
    // A spline isn't a line or a circle.
    SketchBuilder c;
    const auto t = c.spline({{0, 0}, {10, 20}, {30, 20}, {40, 0}});
    c.constrain(K::Horizontal, {}, {t});
    CHECK(check(c.sketch).front().code == "sketch.constraint");
}

TEST_CASE("the solver moves a spline's poles to make constraints true, and counts them free",
          "[sketch][spline]") {
    SketchBuilder b;
    const auto s = b.spline({{0, 0}, {10, 20}, {30, 20}, {40, 1}});
    auto solver = makePlaneGcsSolver();
    auto r = solver->solve(b.sketch);
    REQUIRE(r.ok());
    CHECK(r.freedom == 8);  // 4 poles, each free in x and y
    CHECK(r.freePoints.size() == 4);
    CHECK(r.freePoints[2] == PointRef{s, PointRole::Pole, 2});

    // Pin the first pole, put the last one 40 mm to its right on the same level, and
    // join a line's end to the spline's end.
    b.constrain(K::Fix, {kh::test::pole(s, 0)}, {}, 0, {0, 0});
    b.constrain(K::HorizontalDistance, {kh::test::pole(s, 0), kh::test::end(s)}, {}, 40);
    b.constrain(K::VerticalDistance, {kh::test::start(s), kh::test::pole(s, 3)}, {}, 0);
    const auto l = b.line(41, 1, 40, -20);
    b.constrain(K::Coincident, {kh::test::start(l), kh::test::end(s)}, {});
    r = solver->solve(b.sketch);
    REQUIRE(r.ok());
    const Sketch after{r.entities, b.sketch.constraints};
    for (const auto& c : b.sketch.constraints) CHECK(error(after, c) < 1e-6);
    const auto& moved = std::get<SplineShape>(r.entities[0].shape);
    CHECK(moved.poles[3].x == Approx(40).margin(1e-6));
    CHECK(moved.poles[3].y == Approx(0).margin(1e-6));
    CHECK(moved.poles[1].x == Approx(10).margin(1e-6));  // the free poles stay put
    CHECK(r.freedom == 4 + 2);                           // the two middle poles, and the line's other end
    CHECK(std::ranges::find(r.freePoints, PointRef{s, PointRole::Pole, 0}) == r.freePoints.end());

    // Dragging a pole moves it, keeping the constraints.
    const auto dragged = solver->drag(b.sketch, {{{s, PointRole::Pole, 1}, {15, 30}}});
    REQUIRE(dragged.ok());
    same(std::get<SplineShape>(dragged.entities[0].shape).poles[1], {15, 30});
}

TEST_CASE("a closed spline is a shape of its own; an open one closes with other pieces",
          "[sketch][spline][profiles]") {
    SketchBuilder b;
    // Drawn clockwise: still a shape, with a positive area.
    b.spline({{-10, -10}, {-10, 10}, {10, 10}, {10, -10}}, 3, true);
    auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 1);
    CHECK(found.shapes[0].outer.edges.size() == 1);
    CHECK(found.shapes[0].outer.edges[0].reversed);  // walked counterclockwise
    // The area of the polygon of 40000 sample points is within a millionth of the curve's.
    double area = 0;
    const auto pts = splineSample(std::get<SplineShape>(b.sketch.entities[0].shape), 10000);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        area += (pts[i].x * pts[i + 1].y - pts[i + 1].x * pts[i].y) / 2;
    CHECK(found.shapes[0].area() == Approx(std::abs(area)).epsilon(1e-6));

    // An arch: a spline from (0, 0) to (40, 0), closed by a line along the bottom.
    SketchBuilder c;
    const auto arch = c.spline({{0, 0}, {0, 30}, {40, 30}, {40, 0}});
    c.line(40, 0, 0, 0);
    found = findProfiles(c.sketch);
    REQUIRE(found.shapes.size() == 1);
    CHECK(found.openEnds.empty());
    // A Bezier arch's area: the integral of y dx, which for these poles is
    // 3/5 of the 40 x 30 box around them, 720 mm^2.
    CHECK(found.shapes[0].area() == Approx(720).epsilon(1e-9));
    const auto& edges = found.shapes[0].outer.edges;
    CHECK(std::ranges::any_of(edges, [&](const LoopEdge& e) { return e.entity == arch; }));
    // Without the line, its two ends are open.
    c.sketch.entities.pop_back();
    found = findProfiles(c.sketch);
    CHECK(found.shapes.empty());
    CHECK(found.openEnds.size() == 2);
}
