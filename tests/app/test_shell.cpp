// Tests for the desktop shell and its built-in panels, driven like a student would:
// menu items, picking rows, typing numbers.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <cstring>
#include <memory>

#include "AppSettings.h"
#include "AppUi.h"
#include "Shell.h"
#include "kh/grid/Grid.h"
#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/Feature.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Axes.h"
#include "kh/plugins/FeatureTree.h"
#include "kh/plugins/Grid.h"
#include "kh/plugins/Modeling.h"
#include "kh/plugins/Properties.h"
#include "kh/plugins/ViewOptions.h"
#include "kh/plugins/Viewer.h"
#include "kh/ui/UiRegistry.h"
#include "kh/view/FlatView.h"

using namespace kh;
using Catch::Approx;

namespace {

void settle() {
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

// A plugin that adds a panel and then fails: its panel must not appear.
void panelThenFail(plugin::PluginContext& ctx) {
    if (auto* ui = ctx.service<ui::UiRegistry>()) {
        ui->addPanel({"test.broken.panel", QStringLiteral("Broken"), ui::PanelArea::Left,
                      [] { return new QLabel(QStringLiteral("should not show")); }});
    }
    ctx.fail("Testing a plugin that fails.");
}

// A plugin with a setting, a check-mark item in a View submenu, and a status-bar
// item, like the plugin guide's examples.
void units(plugin::PluginContext& ctx) {
    auto& settings = ctx.settings();
    ctx.addAction(
        {.id = "test.units.inches",
         .text = "Inches",
         .menu = "View/Units",
         .tip = "Show sizes in inches.",
         .run = [&settings] { settings.set("inches", !settings.flag("inches")); },
         .checked = [&settings] { return settings.flag("inches"); },
         .currentText =
             [&settings] { return std::string(settings.flag("inches") ? "Inches (on)" : "Inches"); }});
    ctx.addAction({.id = "test.units.oops", .text = "Oops", .menu = "View/Units", .run = [] {
                       throw std::runtime_error("no tape");
                   }});
    if (auto* ui = ctx.service<ui::UiRegistry>()) {
        ui->addStatusItem({"test.units.status", [&settings] {
                               return new QLabel(settings.flag("inches") ? QStringLiteral("in")
                                                                         : QStringLiteral("mm"));
                           }});
    }
}

// A plugin that uses the grid through its contract, like a sketcher would.
const grid::IGrid* seenGrid = nullptr;
viewoptions::IViewOptions* seenOptions = nullptr;
void gridClient(plugin::PluginContext& ctx) {
    ctx.onStart([&ctx] {
        seenGrid = ctx.service<grid::IGrid>();
        seenOptions = ctx.service<viewoptions::IViewOptions>();
    });
}

// The grid, among the overlays a view draws (the axes are overlays too).
const view::ViewOverlay* lattice(const view::FlatView* v) {
    for (const auto& o : v->overlays()) {
        if (o.shape == view::OverlayShape::Lattice) return &o;
    }
    return nullptr;
}

// What a view draws for the axes: lines and labels, by color.
int countShape(const view::FlatView* v, view::OverlayShape shape) {
    int n = 0;
    for (const auto& o : v->overlays()) n += o.shape == shape ? 1 : 0;
    return n;
}

struct App {
    QTemporaryDir dir;
    QSettings settings{dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    app::AppSettings pluginSettings{settings};
    app::AppUi ui;
    plugin::PluginHost host{kernel.get()};
    std::unique_ptr<app::Shell> shell;

    App() {
        host.provide<ui::UiRegistry>(ui);
        host.setSettings(pluginSettings);
        host.addBuiltin(plugins::modelingPlugin());
        host.addBuiltin(plugins::featureTreePlugin());
        host.addBuiltin(plugins::propertiesPlugin());
        host.addBuiltin(plugins::viewOptionsPlugin());
        host.addBuiltin(plugins::axesPlugin());
        host.addBuiltin(plugins::gridPlugin());
        host.addBuiltin(plugins::viewerPlugin());
        host.addBuiltin({R"({"id":"test.broken","name":"Broken","version":"1","apiVersion":)" +
                             std::to_string(plugin::kPluginApiVersion) + "}",
                         &panelThenFail});
        host.addBuiltin(
            {R"({"id":"test.gridclient","name":"Grid client","version":"1","uses":["kh.grid","kh.viewoptions"],"apiVersion":)" +
                 std::to_string(plugin::kPluginApiVersion) + "}",
             &gridClient});
        host.addBuiltin({R"({"id":"test.units","name":"Units","version":"1","apiVersion":)" +
                             std::to_string(plugin::kPluginApiVersion) + "}",
                         &units});
        host.loadAll();
        open();
    }

    void open() {
        shell = std::make_unique<app::Shell>(host, ui, settings);
        shell->askUnsaved = [](const QString&) { return app::Shell::Unsaved::Discard; };
        shell->show();
        settle();
    }

    QAction* menu(const char* id) { return shell->findChild<QAction*>(QString::fromLatin1(id)); }
    view::FlatView* flat() { return shell->findChild<view::FlatView*>(QStringLiteral("kh.view.flat")); }

    const model::Document& doc() { return *host.documents().find(shell->activeDesign()); }
    QTreeWidget* tree() {
        return shell->panelWidget(QStringLiteral("kh.featuretree.panel"))
            ->findChild<QTreeWidget*>(QStringLiteral("kh.featuretree.tree"));
    }
    QWidget* properties() { return shell->panelWidget(QStringLiteral("kh.properties.panel")); }
    template <typename W>
    W* field(const char* name) {
        return properties()->findChild<W*>(QString::fromLatin1(name));
    }
    void click(const char* actionId) {
        auto* a = shell->action(actionId);
        REQUIRE(a);
        REQUIRE(a->isEnabled());
        a->trigger();
        settle();
    }
    void pickRows(std::initializer_list<int> rows) {
        tree()->clearSelection();
        for (int r : rows) tree()->topLevelItem(r)->setSelected(true);
        settle();
    }
};

}  // namespace

TEST_CASE("the shell is built from plugins", "[app]") {
    App app;
    CHECK(app.shell->dockIds() ==
          QStringList({"kh.featuretree.panel", "kh.properties.panel", "kh.viewer.main",
                       "kh.viewoptions.panel"}));  // no test.broken.panel
    CHECK(app.shell->action("kh.modeling.box"));
    CHECK(app.shell->action("kh.modeling.cut"));
    CHECK_FALSE(app.shell->action("kh.modeling.cut")->isEnabled());  // needs two steps picked
    CHECK(app.doc().featureCount() == 0);
    CHECK(app.shell->windowTitle() == QStringLiteral("Untitled[*] - Khan"));
}

TEST_CASE("Khan's own plugins follow the rules: no warnings, and each has a tier", "[app]") {
    App app;
    int builtins = 0;
    for (const auto& p : app.host.plugins()) {
        if (p.manifest.id.rfind("kh.", 0) != 0) continue;
        ++builtins;
        INFO(p.manifest.id);
        CHECK(p.state == plugin::PluginState::Loaded);
        CHECK(p.issues.empty());  // as strict as kh_cli plugins --strict
        CHECK_FALSE(p.manifest.tier.empty());
    }
    CHECK(builtins == 7);
}

TEST_CASE("Create > Box adds a step, selects it, and shows its numbers", "[app]") {
    App app;
    app.click("kh.modeling.box");
    REQUIRE(app.doc().featureCount() == 1);
    CHECK(app.tree()->topLevelItemCount() == 1);
    CHECK(app.tree()->topLevelItem(0)->text(0) == QStringLiteral("Box1"));
    CHECK(app.tree()->topLevelItem(0)->text(2) == QStringLiteral("ok"));
    CHECK(app.tree()->topLevelItem(0)->isSelected());
    CHECK(app.field<QDoubleSpinBox>("size.x")->value() == Approx(20.0));
    CHECK(app.shell->undoAction()->text() == QStringLiteral("Undo Add Box1"));
    CHECK(app.shell->isWindowModified());
}

TEST_CASE("typing a number in Properties edits the step, and undo puts it back", "[app]") {
    App app;
    app.click("kh.modeling.box");
    const auto id = app.doc().featureAt(0).id();
    auto size = [&] { return static_cast<const model::BoxFeature*>(app.doc().find(id))->size(); };

    app.field<QDoubleSpinBox>("size.x")->setValue(35.0);
    settle();
    CHECK(size().x == 35.0);

    app.shell->undoAction()->trigger();
    settle();
    CHECK(size().x == 20.0);
    CHECK(app.field<QDoubleSpinBox>("size.x")->value() == Approx(20.0));  // the panel follows
    app.shell->redoAction()->trigger();
    settle();
    CHECK(size().x == 35.0);
}

TEST_CASE("quick changes to one number are one undo step", "[app]") {
    App app;
    app.click("kh.modeling.cylinder");
    auto* radius = app.field<QDoubleSpinBox>("radius");
    radius->setValue(11.0);
    radius->setValue(12.0);
    radius->setValue(13.0);  // like holding the up arrow
    settle();
    app.shell->undoAction()->trigger();
    settle();
    CHECK(static_cast<const model::CylinderFeature&>(app.doc().featureAt(0)).radius() == 10.0);
}

TEST_CASE("picking two steps enables Combine, which makes a Cut", "[app]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.cylinder");
    CHECK_FALSE(app.shell->action("kh.modeling.cut")->isEnabled());
    app.pickRows({0, 1});
    CHECK(app.host.selection().features().size() == 2);
    app.click("kh.modeling.cut");
    REQUIRE(app.doc().featureCount() == 3);
    CHECK(app.doc().featureAt(2).name() == "Cut1");
    CHECK(app.tree()->topLevelItem(2)->text(1) == QStringLiteral("Cut"));
    CHECK(app.field<QComboBox>("operation")->currentText().startsWith(QStringLiteral("Cut")));
}

TEST_CASE("renaming in the tree, and refusing a taken name", "[app]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.box");
    app.tree()->topLevelItem(0)->setText(0, QStringLiteral("Plate"));
    settle();
    CHECK(app.doc().featureAt(0).name() == "Plate");

