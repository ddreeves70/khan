#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>

#include "kh/kernel_occt/OcctKernel.h"

using namespace kh::kernel;
using Catch::Approx;

TEST_CASE("Box has the expected volume and bounds", "[kernel][occt]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 60, 30, 4);
    REQUIRE(box.ok());

    REQUIRE(k->massProps(box.shape).volume == Approx(60.0 * 30.0 * 4.0));
    const auto b = k->bounds(box.shape);
    REQUIRE(b.max.x - b.min.x == Approx(60.0).margin(1e-6));
}

TEST_CASE("Invalid sizes are reported, not thrown", "[kernel][occt]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 0, 10, 10);
    REQUIRE_FALSE(box.ok());
    REQUIRE(box.issues.size() == 1);
    REQUIRE(box.issues[0].code == "kernel.box.size");
}

// Bug: a 0, 0, 0 axis reached OCCT and came back as "Standard_ConstructionError:
// gp_Dir() - input vector has zero norm".
TEST_CASE("A cylinder with no axis direction gets a plain message", "[kernel][occt]") {
    auto kernel = kh::kernel::makeOcctKernel();
    const auto cyl = kernel->makeCylinder({0, 0, 0}, {0, 0, 0}, 5, 20);
    REQUIRE_FALSE(cyl.ok());
    REQUIRE(cyl.issues[0].code == "kernel.cylinder.axis");
    REQUIRE(cyl.issues[0].detail.empty());
}

TEST_CASE("Cutting a through-hole removes its volume", "[kernel][occt]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 60, 30, 4);
    auto hole = k->makeCylinder({9, 15, -1}, {0, 0, 1}, 2.5, 6);
    auto cut = k->boolean(BoolOp::Cut, box.shape, hole.shape);
    REQUIRE(cut.ok());

    const double expected = 60.0 * 30.0 * 4.0 - std::numbers::pi * 2.5 * 2.5 * 4.0;
    REQUIRE(k->massProps(cut.shape).volume == Approx(expected).epsilon(1e-6));
}

TEST_CASE("Box topology and validity", "[kernel][occt]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    const auto t = k->topology(box.shape);
    REQUIRE(t.solids == 1);
    REQUIRE(t.shells == 1);
    REQUIRE(t.faces == 6);
    REQUIRE(t.edges == 12);
    REQUIRE(t.vertices == 8);
    REQUIRE(k->validate(box.shape).empty());
}

TEST_CASE("Cylinder bounds are tight", "[kernel][occt]") {
    auto k = makeOcctKernel();
    auto cyl = k->makeCylinder({0, 0, 0}, {0, 0, 1}, 5, 10);
    const auto b = k->bounds(cyl.shape);
    REQUIRE(b.min.x == Approx(-5.0).margin(1e-6));
    REQUIRE(b.max.x == Approx(5.0).margin(1e-6));
    REQUIRE(b.max.z == Approx(10.0).margin(1e-6));
}

TEST_CASE("Validating a missing shape reports a problem", "[kernel][occt]") {
    auto k = makeOcctKernel();
    const auto issues = k->validate(ShapeHandle{});
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].code == "shape.missing");
}

