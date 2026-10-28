#include "Icons.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

namespace kh::app {

QIcon svgIcon(std::string_view svg) {
    QByteArray text(svg.data(), static_cast<qsizetype>(svg.size()));
    if (text.trimmed().isEmpty()) return {};
    // "currentColor" is the theme's text color, so lines show on light and dark themes.
    text.replace("currentColor", QApplication::palette().color(QPalette::ButtonText).name().toLatin1());
    QSvgRenderer renderer(text);
    if (!renderer.isValid()) return {};
    // Drawn ahead of time at the sizes menus and toolbars use, for ordinary and
    // high-DPI screens; Qt picks the closest and makes the greyed-out look itself.
    QIcon icon;
    for (const int size : {16, 24, 32, 48}) {
        for (const qreal scale : {1.0, 2.0, 3.0}) {
            const int px = qRound(size * scale);
            QPixmap pm(px, px);
            pm.fill(Qt::transparent);
            {
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                renderer.render(&p, QRectF(0, 0, px, px));
            }
            pm.setDevicePixelRatio(scale);
            icon.addPixmap(pm);
        }
    }
    return icon;
}

}  // namespace kh::app
