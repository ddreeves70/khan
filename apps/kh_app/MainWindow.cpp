#include "MainWindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QStatusBar>
#include <QToolBar>

#include "kh/core/Version.h"

namespace kh::app {

namespace {
QLabel* placeholder(const QString& text) {
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setMargin(16);
    return label;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Khan"));
    resize(1200, 760);

    auto* tools = addToolBar(tr("Tools"));
    tools->setMovable(false);
    tools->addAction(tr("Sketch"));
    tools->addAction(tr("Extrude"));
    tools->addAction(tr("Export"));

    setCentralWidget(placeholder(tr("3D viewport (OCCT viewer arrives in Phase 0, week 4)")));

    auto* browser = new QDockWidget(tr("Browser"), this);
    browser->setWidget(placeholder(tr("Bodies and sketches")));
    addDockWidget(Qt::LeftDockWidgetArea, browser);

    auto* timeline = new QDockWidget(tr("Timeline"), this);
    timeline->setWidget(placeholder(tr("Your steps will appear here as you build.")));
    addDockWidget(Qt::BottomDockWidgetArea, timeline);

    const auto v = kh::version();
    statusBar()->showMessage(tr("Version %1").arg(QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()))));
}

}  // namespace kh::app