TEST_CASE("Tessellation and STL export produce output", "[kernel][occt]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 10, 10, 10);
    const auto mesh = k->tessellate(box.shape, {});
    REQUIRE(mesh.triangles.size() >= 12);

    const auto path = std::filesystem::temp_directory_path() / "kh_test_box.stl";
    REQUIRE(k->exportFile(box.shape, ExportFormat::Stl, path).empty());
    REQUIRE(std::filesystem::file_size(path) > 0);
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Moving (transform)
// ---------------------------------------------------------------------------

TEST_CASE("Moving keeps volume, area, topology and validity, and shifts the bounds", "[kernel][occt][move]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 60, 30, 4);
    auto hole = k->makeCylinder({9, 15, -1}, {0, 0, 1}, 2.5, 6);
    auto tag = k->boolean(BoolOp::Cut, box.shape, hole.shape);
    REQUIRE(tag.ok());

    Motion m;
    m.offset = {10, -20, 5};
    auto moved = k->transform(tag.shape, m);
    REQUIRE(moved.ok());
    CHECK(k->validate(moved.shape).empty());
    CHECK(k->massProps(moved.shape).volume == Approx(k->massProps(tag.shape).volume).epsilon(1e-9));
    CHECK(k->massProps(moved.shape).area == Approx(k->massProps(tag.shape).area).epsilon(1e-9));
    const auto t0 = k->topology(tag.shape);
    const auto t1 = k->topology(moved.shape);
    CHECK(t1.faces == t0.faces);
    CHECK(t1.edges == t0.edges);
    CHECK(t1.vertices == t0.vertices);
    const auto b = k->bounds(moved.shape);
    CHECK(b.min.x == Approx(10.0).margin(1e-6));
    CHECK(b.min.y == Approx(-20.0).margin(1e-6));
    CHECK(b.max.z == Approx(9.0).margin(1e-6));
    // The original isn't changed.
    CHECK(k->bounds(tag.shape).min.x == Approx(0.0).margin(1e-6));
}

TEST_CASE("Turning happens about the given line, before the slide", "[kernel][occt][move]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 10, 20, 30);
    Motion m;
    m.angle = std::numbers::pi / 2;  // 90 degrees
    m.axis = {0, 0, 1};
    m.center = {10, 0, 0};  // turn about the box's corner at x = 10
    m.offset = {0, 0, 7};
    auto moved = k->transform(box.shape, m);
    REQUIRE(moved.ok());
    // About the line x = 10, y = 0: (x, y) -> (10 - y, x - 10). The box spans
    // x 0..10, y 0..20, so it ends up at x -10..10, y -10..0, then z 7..37.
    const auto b = k->bounds(moved.shape);
    CHECK(b.min.x == Approx(-10.0).margin(1e-6));
    CHECK(b.max.x == Approx(10.0).margin(1e-6));
    CHECK(b.min.y == Approx(-10.0).margin(1e-6));
    CHECK(b.max.y == Approx(0.0).margin(1e-6));
    CHECK(b.min.z == Approx(7.0).margin(1e-6));
    CHECK(k->massProps(moved.shape).volume == Approx(6000.0).epsilon(1e-9));
    CHECK(k->validate(moved.shape).empty());
}

TEST_CASE("Moving the same way twice gives the same shape", "[kernel][occt][move]") {
    auto k = makeOcctKernel();
    auto cyl = k->makeCylinder({0, 0, 0}, {0, 0, 1}, 5, 10);
    Motion m{{3, 4, 5}, {1, 2, 3}, {1, 1, 0}, 0.7};
    auto a = k->transform(cyl.shape, m);
    auto b = k->transform(cyl.shape, m);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    const auto ba = k->bounds(a.shape);
    const auto bb = k->bounds(b.shape);
    CHECK(ba.min.x == bb.min.x);
    CHECK(ba.max.z == bb.max.z);
    CHECK(k->massProps(a.shape).volume == k->massProps(b.shape).volume);
}

TEST_CASE("Turning about an axis of 0, 0, 0 gets a plain message; not turning needs no axis",
          "[kernel][occt][move]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 10, 10, 10);
    Motion m;
    m.axis = {0, 0, 0};
    m.angle = 0.5;
    auto bad = k->transform(box.shape, m);
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.issues[0].code == "kernel.move.axis");
    CHECK(bad.issues[0].detail.empty());

    m.angle = 0;  // only sliding: the axis doesn't matter
    m.offset = {1, 0, 0};
    CHECK(k->transform(box.shape, m).ok());

    CHECK(k->transform(ShapeHandle{}, Motion{}).issues.at(0).code == "kernel.move.input");
}

TEST_CASE("A moved shape exports and meshes", "[kernel][occt][move]") {
    auto k = makeOcctKernel();
    auto box = k->makeBox({0, 0, 0}, 10, 10, 10);
    Motion m;
    m.angle = 0.3;
    m.axis = {1, 2, 3};
    auto moved = k->transform(box.shape, m);
    REQUIRE(moved.ok());
    const auto mesh = k->tessellate(moved.shape, MeshQuality{});
    CHECK(mesh.triangles.size() == 12);
}

// --- Faces from flat outlines, and extrude ------------------------------------------