    app.tree()->topLevelItem(1)->setText(0, QStringLiteral("Plate"));  // taken
    settle();
    CHECK(app.doc().featureAt(1).name() == "Box2");
    CHECK(app.tree()->topLevelItem(1)->text(0) == QStringLiteral("Box2"));  // put back
    REQUIRE_FALSE(app.shell->lastProblems().empty());
    CHECK(app.shell->lastProblems()[0].code == "feature.name_taken");
}

TEST_CASE("Edit > Duplicate step copies the picked step and the steps it uses", "[app][copy]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.cylinder");
    app.pickRows({0, 1});
    app.click("kh.modeling.cut");
    CHECK(app.shell->action("kh.modeling.duplicate")->isEnabled());  // Cut1 is picked
    app.click("kh.modeling.duplicate");
    REQUIRE(app.doc().featureCount() == 6);
    CHECK(app.doc().featureAt(3).name() == "Box2");
    CHECK(app.doc().featureAt(4).name() == "Cylinder2");
    CHECK(app.doc().featureAt(5).name() == "Cut2");
    REQUIRE(app.host.selection().features().size() == 1);
    CHECK(app.host.selection().features()[0] == app.doc().featureAt(5).id());  // the copy is picked
    app.shell->undoAction()->trigger();
    settle();
    CHECK(app.doc().featureCount() == 3);
}

