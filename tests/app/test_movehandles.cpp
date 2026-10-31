// Tests for the kh.movehandles plugin: arrows on the picked body, and dragging
// them slides it, as one undo step.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Modeling.h"
#include "kh/plugins/MoveHandles.h"
#include "kh/plugins/Viewer.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;
using Catch::Approx;

namespace {

viewer::IViewer* peekedViewer = nullptr;
void peekViewer(plugin::PluginContext& ctx) {
    ctx.onStart([&ctx] { peekedViewer = ctx.service<viewer::IViewer>(); });
}

// Khan without the desktop app's window: the viewer's service and the move handles.
struct Khan {
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();
    plugin::PluginHost host{kernel.get(), solver.get()};
    DocId doc;
    viewer::IViewer* viewer = nullptr;

    Khan() {
        host.addBuiltin(plugins::modelingPlugin());
        host.addBuiltin(plugins::viewerPlugin());
        host.addBuiltin(plugins::moveHandlesPlugin());
        host.addBuiltin({R"({"id": "test.peek", "name": "Peek", "version": "1", "requires": ["kh.viewer"]})",
                         &peekViewer});
        host.loadAll();
        REQUIRE(host.find("kh.movehandles")->state == plugin::PluginState::Loaded);
        viewer = peekedViewer;
        REQUIRE(viewer);
        doc = host.documents().add(std::make_unique<model::Document>());
    }

    const model::Document& design() { return *host.documents().find(doc); }

    FeatureId run(std::string_view name, const model::Args& args) {
        REQUIRE(host.documents().run(doc, name, args).empty());
        return design().featureAt(design().featureCount() - 1).id();
    }

    void pick(std::vector<FeatureId> ids) { host.selection().set(doc, std::move(ids)); }

    const model::BoxFeature& box(const FeatureId& id) {
        return static_cast<const model::BoxFeature&>(*design().find(id));
    }
};

}  // namespace

TEST_CASE("the handles are arrows along x, y and z from the body's middle, and a square on the ground",
          "[movehandles]") {
    const auto h = plugins::moveHandles({{0, 0, 0}, {40, 20, 10}});
    REQUIRE(h.size() == 4);
    CHECK(h[0].id == "x");
    CHECK(h[1].id == "y");
    CHECK(h[2].id == "z");
    CHECK(h[3].id == "xy");
    CHECK(h[3].kind == viewer::HandleKind::Square);
    for (const auto& a : h) {
        CHECK(a.at.x == 20);
        CHECK(a.at.y == 10);
        CHECK(a.at.z == 5);
    }
    CHECK(h[0].length == plugins::kArrowPixels);  // the same size on the screen, whatever the zoom
    CHECK(h[3].length == plugins::kSquarePixels);
    CHECK(h[0].r > h[0].g);  // x is red, y green, z blue, like the axes
    CHECK(h[1].g > h[1].r);
    CHECK(h[2].b > h[2].r);

    CHECK(plugins::snapSlide({4.4, -0.6, 2.5}, LengthUnit::Millimeter).x == 4);
    CHECK(plugins::snapSlide({4.4, -0.6, 2.5}, LengthUnit::Millimeter).y == -1);
    CHECK(plugins::snapSlide({0.4, 0, 0}, LengthUnit::Centimeter).x == 0);
    CHECK(plugins::snapSlide({3, 0, 0}, LengthUnit::Inch).x == Approx(2.54));  // 0.1 in steps
}

TEST_CASE("handles show on one picked body, and not on a sketch, nothing or two bodies", "[movehandles]") {
    Khan k;
    const auto box = k.run("feature.add_box", {{"size", kernel::Vec3{40, 20, 10}}});
    const auto other =
        k.run("feature.add_box", {{"corner", kernel::Vec3{100, 0, 0}}, {"size", kernel::Vec3{5, 5, 5}}});
    const auto sketch = k.run("feature.add_sketch", {});
    CHECK(k.viewer->handles().empty());
    k.pick({box});
    REQUIRE(k.viewer->handles().size() == 4);
    CHECK(k.viewer->handles()[0].at.x == Approx(20));
    k.pick({box, other});
    CHECK(k.viewer->handles().empty());
    k.pick({sketch});
    CHECK(k.viewer->handles().empty());
    k.pick({});
    CHECK(k.viewer->handles().empty());
}

