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
