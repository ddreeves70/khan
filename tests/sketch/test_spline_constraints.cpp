// Tests for constraints on splines: a point on a spline, and tangent (G1) and
// smooth (G2) joins where a spline's end meets another curve. Each checks the
// geometry of the answer, not just that it solved.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "SketchHelpers.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::sketch;
using Catch::Approx;
using kh::test::SketchBuilder;
using K = ConstraintKind;

namespace {

double length(Point2 v) {
    return std::hypot(v.x, v.y);
}

// The angle between two directions, either way along: 0 for parallel.
double offParallel(Point2 a, Point2 b) {
    return std::abs(
        std::remainder(std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y), std::numbers::pi));
}

const SplineShape& splineIn(const SolveResult& r, std::size_t i) {
    return std::get<SplineShape>(r.entities[i].shape);
}

SolveResult solved(const Sketch& s) {
    const auto r = makePlaneGcsSolver()->solve(s);
    INFO((r.issues.empty() ? std::string() : r.issues.front().message));
    REQUIRE(r.ok());
    return r;
}

}  // namespace

TEST_CASE("curveEnd measures a spline's ends like small steps along it do", "[sketch][spline][g2]") {
    const std::vector<Point2> poles{{0, 0}, {5, 18}, {22, 25}, {35, 9}, {41, 30}, {55, 4}};
    for (int degree = 1; degree <= 5; ++degree) {
        for (const bool weighted : {false, true}) {
            SplineShape s{poles,
                          weighted ? std::vector<double>{1, 2.5, 0.6, 1.4, 3, 0.8} : std::vector<double>{},
                          degree, false};
            const double spans = static_cast<double>(poles.size()) - degree;
            for (const bool atEnd : {false, true}) {
                INFO("degree " << degree << (weighted ? ", weighted" : "") << (atEnd ? ", end" : ", start"));
                const auto e = curveEnd(s, {{}, atEnd ? PointRole::End : PointRole::Start});
                REQUIRE(e);
                // Steps of h along it (in knot units: t = u / spans), going inwards.
                const double h = 1e-4;
                auto f = [&](int k) {
                    const double u = atEnd ? spans - k * h : k * h;
                    return splinePoint(s, u / spans);
                };
                const double sign = atEnd ? -1 : 1;
                const auto f0 = f(0), f1 = f(1), f2 = f(2), f3 = f(3);
                CHECK(e->at.x == Approx(f0.x).margin(1e-12));
                CHECK(e->at.y == Approx(f0.y).margin(1e-12));
                // Second-order one-sided differences.
                const Point2 d1{sign * (-3 * f0.x + 4 * f1.x - f2.x) / (2 * h),
                                sign * (-3 * f0.y + 4 * f1.y - f2.y) / (2 * h)};
                const Point2 d2{(2 * f0.x - 5 * f1.x + 4 * f2.x - f3.x) / (h * h),
                                (2 * f0.y - 5 * f1.y + 4 * f2.y - f3.y) / (h * h)};
                CHECK(e->d1.x == Approx(d1.x).margin(1e-4 * (1 + length(d1))));
                CHECK(e->d1.y == Approx(d1.y).margin(1e-4 * (1 + length(d1))));
                CHECK(e->d2.x == Approx(d2.x).margin(1e-2 * (1 + length(d2))));
                CHECK(e->d2.y == Approx(d2.y).margin(1e-2 * (1 + length(d2))));
            }
        }
    }
}

TEST_CASE("a curvature vector points into the bend, 1 / radius long; a line has none",
          "[sketch][spline][g2]") {
    const ArcShape arc{{10, 5}, 4, 0.3, 2.0};
    for (const auto role : {PointRole::Start, PointRole::End}) {
        const auto e = curveEnd(arc, {{}, role});
        REQUIRE(e);
        const auto k = curvatureVector(*e);
        CHECK(length(k) == Approx(0.25));
        CHECK((k.x * (arc.center.x - e->at.x) + k.y * (arc.center.y - e->at.y)) / 4 ==
              Approx(0.25));  // inwards
    }
    const auto line = curveEnd(LineShape{{0, 0}, {10, 3}}, {{}, PointRole::End});
    REQUIRE(line);
    CHECK(length(curvatureVector(*line)) == 0);
    CHECK_FALSE(curveEnd(CircleShape{{0, 0}, 3}, {{}, PointRole::Start}));  // no ends
    CHECK_FALSE(curveEnd(SplineShape{{{0, 0}, {9, 0}, {9, 9}, {0, 9}}, {}, 3, true}, {{}, PointRole::Start}));
}

