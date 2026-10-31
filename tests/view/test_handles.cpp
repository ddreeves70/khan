// Tests for handles' maths (kh/view/Handles.h): drawing, picking and dragging,
// without a screen.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "kh/view/Handles.h"

using namespace kh;
using namespace kh::view;
using Catch::Approx;

namespace {

// Looking straight down at the ground, 2 pixels per mm, (0, 0) in the middle of
// a 400 x 300 view; screen y grows down, so design y goes up the screen.
std::optional<ScreenPoint> fromAbove(const kernel::Vec3& p) {
    return ScreenPoint{200 + 2 * p.x, 150 - 2 * p.y};
}

// The line of sight through a screen point, for the same view.
Ray downAt(double sx, double sy) {
    return {{(sx - 200) / 2, (150 - sy) / 2, 100}, {0, 0, -1}};
}

ViewHandle arrow(std::string id, kernel::Vec3 direction) {
    ViewHandle h;
    h.id = std::move(id);
    h.direction = direction;
    h.length = 20;
    return h;
}

ViewHandle square() {
    ViewHandle h;
    h.id = "xy";
    h.kind = HandleKind::Square;
    h.direction = {0, 0, 1};
    h.length = 6;
    return h;
}

}  // namespace

TEST_CASE("a plane's axes are square to each other and to its normal", "[view][handles]") {
    auto [u, v] = planeAxes({0, 0, 1});
    CHECK(u.x == Approx(1));
    CHECK(v.y == Approx(1));
    // A property: for any normal, the axes are unit vectors, square to it and to each other.
    for (const kernel::Vec3 n : {kernel::Vec3{1, 0, 0}, kernel::Vec3{0, 1, 0}, kernel::Vec3{0.6, 0, 0.8},
                                 kernel::Vec3{0.48, 0.6, 0.64}, kernel::Vec3{-1, 0, 0}}) {
        std::tie(u, v) = planeAxes(n);
        auto dot = [](const kernel::Vec3& a, const kernel::Vec3& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        };
        CHECK(dot(u, u) == Approx(1));
        CHECK(dot(v, v) == Approx(1));
        CHECK(dot(u, n) == Approx(0).margin(1e-12));
        CHECK(dot(v, n) == Approx(0).margin(1e-12));
        CHECK(dot(u, v) == Approx(0).margin(1e-12));
    }
}

TEST_CASE("an arrow is drawn as a shaft and a four-spoke head; a square as four sides", "[view][handles]") {
    const auto lines = handleLines(arrow("x", {1, 0, 0}));
    REQUIRE(lines.size() == 10);      // shaft + 4 spokes, two points each
    CHECK(lines[1].x == Approx(20));  // the tip
    for (std::size_t i = 2; i < lines.size(); i += 2) {
        CHECK(lines[i].x == Approx(20));      // each spoke starts at the tip
        CHECK(lines[i + 1].x == Approx(16));  // and goes back a fifth of the length
    }
    const auto sides = handleLines(square());
    REQUIRE(sides.size() == 8);
    for (const auto& p : sides) {
        CHECK(std::abs(p.x) == Approx(3));
        CHECK(std::abs(p.y) == Approx(3));
        CHECK(p.z == 0);
    }
}

TEST_CASE("the handle under the mouse is found: arrows first, then squares", "[view][handles]") {
    const std::vector<ViewHandle> handles{arrow("x", {1, 0, 0}), arrow("y", {0, 1, 0}), arrow("z", {0, 0, 1}),
                                          square()};
    CHECK(handleAt(handles, fromAbove, {230, 152}, 8) == 0u);  // near the x arrow (15 mm along x)
    CHECK(handleAt(handles, fromAbove, {198, 120}, 8) == 1u);  // near the y arrow
    CHECK(handleAt(handles, fromAbove, {202, 148}, 8) == 3u);  // the square in the middle
    CHECK_FALSE(handleAt(handles, fromAbove, {300, 300}, 8));  // nothing there
    CHECK_FALSE(handleAt(handles, fromAbove, {300, 150}, 8));  // past the tip of x (at 240)
    // The z arrow points straight at the camera: it can't be picked (or dragged).
    const std::vector<ViewHandle> justZ{arrow("z", {0, 0, 1})};
    CHECK_FALSE(handleAt(justZ, fromAbove, {200, 150}, 8));
}

TEST_CASE("dragging an arrow moves only along it, by as far as the mouse went along it", "[view][handles]") {
    const auto x = arrow("x", {1, 0, 0});
    // 30 pixels right and 10 down: 15 mm along x; the 5 mm across doesn't count.
    const auto by = handleDrag(x, downAt(230, 150), downAt(260, 160));
    REQUIRE(by);
    CHECK(by->x == Approx(15));
    CHECK(by->y == Approx(0).margin(1e-12));
    CHECK(by->z == Approx(0).margin(1e-12));
    // Back the other way: negative.
    CHECK(handleDrag(x, downAt(230, 150), downAt(210, 150))->x == Approx(-10));

    // A property: seen from the side (looking along -y), dragging z follows the mouse up.
    const auto z = arrow("z", {0, 0, 1});
    auto side = [](double sx, double sy) { return Ray{{(sx - 200) / 2, 100, (150 - sy) / 2}, {0, -1, 0}}; };
    for (double up : {4.0, 20.0, -14.0}) {
        const auto moved = handleDrag(z, side(200, 140), side(207, 140 - up * 2));
        REQUIRE(moved);
        CHECK(moved->z == Approx(up));
        CHECK(moved->x == Approx(0).margin(1e-12));
    }
    // Looking straight along an arrow: no answer, rather than a wild one.
    CHECK_FALSE(handleDrag(z, downAt(200, 150), downAt(210, 150)));
}

TEST_CASE("dragging a square slides on its plane", "[view][handles]") {
    const auto by = handleDrag(square(), downAt(200, 150), downAt(240, 110));
    REQUIRE(by);
    CHECK(by->x == Approx(20));
    CHECK(by->y == Approx(20));
    CHECK(by->z == Approx(0).margin(1e-12));
    // Seen edge on, it gives no answer.
    auto side = [](double sx, double sy) { return Ray{{(sx - 200) / 2, 100, (150 - sy) / 2}, {0, -1, 0}}; };
    CHECK_FALSE(handleDrag(square(), side(200, 150), side(220, 150)));
}
