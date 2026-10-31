#pragma once

#include <QSize>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;
class QVBoxLayout;

namespace kh::app {

// A panel floating over the 3D view, see-through, at its top left, like the
// browser in Fusion (ui::PanelSpec::floatsOverView). It's as tall as the panel's
// sizeHint() asks, up to the size the student drags it to. It's a child of the view's
// dock, kept inside it: a title bar with a fold arrow and a dock button, the
// panel's widget under it, and edges to drag for its size.
class ViewOverlay final : public QWidget {
public:
    // A wanted height meaning "as tall as its content needs".
    static constexpr int kAsTallAsItNeeds = 100000;

    // host: the widget it floats over (the view's dock), which owns it.
    // right: at the host's top right instead of its top left.
    ViewOverlay(const QString& title, QWidget* host, bool right = false);

    // The panel's widget. setBody() takes it over; takeBody() gives it back,
    // with no parent.
    void setBody(QWidget* body);
    QWidget* takeBody();
    QWidget* body() const { return body_; }

    // Folded up to its title bar.
    void setFolded(bool folded);
    bool folded() const { return folded_; }

    // The size the student gave it (it shrinks to fit a small view).
    void setWantedSize(QSize size);
    QSize wantedSize() const { return wanted_; }

    // How far down from the host's top it starts, e.g. below a toolbar there,
    // or below another floating panel on the same side.
    std::function<int()> topInset;
    // It moved or changed size (so panels under it can move too).
    std::function<void()> onPlaced;
    // The dock button was clicked.
    std::function<void()> onDock;
    // Folded, unfolded or resized by the student, so it can be remembered.
    std::function<void()> onChanged;

    // Puts it at the host's top left (or right), as big as it should be.
    void place();
    bool right() const { return right_; }

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Which edges a point is on: bit 1 the side edge towards the view's middle
    // (right edge, or left edge for a panel on the right), bit 2 the bottom one.
    int edgesAt(QPoint p) const;
    void showCursor(int edges);

    QWidget* host_ = nullptr;
    QWidget* body_ = nullptr;
    QWidget* bar_ = nullptr;  // the title bar
    QVBoxLayout* layout_ = nullptr;
    QToolButton* fold_ = nullptr;
    QLabel* title_ = nullptr;
    // Its width, and the most it may grow to; until the student drags it, as tall
    // as its content (up to the view's height).
    QSize wanted_{300, kAsTallAsItNeeds};
    bool folded_ = false;
    bool right_ = false;
    int dragging_ = 0;  // edges being dragged
    QPoint dragStart_;
    QSize sizeAtStart_;
};

}  // namespace kh::app