TEST_CASE("nearestOnSpline finds the point on a spline, or the nearest one to it", "[sketch][spline]") {
    const SplineShape s{{{0, 0}, {0, 30}, {40, 30}, {40, 0}}, {}, 3, false};
    for (double t : {0.0, 0.13, 0.5, 0.77, 1.0}) {
        const auto p = splinePoint(s, t);
        CHECK(nearestOnSpline(s, p) == Approx(t).margin(1e-7));
    }
    // Off the curve: no nearby place along it is nearer.
    const Point2 off{12, 40};
    const double t = nearestOnSpline(s, off);
    auto d = [&](double u) { return std::hypot(splinePoint(s, u).x - off.x, splinePoint(s, u).y - off.y); };
    CHECK(d(t) <= d(t + 1e-4));
    CHECK(d(t) <= d(t - 1e-4));
}

TEST_CASE("a point on a spline ends up on the curve, open or closed; it takes away one way to move",
          "[sketch][spline][solver]") {
    for (const bool closed : {false, true}) {
        SketchBuilder b;
        const auto s = b.spline({{0, 0}, {0, 30}, {40, 30}, {40, 0}}, 3, closed);
        const auto p = b.point(25, 45);
        b.constrain(K::PointOnSpline, {kh::test::self(p)}, {s});
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 2 - 1);
        const Point2 at = std::get<PointShape>(r.entities[1].shape).at;
        const auto& sp = splineIn(r, 0);
        const auto on = splinePoint(sp, nearestOnSpline(sp, at));
        CHECK(std::hypot(on.x - at.x, on.y - at.y) < 1e-6);
        CHECK(error({r.entities, b.sketch.constraints}, b.sketch.constraints[0]) < 1e-6);
    }
}

TEST_CASE("tangent at a join: the spline leaves its end the way the line, arc or spline goes",
          "[sketch][spline][solver]") {
    // The arch ends at (40, 0) heading straight down; each other curve starts there heading elsewhere.
    const std::vector<Point2> arch{{0, 0}, {0, 30}, {40, 30}, {40, 0}};
    SECTION("a line") {
        SketchBuilder b;
        const auto s = b.spline(arch);
        const auto l = b.line(40, 0, 60, -10);
        b.constrain(K::Coincident, {kh::test::pole(s, 3), kh::test::start(l)}, {});
        b.constrain(K::Tangent, {}, {s, l});
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 4 - 2 - 1);
        const auto& sp = splineIn(r, 0);
        const auto& line = std::get<LineShape>(r.entities[1].shape);
        const Point2 leg{sp.poles[3].x - sp.poles[2].x, sp.poles[3].y - sp.poles[2].y};
        CHECK(offParallel(leg, {line.end.x - line.start.x, line.end.y - line.start.y}) < 1e-6);
    }
    SECTION("an arc") {
        SketchBuilder b;
        const auto s = b.spline(arch);
        const auto a = b.arc(50, 0, 10, std::numbers::pi, 1.5 * std::numbers::pi);  // starts at (40, 0)
        b.constrain(K::Coincident, {kh::test::end(s), kh::test::start(a)}, {});
        b.constrain(K::Tangent, {}, {s, a});
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 5 - 2 - 1);
        const auto es = curveEnd(r.entities[0].shape, kh::test::end(s));
        const auto ea = curveEnd(r.entities[1].shape, kh::test::start(a));
        REQUIRE(es);
        REQUIRE(ea);
        CHECK(offParallel(es->d1, ea->d1) < 1e-6);
        CHECK(std::hypot(es->at.x - ea->at.x, es->at.y - ea->at.y) < 1e-6);
    }
    SECTION("another spline") {
        SketchBuilder b;
        const auto s = b.spline(arch);
        const auto t = b.spline({{40, 0}, {55, 5}, {60, -20}, {80, -10}});
        b.constrain(K::Coincident, {kh::test::pole(s, 3), kh::test::pole(t, 0)}, {});
        b.constrain(K::Tangent, {}, {s, t});
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 8 - 2 - 1);
        const auto es = curveEnd(r.entities[0].shape, kh::test::end(s));
        const auto et = curveEnd(r.entities[1].shape, kh::test::start(t));
        REQUIRE(es);
        REQUIRE(et);
        CHECK(offParallel(es->d1, et->d1) < 1e-6);
    }
}

