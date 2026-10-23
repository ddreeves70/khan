#include "MainWindow.h"

#include <DockManager.h>
#include <DockWidget.h>

#include <QLabel>
#include <QStatusBar>
#include <QToolBar>

#include "kh/core/Version.h"

namespace kh::app {

namespace {

constexpr int kLayoutVersion = 1;  // bump when dock ids change, so old layouts are ignored

QLabel* placeholder(const QString& text) {
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setMargin(16);
    label->setWordWrap(true);
    return label;
}

// A dock widget with a stable id: saved layouts, menus and plugins find it by id.
ads::CDockWidget* panel(ads::CDockManager* manager, const QString& id, const QString& title,
                        QWidget* content) {
    auto* dock = new ads::CDockWidget(manager, title);
    dock->setObjectName(id);
    dock->setWidget(content);
    return dock;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Khan"));
    resize(1200, 760);

    // Flags must be set before the dock manager is created.
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    docks_ = new ads::CDockManager(this);

    auto* tools = addToolBar(tr("Tools"));
    tools->setMovable(false);
    tools->addAction(tr("Sketch"));
    tools->addAction(tr("Extrude"));
    tools->addAction(tr("Export"));

    docks_->setCentralWidget(panel(docks_, QStringLiteral("kh.viewer.main"), tr("Viewer"),
                                   placeholder(tr("3D view (the OCCT viewer arrives with kh.viewer)"))));
    docks_->addDockWidget(ads::LeftDockWidgetArea,
                          panel(docks_, QStringLiteral("kh.featuretree"), tr("Feature tree"),
                                placeholder(tr("Your steps will appear here as you build."))));
    docks_->addDockWidget(ads::RightDockWidgetArea,
                          panel(docks_, QStringLiteral("kh.properties"), tr("Properties"),
                                placeholder(tr("Pick a step to see and change its numbers."))));
    docks_->addDockWidget(ads::BottomDockWidgetArea,
                          panel(docks_, QStringLiteral("kh.timeline"), tr("Timeline"),
                                placeholder(tr("The history of your design."))));

    const auto v = kh::version();
    statusBar()->showMessage(
        tr("Version %1").arg(QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()))));
}

QStringList MainWindow::dockIds() const {
    QStringList ids = docks_->dockWidgetsMap().keys();
    ids.sort();
    return ids;
}

QByteArray MainWindow::saveLayout() const {
    return docks_->saveState(kLayoutVersion);
}

bool MainWindow::restoreLayout(const QByteArray& layout) {
    return docks_->restoreState(layout, kLayoutVersion);
}

}  // namespace kh::app
