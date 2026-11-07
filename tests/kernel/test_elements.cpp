// Faces, edges and vertices (IKernel::elements()), the history each operation
// gives (where each face of the result came from), and fillet and chamfer.
// These feed topological naming in sb_model, so every rule is checked against
// geometry worked out by hand, not just "it didn't crash".

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <set>

#include "sb/kernel_occt/OcctKernel.h"

using namespace sb::kernel;
using Catch::Approx;

namespace {

constexpr double kPi = std::numbers::pi;

Loop2 rectangle(double x, double y, double w, double h) {
    return {Edge2{Line2{{x, y}, {x + w, y}}}, Edge2{Line2{{x + w, y}, {x + w, y + h}}},
            Edge2{Line2{{x + w, y + h}, {x, y + h}}}, Edge2{Line2{{x, y + h}, {x, y}}}};
}

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool near(const Point3& a, const Point3& b, double tol = 1e-6) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol && std::abs(a.z - b.z) < tol;
}

// The face whose outward normal is `n`, or -1.
int faceFacing(const Elements& e, const Vec3& n) {
    for (std::size_t i = 0; i < e.faces.size(); ++i) {
        if (e.faces[i].geom == GeomKind::Plane && dot(e.faces[i].direction, n) > 0.999)
            return static_cast<int>(i);
    }
    return -1;
}

// The edge whose middle is at `p`, or -1.
int edgeAt(const Elements& e, const Point3& p) {
    for (std::size_t i = 0; i < e.edges.size(); ++i) {
        if (near(e.edges[i].center, p, 1e-6)) return static_cast<int>(i);
    }
    return -1;
}