namespace {

constexpr double kPi = std::numbers::pi;

// A w by h rectangle from (x, y), counterclockwise.
Loop2 rectangle(double x, double y, double w, double h) {
    return {Edge2{Line2{{x, y}, {x + w, y}}}, Edge2{Line2{{x + w, y}, {x + w, y + h}}},
            Edge2{Line2{{x + w, y + h}, {x, y + h}}}, Edge2{Line2{{x, y + h}, {x, y}}}};
}

const Plane kGround{};  // XY: x right, y up, normal +z

}  // namespace

TEST_CASE("a rectangle extruded 10 mm is a 40 x 20 x 10 block", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    const auto face = k->makeFace({rectangle(0, 0, 40, 20), {}}, kGround);
    REQUIRE(face.ok());
    const auto solid = k->extrude(face.shape, {0, 0, 10});
    REQUIRE(solid.ok());
    CHECK(k->massProps(solid.shape).volume == Approx(8000));
    const auto b = k->bounds(solid.shape);
    CHECK(b.min.x == Approx(0).margin(1e-7));
    CHECK(b.max.x == Approx(40));
    CHECK(b.max.y == Approx(20));
    CHECK(b.min.z == Approx(0).margin(1e-7));
    CHECK(b.max.z == Approx(10));
    const auto t = k->topology(solid.shape);
    CHECK(t.solids == 1);
    CHECK(t.faces == 6);
    CHECK(k->validate(solid.shape).empty());
}

TEST_CASE("a hole in the face goes through the solid", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    // The hole runs clockwise, as findProfiles() gives holes.
    const Profile2 plate{rectangle(0, 0, 40, 20), {Loop2{Edge2{Circle2{{10, 10}, 4}, true}}}};
    const auto solid = k->extrude(k->makeFace(plate, kGround).shape, {0, 0, 5});
    REQUIRE(solid.ok());
    CHECK(k->massProps(solid.shape).volume == Approx((800 - kPi * 16) * 5));
    CHECK(k->topology(solid.shape).faces == 7);  // 6 sides of the block and the hole's wall
    CHECK(k->validate(solid.shape).empty());
}

TEST_CASE("a slot of lines and arcs extrudes to its exact volume", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    // 30 mm lines 10 mm apart, closed by half circles of radius 5, counterclockwise.
    const Loop2 slot{Edge2{Line2{{0, 0}, {30, 0}}}, Edge2{Arc2{{30, 5}, 5, -kPi / 2, kPi / 2}},
                     Edge2{Line2{{30, 10}, {0, 10}}}, Edge2{Arc2{{0, 5}, 5, kPi / 2, 3 * kPi / 2}}};
    const auto solid = k->extrude(k->makeFace({slot, {}}, kGround).shape, {0, 0, 2});
    REQUIRE(solid.ok());
    CHECK(k->massProps(solid.shape).volume == Approx((300 + kPi * 25) * 2));
    CHECK(k->validate(solid.shape).empty());
}

TEST_CASE("an outline that runs backwards along some edges still makes a face", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    // The triangle (0,0) -> (10,0) -> (0,10), with two of its lines stored the other way round.
    const Loop2 tri{Edge2{Line2{{10, 0}, {0, 0}}, true}, Edge2{Line2{{10, 0}, {0, 10}}},
                    Edge2{Line2{{0, 0}, {0, 10}}, true}};
    const auto solid = k->extrude(k->makeFace({tri, {}}, kGround).shape, {0, 0, 3});
    REQUIRE(solid.ok());
    CHECK(k->massProps(solid.shape).volume == Approx(150));
}

