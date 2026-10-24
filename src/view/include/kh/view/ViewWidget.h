#pragma once

/// @file
/// One 3D view of a design: OCCT draws it inside a Qt widget.

#include <QOpenGLWidget>
#include <QString>

#include <memory>
#include <vector>

#include "kh/core/Id.h"
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

    /// Changes the grid.
    /// @param grid The grid.
    void setGrid(const ViewGrid& grid);

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

    /// Selects as if the student clicked at a point (for tests). It runs on the
    /// next draw, like a real click.
    /// @param point Where, in widget coordinates.
    /// @param add True for a Ctrl-click (add or remove), false for a plain click.
    void clickAt(QPoint point, bool add = false);

signals:
    /// The student clicked in the view and the selection should change.
    /// @param selection The steps that should be selected, in picking order.
    void picked(const std::vector<kh::FeatureId>& selection);

    /// The student clicked or focused this view (so it becomes the active pane).
    void activated();

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
    /// Gives the view a sensible starting size.
    /// @return 640 by 480 pixels.
    QSize sizeHint() const override;

private:
    friend class ViewController;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kh::view
