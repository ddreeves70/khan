// Tests for the PlaneGCS sketch solver. Every answer is checked with Khan's own
// error() measures, so these test the solver, not just that it runs.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>

#include "SketchHelpers.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using namespace kh::sketch;
using Catch::Approx;
using kh::test::SketchBuilder;
using K = ConstraintKind;

namespace {

constexpr double kPi = std::numbers::pi;

// Solves, and checks every constraint really is true afterwards.
SolveResult solveAndCheck(const Sketch& sketch) {
    auto solver = makePlaneGcsSolver();
    auto result = solver->solve(sketch);
    INFO((result.issues.empty() ? std::string() : result.issues.front().message));
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == sketch.entities.size());
    const Sketch after{result.entities, sketch.constraints};
    for (const auto& c : sketch.constraints) {
        INFO(kindName(c.kind));
        CHECK(error(after, c) < 1e-6);
    }
    for (std::size_t i = 0; i < sketch.entities.size(); ++i) {
        CHECK(result.entities[i].id == sketch.entities[i].id);  // same entities, same order
        CHECK(result.entities[i].shape.index() == sketch.entities[i].shape.index());
    }
    return result;
}

Point2 at(const SolveResult& r, const Sketch& s, PointRef ref) {
    return *pointOf(Sketch{r.entities, s.constraints}, ref);
}

// A rough rectangle, closed with coincident corners, 40 x 20 mm, with its
// bottom-left corner fixed at the origin.
struct Rectangle {
    SketchBuilder b;
    EntityId bottom = b.line(0.5, -0.3, 37, 1);
    EntityId right = b.line(38, 0.4, 41, 18);
    EntityId top = b.line(39, 21, 2, 19);
    EntityId left = b.line(-1, 22, 0.2, 1);

    Rectangle() {
        using test::end;
        using test::start;
        b.constrain(K::Coincident, {end(bottom), start(right)}, {});
        b.constrain(K::Coincident, {end(right), start(top)}, {});
        b.constrain(K::Coincident, {end(top), start(left)}, {});
        b.constrain(K::Coincident, {end(left), start(bottom)}, {});
        b.constrain(K::Horizontal, {}, {bottom});
        b.constrain(K::Horizontal, {}, {top});
        b.constrain(K::Vertical, {}, {left});
        b.constrain(K::Vertical, {}, {right});
    }
};

}  // namespace

TEST_CASE("a fully constrained rectangle solves to its exact corners", "[sketch][planegcs]") {
    Rectangle r;
    r.b.constrain(K::Fix, {test::start(r.bottom)}, {}, 0, {0, 0});
    r.b.constrain(K::Length, {}, {r.bottom}, 40);
    r.b.constrain(K::Length, {}, {r.left}, 20);
    const auto result = solveAndCheck(r.b.sketch);
    CHECK(result.freedom == 0);
    CHECK(result.issues.empty());
    const auto corner = at(result, r.b.sketch, test::end(r.right));
    CHECK(corner.x == Approx(40).margin(1e-9));
    CHECK(corner.y == Approx(20).margin(1e-9));
}

TEST_CASE("degrees of freedom count what can still move", "[sketch][planegcs]") {
    auto freedom = [](const SketchBuilder& b) { return makePlaneGcsSolver()->solve(b.sketch).freedom; };
    SketchBuilder point;
    point.point(1, 2);
    CHECK(freedom(point) == 2);
    SketchBuilder line;
    line.line(0, 0, 10, 2);
    CHECK(freedom(line) == 4);
    SketchBuilder circle;
    circle.circle(0, 0, 5);
    CHECK(freedom(circle) == 3);
    SketchBuilder arc;
    arc.arc(0, 0, 5, 0, 1);
    CHECK(freedom(arc) == 5);  // center (2), radius, two angles

    Rectangle r;  // closed and square, but free to move and stretch
    CHECK(freedom(r.b) == 4);
    r.b.constrain(K::Fix, {test::start(r.bottom)}, {}, 0, {0, 0});
    CHECK(freedom(r.b) == 2);
    r.b.constrain(K::Length, {}, {r.bottom}, 40);
    CHECK(freedom(r.b) == 1);
}

