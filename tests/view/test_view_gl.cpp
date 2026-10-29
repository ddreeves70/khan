// Tests for ViewWidget that draw with OpenGL: what it shows, the camera and picking.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>
#include <memory>

#include "kh/kernel_occt/OcctKernel.h"
#include "kh/view/Graphics.h"
#include "kh/view/ViewWidget.h"

using namespace kh;
using namespace kh::view;
using Catch::Approx;

namespace {

// Lets Qt draw until `done` is true (at most 5 s).
template <typename F>
bool waitFor(F done) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 5000) QApplication::processEvents(QEventLoop::AllEvents, 50);
    return done();
}

// Draws now, so queued mouse input is handled before the next event (a drag
// released before any frame is drawn would be dropped). grabFramebuffer()
// always draws; update() and repaint() skip it while the virtual screen hasn't
// shown the window yet, which made these tests fail now and then.
void settle(ViewWidget& v) {
    for (int i = 0; i < 4; ++i) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        (void)v.grabFramebuffer();
    }
}

bool isBackground(QColor c) {
    // The sky is light grey-blue to white: every channel high and close together.
    return c.red() > 200 && c.green() > 200 && c.blue() > 200;
}

struct Fixture {
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    FeatureId boxId = FeatureId::generate();
    kernel::ShapeHandle box = kernel->makeBox({-10, -10, 0}, 20, 20, 20).shape;
    ViewWidget view;

    Fixture() {
        view.resize(400, 300);
        view.show();
        REQUIRE(waitFor([&] { return view.ready() || !view.problem().isEmpty(); }));
        REQUIRE(view.problem().isEmpty());
    }
};

}  // namespace

TEST_CASE("3D graphics start on this machine", "[view][gl]") {
    CHECK(graphicsProblem().isEmpty());
}

TEST_CASE("a view draws a body in the middle after fitting it", "[view][gl]") {
    Fixture f;
    const QImage empty = f.view.grabFramebuffer();
    CHECK(isBackground(empty.pixelColor(empty.width() / 2, empty.height() / 5)));

    f.view.setContent({{{f.boxId, f.box, false}}, {}});
    settle(f.view);
    CHECK(f.view.shownCount() == 1);
    const QImage img = f.view.grabFramebuffer();
    const QColor middle = img.pixelColor(img.width() / 2, img.height() / 2);
    INFO("middle pixel " << middle.name().toStdString());
    CHECK_FALSE(isBackground(middle));
    // A corner stays sky.
    CHECK(isBackground(img.pixelColor(5, img.height() - 5)));
}

TEST_CASE("unchanged bodies are kept, changed ones redrawn, unusable ones skipped", "[view][gl]") {
    Fixture f;
    f.view.setContent({{{f.boxId, f.box, false}}, {}});
    f.view.setContent({{{f.boxId, f.box, false}}, {f.boxId}});
    CHECK(f.view.shownCount() == 1);
    CHECK(f.view.content().selected == std::vector{f.boxId});
    f.view.setContent({{{f.boxId, f.box, true}}, {}});  // now see-through
    CHECK(f.view.shownCount() == 1);
    f.view.setContent({{{f.boxId, kernel::ShapeHandle{}, false}}, {}});  // no shape
    CHECK(f.view.shownCount() == 0);
    f.view.setContent({});
    CHECK(f.view.shownCount() == 0);
}

TEST_CASE("standard views turn the camera", "[view][gl]") {
    Fixture f;
    for (auto v : allViews()) {
        f.view.setStandardView(v);
        settle(f.view);
        const auto want = direction(v);
        const auto got = f.view.camera();
        INFO(viewName(v));
        CHECK(got.toEye.x == Approx(want.toEye.x).margin(1e-6));
        CHECK(got.toEye.y == Approx(want.toEye.y).margin(1e-6));
        CHECK(got.toEye.z == Approx(want.toEye.z).margin(1e-6));
        CHECK(got.up.x == Approx(want.up.x).margin(1e-6));
        CHECK(got.up.y == Approx(want.up.y).margin(1e-6));
        CHECK(got.up.z == Approx(want.up.z).margin(1e-6));
    }
}

