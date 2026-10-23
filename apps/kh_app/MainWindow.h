#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QStringList>

namespace ads {
class CDockManager;
}

namespace kh::app {

// The main window shell. Every panel and viewer is a dock widget in the Qt
// Advanced Docking System: it can dock to any side, tab with others, float as
// its own window and move to another monitor. Phase 0 shows the layout with
// placeholders; the plugin shell fills the panels.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // The stable ids of every panel and viewer, e.g. "kh.featuretree".
    QStringList dockIds() const;

    // The current arrangement of panels, to save and restore later.
    QByteArray saveLayout() const;

    // Puts the panels back as a saveLayout() result had them. False if the data
    // doesn't fit this version of Khan.
    bool restoreLayout(const QByteArray& layout);

private:
    ads::CDockManager* docks_;
};

}  // namespace kh::app
