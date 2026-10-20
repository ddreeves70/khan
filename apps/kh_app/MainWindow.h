#pragma once

#include <QMainWindow>

namespace kh::app {

// The main window shell: workspace tabs, toolbar, browser tree, viewport,
// command panel and timeline. Phase 0 shows the layout with placeholders.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}  // namespace kh::app
