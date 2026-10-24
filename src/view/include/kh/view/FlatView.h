#pragma once

/// @file
/// A flat, drawn-with-Qt stand-in for the 3D view, for computers without 3D graphics.

#include <QString>
#include <QWidget>

#include "kh/view/Scene.h"

namespace kh::view {

/// Draws the grid seen from above, with a text card in the top-left corner.
///
/// The viewer uses it when 3D graphics can't start (see graphicsProblem()), so
/// the student still sees the grid and a list of the finished bodies.
class FlatView : public QWidget {
    Q_OBJECT
public:
    /// Makes an empty view with the default grid. Its object name is "kh.view.flat".
    /// @param parent The widget it sits in.
    explicit FlatView(QWidget* parent = nullptr);

    /// Changes the text on the card. Empty hides the card.
    /// @param text The text, with line breaks.
    void setText(const QString& text);

    /// Changes the grid it draws.
    /// @param grid The grid.
    void setGrid(const ViewGrid& grid);

    /// Gives the text on the card.
    /// @return The text.
    const QString& text() const { return text_; }

    /// Gives the grid it draws.
    /// @return The grid.
    const ViewGrid& grid() const { return grid_; }

    /// Gives the distance between drawn grid lines or dots, in screen pixels.
    /// A grid too fine to see draws every 2nd, 5th, 10th, ... line instead.
    /// @return The distance in pixels: 4 pixels per mm, and at least 8.
    double pixelsPerSpace() const;

protected:
    /// Draws the grid and the card.
    /// @param event Which part to draw (it draws everything).
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    ViewGrid grid_;
};

}  // namespace kh::view