TEST_CASE("conflicting constraints are named, and nothing moves", "[sketch][planegcs]") {
    SketchBuilder b;
    const auto l = b.line(0, 0, 20, 1);
    b.constrain(K::Fix, {test::start(l)}, {}, 0, {0, 0});
    b.constrain(K::Horizontal, {}, {l});
    const auto length = b.constrain(K::Length, {}, {l}, 25);
    const auto across = b.constrain(K::HorizontalDistance, {test::start(l), test::end(l)}, {}, 30);
    const auto result = makePlaneGcsSolver()->solve(b.sketch);
    REQUIRE(result.status == SolveStatus::Conflicting);
    CHECK(std::ranges::find(result.conflicting, length) != result.conflicting.end());
    CHECK(std::ranges::find(result.conflicting, across) != result.conflicting.end());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].code == "sketch.conflicting");
    CHECK(result.issues[0].message.find("Length") != std::string::npos);
    CHECK(result.issues[0].message.find("Horizontal distance") != std::string::npos);
    CHECK(std::get<LineShape>(result.entities[0].shape).end.x == 20);  // unchanged
}

TEST_CASE("constraints that repeat others are reported, and the sketch still solves", "[sketch][planegcs]") {
    Rectangle r;
    const auto extra = r.b.constrain(K::Parallel, {}, {r.bottom, r.top});  // both are already horizontal
    const auto result = solveAndCheck(r.b.sketch);
    REQUIRE_FALSE(result.redundant.empty());
    CHECK(std::ranges::find(result.redundant, extra) != result.redundant.end());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].severity == Severity::Warning);
    CHECK(result.issues[0].code == "sketch.redundant");
}