bool sameElements(const Elements& a, const Elements& b) {
    if (a.faces.size() != b.faces.size() || a.edges.size() != b.edges.size() ||
        a.vertices.size() != b.vertices.size() || a.edgeFaces != b.edgeFaces ||
        a.edgeVertices != b.edgeVertices) {
        return false;
    }
    for (std::size_t i = 0; i < a.faces.size(); ++i) {
        if (!near(a.faces[i].center, b.faces[i].center) || a.faces[i].geom != b.faces[i].geom) return false;
    }
    for (std::size_t i = 0; i < a.edges.size(); ++i) {
        if (!near(a.edges[i].center, b.edges[i].center)) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("a box has 6 flat faces facing out, 12 straight edges and 8 corners", "[kernel][occt][elements]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    const auto e = k->elements(box.shape);
    REQUIRE(e.faces.size() == 6);
    REQUIRE(e.edges.size() == 12);
    REQUIRE(e.vertices.size() == 8);
    REQUIRE(e.edgeFaces.size() == 12);
    REQUIRE(e.edgeVertices.size() == 12);

    // One face facing each way, with the area and middle worked out by hand.
    const int top = faceFacing(e, {0, 0, 1});
    const int bottom = faceFacing(e, {0, 0, -1});
    const int right = faceFacing(e, {1, 0, 0});
    REQUIRE(top >= 0);
    REQUIRE(bottom >= 0);
    REQUIRE(right >= 0);
    REQUIRE(faceFacing(e, {-1, 0, 0}) >= 0);
    REQUIRE(faceFacing(e, {0, 1, 0}) >= 0);
    REQUIRE(faceFacing(e, {0, -1, 0}) >= 0);
    CHECK(e.faces[static_cast<std::size_t>(top)].size == Approx(200));
    CHECK(near(e.faces[static_cast<std::size_t>(top)].center, {5, 10, 30}));
    CHECK(e.faces[static_cast<std::size_t>(right)].size == Approx(600));
    CHECK(near(e.faces[static_cast<std::size_t>(bottom)].center, {5, 10, 0}));

    // Four edges each 10, 20 and 30 mm long, each between two faces and two corners.
    std::multiset<long> lengths;
    double total = 0;
    for (std::size_t i = 0; i < e.edges.size(); ++i) {
        CHECK(e.edges[i].geom == GeomKind::Line);
        lengths.insert(std::lround(e.edges[i].size));
        total += e.edges[i].size;
        REQUIRE(e.edgeFaces[i].size() == 2);
        CHECK(e.edgeFaces[i][0] < e.edgeFaces[i][1]);
        CHECK(e.edgeVertices[i].size() == 2);
    }
    CHECK(lengths.count(10) == 4);
    CHECK(lengths.count(20) == 4);
    CHECK(lengths.count(30) == 4);
    CHECK(total == Approx(240));

    // The edge along the top's right side borders the top and the right face.
    const int edge = edgeAt(e, {10, 10, 30});
    REQUIRE(edge >= 0);
    CHECK(e.edgeFaces[static_cast<std::size_t>(edge)] ==
          std::vector<int>{std::min(top, right), std::max(top, right)});
    for (const auto& v : e.vertices) CHECK(v.geom == GeomKind::Point);
}

TEST_CASE("a cylinder's side is round, with a seam that borders only it", "[kernel][occt][elements]") {
    auto k = makeOcctKernel();
    const auto cyl = k->makeCylinder({0, 0, 0}, {0, 0, 1}, 5, 20);
    const auto e = k->elements(cyl.shape);
    REQUIRE(e.faces.size() == 3);
    int round = -1;
    for (std::size_t i = 0; i < e.faces.size(); ++i) {
        if (e.faces[i].geom == GeomKind::Cylinder) round = static_cast<int>(i);
    }
    REQUIRE(round >= 0);
    const auto& side = e.faces[static_cast<std::size_t>(round)];
    CHECK(side.radius == Approx(5));
    CHECK(std::abs(side.direction.z) == Approx(1));
    CHECK(side.size == Approx(2 * kPi * 5 * 20));

    int circles = 0, seams = 0;
    for (std::size_t i = 0; i < e.edges.size(); ++i) {
        if (e.edges[i].geom == GeomKind::Circle) {
            ++circles;
            CHECK(e.edges[i].radius == Approx(5));
            CHECK(e.edges[i].size == Approx(2 * kPi * 5));
            CHECK(e.edgeVertices[i].size() == 1);  // a closed edge starts and ends at one vertex
        }
        if (e.edgeFaces[i] == std::vector<int>{round}) ++seams;
    }
    CHECK(circles == 2);
    CHECK(seams == 1);
}

TEST_CASE("the same shape always lists its elements in the same order", "[kernel][occt][elements]") {
    auto k = makeOcctKernel();
    auto build = [&] {
        const auto box = k->makeBox({0, 0, 0}, 40, 30, 10);
        const auto hole = k->makeCylinder({20, 15, -1}, {0, 0, 1}, 4, 12);
        return k->elements(k->boolean(BoolOp::Cut, box.shape, hole.shape).shape);
    };
    CHECK(sameElements(build(), build()));
    CHECK(k->elements(ShapeHandle()).faces.empty());
}

TEST_CASE("each edge of a face says which outline curve it was made from",
          "[kernel][occt][elements][history]") {
    auto k = makeOcctKernel();
    // Curves 0 to 3: the rectangle; curve 4: the hole.
    const Profile2 plate{rectangle(0, 0, 40, 20), {Loop2{Edge2{Circle2{{10, 10}, 4}, true}}}};
    const auto face = k->makeFace(plate, Plane{});
    REQUIRE(face.ok());
    const auto e = k->elements(face.shape);
    REQUIRE(face.history.edges.size() == e.edges.size());
    // Where each curve's middle is.
    const std::vector<Point3> middles{{20, 0, 0}, {40, 10, 0}, {20, 20, 0}, {0, 10, 0}};
    std::set<int> seen;
    for (std::size_t i = 0; i < e.edges.size(); ++i) {
        REQUIRE(face.history.edges[i].size() == 1);
        const Origin o = face.history.edges[i][0];
        CHECK(o.kind == OriginKind::Curve);
        seen.insert(o.index);
        if (o.index < 4) {
            CHECK(near(e.edges[i].center, middles[static_cast<std::size_t>(o.index)]));
        } else {
            CHECK(o.index == 4);
            CHECK(e.edges[i].geom == GeomKind::Circle);
        }
    }
    CHECK(seen == std::set<int>{0, 1, 2, 3, 4});
}

TEST_CASE("an extrude's sides come from the face's edges, and its ends are marked",
          "[kernel][occt][elements][history]") {
    auto k = makeOcctKernel();
    const auto face = k->makeFace({rectangle(0, 0, 40, 20), {}}, Plane{});
    const auto faceParts = k->elements(face.shape);
    const auto solid = k->extrude(face.shape, {0, 0, 10});
    REQUIRE(solid.ok());
    const auto e = k->elements(solid.shape);
    REQUIRE(solid.history.faces.size() == 6);
    int starts = 0, ends = 0;
    for (std::size_t i = 0; i < e.faces.size(); ++i) {
        REQUIRE(solid.history.faces[i].size() == 1);
        const Origin o = solid.history.faces[i][0];
        if (o.kind == OriginKind::Start) {
            ++starts;
            CHECK(e.faces[i].center.z == Approx(0).margin(1e-9));
        } else if (o.kind == OriginKind::End) {
            ++ends;
            CHECK(e.faces[i].center.z == Approx(10));
        } else {
            // A side stands on the edge it came from: the same x and y in the middle.
            REQUIRE(o.kind == OriginKind::Edge);
            const auto& edge = faceParts.edges.at(static_cast<std::size_t>(o.index));
            CHECK(e.faces[i].center.x == Approx(edge.center.x).margin(1e-9));
            CHECK(e.faces[i].center.y == Approx(edge.center.y).margin(1e-9));
            CHECK(e.faces[i].center.z == Approx(5));
        }
    }
    CHECK(starts == 1);
    CHECK(ends == 1);
}

TEST_CASE("a cut keeps track of which body each face came from", "[kernel][occt][elements][history]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 40, 30, 10);
    const auto tool = k->makeCylinder({20, 15, -1}, {0, 0, 1}, 4, 12);
    const auto boxParts = k->elements(box.shape);
    const auto cut = k->boolean(BoolOp::Cut, box.shape, tool.shape);
    REQUIRE(cut.ok());
    const auto e = k->elements(cut.shape);
    REQUIRE(e.faces.size() == 7);
    REQUIRE(cut.history.faces.size() == 7);
    int fromTool = 0;
    for (std::size_t i = 0; i < e.faces.size(); ++i) {
        REQUIRE(cut.history.faces[i].size() == 1);
        const Origin o = cut.history.faces[i][0];
        CHECK(o.kind == OriginKind::Face);
        if (o.input == 1) {
            ++fromTool;
            CHECK(e.faces[i].geom == GeomKind::Cylinder);  // the hole's wall, from the cylinder's side
        } else {
            // A box face keeps facing the same way.
            CHECK(dot(e.faces[i].direction, boxParts.faces.at(static_cast<std::size_t>(o.index)).direction) ==
                  Approx(1));
        }
    }
    CHECK(fromTool == 1);
    // The top lost the hole's area.
    const int top = faceFacing(e, {0, 0, 1});
    REQUIRE(top >= 0);
    CHECK(e.faces[static_cast<std::size_t>(top)].size == Approx(1200 - kPi * 16));
}

TEST_CASE("moving a body keeps each face's number", "[kernel][occt][elements][history]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    const auto before = k->elements(box.shape);
    const auto moved = k->transform(box.shape, Motion{{5, 0, 0}, {0, 0, 0}, {0, 0, 1}, kPi / 2});
    REQUIRE(moved.ok());
    const auto after = k->elements(moved.shape);
    REQUIRE(moved.history.faces.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        REQUIRE(moved.history.faces[i].size() == 1);
        const int from = moved.history.faces[i][0].index;
        CHECK(after.faces[i].size == Approx(before.faces.at(static_cast<std::size_t>(from)).size));
    }
}

TEST_CASE("a fillet rounds an edge into a round face of its radius", "[kernel][occt][fillet]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    const auto parts = k->elements(box.shape);
    const int edge = edgeAt(parts, {10, 10, 30});  // along the top's right side, 20 mm long
    REQUIRE(edge >= 0);
    const auto round = k->fillet(box.shape, {edge}, 2);
    REQUIRE(round.ok());
    CHECK(k->validate(round.shape).empty());
    // Each mm along the edge loses a 2 x 2 square less a quarter circle.
    const double lost = (4 - kPi) * 20;
    CHECK(k->massProps(round.shape).volume == Approx(6000 - lost).epsilon(1e-9));

    const auto e = k->elements(round.shape);
    REQUIRE(e.faces.size() == 7);
    REQUIRE(round.history.faces.size() == 7);
    int rounds = 0;
    for (std::size_t i = 0; i < e.faces.size(); ++i) {
        REQUIRE_FALSE(round.history.faces[i].empty());
        const Origin o = round.history.faces[i][0];
        if (o.kind == OriginKind::Edge) {
            ++rounds;
            CHECK(o.index == edge);
            CHECK(e.faces[i].geom == GeomKind::Cylinder);
            CHECK(e.faces[i].radius == Approx(2));
            CHECK(e.faces[i].size == Approx(kPi * 20));  // a quarter of a 2 mm circle, 20 mm long
        } else {
            CHECK(o.kind == OriginKind::Face);
        }
    }
    CHECK(rounds == 1);
}

TEST_CASE("fillets on several edges meet at the corners", "[kernel][occt][fillet]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 40, 30, 10);
    const auto parts = k->elements(box.shape);
    // The four upright edges, each 10 mm long.
    std::vector<int> upright;
    for (std::size_t i = 0; i < parts.edges.size(); ++i) {
        if (std::abs(parts.edges[i].direction.z) > 0.999) upright.push_back(static_cast<int>(i));
    }
    REQUIRE(upright.size() == 4);
    const auto round = k->fillet(box.shape, upright, 5);
    REQUIRE(round.ok());
    CHECK(k->validate(round.shape).empty());
    CHECK(k->massProps(round.shape).volume == Approx(12000 - 4 * (25 - kPi * 25 / 4) * 10).epsilon(1e-9));
    CHECK(k->elements(round.shape).faces.size() == 10);
}

TEST_CASE("a chamfer cuts an edge off with a flat face", "[kernel][occt][chamfer]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    const auto parts = k->elements(box.shape);
    const int edge = edgeAt(parts, {10, 10, 30});
    REQUIRE(edge >= 0);
    const auto bevel = k->chamfer(box.shape, {edge}, 3);
    REQUIRE(bevel.ok());
    CHECK(k->validate(bevel.shape).empty());
    CHECK(k->massProps(bevel.shape).volume ==
          Approx(6000 - 4.5 * 20).epsilon(1e-9));  // a 3 x 3 triangle, 20 long
    const auto e = k->elements(bevel.shape);
    int bevels = 0;
    for (std::size_t i = 0; i < e.faces.size(); ++i) {
        REQUIRE_FALSE(bevel.history.faces[i].empty());
        if (bevel.history.faces[i][0].kind != OriginKind::Edge) continue;
        ++bevels;
        CHECK(e.faces[i].geom == GeomKind::Plane);
        CHECK(e.faces[i].size == Approx(3 * std::sqrt(2.0) * 20));
        CHECK(e.faces[i].direction.x == Approx(std::sqrt(0.5)));  // facing up and to the right
        CHECK(e.faces[i].direction.z == Approx(std::sqrt(0.5)));
    }
    CHECK(bevels == 1);
}

TEST_CASE("fillets and chamfers that can't be made say why", "[kernel][occt][fillet][chamfer]") {
    auto k = makeOcctKernel();
    const auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    CHECK(k->fillet(box.shape, {}, 2).issues.at(0).code == "kernel.fillet.edges");
    CHECK(k->fillet(box.shape, {12}, 2).issues.at(0).code == "kernel.fillet.edge");
    CHECK(k->fillet(box.shape, {-1}, 2).issues.at(0).code == "kernel.fillet.edge");
    CHECK(k->fillet(box.shape, {0}, 0).issues.at(0).code == "kernel.fillet.size");
    CHECK(k->chamfer(box.shape, {0}, -1).issues.at(0).code == "kernel.chamfer.size");
    CHECK(k->fillet(ShapeHandle(), {0}, 2).issues.at(0).code == "kernel.fillet.input");
    // Rounding a 10 mm wide face's edges by 20 mm can't fit.
    const auto tooBig = k->fillet(box.shape, {0, 1, 2, 3}, 20);
    REQUIRE_FALSE(tooBig.ok());
    CHECK(tooBig.issues.at(0).code == "kernel.fillet.failed");
    CHECK(tooBig.issues.at(0).message.find("smaller") != std::string::npos);
}