TEST_CASE("Copy and Paste put steps into another design", "[app][copy]") {
    App app;
    CHECK_FALSE(app.menu("kh.shell.copy")->isEnabled());  // nothing picked
    app.click("kh.modeling.box");                         // Box1 is picked
    app.tree()->topLevelItem(0)->setText(0, QStringLiteral("Plate"));
    settle();
    app.pickRows({0});
    REQUIRE(app.menu("kh.shell.copy")->isEnabled());
    app.menu("kh.shell.copy")->trigger();
    settle();
    CHECK(app.menu("kh.shell.paste")->isEnabled());
    CHECK(app.shell->statusBar()->currentMessage() == QStringLiteral("Copied 1 step."));

    REQUIRE(app.shell->newDesign());
    settle();
    CHECK(app.doc().featureCount() == 0);
    app.menu("kh.shell.paste")->trigger();
    settle();
    REQUIRE(app.doc().featureCount() == 1);
    CHECK(app.doc().featureAt(0).name() == "Plate");
    CHECK(app.tree()->topLevelItem(0)->text(0) == QStringLiteral("Plate"));
    CHECK(app.host.selection().features() == std::vector<FeatureId>{app.doc().featureAt(0).id()});
    CHECK(app.doc().undoLabel() == "Paste 1 step");

    app.menu("kh.shell.paste")->trigger();  // again: a second, renamed copy
    settle();
    REQUIRE(app.doc().featureCount() == 2);
    CHECK(app.doc().featureAt(1).name() == "Plate1");
    CHECK(app.doc().featureAt(1).id() != app.doc().featureAt(0).id());
}

TEST_CASE("Edit > Turn off and Turn on change every picked step, as one undo", "[app][suppress]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.cylinder");
    app.click("kh.modeling.box");
    app.tree()->clearSelection();
    settle();
    CHECK_FALSE(app.shell->action("kh.modeling.turn_off")->isEnabled());  // nothing picked
    app.pickRows({0, 2});
    app.click("kh.modeling.turn_off");
    CHECK(app.doc().featureAt(0).suppressed());
    CHECK_FALSE(app.doc().featureAt(1).suppressed());
    CHECK(app.doc().featureAt(2).suppressed());
    CHECK(app.tree()->topLevelItem(0)->text(2) == QStringLiteral("off"));
    CHECK(app.tree()->topLevelItem(2)->text(2) == QStringLiteral("off"));
    CHECK(app.tree()->topLevelItem(1)->text(2) == QStringLiteral("ok"));
    CHECK(app.shell->undoAction()->text() == QStringLiteral("Undo Turn off 2 steps"));

    app.click("kh.modeling.turn_off");  // again: they're already off
    REQUIRE(app.shell->lastProblems().size() == 1);
    CHECK(app.shell->lastProblems()[0].code == "feature.already_off");

    app.pickRows({0});
    app.click("kh.modeling.turn_on");
    CHECK_FALSE(app.doc().featureAt(0).suppressed());
    CHECK(app.doc().featureAt(2).suppressed());

    app.shell->undoAction()->trigger();
    settle();
    CHECK(app.doc().featureAt(0).suppressed());
    app.shell->undoAction()->trigger();
    settle();
    CHECK_FALSE(app.doc().featureAt(0).suppressed());
    CHECK_FALSE(app.doc().featureAt(2).suppressed());
    CHECK(app.doc().featureCount() == 3);
}

TEST_CASE("Paste explains when the clipboard has no steps", "[app][copy]") {
    App app;
    QGuiApplication::clipboard()->setText(QStringLiteral("just some words"));
    settle();
    CHECK_FALSE(app.menu("kh.shell.paste")->isEnabled());
    app.shell->pasteSteps();  // Ctrl+V does this even if the menu item is grey
    REQUIRE(app.shell->lastProblems().size() == 1);
    CHECK(app.shell->lastProblems()[0].code == "paste.not_steps");
    CHECK(app.doc().featureCount() == 0);
}

TEST_CASE("a step that doesn't build shows as failed, with the reason", "[app]") {
    App app;
    app.click("kh.modeling.cylinder");
    // Point the axis nowhere: the kernel can't build it.
    app.field<QDoubleSpinBox>("axis.z")->setValue(0.0);
    settle();
    CHECK(app.tree()->topLevelItem(0)->text(2) == QStringLiteral("failed"));
    CHECK_FALSE(app.field<QLabel>("kh.properties.status")->text().isEmpty());
}

TEST_CASE("save, then open again", "[app]") {
    App app;
    app.click("kh.modeling.box");
    const auto path = app.dir.filePath(QStringLiteral("part.khan")).toStdWString();
    REQUIRE(app.shell->saveDesign(path).empty());
    CHECK_FALSE(app.shell->isWindowModified());
    CHECK(app.shell->activePath() == std::filesystem::path(path));

    REQUIRE(app.shell->newDesign());
    CHECK(app.doc().featureCount() == 0);
    REQUIRE(app.shell->openDesign(path).empty());
    settle();
    CHECK(app.doc().featureCount() == 1);
    CHECK(app.tree()->topLevelItem(0)->text(0) == QStringLiteral("Box1"));
    CHECK(app.host.documents().ids().size() == 1);  // one design at a time
}