TEST_CASE("every kind of constraint is made true", "[sketch][planegcs]") {
    // Each starts false, from a rough drawing.
    SECTION("Fix and Coincident") {
        SketchBuilder b;
        const auto p = b.point(3, 4);
        const auto q = b.point(9, -2);
        b.constrain(K::Fix, {test::self(p)}, {}, 0, {1, 1});
        b.constrain(K::Coincident, {test::self(p), test::self(q)}, {});
        const auto r = solveAndCheck(b.sketch);
        CHECK(at(r, b.sketch, test::self(q)).x == Approx(1));
    }
    SECTION("Horizontal and Vertical") {
        SketchBuilder b;
        b.constrain(K::Horizontal, {}, {b.line(0, 0, 10, 3)});
        b.constrain(K::Vertical, {}, {b.line(0, 0, 2, 10)});
        solveAndCheck(b.sketch);
    }
    SECTION("Parallel, Perpendicular and Angle") {
        SketchBuilder b;
        const auto a = b.line(0, 0, 10, 1);
        const auto c = b.line(0, 5, 10, 7);
        const auto d = b.line(0, 0, 3, 10);
        const auto e = b.line(5, 5, 15, 8);
        b.constrain(K::Parallel, {}, {a, c});
        b.constrain(K::Perpendicular, {}, {a, d});
        b.constrain(K::Angle, {}, {a, e}, kPi / 6);  // 30 degrees
        solveAndCheck(b.sketch);
    }
    SECTION("Equal lines, and Equal circle and arc") {
        SketchBuilder b;
        b.constrain(K::Equal, {}, {b.line(0, 0, 10, 0), b.line(0, 5, 14, 5)});
        b.constrain(K::Equal, {}, {b.circle(0, 0, 3), b.arc(20, 0, 5, 0, 2)});
        b.constrain(K::Equal, {}, {b.arc(40, 0, 2, 0, 2), b.circle(60, 0, 6)});
        solveAndCheck(b.sketch);
    }
    SECTION("Point on line, point on circle, midpoint") {
        SketchBuilder b;
        const auto l = b.line(0, 0, 10, 0);
        const auto c = b.circle(0, 0, 5);
        const auto a = b.arc(20, 0, 5, 0, 2);
        b.constrain(K::PointOnLine, {test::self(b.point(4, 3))}, {l});
        b.constrain(K::PointOnCircle, {test::self(b.point(1, 1))}, {c});
        b.constrain(K::PointOnCircle, {test::self(b.point(21, 1))}, {a});
        b.constrain(K::Midpoint, {test::self(b.point(2, 2))}, {l});
        solveAndCheck(b.sketch);
    }
    SECTION("Tangent: line and circle, line and arc, two circles, circle and arc") {
        SketchBuilder b;
        b.constrain(K::Tangent, {}, {b.line(-10, 6, 10, 6), b.circle(0, 0, 4)});
        b.constrain(K::Tangent, {}, {b.arc(30, 0, 4, 0, 3), b.line(20, 7, 40, 7)});
        b.constrain(K::Tangent, {}, {b.circle(60, 0, 5), b.circle(70, 0, 3)});
        b.constrain(K::Tangent, {}, {b.circle(90, 0, 5), b.arc(101, 0, 4, 2, 4)});
        solveAndCheck(b.sketch);
    }
    SECTION("Distance, horizontal and vertical distance") {
        SketchBuilder b;
        const auto p = b.point(0, 0);
        const auto q = b.point(3, 3);
        b.constrain(K::Fix, {test::self(p)}, {}, 0, {0, 0});
        b.constrain(K::HorizontalDistance, {test::self(p), test::self(q)}, {}, -12);
        b.constrain(K::VerticalDistance, {test::self(p), test::self(q)}, {}, 5);
        const auto d = b.point(7, 1);
        b.constrain(K::Distance, {test::self(p), test::self(d)}, {}, 25);
        const auto r = solveAndCheck(b.sketch);
        CHECK(at(r, b.sketch, test::self(q)).x == Approx(-12));
        CHECK(at(r, b.sketch, test::self(q)).y == Approx(5));
    }
    SECTION("Radius and diameter") {
        SketchBuilder b;
        b.constrain(K::Radius, {}, {b.circle(0, 0, 3)}, 7.5);
        b.constrain(K::Diameter, {}, {b.circle(20, 0, 3)}, 12);
        b.constrain(K::Radius, {}, {b.arc(40, 0, 3, 0, 2)}, 4);
        b.constrain(K::Diameter, {}, {b.arc(60, 0, 3, 0, 2)}, 9);
        solveAndCheck(b.sketch);
    }
}

TEST_CASE("a tangent line stays on the side of the circle it was drawn on", "[sketch][planegcs]") {
    SketchBuilder b;
    const auto c = b.circle(0, 0, 4);
    const auto below = b.line(-10, -6, 10, -5);
    b.constrain(K::Fix, {test::center(c)}, {}, 0, {0, 0});
    b.constrain(K::Radius, {}, {c}, 4);
    b.constrain(K::Horizontal, {}, {below});
    b.constrain(K::Tangent, {}, {below, c});
    const auto r = solveAndCheck(b.sketch);
    CHECK(std::get<LineShape>(r.entities[1].shape).start.y == Approx(-4));
}

TEST_CASE("a slot: two lines joined by two tangent arcs, fully constrained", "[sketch][planegcs]") {
    // The classic slot: 30 mm between arc centers, 10 mm wide.
    SketchBuilder b;
    const auto bottom = b.line(0, -5.2, 30, -4.8);
    const auto top = b.line(31, 5.1, -1, 4.9);
    const auto rightArc = b.arc(30, 0, 5, -kPi / 2, kPi / 2);
    const auto leftArc = b.arc(0, 0, 5, kPi / 2, 3 * kPi / 2);
    using test::center;
    using test::end;
    using test::start;
    b.constrain(K::Coincident, {end(bottom), start(rightArc)}, {});
    b.constrain(K::Coincident, {end(rightArc), start(top)}, {});
    b.constrain(K::Coincident, {end(top), start(leftArc)}, {});
    b.constrain(K::Coincident, {end(leftArc), start(bottom)}, {});
    b.constrain(K::Tangent, {}, {bottom, rightArc});
    b.constrain(K::Tangent, {}, {top, rightArc});
    b.constrain(K::Tangent, {}, {top, leftArc});
    b.constrain(K::Tangent, {}, {bottom, leftArc});
    b.constrain(K::Horizontal, {}, {bottom});
    b.constrain(K::Fix, {center(leftArc)}, {}, 0, {0, 0});
    b.constrain(K::Distance, {center(leftArc), center(rightArc)}, {}, 30);
    b.constrain(K::Radius, {}, {leftArc}, 5);
    // Without this the right end could be a different size, and the top would tilt.
    const auto before = makePlaneGcsSolver()->solve(b.sketch);
    CHECK(before.freedom == 1);
    b.constrain(K::Equal, {}, {leftArc, rightArc});
    const auto r = solveAndCheck(b.sketch);
    CHECK(r.freedom == 0);
    CHECK(at(r, b.sketch, center(rightArc)).x == Approx(30));
    CHECK(at(r, b.sketch, start(bottom)).y == Approx(-5));
    CHECK(at(r, b.sketch, end(top)).y == Approx(5));
}

