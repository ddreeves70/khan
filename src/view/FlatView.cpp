#include "kh/view/FlatView.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace kh::view {

namespace {
constexpr double kPixelsPerMm = 4.0;
constexpr double kMinPixels = 8.0;  // closer than this, a lattice turns into a grey smear

QColor qcolor(const ViewOverlay& o) {
    return QColor(o.color.r, o.color.g, o.color.b, std::clamp(o.alpha, 0, 255));
}
}  // namespace

FlatView::FlatView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("kh.view.flat"));
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(246, 247, 249));
    setPalette(p);
}

void FlatView::setText(const QString& text) {
    text_ = text;
    update();
}

void FlatView::setOverlays(const std::vector<ViewOverlay>& overlays) {
    overlays_ = overlays;
    update();
}

double FlatView::pixelsPerSpace(const ViewOverlay& lattice) {
    double step = lattice.spacing * kPixelsPerMm;
    // Too dense to see: draw every 2nd, 5th, 10th, ... line instead.
    for (double factor : {2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0}) {
        if (step >= kMinPixels) break;
        step = lattice.spacing * factor * kPixelsPerMm;
    }
    return step;
}

void FlatView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    // The origin is the middle of the view, so drawings stay put as the window resizes.
    const QPointF origin(width() / 2.0, height() / 2.0);
    auto toScreen = [&](const kernel::Vec3& p) {
        return QPointF(origin.x() + p.x * kPixelsPerMm, origin.y() - p.y * kPixelsPerMm);
    };

    for (const auto& o : overlays_) {
        if (o.shape == OverlayShape::Lattice) {
            // Seen from above: only a lattice on a floor-like plane makes sense here,
            // and it covers the whole view.
            if (!(o.spacing > 0)) continue;
            const double step = pixelsPerSpace(o);
            const QPointF o0 = toScreen(o.origin);
            const double x0 = o0.x() - std::floor(o0.x() / step) * step;
            const double y0 = o0.y() - std::floor(o0.y() / step) * step;
            if (!o.dots) {
                painter.setPen(QPen(qcolor(o), std::max(1.0, o.size)));
                for (double x = x0; x < width(); x += step)
                    painter.drawLine(QPointF(x, 0), QPointF(x, height()));
                for (double y = y0; y < height(); y += step)
                    painter.drawLine(QPointF(0, y), QPointF(width(), y));
            } else {
                painter.setPen(Qt::NoPen);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setBrush(qcolor(o));
                const double r = std::max(1.0, o.size) / 2 + 0.1;
                for (double x = x0; x < width(); x += step) {
                    for (double y = y0; y < height(); y += step) painter.drawEllipse(QPointF(x, y), r, r);
                }
                painter.setRenderHint(QPainter::Antialiasing, false);
            }
        } else if (o.shape == OverlayShape::Lines) {
            painter.setPen(QPen(qcolor(o), std::max(1.0, o.size)));
            for (std::size_t i = 0; i + 1 < o.points.size(); i += 2)
                painter.drawLine(toScreen(o.points[i]), toScreen(o.points[i + 1]));
        } else {
            painter.setPen(Qt::NoPen);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(qcolor(o));
            const double r = std::max(1.0, o.size) / 2 + 0.1;
            for (const auto& p : o.points) painter.drawEllipse(toScreen(p), r, r);
            painter.setRenderHint(QPainter::Antialiasing, false);
        }
    }
    // The two axes through the origin: x red, y green, like the 3D view.
    painter.setPen(QPen(QColor(209, 69, 59, 160), 1.5));
    painter.drawLine(QPointF(origin.x(), origin.y()), QPointF(width(), origin.y()));
    painter.setPen(QPen(QColor(46, 155, 79, 160), 1.5));
    painter.drawLine(QPointF(origin.x(), origin.y()), QPointF(origin.x(), 0));

    // The text, on a light card in the top-left corner, so the origin stays clear.
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

}  // namespace kh::view
