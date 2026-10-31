#include "ViewOverlay.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "AppIcons.h"
#include "Icons.h"

namespace kh::app {

namespace {

constexpr int kMargin = 8;  // from the view's edges, in pixels
constexpr int kEdge = 6;    // the draggable border, in pixels
constexpr int kMinWidth = 200;
constexpr int kMinHeight = 120;
constexpr int kBackAlpha = 90;  // how solid the back is, 0 to 255

// See-through: what's under it shows, and its rows stay readable.
const char* kStyle = R"(
QAbstractScrollArea, QAbstractScrollArea > QWidget { background: transparent; border: none; }
QHeaderView { background: transparent; }
QHeaderView::section { background: transparent; border: none; border-bottom: 1px solid palette(mid); padding: 2px 6px; }
)";

}  // namespace

ViewOverlay::ViewOverlay(const QString& title, QWidget* host, bool right)
    : QWidget(host), host_(host), right_(right) {
    if (right_) wanted_.setWidth(360);           // panels on the right hold forms and tables
    setAttribute(Qt::WA_TranslucentBackground);  // paintEvent draws the rounded, see-through back
    setMouseTracking(true);
    setStyleSheet(QString::fromLatin1(kStyle));
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(kEdge, 2, kEdge, kEdge);
    layout_->setSpacing(2);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("kh.shell.overlay.bar"));
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    fold_ = new QToolButton(bar);
    fold_->setObjectName(QStringLiteral("kh.shell.overlay.fold"));
    fold_->setAutoRaise(true);
    fold_->setArrowType(Qt::DownArrow);
    fold_->setToolTip(tr("Fold up to the title, so it covers less of the view. Click again to open it."));
    connect(fold_, &QToolButton::clicked, this, [this] {
        setFolded(!folded_);
        if (onChanged) onChanged();
    });
    title_ = new QLabel(title, bar);
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    auto* dock = new QToolButton(bar);
    dock->setObjectName(QStringLiteral("kh.shell.overlay.dock"));
    dock->setAutoRaise(true);
    dock->setIcon(svgIcon(icons::app::dock_svg));
    dock->setToolTip(tr("Put %1 in its own panel beside the view.").arg(title.toLower()));
    connect(dock, &QToolButton::clicked, this, [this] {
        if (onDock) onDock();
    });
    barLayout->addWidget(fold_);
    barLayout->addWidget(title_, 1);
    barLayout->addWidget(dock);
    layout_->addWidget(bar);
    bar_ = bar;
    bar_->installEventFilter(this);

    host_->installEventFilter(this);
    hide();
}

void ViewOverlay::setBody(QWidget* body) {
    if (body_) return;
    body_ = body;
    body_->setParent(this);
    body_->installEventFilter(this);  // its content changing size moves this
    layout_->addWidget(body_, 1);
    body_->setVisible(!folded_);
    place();
}

QWidget* ViewOverlay::takeBody() {
    QWidget* body = body_;
    if (!body) return nullptr;
    layout_->removeWidget(body);
    body->removeEventFilter(this);
    body->setParent(nullptr);
    body->show();
    body_ = nullptr;
    return body;
}

void ViewOverlay::setFolded(bool folded) {
    folded_ = folded;
    fold_->setArrowType(folded ? Qt::RightArrow : Qt::DownArrow);
    if (body_) body_->setVisible(!folded);
    place();
}

void ViewOverlay::setWantedSize(QSize size) {
    wanted_ = size.expandedTo({kMinWidth, kMinHeight});
    place();
}