namespace {

// How many triangles an STL file holds, binary or text; -1 if it isn't one.
long stlTriangles(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QByteArray data = f.readAll();
    if (data.startsWith("solid") && data.contains("endsolid")) return data.count("facet normal");
    if (data.size() < 84) return -1;
    quint32 count = 0;
    std::memcpy(&count, data.constData() + 80, 4);  // little-endian, as STL files are
    return data.size() == 84 + 50 * static_cast<qsizetype>(count) ? static_cast<long>(count) : -1;
}

QByteArray fileStart(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(64) : QByteArray();
}

}  // namespace

TEST_CASE("File > Export writes the part as STL or STEP", "[app][export]") {
    App app;
    app.click("kh.modeling.box");
    auto* exportAct = app.menu("kh.shell.export");
    REQUIRE(exportAct);
    CHECK(exportAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+E")));
    const QString stl = app.dir.filePath(QStringLiteral("part.stl"));
    QString suggested;
    app.shell->askExportPath = [&](const QString& s) {
        suggested = s;
        return stl;
    };
    exportAct->trigger();
    settle();
    CHECK(suggested == QStringLiteral("Untitled.stl"));
    CHECK(stlTriangles(stl) == 12);  // a box: 6 faces, 2 triangles each
    CHECK(app.shell->lastProblems().empty());
    // The design itself is unchanged, and still needs saving.
    CHECK(app.doc().featureCount() == 1);
    CHECK(app.shell->isWindowModified());

    const QString step = app.dir.filePath(QStringLiteral("part.STEP"));  // any capitals
    REQUIRE(app.shell->exportDesign(step.toStdWString()).empty());
    CHECK(fileStart(step).startsWith("ISO-10303-21"));
}

TEST_CASE("Export explains what to do when it can't pick one body", "[app][export]") {
    App app;
    const auto path = [&](const char* name) {
        return app.dir.filePath(QString::fromLatin1(name)).toStdWString();
    };

    auto problems = app.shell->exportDesign(path("empty.stl"));  // nothing made yet
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "export.nothing");
    CHECK(problems[0].message.find("Create > Box") != std::string::npos);

    app.click("kh.modeling.box");
    app.click("kh.modeling.cylinder");  // two separate bodies
    app.tree()->clearSelection();
    settle();
    problems = app.shell->exportDesign(path("two.stl"));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "export.which");
    CHECK(problems[0].message.find("2 bodies") != std::string::npos);
    CHECK_FALSE(QFile::exists(app.dir.filePath(QStringLiteral("two.stl"))));

    app.pickRows({1});  // pick the cylinder: that's what's exported
    REQUIRE(app.shell->exportDesign(path("cylinder.stl")).empty());
    CHECK(stlTriangles(app.dir.filePath(QStringLiteral("cylinder.stl"))) > 12);

    problems =
        app.shell->exportDesign(app.dir.filePath(QStringLiteral("no-such-folder/part.stl")).toStdWString());
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "export.stl.failed");
    CHECK(problems[0].message ==
          "Khan couldn't write part.stl. Check that the folder is still there and that "
          "you're allowed to save in it.");

    problems = app.shell->exportDesign(path("part.obj"));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].code == "export.format");
    CHECK(problems[0].message.find(".stl") != std::string::npos);
}

TEST_CASE("Phase 0 exit check: box, undo, save, reopen, export STL, all through plugins", "[app][export]") {
    // docs/design/phase0-exit.md: this runs on Windows, macOS and Linux in CI.
    App app;
    // The box tool and the viewer are plugins.
    REQUIRE(app.host.find("kh.modeling"));
    CHECK(app.host.find("kh.modeling")->state == plugin::PluginState::Loaded);
    CHECK(app.host.find("kh.viewer")->state == plugin::PluginState::Loaded);
    CHECK(app.shell->panelWidget(QStringLiteral("kh.viewer.main")));

    app.click("kh.modeling.box");  // Create > Box
    REQUIRE(app.doc().featureCount() == 1);
    app.shell->undoAction()->trigger();  // Edit > Undo
    settle();
    CHECK(app.doc().featureCount() == 0);
    app.shell->redoAction()->trigger();  // Edit > Redo
    settle();
    REQUIRE(app.doc().featureCount() == 1);

    const QString khan = app.dir.filePath(QStringLiteral("exit.khan"));
    app.shell->askSavePath = [&](const QString&) { return khan; };
    REQUIRE(app.shell->save());  // File > Save
    CHECK_FALSE(app.shell->isWindowModified());

    REQUIRE(app.shell->newDesign());  // File > New, then File > Open
    REQUIRE(app.shell->openDesign(khan.toStdWString()).empty());
    settle();
    REQUIRE(app.doc().featureCount() == 1);
    CHECK(app.doc().featureAt(0).name() == "Box1");

    const QString stl = app.dir.filePath(QStringLiteral("exit.stl"));
    app.shell->askExportPath = [&](const QString&) { return stl; };
    app.menu("kh.shell.export")->trigger();  // File > Export
    settle();
    CHECK(stlTriangles(stl) == 12);
}

TEST_CASE("unsaved changes: cancel keeps the design", "[app]") {
    App app;
    app.click("kh.modeling.box");
    app.shell->askUnsaved = [](const QString&) { return app::Shell::Unsaved::Cancel; };
    CHECK_FALSE(app.shell->newDesign());
    CHECK(app.doc().featureCount() == 1);
}

