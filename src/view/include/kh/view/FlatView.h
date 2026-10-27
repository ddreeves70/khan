#pragma once

/// @file
/// A flat, drawn-with-Qt stand-in for the 3D view, for computers without 3D graphics.

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

protected:
    /// Draws the overlays and the card.
    /// @param event Which part to draw (it draws everything).
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    std::vector<ViewOverlay> overlays_;
};

}  // namespace kh::view