void ViewOverlay::place() {
    if (!host_) return;
    const int top = (topInset ? topInset() : 0) + kMargin;
    int width = std::max(kMinWidth, std::min(wanted_.width(), host_->width() - 2 * kMargin));
    // Never narrower than the panel can be, so nothing in it is cut off. (Not
    // its preferred width: that changes as it fills in, and a width that keeps
    // changing leaves wrapped text in a form the wrong height.)
    if (body_) {
        const int needs = std::max(body_->minimumWidth(), body_->minimumSizeHint().width());
        width = std::max(width, needs + 2 * kEdge);
    }
    width = std::min(width, std::max(kMinWidth, host_->width() - 2 * kMargin));
    int height = layout_->contentsMargins().top() + layout_->itemAt(0)->sizeHint().height() + kEdge;
    if (!folded_) {
        // As tall as the panel wants (its rows), up to the size the student gave it.
        int tallest = wanted_.height();
        if (body_ && body_->sizeHint().height() > 0) {
            // Wrapped text (like a material's description) is taller when narrower.
            // A form can under-report it, so never less than its own size hint.
            const int bodyWidth = width - 2 * kEdge;
            const int wants = body_->hasHeightForWidth() ? std::max(body_->heightForWidth(bodyWidth), 0)
                                                         : body_->sizeHint().height();
            tallest =
                std::min(tallest, height + layout_->spacing() + std::max(wants, body_->sizeHint().height()));
        }
        height = std::max(height, std::min(tallest, host_->height() - top - kMargin));
    }
    const QRect was = geometry();
    setGeometry(right_ ? host_->width() - kMargin - width : kMargin, top, width, height);
    raise();
    if (geometry() != was && onPlaced) onPlaced();
}

bool ViewOverlay::event(QEvent* event) {
    if (event->type() == QEvent::LayoutRequest) place();  // the panel's rows changed
    return QWidget::event(event);
}

bool ViewOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host_ && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)) {
        place();
    }
    if (watched == body_ && event->type() == QEvent::LayoutRequest) place();
    // The mouse went from the border straight onto the panel or the title bar.
    // This frame gets no move or leave for that, and the panel would keep the
    // border's resize arrows (it uses its parent's cursor).
    if ((watched == body_ || watched == bar_) && event->type() == QEvent::Enter && !dragging_) unsetCursor();
    return QWidget::eventFilter(watched, event);
}

void ViewOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Mostly clear, so the design shows through; just enough tint to read the words.
    QColor back = palette().color(QPalette::Window);
    back.setAlpha(kBackAlpha);
    QColor line = palette().color(QPalette::Mid);
    line.setAlpha(160);
    p.setPen(line);
    p.setBrush(back);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
}

int ViewOverlay::edgesAt(QPoint p) const {
    int edges = 0;
    if (right_ ? p.x() < kEdge : p.x() >= width() - kEdge) edges |= 1;  // the side towards the view's middle
    if (!folded_ && p.y() >= height() - kEdge) edges |= 2;
    return edges;
}

void ViewOverlay::showCursor(int edges) {
    switch (edges) {
        case 1:
            setCursor(Qt::SizeHorCursor);
            break;
        case 2:
            setCursor(Qt::SizeVerCursor);
            break;
        case 3:
            setCursor(right_ ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor);
            break;
        default:
            unsetCursor();
    }
}

void ViewOverlay::mousePressEvent(QMouseEvent* event) {
    dragging_ = event->button() == Qt::LeftButton ? edgesAt(event->position().toPoint()) : 0;
    dragStart_ = event->globalPosition().toPoint();
    sizeAtStart_ = size();
    event->accept();  // clicks on its back don't reach the view under it
}

void ViewOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        showCursor(edgesAt(event->position().toPoint()));
        return;
    }
    const QPoint by = event->globalPosition().toPoint() - dragStart_;
    QSize s = wanted_;
    if (dragging_ & 1) s.setWidth(sizeAtStart_.width() + (right_ ? -by.x() : by.x()));
    if (dragging_ & 2) s.setHeight(sizeAtStart_.height() + by.y());
    setWantedSize(s);
}

void ViewOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ && onChanged) onChanged();
    dragging_ = 0;
    showCursor(edgesAt(event->position().toPoint()));
}

void ViewOverlay::leaveEvent(QEvent*) {
    if (!dragging_) unsetCursor();
}

}  // namespace kh::app