TEST_CASE("opening a file that isn't a design explains why", "[app]") {
    App app;
    const auto problems =
        app.shell->openDesign(app.dir.filePath(QStringLiteral("missing.khan")).toStdWString());
    REQUIRE_FALSE(problems.empty());
    CHECK(problems[0].code == "file.cant_read");
}

TEST_CASE("named layouts save and open", "[app]") {
    App app;
    const auto before = app.shell->saveLayout();
    app.shell->saveNamedLayout(QStringLiteral("Mine"));
    CHECK(app.shell->layoutNames() == QStringList{QStringLiteral("Mine")});
    CHECK(app.shell->openNamedLayout(QStringLiteral("Mine")));
    CHECK(app.shell->saveLayout() == before);
    CHECK_FALSE(app.shell->openNamedLayout(QStringLiteral("Nope")));
    CHECK(app.shell->lastProblems()[0].code == "layout.bad");
}

TEST_CASE("the grid starts on, with lines and no snapping", "[app][grid]") {
    App app;
    REQUIRE(seenGrid);  // other plugins find it as kh.grid/2
    const auto g = seenGrid->settings();
    CHECK(g.visible);
    CHECK(g.style == grid::GridStyle::Lines);
    CHECK_FALSE(g.snap);
    CHECK(g.spacing == 10.0);
    CHECK(app.menu("kh.grid.show")->isChecked());
    CHECK(app.menu("kh.grid.style.lines")->isChecked());
    CHECK_FALSE(app.menu("kh.grid.style.dots")->isChecked());
    CHECK_FALSE(app.menu("kh.grid.snap")->isChecked());
    CHECK(app.menu("kh.grid.spacing.10")->isChecked());
    CHECK(app.menu("kh.grid.show")->shortcut() == QKeySequence(QStringLiteral("Ctrl+G")));
    CHECK(app.shell->findChild<QLabel*>(QStringLiteral("kh.grid.status"))->text() ==
          QStringLiteral("Grid 10 mm, lines, snap off"));
    // The viewer draws it as an overlay (no screen in the tests, so it draws flat).
    auto* view = app.flat();
    REQUIRE(view);
    const auto* grid = lattice(view);
    REQUIRE(grid);
    CHECK(grid->spacing == 10.0);
    CHECK_FALSE(grid->dots);
}

TEST_CASE("the View menu changes the grid, and Khan remembers it", "[app][grid]") {
    App app;
    REQUIRE(seenGrid);
    app.menu("kh.grid.style.dots")->trigger();
    app.menu("kh.grid.snap")->trigger();
    app.menu("kh.grid.spacing.5")->trigger();
    app.menu("kh.grid.show")->trigger();  // off
    settle();
    const auto g = seenGrid->settings();
    CHECK(g.style == grid::GridStyle::Dots);
    CHECK(g.snap);
    CHECK(g.spacing == 5.0);
    CHECK_FALSE(g.visible);
    CHECK(app.menu("kh.grid.style.dots")->isChecked());
    CHECK_FALSE(app.menu("kh.grid.style.lines")->isChecked());
    CHECK(app.menu("kh.grid.spacing.5")->isChecked());
    CHECK_FALSE(app.menu("kh.grid.spacing.10")->isChecked());
    CHECK_FALSE(app.menu("kh.grid.show")->isChecked());
    CHECK(app.shell->findChild<QLabel*>(QStringLiteral("kh.grid.status"))->text() ==
          QStringLiteral("Grid off, snap on"));
    CHECK_FALSE(lattice(app.flat()));     // the viewer follows: no grid to draw
    app.menu("kh.grid.show")->trigger();  // on again
    settle();
    REQUIRE(lattice(app.flat()));
    CHECK(lattice(app.flat())->dots);
    CHECK(lattice(app.flat())->spacing == 5.0);

    // Saved with the app's settings, so the next run starts the same way.
    CHECK(app.settings.value(QStringLiteral("plugins/kh.viewoptions/kh.grid.style")).toString() ==
          QStringLiteral("dots"));
    CHECK(app.settings.value(QStringLiteral("plugins/kh.viewoptions/kh.grid.spacing")).toString() ==
          QStringLiteral("5"));
    CHECK(app.settings.value(QStringLiteral("plugins/kh.viewoptions/kh.grid.snap")).toString() ==
          QStringLiteral("true"));
}

TEST_CASE("a spacing that isn't a preset shows in the Other item", "[app][grid]") {
    App app;
    // Changed in the store, as another part of Khan might: the grid follows.
    app.pluginSettings.write("plugins/kh.viewoptions/kh.grid.spacing", "12.5");
    app.host.events().publish(plugin::SettingChanged{"kh.viewoptions", "kh.grid.spacing"});
    settle();
    REQUIRE(seenGrid);
    CHECK(seenGrid->settings().spacing == 12.5);
    CHECK(app.menu("kh.grid.spacing.other")->text() == QStringLiteral("Other (12.5 mm)..."));
    CHECK_FALSE(app.menu("kh.grid.spacing.10")->isChecked());
    CHECK(app.shell->findChild<QLabel*>(QStringLiteral("kh.grid.status"))->text() ==
          QStringLiteral("Grid 12.5 mm, lines, snap off"));
}

