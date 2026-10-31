#pragma once

/// @file
/// One 3D view of a design: OCCT draws it inside a Qt widget.

#include <QOpenGLWidget>
#include <QPoint>
#include <QString>

#include <memory>
#include <vector>

#include "kh/core/Id.h"
#include "kh/view/Handles.h"
#include "kh/view/Layout.h"
#include "kh/view/Scene.h"

namespace kh::view {

/// A 3D view with its own camera, a view cube and the grid.
///
/// Mouse: drag with the left button to orbit, with the right or middle button
/// (or Shift and the left button) to pan, and turn the wheel to zoom. Click a
/// body to select it; Ctrl-click or Shift-click adds or removes one. Click a
/// face, edge or corner of the view cube to turn the camera to it.
///
/// The view never changes the design or the selection itself: it shows the
/// SceneContent it is given, and reports clicks through picked().
///
/// @code
/// auto* view = new kh::view::ViewWidget;
/// view->setContent({{{boxId, boxShape, false}}, {}});
/// view->setStandardView(kh::view::StandardView::Iso);
/// QObject::connect(view, &kh::view::ViewWidget::picked,
///                  [&](const std::vector<kh::FeatureId>& ids) { selection.set(doc, ids); });
/// @endcode
class ViewWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    /// Makes a view looking from Iso. Call prepareGraphics() before the QApplication
    /// is made, and check graphicsProblem() first.
    /// @param parent The widget it sits in.
    explicit ViewWidget(QWidget* parent = nullptr);
    /// Frees the view's OCCT objects.
    ~ViewWidget() override;

    /// Changes what the view shows. Bodies that didn't change are kept, so this is
    /// quick to call after every rebuild or selection change. The first time
    /// there is something to show, the camera zooms to fit it.
    /// @param content The bodies and the selection.
    void setContent(const SceneContent& content);

    /// Changes what is drawn over the design (the grid, guide lines, ...).
    /// @param overlays The overlays, drawn in this order.
    void setOverlays(const std::vector<ViewOverlay>& overlays);

    /// Changes the handles drawn on top of everything, like the arrows on a picked
    /// body. Pressing the left button on one and dragging reports how far it was
    /// dragged (handleDragged(), then handleDropped()) instead of orbiting or
    /// selecting. While drawing (see setDrawingPlane()) no handles are shown.
    /// @param handles The handles.
    void setHandles(const std::vector<ViewHandle>& handles);

    /// Gives the handles the view draws.
    /// @return The handles last given to setHandles().
    const std::vector<ViewHandle>& handles() const;

    /// Gives where a point is drawn in the view, for tests that drag a handle.
    /// @param point A point, in mm.
    /// @return Where, in widget coordinates, or nothing before the view has drawn.
    std::optional<QPoint> toScreen(const kernel::Vec3& point) const;

    /// Gives how many overlays the view draws.
    /// @return The number of overlays.
    int overlayCount() const;

    /// Turns the camera to look from a standard direction, and zooms to fit.
    /// @param view The direction.
    void setStandardView(StandardView view);

    /// Zooms and pans so every body fits in the view.
    void fitAll();

    /// Gives the camera's current direction.
    /// @return Unit vectors, from the point looked at towards the camera, and up.
    ViewDirection camera() const;

    /// Gives what the view shows.
    /// @return The content last given to setContent().
    const SceneContent& content() const;

    /// Gives how many bodies the view draws (bodies whose shape it could use).
    /// @return The number of bodies.
    int shownCount() const;

    /// Checks whether OpenGL started and the view has drawn.
    /// @return True after the first successful draw.
    bool ready() const;

    /// Gives why the view can't draw, if it can't.
    /// @return Empty, or a plain sentence saying what went wrong and what to do.
    QString problem() const;

    /// Starts or stops drawing mode. While drawing, a left click reports the
    /// point on the plane (planeClicked()) instead of orbiting or selecting,
    /// moving the mouse reports it too (planeMoved()), and a left drag reports
    /// where it started and where it is (planeDragged(), then planeDropped()).
    /// Panning and zooming still work, and Escape sends planeCancelled().
    /// @param plane The plane to draw on, or nothing to stop drawing.
    void setDrawingPlane(std::optional<DrawingPlane> plane);

    /// Turns the camera to look along a direction, and zooms to fit.
    /// @param direction From the point looked at towards the camera, and up.
    void lookAlong(const ViewDirection& direction);