TEST_CASE("clicking a body picks it; Ctrl-click and empty space change the pick", "[view][gl]") {
    Fixture f;
    const FeatureId other = FeatureId::generate();
    auto far = f.kernel->makeBox({200, 200, 0}, 5, 5, 5).shape;  // off to the side
    f.view.setContent({{{f.boxId, f.box, false}}, {}});
    settle(f.view);

    std::vector<FeatureId> last;
    int picks = 0;
    QObject::connect(&f.view, &ViewWidget::picked, [&](const std::vector<FeatureId>& ids) {
        last = ids;
        ++picks;
        // Do what the viewer plugin does: the selection comes back as content.
        auto c = f.view.content();
        c.selected = ids;
        f.view.setContent(c);
    });
    const QPoint middle(f.view.width() / 2, f.view.height() / 2);

    f.view.clickAt(middle);
    REQUIRE(waitFor([&] { return picks == 1; }));
    CHECK(last == std::vector{f.boxId});

    // Clicking it again changes nothing, so nothing is reported.
    f.view.clickAt(middle);
    settle(f.view);
    CHECK(picks == 1);

    // Ctrl-click keeps a selected step the view doesn't draw, and removes the box.
    auto c = f.view.content();
    c.selected = {other, f.boxId};
    f.view.setContent(c);
    settle(f.view);
    f.view.clickAt(middle, true);
    REQUIRE(waitFor([&] { return picks == 2; }));
    CHECK(last == std::vector{other});

    // A plain click on empty sky clears the selection.
    f.view.clickAt(QPoint(8, f.view.height() - 8));
    REQUIRE(waitFor([&] { return picks == 3; }));
    CHECK(last.empty());
    (void)far;
}

TEST_CASE("overlays are drawn, never picked, and left out of Fit all", "[view][gl]") {
    Fixture f;
    f.view.setStandardView(StandardView::Top);
    ViewOverlay line;  // a thick magenta line along y, through the middle
    line.points = {{0, -40, 1}, {0, 40, 1}};
    line.color = {220, 0, 220};
    line.size = 5;
    ViewOverlay lattice;  // a huge lattice: Fit all must ignore it
    lattice.shape = OverlayShape::Lattice;
    lattice.spacing = 100;
    lattice.halfSize = 5000;
    lattice.color = {204, 209, 218};
    ViewOverlay dots = lattice;
    dots.dots = true;
    f.view.setOverlays({lattice, dots, line});
    settle(f.view);
    CHECK(f.view.overlayCount() == 3);
    {
        const QImage img = f.view.grabFramebuffer();
        bool magenta = false;
        const int y = img.height() / 3;  // above the middle, away from the x axis
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 150 && c.blue() > 150 && c.green() < 90) magenta = true;
        }
        CHECK(magenta);
    }

    // Clicking on the line picks nothing.
    int picks = 0;
    QObject::connect(&f.view, &ViewWidget::picked, [&](const std::vector<FeatureId>&) { ++picks; });
    f.view.clickAt(QPoint(f.view.width() / 2, f.view.height() / 3));
    settle(f.view);
    CHECK(picks == 0);

    // With a body, Fit all fits the body, not the 10 m lattice.
    f.view.setContent({{{f.boxId, f.box, false}}, {}});
    f.view.fitAll();
    settle(f.view);
    const QImage img = f.view.grabFramebuffer();
    CHECK_FALSE(isBackground(img.pixelColor(img.width() / 2, img.height() / 2)));
    CHECK_FALSE(isBackground(img.pixelColor(img.width() / 2, img.height() / 4)));  // the box fills the middle

    f.view.setOverlays({});
    settle(f.view);
    CHECK(f.view.overlayCount() == 0);
    CHECK(f.view.ready());
}

TEST_CASE("dragging with the left button orbits, with the right button pans", "[view][gl]") {
    Fixture f;
    f.view.setContent({{{f.boxId, f.box, false}}, {}});
    settle(f.view);
    auto drag = [&](Qt::MouseButton button, QPoint from, QPoint to) {
        auto send = [&](QEvent::Type type, QPoint at, Qt::MouseButtons held) {
            QMouseEvent e(type, QPointF(at), f.view.mapToGlobal(QPointF(at)), button, held, Qt::NoModifier);
            QApplication::sendEvent(&f.view, &e);
            settle(f.view);
        };
        send(QEvent::MouseButtonPress, from, button);
        for (int i = 1; i <= 5; ++i) send(QEvent::MouseMove, from + (to - from) * i / 5, button);
        send(QEvent::MouseButtonRelease, to, Qt::NoButton);
    };
    const auto before = f.view.camera();
    drag(Qt::RightButton, {200, 150}, {260, 170});
    const auto panned = f.view.camera();
    CHECK(panned.toEye.x == Approx(before.toEye.x).margin(1e-9));  // panning doesn't turn
    CHECK(panned.toEye.z == Approx(before.toEye.z).margin(1e-9));

    drag(Qt::LeftButton, {200, 150}, {300, 150});
    const auto turned = f.view.camera();
    CHECK(std::abs(turned.toEye.x - before.toEye.x) > 0.05);  // orbiting does
    CHECK(turned.up.z > 0.5);                                 // and Z stays up
    CHECK(f.view.content().selected.empty());                 // a drag isn't a click
}