TEST_CASE("a dense grid is drawn every few lines, so it doesn't turn grey", "[app][grid]") {
    App app;
    auto* view = app.flat();
    REQUIRE(view);
    REQUIRE(lattice(view));
    CHECK(view::FlatView::pixelsPerSpace(*lattice(view)) == 40.0);  // 10 mm at 4 px per mm
    app.pluginSettings.write("plugins/kh.viewoptions/kh.grid.spacing", "0.5");
    app.host.events().publish(plugin::SettingChanged{"kh.viewoptions", "kh.grid.spacing"});
    settle();
    REQUIRE(lattice(view));
    CHECK(view::FlatView::pixelsPerSpace(*lattice(view)) >= 8.0);  // every 5th line: 2.5 mm = 10 px
}

TEST_CASE("the viewer fills the middle; without 3D graphics it draws flat and says why", "[app][viewer]") {
    App app;
    auto* panel = app.shell->panelWidget(QStringLiteral("kh.viewer.main"));
    REQUIRE(panel);
    CHECK(panel->objectName() == QStringLiteral("kh.viewer.panel"));
    REQUIRE(app.flat());
    CHECK(app.flat()->text().contains(QStringLiteral("running without a screen")));
    CHECK(app.flat()->text().contains(QStringLiteral("Create > Box")));
    // The panes' toolbar is hidden: there are no panes to turn.
    CHECK_FALSE(panel->findChild<QToolBar*>(QStringLiteral("kh.viewer.toolbar"))->isVisible());

    app.shell->action("kh.modeling.box")->trigger();
    settle();
    CHECK(app.flat()->text().contains(QStringLiteral("Finished bodies: Box1")));
}

TEST_CASE("the flat view draws lines on top of the grid", "[app][viewer]") {
    view::FlatView flat;
    view::ViewOverlay grid;
    grid.shape = view::OverlayShape::Lattice;
    grid.color = {204, 209, 218};
    view::ViewOverlay axis;  // along the grid's x line through the origin
    axis.points = {{0, 0, 0}, {25, 0, 0}};
    axis.color = {209, 69, 59};
    axis.size = 2;
    flat.setOverlays({axis, grid});  // the grid given last, as when its plugin loads later
    flat.resize(400, 300);
    const QImage img = flat.grab().toImage();
    bool red = false;
    for (int y = 145; y <= 155; ++y) red = red || img.pixelColor(250, y) == QColor(209, 69, 59);
    CHECK(red);
}

TEST_CASE("the flat view's words can be read in dark mode", "[app][viewer]") {
    view::FlatView flat;
    QPalette dark = flat.palette();
    dark.setColor(QPalette::WindowText, Qt::white);  // as in Windows dark mode
    flat.setPalette(dark);
    flat.setText(QStringLiteral("MMMMMMMMMMMMMMMM"));
    flat.resize(400, 300);
    const QImage img = flat.grab().toImage();
    int darkPixels = 0;  // the card is light, so its words must be dark
    for (int y = 28; y < 60; ++y) {
        for (int x = 28; x < 300; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() < 120 && c.green() < 120 && c.blue() < 120) ++darkPixels;
        }
    }
    CHECK(darkPixels > 50);
}

TEST_CASE("the axes are drawn with their letters, and View can hide either", "[app][axes]") {
    App app;
    auto* view = app.flat();
    REQUIRE(view);
    CHECK(countShape(view, view::OverlayShape::Lines) == 3);   // x, y and z
    CHECK(countShape(view, view::OverlayShape::Labels) == 3);  // X, Y and Z
    auto* axes = app.menu("kh.axes.show");
    auto* labels = app.menu("kh.axes.labels");
    REQUIRE(axes);
    REQUIRE(labels);
    CHECK(axes->isChecked());
    CHECK(labels->isChecked());

    labels->trigger();  // letters off, axes stay
    settle();
    CHECK_FALSE(labels->isChecked());
    CHECK(countShape(view, view::OverlayShape::Lines) == 3);
    CHECK(countShape(view, view::OverlayShape::Labels) == 0);
    CHECK(app.settings.value(QStringLiteral("plugins/kh.viewoptions/kh.axes.labels")).toString() ==
          QStringLiteral("false"));

    labels->trigger();
    axes->trigger();  // axes off: their letters go too
    settle();
    CHECK(countShape(view, view::OverlayShape::Lines) == 0);
    CHECK(countShape(view, view::OverlayShape::Labels) == 0);
    CHECK(labels->isChecked());  // still on, for when the axes come back
    axes->trigger();
    settle();
    CHECK(countShape(view, view::OverlayShape::Labels) == 3);
}

TEST_CASE("the View options panel starts closed, opens from View, and changes options",
          "[app][viewoptions]") {
    App app;
    REQUIRE(app.shell->dockIds().contains(QStringLiteral("kh.viewoptions.panel")));
    auto* panel = app.shell->panelWidget(QStringLiteral("kh.viewoptions.panel"));
    REQUIRE(panel);
    CHECK_FALSE(panel->isVisible());   // closed until asked for
    app.click("kh.viewoptions.show");  // View > View options...
    CHECK(panel->isVisible());

    // Every view option is there, grouped: the grid's and the axes'.
    auto* labels = panel->findChild<QCheckBox*>(QStringLiteral("kh.axes.labels"));
    auto* spacing = panel->findChild<QDoubleSpinBox*>(QStringLiteral("kh.grid.spacing"));
    auto* style = panel->findChild<QComboBox*>(QStringLiteral("kh.grid.style"));
    REQUIRE(labels);
    REQUIRE(spacing);
    REQUIRE(style);
    CHECK(labels->isChecked());
    CHECK(spacing->value() == 10.0);
    CHECK(spacing->suffix() == QStringLiteral(" mm"));
    CHECK(style->currentText() == QStringLiteral("Lines"));

    // Changing it in the panel changes it everywhere: the drawing and the menu.
    labels->setChecked(false);
    spacing->setValue(5.0);
    style->setCurrentIndex(style->findData(QStringLiteral("dots")));
    settle();
    CHECK(countShape(app.flat(), view::OverlayShape::Labels) == 0);
    CHECK_FALSE(app.menu("kh.axes.labels")->isChecked());
    REQUIRE(seenGrid);
    CHECK(seenGrid->settings().spacing == 5.0);
    CHECK(app.menu("kh.grid.spacing.5")->isChecked());
    CHECK(app.menu("kh.grid.style.dots")->isChecked());

    // And the menu changes the panel.
    app.menu("kh.axes.labels")->trigger();
    settle();
    CHECK(labels->isChecked());
}

