#pragma once

#include <QString>
#include <QWidget>

#include "kh/plugin/Grid.h"

namespace kh::app {

// The middle of the window until kh.viewer arrives: the grid, seen from above,
// with a short text on top (the finished bodies and their volumes). Shows the
// grid settings working before there is a 3D view to draw them in.
class GridView final : public QWidget {
    Q_OBJECT
public:
    explicit GridView(QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return text_; }
    void setGrid(const plugin::GridSettings& grid);
    const plugin::GridSettings& grid() const { return grid_; }

    // How many screen pixels one grid space takes at this view's fixed scale
    // (4 px per mm); spaces closer than 8 px are drawn every 2nd, 5th, ... line.
    double pixelsPerSpace() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    plugin::GridSettings grid_;
};

}  // namespace kh::app