TEST_CASE("smooth at a join: tangent, and bending equally (the same curvature)",
          "[sketch][spline][solver][g2]") {
    const std::vector<Point2> arch{{0, 0}, {0, 30}, {40, 30}, {40, 0}};
    auto same = [](const SolveResult& r, PointRef a, PointRef b) {
        const auto ea = curveEnd(r.entities[0].shape, a);
        const auto eb = curveEnd(r.entities[1].shape, b);
        REQUIRE(ea);
        REQUIRE(eb);
        CHECK(offParallel(ea->d1, eb->d1) < 1e-6);
        const auto ka = curvatureVector(*ea), kb = curvatureVector(*eb);
        CHECK(std::hypot(ka.x - kb.x, ka.y - kb.y) < 1e-6);
        return ka;
    };
    SECTION("a line: the spline's end is straight, so its last 3 control points are in line") {
        SketchBuilder b;
        const auto s = b.spline(arch);
        const auto l = b.line(40, 0, 60, -10);
        b.constrain(K::Coincident, {kh::test::pole(s, 3), kh::test::start(l)}, {});
        b.constrain(K::Smooth, {}, {s, l});
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 4 - 2 - 2);
        CHECK(length(same(r, kh::test::end(s), kh::test::start(l))) < 1e-6);
        const auto& p = splineIn(r, 0).poles;
        CHECK(std::abs((p[2].x - p[1].x) * (p[3].y - p[1].y) - (p[2].y - p[1].y) * (p[3].x - p[1].x)) < 1e-4);
    }
    SECTION("an arc: the spline ends bending as tightly as the arc, 1 / its radius") {
        SketchBuilder b;
        const auto s = b.spline(arch);
        // The arc ends where the arch ends, bending the same way (its center is inside the arch).
        const auto a = b.arc(30, 0, 10, -std::numbers::pi / 2, 0);
        b.constrain(K::Coincident, {kh::test::end(s), kh::test::end(a)}, {});
        b.constrain(K::Smooth, {}, {s, a});
        b.constrain(K::Radius, {}, {a}, 10);
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 5 - 2 - 2 - 1);
        CHECK(length(same(r, kh::test::end(s), kh::test::end(a))) == Approx(0.1).margin(1e-6));
    }
    SECTION("an arc bending the other way: the spline's bend turns around to match it") {
        SketchBuilder b;
        const auto s = b.spline(arch);
        const auto a = b.arc(50, 0, 10, std::numbers::pi, 1.5 * std::numbers::pi);  // starts at (40, 0)
        b.constrain(K::Coincident, {kh::test::end(s), kh::test::start(a)}, {});
        b.constrain(K::Smooth, {}, {s, a});
        b.constrain(K::Radius, {}, {a}, 10);
        const auto r = solved(b.sketch);
        CHECK(length(same(r, kh::test::end(s), kh::test::start(a))) == Approx(0.1).margin(1e-6));
    }
    SECTION("another spline, with weights") {
        SketchBuilder b;
        const auto s = b.spline(arch, 3, false, {1, 2, 1, 1});
        const auto t = b.spline({{40, 0}, {45, -15}, {65, -20}, {70, 0}, {90, 5}});
        b.constrain(K::Coincident, {kh::test::pole(s, 3), kh::test::pole(t, 0)}, {});
        b.constrain(K::Smooth, {}, {s, t});
        const auto r = solved(b.sketch);
        CHECK(r.freedom == 8 + 10 - 2 - 2);
        CHECK(length(same(r, kh::test::end(s), kh::test::start(t))) > 0);  // it really bends there
    }
}