TEST_CASE("drawing mode: clicks report points on the plane, and the left button doesn't orbit",
          "[view][gl][draw]") {
    Fixture f;
    f.view.setContent({{{f.boxId, f.box, false}}, {}});
    f.view.lookAlong({{0, 0, 1}, {0, 1, 0}});  // straight down at the ground, y up the screen
    settle(f.view);
    f.view.setDrawingPlane(DrawingPlane{{0, 0, 0}, {0, 0, 1}});
    std::vector<PlanePoint> clicks;
    int moves = 0;
    QObject::connect(&f.view, &ViewWidget::planeClicked, [&](const PlanePoint& p) { clicks.push_back(p); });
    QObject::connect(&f.view, &ViewWidget::planeMoved, [&](const PlanePoint&) { ++moves; });
    auto send = [&](QEvent::Type type, QPoint at, Qt::MouseButton button, Qt::MouseButtons held) {
        QMouseEvent e(type, QPointF(at), f.view.mapToGlobal(QPointF(at)), button, held, Qt::NoModifier);
        QApplication::sendEvent(&f.view, &e);
        settle(f.view);
    };
    auto click = [&](QPoint at) {
        send(QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    };
    const auto before = f.view.camera();
    click({200, 150});
    click({300, 150});  // 100 pixels to the right
    click({200, 50});   // 100 pixels up
    REQUIRE(clicks.size() == 3);
    CHECK(moves >= 3);
    for (const auto& p : clicks) {
        CHECK(p.at.z == Approx(0).margin(1e-6));  // on the plane
        CHECK(p.pixel > 0);
    }
    // Looking straight down with y up the screen: right is +x, up the screen is +y,
    // and 100 pixels are 100 times the size of one.
    CHECK(clicks[1].at.x - clicks[0].at.x == Approx(100 * clicks[0].pixel).epsilon(0.02));
    CHECK(clicks[1].at.y == Approx(clicks[0].at.y).margin(clicks[0].pixel));
    CHECK(clicks[2].at.y - clicks[0].at.y == Approx(100 * clicks[0].pixel).epsilon(0.02));
    CHECK(f.view.content().selected.empty());  // clicks draw, they don't pick

    // A left drag doesn't orbit while drawing: it's reported as a drag on the plane.
    std::vector<std::pair<PlanePoint, PlanePoint>> drags, drops;
    QObject::connect(&f.view, &ViewWidget::planeDragged,
                     [&](const PlanePoint& a, const PlanePoint& b) { drags.emplace_back(a, b); });
    QObject::connect(&f.view, &ViewWidget::planeDropped,
                     [&](const PlanePoint& a, const PlanePoint& b) { drops.emplace_back(a, b); });
    send(QEvent::MouseButtonPress, {200, 150}, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 5; ++i)
        send(QEvent::MouseMove, QPoint(200 + 20 * i, 150), Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, {300, 150}, Qt::LeftButton, Qt::NoButton);
    const auto after = f.view.camera();
    CHECK(after.toEye.z == Approx(before.toEye.z).margin(1e-9));
    CHECK(clicks.size() == 3);  // and a drag isn't a click
    CHECK(drags.size() == 5);
    REQUIRE(drops.size() == 1);
    CHECK(drops[0].first.at.x == Approx(clicks[0].at.x).margin(clicks[0].pixel));  // from where it went down
    CHECK(drops[0].second.at.x - drops[0].first.at.x == Approx(100 * clicks[0].pixel).epsilon(0.02));

    // Escape is sent to the tool; after drawing stops, clicks pick again.
    bool cancelled = false;
    QObject::connect(&f.view, &ViewWidget::planeCancelled, [&] { cancelled = true; });
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&f.view, &esc);
    CHECK(cancelled);
    f.view.setDrawingPlane(std::nullopt);
    click({200, 150});
    CHECK(clicks.size() == 3);
}