TEST_CASE("solving the same sketch twice gives exactly the same numbers", "[sketch][planegcs]") {
    Rectangle r;
    r.b.constrain(K::Fix, {test::start(r.bottom)}, {}, 0, {0, 0});
    r.b.constrain(K::Length, {}, {r.bottom}, 40);
    r.b.constrain(K::Angle, {}, {r.bottom, r.b.line(1, 1, 20, 9)}, 0.4);
    const auto a = makePlaneGcsSolver()->solve(r.b.sketch);
    // Scatter the heap, so the second solve's numbers live at different, unordered
    // addresses (as they do on Windows). The answer must not depend on them.
    std::vector<std::unique_ptr<char[]>> clutter;
    for (int i = 0; i < 400; ++i)
        clutter.push_back(std::make_unique<char[]>(static_cast<std::size_t>(16 + (i * 7919) % 1500)));
    for (std::size_t i = 0; i < clutter.size(); i += 2) clutter[i].reset();
    const auto c = makePlaneGcsSolver()->solve(r.b.sketch);
    REQUIRE(a.ok());
    REQUIRE(c.ok());
    for (std::size_t i = 0; i < a.entities.size(); ++i) {
        const auto& la = std::get<LineShape>(a.entities[i].shape);
        const auto& lc = std::get<LineShape>(c.entities[i].shape);
        CHECK(std::memcmp(&la, &lc, sizeof la) == 0);
    }
    CHECK(a.freedom == c.freedom);
}

TEST_CASE("a sketch with problems isn't solved, and says why", "[sketch][planegcs]") {
    SketchBuilder b;
    const auto c = b.circle(0, 0, 5);
    b.constrain(K::Horizontal, {}, {c});
    const auto result = makePlaneGcsSolver()->solve(b.sketch);
    CHECK(result.status == SolveStatus::Invalid);
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].message == "Horizontal needs one line.");

    SketchBuilder empty;
    const auto none = makePlaneGcsSolver()->solve(empty.sketch);
    CHECK(none.ok());
    CHECK(none.freedom == 0);
}

TEST_CASE("an impossible size is refused, not drawn inside out", "[sketch][planegcs]") {
    // A circle tangent to a line 6 mm away, with a 4 mm radius it isn't allowed to change,
    // and a centre that can't move either: the solver can't make it touch.
    SketchBuilder b;
    const auto circle = b.circle(0, 0, 4);
    const auto line = b.line(-10, 6, 10, 6);
    b.constrain(K::Fix, {test::center(circle)}, {}, 0, {0, 0});
    b.constrain(K::Radius, {}, {circle}, 4);
    b.constrain(K::Fix, {test::start(line)}, {}, 0, {-10, 6});
    b.constrain(K::Fix, {test::end(line)}, {}, 0, {10, 6});
    b.constrain(K::Tangent, {}, {line, circle});
    const auto result = makePlaneGcsSolver()->solve(b.sketch);
    CHECK_FALSE(result.ok());
    REQUIRE_FALSE(result.issues.empty());
    CHECK(result.issues[0].severity == Severity::Error);
}