TEST_CASE("joins need joining first, and closed splines have no ends", "[sketch][spline]") {
    auto message = [](const Sketch& s) {
        return check(s).empty() ? std::string() : check(s).front().message;
    };
    SketchBuilder b;
    const auto s = b.spline({{0, 0}, {0, 30}, {40, 30}, {40, 0}});
    const auto l = b.line(40, 0, 60, -10);
    b.constrain(K::Tangent, {}, {s, l});
    CHECK(message(b.sketch) ==
          "Tangent needs the two joined end to end: join an end of each with Coincident first.");
    b.sketch.constraints[0].kind = K::Smooth;
    CHECK(message(b.sketch) ==
          "Smooth needs the two joined end to end: join an end of each with Coincident first.");
    // A join somewhere other than their ends isn't a join.
    b.constrain(K::Coincident, {kh::test::pole(s, 1), kh::test::start(l)}, {});
    CHECK_FALSE(message(b.sketch).empty());
    b.sketch.constraints[1].points[0] = kh::test::pole(s, 3);
    CHECK(message(b.sketch).empty());

    SketchBuilder c;
    const auto loop = c.spline({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 3, true);
    const auto m = c.line(0, 0, 10, 0);
    c.constrain(K::Smooth, {}, {loop, m});
    CHECK(message(c.sketch) == "Smooth needs a spline with ends and a line, arc or spline.");
    SketchBuilder d;  // Smooth is for splines
    const auto l1 = d.line(0, 0, 10, 0);
    const auto a1 = d.arc(10, 5, 5, -std::numbers::pi / 2, 0);
    d.constrain(K::Coincident, {kh::test::end(l1), kh::test::start(a1)}, {});
    d.constrain(K::Smooth, {}, {l1, a1});
    CHECK(message(d.sketch) == "Smooth needs a spline with ends and a line, arc or spline.");
    SketchBuilder e;
    const auto sp = e.spline({{0, 0}, {0, 30}, {40, 30}, {40, 0}});
    const auto pt = e.point(1, 1);
    e.constrain(K::PointOnSpline, {kh::test::self(pt)}, {pt});
    CHECK(message(e.sketch) == "Point on spline needs one point and one spline.");
    e.sketch.constraints[0].entities = {sp};
    CHECK(message(e.sketch).empty());
}

TEST_CASE("symmetric points end up mirror images across the line; it takes away 2 ways to move",
          "[sketch][solver][symmetric]") {
    SketchBuilder b;
    const auto axis = b.line(0, -10, 3, 30);  // a slanted line
    const auto p = b.point(-12, 5);
    const auto q = b.point(20, 14);
    b.constrain(K::Symmetric, {kh::test::self(p), kh::test::self(q)}, {axis});
    const auto r = solved(b.sketch);
    CHECK(r.freedom == 4 + 2 + 2 - 2);
    const auto& l = std::get<LineShape>(r.entities[0].shape);
    const auto a = std::get<PointShape>(r.entities[1].shape).at,
               c = std::get<PointShape>(r.entities[2].shape).at;
    // The middle of the two is on the line, and the line between them is square to it.
    const Point2 u{l.end.x - l.start.x, l.end.y - l.start.y};
    const Point2 mid{(a.x + c.x) / 2, (a.y + c.y) / 2};
    CHECK(std::abs((mid.x - l.start.x) * u.y - (mid.y - l.start.y) * u.x) / length(u) < 1e-9);
    CHECK(std::abs((c.x - a.x) * u.x + (c.y - a.y) * u.y) / length(u) < 1e-9);
    CHECK(length({c.x - a.x, c.y - a.y}) > 1);  // not squashed onto the line
    CHECK(error({r.entities, b.sketch.constraints}, b.sketch.constraints[0]) < 1e-9);
    // Two of the same point, or no line, can't be symmetric.
    b.sketch.constraints[0].points[1] = kh::test::self(p);
    CHECK(check(b.sketch).front().message ==
          "Symmetric needs two different points and a line to mirror them across.");
}