TEST_CASE("dragging an arrow slides the box in whole mm, follows it, and undoes in one step",
          "[movehandles]") {
    Khan k;
    const auto box = k.run("feature.add_box", {{"size", kernel::Vec3{40, 20, 10}}});
    k.pick({box});
    auto* user = k.viewer->handleUser();
    REQUIRE(user);
    user->dragged("x", {2.3, 0, 0});
    CHECK(k.box(box).corner().x == 2);
    user->dragged("x", {6.8, 0, 0});
    CHECK(k.box(box).corner().x == 7);
    CHECK(k.viewer->handles()[0].at.x == Approx(27));  // the handles follow the body
    user->dragged("x", {7.1, 0, 0});                   // still 7: nothing to do
    user->dropped("x", {7.1, 0, 0});
    CHECK(k.box(box).corner().x == 7);
    CHECK(k.design().undoLabel() == "Move Box1");

    // A second drag is its own undo step.
    user->dragged("xy", {1, 2, 0});
    user->dropped("xy", {1, 2, 0});
    CHECK(k.box(box).corner().y == 2);
    REQUIRE(k.host.documents().undo(k.doc));
    CHECK(k.box(box).corner().x == 7);
    CHECK(k.box(box).corner().y == 0);
    REQUIRE(k.host.documents().undo(k.doc));  // the whole first drag
    CHECK(k.box(box).corner().x == 0);
}

TEST_CASE("dragging another body adds a Move step, which is then what's picked and dragged",
          "[movehandles]") {
    Khan k;
    const auto a = k.run("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
    const auto b = k.run("feature.add_cylinder", {{"radius", 2.0}, {"height", 20.0}});
    const auto cut = k.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", a}, {"b", b}});
    k.pick({cut});
    REQUIRE(k.viewer->handles().size() == 4);
    auto* user = k.viewer->handleUser();
    user->dragged("z", {0, 0, 3});
    REQUIRE(k.design().featureCount() == 4);
    const auto& move = k.design().featureAt(3);
    CHECK(move.type() == model::FeatureType::Move);
    CHECK(k.host.selection().features() == std::vector<FeatureId>{move.id()});
    user->dragged("z", {0, 0, 5});
    user->dropped("z", {0, 0, 5});
    CHECK(static_cast<const model::MoveFeature&>(k.design().featureAt(3)).motion().offset.z == 5);
    CHECK(k.design().featureCount() == 4);     // still one Move
    REQUIRE(k.viewer->handles().size() == 4);  // on the Move now
    REQUIRE(k.host.documents().undo(k.doc));
    CHECK(k.design().featureCount() == 3);  // the drag, Move step and all, was one step
}

TEST_CASE("Escape during a drag puts the body back, and what was picked", "[movehandles]") {
    Khan k;
    const auto a = k.run("feature.add_box", {{"size", kernel::Vec3{10, 10, 10}}});
    const auto b = k.run("feature.add_cylinder", {{"radius", 2.0}, {"height", 20.0}});
    const auto cut = k.run("feature.add_boolean", {{"operation", std::string("cut")}, {"a", a}, {"b", b}});
    k.pick({cut});
    auto* user = k.viewer->handleUser();
    user->dragged("x", {4, 0, 0});
    user->dragged("x", {9, 0, 0});
    REQUIRE(k.design().featureCount() == 4);
    user->cancelled();
    CHECK(k.design().featureCount() == 3);
    CHECK(k.host.selection().features() == std::vector<FeatureId>{cut});
    CHECK(k.viewer->handles().size() == 4);

    // On a box: back where it was.
    k.pick({a});
    user->dragged("y", {0, 6, 0});
    CHECK(k.box(a).corner().y == 6);
    user->cancelled();
    CHECK(k.box(a).corner().y == 0);
    CHECK(k.design().redoLabel() == "Move Box1");  // undone, not left half-done
}
