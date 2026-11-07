// sketchbillet: the SketchBillet desktop app.
//
//   sketchbillet [file.sb]                 open a design (or start with an empty one)
//   sketchbillet --open file.sb            the same
//   sketchbillet --select Step1,Step2        pick steps after opening (for screenshots)
//   sketchbillet --trigger id1,id2           click menu items and buttons by object name, e.g.
//                                      sb.viewer.layout.fourpanes (for screenshots)
//   sketchbillet --edges Extrude1:9,12       then pick edges of a body, numbered as
//                                      `sketchbillet_cli open --edges` lists them (for screenshots)
//   sketchbillet --settings file.ini         keep layouts in this file instead of the user's settings
//   sketchbillet --screenshot out.png        save a picture of the window and quit
//   sketchbillet --smoke-test                build the window, check it, and quit (CI)
//
// --screenshot and --smoke-test work without a screen: QT_QPA_PLATFORM=offscreen
// (the viewer then shows its flat view). For a screenshot of the 3D view on Linux
// without a screen, run it under xvfb-run with QT_QPA_PLATFORM=xcb.

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "AppLogo.h"
#include "AppSettings.h"
#include "AppUi.h"
#include "Icons.h"
#include "Shell.h"
#include "sb/core/Version.h"
#include "sb/kernel_occt/OcctKernel.h"
#include "sb/plugin/PluginHost.h"
#include "sb/plugins/Axes.h"
#include "sb/plugins/FeatureTree.h"
#include "sb/plugins/Grid.h"
#include "sb/plugins/Modeling.h"
#include "sb/plugins/MoveHandles.h"
#include "sb/plugins/Parameters.h"
#include "sb/plugins/Properties.h"
#include "sb/plugins/Sketcher.h"
#include "sb/plugins/ViewOptions.h"
#include "sb/plugins/Viewer.h"
#include "sb/sketch_planegcs/PlaneGcsSolver.h"
#include "sb/view/Graphics.h"

