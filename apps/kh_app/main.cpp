// kh_app: the Khan desktop app.
//
//   kh_app [file.khan]                 open a design (or start with an empty one)
//   kh_app --open file.khan            the same
//   kh_app --select Step1,Step2        pick steps after opening (for screenshots)
//   kh_app --trigger id1,id2           click menu items and buttons by object name, e.g.
//                                      kh.viewer.layout.fourpanes (for screenshots)
//   kh_app --settings file.ini         keep layouts in this file instead of the user's settings
//   kh_app --screenshot out.png        save a picture of the window and quit
//   kh_app --smoke-test                build the window, check it, and quit (CI)
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

#include "AppSettings.h"
#include "AppUi.h"
#include "Shell.h"
#include "kh/kernel_occt/OcctKernel.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Axes.h"
#include "kh/plugins/FeatureTree.h"
#include "kh/plugins/Grid.h"
#include "kh/plugins/Modeling.h"
#include "kh/plugins/MoveHandles.h"
#include "kh/plugins/Properties.h"
#include "kh/plugins/Sketcher.h"
#include "kh/plugins/ViewOptions.h"
#include "kh/plugins/Viewer.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"
#include "kh/view/Graphics.h"

namespace {

QString option(const QStringList& args, const QString& name) {
    const auto i = args.indexOf(name);
    return i > 0 && i + 1 < args.size() ? args.at(i + 1) : QString();
}

int smokeTest(kh::app::Shell& shell) {
    shell.show();
    QApplication::processEvents();
    auto fail = [](const char* what) {
        std::fprintf(stderr, "smoke test: %s\n", what);
        return 1;
    };
    const auto ids = shell.dockIds();
    for (const char* id : {"kh.featuretree.panel", "kh.properties.panel", "kh.viewer.main"}) {
        if (!ids.contains(QString::fromLatin1(id))) return fail("a built-in panel is missing");
    }
    auto* box = shell.action("kh.modeling.box");
    if (!box) return fail("Create > Box is missing");
    box->trigger();
    QApplication::processEvents();
    if (!shell.undoAction()->isEnabled()) return fail("Create > Box didn't add a step");
    const auto layout = shell.saveLayout();
    if (layout.isEmpty() || !shell.restoreLayout(layout)) return fail("the layout didn't save and restore");
    std::printf("kh_app smoke test: %lld panels, Create > Box works, layout saved and restored\n",
                static_cast<long long>(ids.size()));
    return 0;
}

int screenshot(kh::app::Shell& shell, const QString& file) {
    shell.resize(1280, 800);
    shell.show();
    // Give the 3D views time to start and draw (and the view cube to settle).
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 1500) QApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!shell.grab().save(file)) {
        std::fprintf(stderr, "Khan couldn't save the picture %s. Check the folder exists.\n",
                     qPrintable(file));
        return 1;
    }
    std::printf("wrote %s\n", qPrintable(file));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    kh::view::prepareGraphics();  // before the QApplication: 3D views share OpenGL
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Khan"));
    QApplication::setOrganizationName(QStringLiteral("Khan"));
    const QStringList args = QApplication::arguments();

    const QString settingsFile = option(args, QStringLiteral("--settings"));
    auto settings = settingsFile.isEmpty() ? std::make_unique<QSettings>()
                                           : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);

    // Plugins: the built-in ones, then folders of outside plugins: "plugins" next
    // to the program, and the user's own plugin folder.
    auto kernel = kh::kernel::makeOcctKernel();
    auto solver = kh::sketch::makePlaneGcsSolver();
    kh::app::AppUi ui;
    kh::plugin::PluginHost host(kernel.get(), solver.get());
    host.provide<kh::ui::UiRegistry>(ui);
    host.addBuiltin(kh::plugins::modelingPlugin());
    host.addBuiltin(kh::plugins::featureTreePlugin());
    host.addBuiltin(kh::plugins::propertiesPlugin());
    host.addBuiltin(kh::plugins::viewOptionsPlugin());
    host.addBuiltin(kh::plugins::axesPlugin());
    host.addBuiltin(kh::plugins::gridPlugin());
    host.addBuiltin(kh::plugins::viewerPlugin());
    host.addBuiltin(kh::plugins::sketcherPlugin());
    host.addBuiltin(kh::plugins::moveHandlesPlugin());
    host.addFolder((QCoreApplication::applicationDirPath() + QStringLiteral("/plugins")).toStdWString());
    host.addFolder(
        (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins"))
            .toStdWString());
    // Plugins' settings last between runs, with the app's own.
    kh::app::AppSettings pluginSettings(*settings);
    host.setSettings(pluginSettings);
    // How long a plugin may take to start before it gets a warning (0 turns it off).
    host.setSlowStartLimit(
        std::chrono::milliseconds(settings->value(QStringLiteral("plugins/slowStartMs"), 500).toInt()));
    host.loadAll();
    // Plugin problems and warnings go to the log, for plugin developers; the
    // Plugin manager will show them too. Students aren't interrupted.
    for (const auto& p : host.plugins()) {
        for (const auto& d : p.issues) {
            if (d.severity == kh::Severity::Info) continue;
            std::fprintf(stderr, "%s [%s]: %s\n", d.severity == kh::Severity::Error ? "error" : "warning",
                         d.code.c_str(), d.message.c_str());
        }
    }

    kh::app::Shell shell(host, ui, *settings);

    QString open = option(args, QStringLiteral("--open"));
    if (open.isEmpty() && args.size() > 1 &&
        args.at(1).endsWith(QStringLiteral(".khan"), Qt::CaseInsensitive)) {
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

    if (args.contains(QStringLiteral("--smoke-test"))) return smokeTest(shell);
    if (const auto file = option(args, QStringLiteral("--screenshot")); !file.isEmpty())
        return screenshot(shell, file);

    shell.show();
    return QApplication::exec();
}