TEST_CASE("the View options panel can show and change one pane's options", "[app][viewoptions][panes]") {
    App app;
    REQUIRE(seenOptions);
    // No screen in the tests, so the viewer has no panes; add two as a viewer would.
    seenOptions->addPane({"test.p1", "Viewer 9, left pane"});
    seenOptions->addPane({"test.p2", "Viewer 9, right pane"});
    app.click("kh.viewoptions.show");
    auto* panel = app.shell->panelWidget(QStringLiteral("kh.viewoptions.panel"));
    auto* scope = panel->findChild<QComboBox*>(QStringLiteral("kh.viewoptions.scope"));
    auto* labels = panel->findChild<QCheckBox*>(QStringLiteral("kh.axes.labels"));
    auto* reset = panel->findChild<QToolButton*>(QStringLiteral("kh.axes.labels.reset"));
    REQUIRE(scope);
    REQUIRE(labels);
    REQUIRE(reset);
    CHECK(scope->count() == 3);
    CHECK(scope->itemText(0) == QStringLiteral("All panes"));
    CHECK(scope->itemText(1) == QStringLiteral("Viewer 9, left pane"));

    scope->setCurrentIndex(1);  // the left pane only
    settle();
    CHECK_FALSE(reset->isVisible());
    labels->setChecked(false);
    settle();
    CHECK(seenOptions->hasOwn("kh.axes.labels", "test.p1"));
    CHECK_FALSE(seenOptions->flag("kh.axes.labels", "test.p1"));
    CHECK(seenOptions->flag("kh.axes.labels"));  // all panes: still on
    CHECK(app.menu("kh.axes.labels")->isChecked());
    CHECK(reset->isVisible());
    CHECK(labels->font().bold());  // its own value stands out

    scope->setCurrentIndex(2);  // the right pane follows all panes
    settle();
    CHECK(labels->isChecked());
    CHECK_FALSE(reset->isVisible());

    scope->setCurrentIndex(1);
    settle();
    reset->click();  // back to the all-panes value
    settle();
    CHECK_FALSE(seenOptions->hasOwn("kh.axes.labels", "test.p1"));
    CHECK(labels->isChecked());

    // A pane that goes away for good leaves the list, and the panel shows all panes.
    seenOptions->removePane("test.p1", true);
    settle();
    CHECK(scope->count() == 2);
    CHECK(scope->currentIndex() == 0);
}

TEST_CASE("Window > New viewer opens more viewers", "[app][viewer]") {
    App app;
    auto* more = app.menu("kh.shell.new.kh.viewer.main");
    REQUIRE(more);
    CHECK(more->text() == QStringLiteral("New viewer"));
    more->trigger();
    settle();
    CHECK(app.shell->dockIds().contains(QStringLiteral("kh.viewer.main.2")));
    CHECK(app.shell->openAnotherPanel(QStringLiteral("kh.viewer.main")) ==
          QStringLiteral("kh.viewer.main.3"));
    auto* second = app.shell->panelWidget(QStringLiteral("kh.viewer.main.2"));
    REQUIRE(second);
    CHECK(second->objectName() == QStringLiteral("kh.viewer.panel"));
    // The feature tree is one of a kind.
    CHECK(app.shell->openAnotherPanel(QStringLiteral("kh.featuretree.panel")).isEmpty());
    CHECK_FALSE(app.menu("kh.shell.new.kh.featuretree.panel"));
}

TEST_CASE("the viewer shows finished bodies, and picked used-up steps see-through", "[app][viewer]") {
    App app;
    auto& docs = app.host.documents();
    const DocId doc = app.shell->activeDesign();
    REQUIRE(docs.run(doc, "feature.add_box", {{"size", kernel::Vec3{20, 20, 20}}}).empty());
    REQUIRE(docs.run(doc, "feature.add_cylinder", {{"radius", 4.0}, {"height", 30.0}}).empty());
    const auto box = app.doc().featureAt(0).id();
    const auto cyl = app.doc().featureAt(1).id();
    REQUIRE(docs.run(doc, "feature.add_boolean", {{"operation", std::string("cut")}, {"a", box}, {"b", cyl}})
                .empty());
    const auto cut = app.doc().featureAt(2).id();

    app.host.selection().clear();
    auto scene = plugins::viewerScene(docs, app.host.selection());
    REQUIRE(scene.bodies.size() == 1);
    CHECK(scene.bodies[0].feature == cut);
    CHECK_FALSE(scene.bodies[0].ghost);
    CHECK(scene.selected.empty());

    app.host.selection().set(doc, {cyl, cut});
    scene = plugins::viewerScene(docs, app.host.selection());
    REQUIRE(scene.bodies.size() == 2);
    CHECK(scene.bodies[1].feature == cyl);
    CHECK(scene.bodies[1].ghost);
    CHECK(scene.selected == std::vector{cyl, cut});
}