    /// Changes what a left drag does: orbit (the usual), pan or zoom. Right and
    /// middle drags still pan, Shift+left drag pans, and the wheel zooms. A left
    /// click still picks.
    /// @param drag What it does.
    void setLeftDrag(LeftDrag drag);

    /// Gives what a left drag does.
    /// @return The choice last given to setLeftDrag(); Orbit at first.
    LeftDrag leftDrag() const;

    /// Changes how bodies are drawn: shaded with edges (the usual), shaded, or
    /// wireframe. Ghosts of used-up steps stay see-through either way.
    /// @param style The style.
    void setDisplayStyle(DisplayStyle style);

    /// Gives how bodies are drawn.
    /// @return The style last given to setDisplayStyle(); ShadedWithEdges at first.
    DisplayStyle displayStyle() const;

    /// Selects as if the student clicked at a point (for tests). It runs on the
    /// next draw, like a real click.
    /// @param point Where, in widget coordinates.
    /// @param add True for a Ctrl-click (add or remove), false for a plain click.
    void clickAt(QPoint point, bool add = false);

signals:
    /// While drawing: the mouse moved over the drawing plane.
    /// @param point Where, on the plane.
    void planeMoved(const kh::view::PlanePoint& point);

    /// While drawing: the student clicked on the drawing plane.
    /// @param point Where, on the plane.
    void planeClicked(const kh::view::PlanePoint& point);

    /// While drawing: the student is dragging with the left button (pressed, then
    /// moved more than a few pixels). Sent on every move until planeDropped().
    /// @param from Where the button went down, on the plane.
    /// @param to Where the mouse is now, on the plane.
    void planeDragged(const kh::view::PlanePoint& from, const kh::view::PlanePoint& to);

    /// While drawing: a drag ended (the left button came up).
    /// @param from Where the button went down, on the plane.
    /// @param to Where it came up, on the plane.
    void planeDropped(const kh::view::PlanePoint& from, const kh::view::PlanePoint& to);

    /// While drawing: the student pressed Escape.
    void planeCancelled();

    /// The student is dragging a handle (see setHandles()). Sent on every move
    /// until handleDropped() or handleCancelled().
    /// @param id The handle's id.
    /// @param by How far it has been dragged since the button went down, along x, y and z (mm).
    void handleDragged(const std::string& id, const kernel::Vec3& by);

    /// A handle drag ended: the left button came up.
    /// @param id The handle's id.
    /// @param by How far it was dragged, along x, y and z (mm).
    void handleDropped(const std::string& id, const kernel::Vec3& by);

    /// The student pressed Escape while dragging a handle: put back what the drag changed.
    void handleCancelled();

    /// The student clicked in the view and the selection should change.
    /// @param selection The steps that should be selected, in picking order.
    void picked(const std::vector<kh::FeatureId>& selection);

    /// The student clicked or focused this view (so it becomes the active pane).
    void activated();

    /// The student clicked the right mouse button without dragging (a drag pans),
    /// asking for the pane's menu.
    /// @param global Where, on the screen.
    void menuRequested(QPoint global);

    /// OpenGL couldn't start, so the view can't draw.
    /// @param why A plain sentence saying what went wrong and what to do.
    void failed(const QString& why);

protected:
    /// Wraps Qt's OpenGL context for OCCT.
    void initializeGL() override;
    /// Lets OCCT handle the queued mouse input and draw.
    void paintGL() override;
    /// Passes a mouse button press to OCCT's view controller.
    /// @param event The press.
    void mousePressEvent(QMouseEvent* event) override;
    /// Passes a mouse button release to OCCT's view controller (a click selects).
    /// @param event The release.
    void mouseReleaseEvent(QMouseEvent* event) override;
    /// Passes mouse movement to OCCT's view controller (drags turn the camera).
    /// @param event The movement.
    void mouseMoveEvent(QMouseEvent* event) override;
    /// Zooms towards the mouse.
    /// @param event The wheel turn.
    void wheelEvent(QWheelEvent* event) override;
    /// Clears the hover highlight when the mouse leaves.
    /// @param event The leave event.
    void leaveEvent(QEvent* event) override;

    /// Sends Escape to the drawing tool while drawing.
    /// @param event The key.
    void keyPressEvent(QKeyEvent* event) override;
    /// Gives the view a sensible starting size.
    /// @return 640 by 480 pixels.
    QSize sizeHint() const override;

private:
    friend class ViewController;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kh::view