namespace {

QString option(const QStringList& args, const QString& name) {
    const auto i = args.indexOf(name);
    return i > 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
}

int smokeTest(sb::app::Shell& shell) {
    shell.show();
    QApplication::processEvents();
    auto fail = [](const char* what) {
        std::fprintf(stderr, "smoke test: %s\n", what);
        return 1;
    };
    const auto ids = shell.dockIds();
    for (const char* id : {"sb.featuretree.panel", "sb.properties.panel", "sb.viewer.main"}) {
        if (!ids.contains(QString::fromLatin1(id))) return fail("a built-in panel is missing");
    }
    auto* box = shell.action("sb.modeling.box");
    if (!box) return fail("Create > Box is missing");
    box->trigger();
    QApplication::processEvents();
    if (!shell.undoAction()->isEnabled()) return fail("Create > Box didn't add a step");
    const auto layout = shell.saveLayout();
    if (layout.isEmpty() || !shell.restoreLayout(layout)) return fail("the layout didn't save and restore");
    std::printf("sketchbillet smoke test: %lld panels, Create > Box works, layout saved and restored\n",
                static_cast<long long>(ids.size()));
    return 0;
}

int screenshot(sb::app::Shell& shell, const QString& file) {
    shell.resize(1280, 800);
    shell.show();
    // Give the 3D views time to start and draw (and the view cube to settle).
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 1500) QApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!shell.grab().save(file)) {
        std::fprintf(stderr, "SketchBillet couldn't save the picture %s. Check the folder exists.\n",
                     qPrintable(file));
        return 1;
    }
    std::printf("wrote %s\n", qPrintable(file));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    sb::view::prepareGraphics();  // before the QApplication: 3D views share OpenGL
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SketchBillet"));
    QApplication::setOrganizationName(QStringLiteral("SketchBillet"));
    QApplication::setApplicationVersion(QString::fromStdString(std::string(sb::version())));
    // The icon on the window and the taskbar, and on Linux the menu entry that
    // packaging/linux/org.sketchbillet.SketchBillet.desktop installs.
    QApplication::setWindowIcon(sb::app::svgIcon(sb::icons::app::sketchbillet_svg));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.sketchbillet.SketchBillet"));
    const QStringList args = QApplication::arguments();

    const QString settingsFile = option(args, QStringLiteral("--settings"));
    auto settings = settingsFile.isEmpty() ? std::make_unique<QSettings>()
                                           : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);

    // Plugins: the built-in ones, then folders of outside plugins: "plugins" next
    // to the program, and the user's own plugin folder.
    auto kernel = sb::kernel::makeOcctKernel();
    auto solver = sb::sketch::makePlaneGcsSolver();
    sb::app::AppUi ui;
    sb::plugin::PluginHost host(kernel.get(), solver.get());
    host.provide<sb::ui::UiRegistry>(ui);
    host.addBuiltin(sb::plugins::modelingPlugin());
    host.addBuiltin(sb::plugins::featureTreePlugin());
    host.addBuiltin(sb::plugins::propertiesPlugin());
    host.addBuiltin(sb::plugins::viewOptionsPlugin());
    host.addBuiltin(sb::plugins::axesPlugin());
    host.addBuiltin(sb::plugins::gridPlugin());
    host.addBuiltin(sb::plugins::viewerPlugin());
    host.addBuiltin(sb::plugins::sketcherPlugin());
    host.addBuiltin(sb::plugins::moveHandlesPlugin());
    host.addBuiltin(sb::plugins::parametersPlugin());
    host.addFolder((QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")).toStdWString());
    host.addFolder(
        (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins"))
            .toStdWString());
    // Plugins' settings last between runs, with the app's own.
    sb::app::AppSettings pluginSettings(*settings);
    host.setSettings(pluginSettings);
    // How long a plugin may take to start before it gets a warning (0 turns it off).
    host.setSlowStartLimit(
        std::chrono::milliseconds(settings->value(QStringLiteral("plugins/slowStartMs"), 500).toInt()));
    host.loadAll();
    // Plugin problems and warnings go to the log, for plugin developers; the
    // Plugin manager will show them too. Students aren't interrupted.
    for (const auto& p : host.plugins()) {
        for (const auto& d : p.issues) {
            if (d.severity == sb::Severity::Info) continue;
            std::fprintf(stderr, "%s [%s]: %s\n", d.severity == sb::Severity::Error ? "error" : "warning",
                         d.code.c_str(), d.message.c_str());
        }
    }

    sb::app::Shell shell(host, ui, *settings);

    QString open = option(args, QStringLiteral("--open"));
    if (open.isEmpty() && args.size() > 1 &&
        args.at(1).endsWith(QStringLiteral(".sb"), Qt::CaseInsensitive)) {
        open = args.at(1);
    }
    if (!open.isEmpty()) shell.openDesign(QDir::current().absoluteFilePath(open).toStdWString());
    if (const auto select = option(args, QStringLiteral("--select")); !select.isEmpty()) {
        shell.selectSteps(select.split(QLatin1Char(',')));
    }

    if (const auto trigger = option(args, QStringLiteral("--trigger")); !trigger.isEmpty()) {
        for (const auto& name : trigger.split(QLatin1Char(','))) {
            auto* action = shell.findChild<QAction*>(name);
            if (!action) {
                std::fprintf(stderr, "There is no menu item or button called %s.\n", qPrintable(name));
                return 1;
            }
            QApplication::processEvents();  // so menu items have caught up with --open and --select
            if (!action->isEnabled()) {
                std::fprintf(stderr, "%s is greyed out, so it can't be clicked. Pick what it needs first.\n",
                             qPrintable(name));
                return 1;
            }
            action->trigger();
            QApplication::processEvents();
        }
    }

    if (const auto edges = option(args, QStringLiteral("--edges")); !edges.isEmpty()) {
        const auto colon = edges.lastIndexOf(QLatin1Char(':'));
        QList<int> numbers;
        bool ok = colon > 0;
        for (const auto& n : edges.mid(colon + 1).split(QLatin1Char(','))) {
            bool isNumber = false;
            numbers << n.toInt(&isNumber);
            ok = ok && isNumber;
        }
        if (!ok || !shell.selectEdges(edges.left(colon), numbers)) {
            std::fprintf(stderr,
                         "--edges must be a body's name and its edge numbers, like Extrude1:9,12 (see "
                         "sketchbillet_cli open <design.sb> --edges <body>).\n");
            return 1;
        }
        QApplication::processEvents();
    }

    if (args.contains(QStringLiteral("--smoke-test"))) return smokeTest(shell);
    if (const auto file = option(args, QStringLiteral("--screenshot")); !file.isEmpty())
        return screenshot(shell, file);

    shell.show();
    return QApplication::exec();
}
