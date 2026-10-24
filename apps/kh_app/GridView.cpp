#include "GridView.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace kh::app {

namespace {
constexpr double kPixelsPerMm = 4.0;
constexpr double kMinPixels = 8.0;  // closer than this, the grid turns into a grey smear
}  // namespace

GridView::GridView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("kh.shell.viewer"));
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(246, 247, 249));
    setPalette(p);
}

void GridView::setText(const QString& text) {
    text_ = text;
    update();
}

void GridView::setGrid(const plugin::GridSettings& grid) {
    grid_ = grid;
    update();
}

double GridView::pixelsPerSpace() const {
    double step = grid_.spacing * kPixelsPerMm;
    // Too dense to see: draw every 2nd, 5th, 10th, ... line instead.
    for (double factor : {2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0}) {
        if (step >= kMinPixels) break;
        step = grid_.spacing * factor * kPixelsPerMm;
    }
    return step;
}

void GridView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (grid_.visible) {
        const double step = pixelsPerSpace();
        // The origin is the middle of the view, so the grid stays put as the window resizes.
        const QPointF origin(width() / 2.0, height() / 2.0);
        const double x0 = origin.x() - std::floor(origin.x() / step) * step;
        const double y0 = origin.y() - std::floor(origin.y() / step) * step;
        if (grid_.style == plugin::GridStyle::Lines) {
            painter.setPen(QPen(QColor(214, 218, 225), 1));
            for (double x = x0; x < width(); x += step) painter.drawLine(QPointF(x, 0), QPointF(x, height()));
            for (double y = y0; y < height(); y += step) painter.drawLine(QPointF(0, y), QPointF(width(), y));
        } else {
            painter.setPen(Qt::NoPen);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(QColor(132, 140, 154));
            for (double x = x0; x < width(); x += step) {
                for (double y = y0; y < height(); y += step) painter.drawEllipse(QPointF(x, y), 1.6, 1.6);
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }
        // The two axes through the origin: x red, y green, like the 3D view will.
        painter.setPen(QPen(QColor(209, 69, 59, 160), 1.5));
        painter.drawLine(QPointF(origin.x(), origin.y()), QPointF(width(), origin.y()));
        painter.setPen(QPen(QColor(46, 155, 79, 160), 1.5));
        painter.drawLine(QPointF(origin.x(), origin.y()), QPointF(origin.x(), 0));
    }
    // The text, on a light card in the top-left corner, so the grid's origin stays clear.
    if (!text_.isEmpty()) {
        const int margin = 16, pad = 12;
        const QRect room(margin + pad, margin + pad, std::max(120, width() / 2), height() / 2);
        const QRect used = painter.boundingRect(room, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text_);
        painter.setPen(QColor(214, 218, 225));
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.drawRoundedRect(used.adjusted(-pad, -pad, pad, pad), 8, 8);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(used, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text_);
    }
}

}  // namespace kh::app