TEST_CASE("Properties shows a body's material, mass and cost, and picks a material", "[app][material]") {
    App app;
    app.click("kh.modeling.box");  // a 20 mm cube: 8 cm^3
    auto* material = app.field<QComboBox>("material");
    REQUIRE(material);
    CHECK(material->currentText() == QStringLiteral("Default: PLA"));
    CHECK(app.field<QLabel>("volume")->text() == QStringLiteral("8000.00 mm³"));
    CHECK(app.field<QLabel>("mass")->text() == QStringLiteral("9.92 g (weighs 0.097 N)"));
    CHECK(app.field<QLabel>("cost")->text() == QStringLiteral("about $0.20"));
    CHECK(app.field<QLabel>("materialinfo")->text().startsWith(QStringLiteral("PLA: density 1.24 g/cm³")));

    // Pick aluminum, as a student would from the list.
    const int at = material->findText(QStringLiteral("Aluminum 6061"));
    REQUIRE(at > 0);
    material->setCurrentIndex(at);
    emit material->activated(at);
    settle();
    const auto id = app.doc().featureAt(0).id();
    CHECK(app.doc().materialOf(id).key == "aluminum_6061");
    CHECK(app.field<QComboBox>("material")->currentText() == QStringLiteral("Aluminum 6061"));
    CHECK(app.field<QLabel>("mass")->text() == QStringLiteral("21.60 g (weighs 0.212 N)"));
    CHECK(app.shell->undoAction()->text() == QStringLiteral("Undo Make Box1 Aluminum 6061"));

    // The viewer draws it in aluminum's color.
    const auto scene = plugins::viewerScene(app.host.documents(), app.host.selection());
    REQUIRE(scene.bodies.size() == 1);
    CHECK(scene.bodies[0].color == view::Rgb{196, 200, 206});

    app.shell->undoAction()->trigger();
    settle();
    CHECK(app.field<QComboBox>("material")->currentText() == QStringLiteral("Default: PLA"));
}

TEST_CASE("a cut shows which body its material comes from", "[app][material]") {
    App app;
    auto& docs = app.host.documents();
    const DocId doc = app.shell->activeDesign();
    REQUIRE(
        docs.run(doc, "feature.add_box", {{"size", kernel::Vec3{20, 20, 20}}, {"name", std::string("Plate")}})
            .empty());
    REQUIRE(docs.run(doc, "feature.add_cylinder", {{"radius", 4.0}, {"height", 30.0}}).empty());
    const auto plate = app.doc().featureAt(0).id();
    const auto cyl = app.doc().featureAt(1).id();
    REQUIRE(docs.run(doc, "feature.set_material", {{"feature", plate}, {"material", std::string("oak")}})
                .empty());
    REQUIRE(
        docs.run(doc, "feature.add_boolean", {{"operation", std::string("cut")}, {"a", plate}, {"b", cyl}})
            .empty());
    app.host.selection().set(doc, {app.doc().featureAt(2).id()});
    settle();
    CHECK(app.field<QComboBox>("material")->currentText() == QStringLiteral("Same as Plate: Oak (red)"));
}

TEST_CASE("a plugin's check-mark item in a View submenu changes its setting", "[app][settings]") {
    App app;
    auto* a = app.shell->action("test.units.inches");
    REQUIRE(a);
    CHECK(a->isCheckable());
    CHECK_FALSE(a->isChecked());
    CHECK(a->isEnabled());
    // It's in View > Units.
    QMenu* units = nullptr;
    for (auto* m : app.shell->findChildren<QMenu*>()) {
        if (m->title() == QStringLiteral("Units")) units = m;
    }
    REQUIRE(units);
    CHECK(units->actions().contains(a));
    auto* view = qobject_cast<QMenu*>(units->parent());
    REQUIRE(view);
    CHECK(view->title() == QStringLiteral("&View"));

    a->trigger();
    settle();
    CHECK(a->isChecked());
    CHECK(a->text() == QStringLiteral("Inches (on)"));
    CHECK(app.settings.value(QStringLiteral("plugins/test.units/inches")).toString() ==
          QStringLiteral("true"));

    // A change made somewhere else (another panel, a script) shows as soon as
    // Khan hears about it.
    app.pluginSettings.write("plugins/test.units/inches", "false");
    app.host.events().publish(plugin::SettingChanged{"test.units", "inches"});
    CHECK_FALSE(a->isChecked());
    CHECK(a->text() == QStringLiteral("Inches"));
}

TEST_CASE("a plugin's status-bar item shows, and its setting lasts between runs", "[app][settings]") {
    App app;
    auto* label = app.shell->statusBar()->findChild<QLabel*>(QStringLiteral("test.units.status"));
    REQUIRE(label);
    CHECK(label->text() == QStringLiteral("mm"));
    app.click("test.units.inches");
    app.open();  // a new window, as on the next run
    label = app.shell->statusBar()->findChild<QLabel*>(QStringLiteral("test.units.status"));
    REQUIRE(label);
    CHECK(label->text() == QStringLiteral("in"));
    CHECK(app.shell->action("test.units.inches")->isChecked());
}

TEST_CASE("a plugin action that fails says so, without closing Khan", "[app][settings]") {
    App app;
    app.click("test.units.oops");
    auto* problems = app.shell->findChild<QLabel*>(QStringLiteral("kh.shell.problems"));
    REQUIRE(problems);
    CHECK(problems->text().contains(QStringLiteral("Oops stopped with an error")));
}
