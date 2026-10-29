#pragma once

/// @file
/// A flat, drawn-with-Qt stand-in for the 3D view, for computers without 3D graphics.

#include <QPointF>
#include <QString>
#include <QWidget>

#include <vector>

#include "kh/view/Scene.h"

namespace kh::view {

/// Draws the overlays (such as the grid) seen from above, with a text card in the top-left corner.
///
/// The viewer uses it when 3D graphics can't start (see graphicsProblem()), so
/// the student still sees the grid and a list of the finished bodies.
class FlatView : public QWidget {
    Q_OBJECT
public:
    /// Makes an empty view. Its object name is "kh.view.flat".
    /// @param parent The widget it sits in.
    explicit FlatView(QWidget* parent = nullptr);

    /// Changes the text on the card. Empty hides the card.
    /// @param text The text, with line breaks.
    void setText(const QString& text);

    /// Changes what it draws behind the text, seen from above (x to the right,
    /// y up, the origin in the middle). Lattices are drawn first, underneath
    /// the others.
    /// @param overlays The overlays, drawn in this order.
    void setOverlays(const std::vector<ViewOverlay>& overlays);

    /// Gives the text on the card.
    /// @return The text.
    const QString& text() const { return text_; }

    /// Gives the overlays it draws.
    /// @return The overlays.
    const std::vector<ViewOverlay>& overlays() const { return overlays_; }

    /// Gives the distance between the drawn lines or dots of a lattice, in
    /// screen pixels. A lattice too fine to see draws every 2nd, 5th, 10th, ...
    /// line instead, so it doesn't turn into a grey smear.
    /// @param lattice A Lattice overlay.
    /// @return The drawn distance, in pixels.
    static double pixelsPerSpace(const ViewOverlay& lattice);

    /// Starts or stops drawing mode, as ViewWidget::setDrawingPlane() does. The
    /// flat view is seen from above, so it only draws on a flat plane (one whose
    /// normal is up or down); on others, it reports nothing.
    /// @param plane The plane to draw on, or nothing to stop drawing.
    void setDrawingPlane(std::optional<DrawingPlane> plane);

    /// Gives the screen position of a point, for tests that click on it.
    /// @param point A point in mm (z is ignored: the view is seen from above).
    /// @return Where it is drawn, in widget coordinates.
    QPointF toScreen(const kernel::Vec3& point) const;

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

protected:
    /// Draws the overlays and the card.
    /// @param event Which part to draw (it draws everything).
    void paintEvent(QPaintEvent* event) override;
    /// Reports a click while drawing.
    /// @param event The mouse.
    void mousePressEvent(QMouseEvent* event) override;
    /// Reports the mouse while drawing.
    /// @param event The mouse.
    void mouseMoveEvent(QMouseEvent* event) override;
    /// Reports a click or the end of a drag while drawing.
    /// @param event The mouse.
    void mouseReleaseEvent(QMouseEvent* event) override;
    /// Sends Escape to the drawing tool while drawing.
    /// @param event The key.
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::optional<PlanePoint> planePoint(QPointF at) const;

    QString text_;
    std::optional<DrawingPlane> drawing_;
    // Where the left button went down, to tell a click from a drag.
    QPoint leftPress_;
    std::optional<PlanePoint> pressPoint_;
    bool leftDown_ = false;
    bool dragging_ = false;
    std::vector<ViewOverlay> overlays_;
};

}  // namespace kh::view
