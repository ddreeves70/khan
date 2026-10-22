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