TEST_CASE("extrude goes either way, and works on any plane", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    SECTION("downwards") {
        const auto solid = k->extrude(k->makeFace({rectangle(0, 0, 10, 10), {}}, kGround).shape, {0, 0, -4});
        REQUIRE(solid.ok());
        CHECK(k->massProps(solid.shape).volume == Approx(400));
        CHECK(k->bounds(solid.shape).min.z == Approx(-4));
        CHECK(k->validate(solid.shape).empty());
    }
    SECTION("on XZ (standing, seen from the front), out towards the viewer") {
        const Plane front{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};  // normal: x * z = -y
        const auto face = k->makeFace({rectangle(0, 0, 30, 10), {}}, front);
        REQUIRE(face.ok());
        const auto b = k->bounds(face.shape);
        CHECK(b.max.z == Approx(10));  // the sketch's y is the design's z
        const auto solid = k->extrude(face.shape, {0, -6, 0});
        REQUIRE(solid.ok());
        CHECK(k->massProps(solid.shape).volume == Approx(1800));
        CHECK(k->bounds(solid.shape).min.y == Approx(-6));
    }
    SECTION("on a plane away from the origin") {
        const Plane up{{5, 5, 20}, {1, 0, 0}, {0, 1, 0}};
        const auto solid = k->extrude(k->makeFace({rectangle(0, 0, 10, 10), {}}, up).shape, {0, 0, 1});
        REQUIRE(solid.ok());
        CHECK(k->bounds(solid.shape).min.x == Approx(5));
        CHECK(k->bounds(solid.shape).min.z == Approx(20));
    }
}

TEST_CASE("ends a hair apart join; ends far apart get a plain message", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    auto loop = rectangle(0, 0, 10, 10);
    std::get<Line2>(loop[1].curve).start.x += 2e-6;  // as a solver might leave it
    const auto ok = k->makeFace({loop, {}}, kGround);
    REQUIRE(ok.ok());
    CHECK(k->validate(k->extrude(ok.shape, {0, 0, 1}).shape).empty());

    std::get<Line2>(loop[1].curve).start.x += 0.5;  // a real gap
    const auto gap = k->makeFace({loop, {}}, kGround);
    CHECK_FALSE(gap.ok());
    REQUIRE(gap.issues.size() == 1);
    CHECK(gap.issues[0].code == "kernel.face.outline");
    CHECK(gap.issues[0].message.find("isn't closed") != std::string::npos);
}

TEST_CASE("faces and extrudes that can't be made say why", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    CHECK(k->makeFace({}, kGround).issues.at(0).code == "kernel.face.outline");
    const Loop2 dot{Edge2{Line2{{1, 1}, {1, 1}}}, Edge2{Line2{{1, 1}, {1, 1}}}};
    CHECK(k->makeFace({dot, {}}, kGround).issues.at(0).message.find("no length") != std::string::npos);
    const Plane skew{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};  // not square
    CHECK(k->makeFace({rectangle(0, 0, 1, 1), {}}, skew).issues.at(0).code == "kernel.face.plane");
    const auto face = k->makeFace({rectangle(0, 0, 10, 10), {}}, kGround);
    CHECK(k->extrude(face.shape, {0, 0, 0}).issues.at(0).code == "kernel.extrude.distance");
    CHECK(k->extrude(face.shape, {5, 0, 0}).issues.at(0).code == "kernel.extrude.direction");  // along the face
    const auto box = k->makeBox({0, 0, 0}, 1, 1, 1);
    CHECK(k->extrude(box.shape, {0, 0, 1}).issues.at(0).code == "kernel.extrude.input");  // not a face
}

TEST_CASE("extruding the same face twice gives the same solid", "[kernel][occt][extrude]") {
    auto k = kh::kernel::makeOcctKernel();
    const Profile2 plate{rectangle(0, 0, 40, 20), {Loop2{Edge2{Circle2{{10, 10}, 4}, true}}}};
    auto make = [&] { return k->extrude(k->makeFace(plate, kGround).shape, {0, 0, 5}).shape; };
    const auto a = make(), b = make();
    const auto ma = k->massProps(a), mb = k->massProps(b);
    CHECK(ma.volume == mb.volume);  // exactly
    CHECK(ma.area == mb.area);
    CHECK(ma.centerOfMass.x == mb.centerOfMass.x);
    const auto ta = k->topology(a), tb = k->topology(b);
    CHECK(ta.faces == tb.faces);
    CHECK(ta.edges == tb.edges);
    const auto meshA = k->tessellate(a, {}), meshB = k->tessellate(b, {});
    CHECK(meshA.triangles.size() == meshB.triangles.size());
    CHECK_FALSE(meshA.triangles.empty());  // and it meshes, for the viewer and STL
}
