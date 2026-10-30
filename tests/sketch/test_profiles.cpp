// Tests for findProfiles(): closed shapes, holes, areas and open ends. Areas are
// checked against what they must be, not just that something was found.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "SketchHelpers.h"
#include "kh/sketch/Profiles.h"

using namespace kh;
using namespace kh::sketch;
using Catch::Approx;
using kh::test::SketchBuilder;

namespace {

constexpr double kPi = std::numbers::pi;

// A closed rectangle from (x, y), w by h mm, drawn counterclockwise.
std::vector<EntityId> rectangle(SketchBuilder& b, double x, double y, double w, double h) {
    return {b.line(x, y, x + w, y), b.line(x + w, y, x + w, y + h), b.line(x + w, y + h, x, y + h),
            b.line(x, y + h, x, y)};
}

}  // namespace

TEST_CASE("a closed rectangle is one shape with its area", "[sketch][profiles]") {
    SketchBuilder b;
    rectangle(b, 0, 0, 40, 20);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 1);
    CHECK(found.shapes[0].outer.edges.size() == 4);
    CHECK(found.shapes[0].area() == Approx(800));
    CHECK(found.shapes[0].holes.empty());
    CHECK(found.openEnds.empty());
    CHECK(found.issues.empty());
}

TEST_CASE("lines drawn any which way still close, and the outline goes counterclockwise",
          "[sketch][profiles]") {
    SketchBuilder b;
    // A triangle with its lines in a muddle: one drawn backwards, out of order.
    const auto c = b.line(10, 0, 0, 0);  // backwards
    const auto a = b.line(0, 10, 0, 0);
    const auto d = b.line(10, 0, 0, 10);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 1);
    CHECK(found.shapes[0].area() == Approx(50));
    // Walking the edges as the loop says gives a positive (counterclockwise) area.
    double signedArea = 0;
    Point2 prev{};
    bool firstEdge = true;
    for (const auto& e : found.shapes[0].outer.edges) {
        const auto& l = std::get<LineShape>(b.sketch.find(e.entity)->shape);
        const Point2 from = e.reversed ? l.end : l.start, to = e.reversed ? l.start : l.end;
        if (!firstEdge)
            CHECK((std::abs(prev.x - from.x) < 1e-9 && std::abs(prev.y - from.y) < 1e-9));  // joined
        signedArea += (from.x * to.y - to.x * from.y) / 2;
        prev = to;
        firstEdge = false;
    }
    CHECK(signedArea == Approx(50));
    (void)a, (void)c, (void)d;
}

TEST_CASE("a circle inside a rectangle is a hole", "[sketch][profiles]") {
    SketchBuilder b;
    rectangle(b, 0, 0, 40, 20);
    const auto hole = b.circle(10, 10, 4);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 1);
    REQUIRE(found.shapes[0].holes.size() == 1);
    CHECK(found.shapes[0].holes[0].edges[0].entity == hole);
    CHECK(found.shapes[0].holes[0].area == Approx(kPi * 16));
    CHECK(found.shapes[0].area() == Approx(800 - kPi * 16));
}

TEST_CASE("a slot: lines and arcs joined end to end", "[sketch][profiles]") {
    SketchBuilder b;
    // Two 30 mm lines 10 mm apart, closed by half circles of radius 5.
    b.line(0, 0, 30, 0);
    b.arc(30, 5, 5, -kPi / 2, kPi / 2);
    b.line(30, 10, 0, 10);
    b.arc(0, 5, 5, kPi / 2, 3 * kPi / 2);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 1);
    CHECK(found.shapes[0].outer.edges.size() == 4);
    CHECK(found.shapes[0].area() == Approx(300 + kPi * 25));
}

TEST_CASE("a shape inside a hole is a shape of its own", "[sketch][profiles]") {
    SketchBuilder b;
    rectangle(b, 0, 0, 100, 100);
    rectangle(b, 10, 10, 80, 80);  // a hole
    rectangle(b, 30, 30, 20, 20);  // an island in the hole
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 2);
    CHECK(found.shapes[0].holes.size() == 1);
    CHECK(found.shapes[0].area() == Approx(10000 - 6400));
    CHECK(found.shapes[1].area() == Approx(400));
    CHECK(found.shapes[1].holes.empty());
}

TEST_CASE("separate shapes come in the sketch's order", "[sketch][profiles]") {
    SketchBuilder b;
    const auto first = rectangle(b, 0, 0, 10, 10);
    const auto c = b.circle(50, 0, 3);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 2);
    CHECK(found.shapes[0].area() == Approx(100));
    CHECK(found.shapes[1].outer.edges[0].entity == c);
    (void)first;
}

TEST_CASE("an unclosed outline has open ends and no shape", "[sketch][profiles]") {
    SketchBuilder b;
    const auto l1 = b.line(0, 0, 10, 0);
    b.line(10, 0, 10, 10);
    const auto l3 = b.line(10, 10, 0, 10);  // the fourth side is missing
    const auto found = findProfiles(b.sketch);
    CHECK(found.shapes.empty());
    REQUIRE(found.openEnds.size() == 2);
    CHECK(found.openEnds[0].entity == l1);
    CHECK(found.openEnds[0].role == PointRole::Start);
    CHECK(found.openEnds[1].entity == l3);
    CHECK(found.openEnds[1].role == PointRole::End);
}

TEST_CASE("three pieces meeting at one point are reported, not guessed", "[sketch][profiles]") {
    SketchBuilder b;
    rectangle(b, 0, 0, 10, 10);
    b.line(10, 10, 20, 20);  // sticks out of a corner
    const auto found = findProfiles(b.sketch);
    CHECK(found.shapes.empty());
    REQUIRE(found.issues.size() == 1);
    CHECK(found.issues[0].code == "sketch.junction");
    CHECK(found.issues[0].message.find("3 lines and arcs meet") != std::string::npos);
    CHECK(found.openEnds.size() == 1);
}

TEST_CASE("shapes that cross each other are reported", "[sketch][profiles]") {
    SketchBuilder b;
    rectangle(b, 0, 0, 20, 20);
    rectangle(b, 10, 10, 20, 20);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.issues.size() == 1);
    CHECK(found.issues[0].code == "sketch.crossing");
    CHECK(found.shapes.size() == 2);  // neither is a hole in the other
    for (const auto& s : found.shapes) CHECK(s.holes.empty());
}

TEST_CASE("helper lines and points don't make or break shapes", "[sketch][profiles]") {
    SketchBuilder b;
    rectangle(b, 0, 0, 10, 10);
    const auto helper = b.line(0, 0, 10, 10);  // a diagonal, as a helper
    b.sketch.entities.back().construction = true;
    b.point(5, 5);
    const auto found = findProfiles(b.sketch);
    REQUIRE(found.shapes.size() == 1);
    CHECK(found.shapes[0].area() == Approx(100));
    CHECK(found.issues.empty());
    (void)helper;
}

TEST_CASE("ends a hair apart (as solved) still join", "[sketch][profiles]") {
    SketchBuilder b;
    b.line(0, 0, 10, 0);
    b.line(10 + 1e-9, 0, 10, 10);
    b.line(10, 10, 0, 1e-10);
    CHECK(findProfiles(b.sketch).shapes.size() == 1);
    CHECK(findProfiles(b.sketch, 1e-12).shapes.empty());  // unless the tolerance is tighter
}
