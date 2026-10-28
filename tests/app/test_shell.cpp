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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPalette>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include <cstring>
#include <memory>
#include <numbers>

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
#include "kh/plugins/Sketcher.h"
#include "kh/plugins/ViewOptions.h"
#include "kh/plugins/Viewer.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"
#include "kh/ui/UiRegistry.h"
#include "kh/view/FlatView.h"
#include "kh/viewer/Viewer.h"

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
bool tapeReady = false;  // something Khan can't see, for the "enabled" rule
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
    ctx.addAction({.id = "test.units.measure",
                   .text = "Measure",
                   .menu = "View/Units",
                   .run = [] {},
                   .enabled = [] { return tapeReady; }});
    ctx.addAction({.id = "test.units.broken_rule",
                   .text = "Broken rule",
                   .menu = "View/Units",
                   .run = [] {},
                   .enabled = []() -> bool { throw std::runtime_error("no tape"); }});
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
viewer::IViewer* seenViewer = nullptr;
void gridClient(plugin::PluginContext& ctx) {
    ctx.onStart([&ctx] {
        seenGrid = ctx.service<grid::IGrid>();
        seenOptions = ctx.service<viewoptions::IViewOptions>();
        seenViewer = ctx.service<viewer::IViewer>();
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
    std::unique_ptr<sketch::ISketchSolver> solver = sketch::makePlaneGcsSolver();
    app::AppSettings pluginSettings{settings};
    app::AppUi ui;
    plugin::PluginHost host{kernel.get(), solver.get()};
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
        host.addBuiltin(plugins::sketcherPlugin());
        host.addBuiltin({R"({"id":"test.broken","name":"Broken","version":"1","apiVersion":)" +
                             std::to_string(plugin::kPluginApiVersion) + "}",
                         &panelThenFail});
        host.addBuiltin(
            {R"({"id":"test.gridclient","name":"Grid client","version":"1","uses":["kh.grid","kh.viewoptions","kh.viewer"],"apiVersion":)" +
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
    CHECK(builtins == 8);
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

TEST_CASE("Edit > New folder puts the picked steps in a folder in the tree", "[app][folders]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.cylinder");
    app.click("kh.modeling.box");
    app.pickRows({0, 1});
    app.click("kh.modeling.new_folder");
    REQUIRE(app.doc().folders().size() == 1);
    CHECK(app.doc().folders()[0].name == "Folder1");

    // The tree: Folder1 (with Box1 and Cylinder1 under it), then Box2.
    REQUIRE(app.tree()->topLevelItemCount() == 2);
    auto* folder = app.tree()->topLevelItem(0);
    CHECK(folder->text(0) == QStringLiteral("Folder1"));
    CHECK(folder->text(1) == QStringLiteral("Folder"));
    REQUIRE(folder->childCount() == 2);
    CHECK(folder->child(0)->text(0) == QStringLiteral("Box1"));
    CHECK(folder->child(1)->text(0) == QStringLiteral("Cylinder1"));
    CHECK(folder->isExpanded());
    CHECK(app.tree()->topLevelItem(1)->text(0) == QStringLiteral("Box2"));

    // Picking the folder row picks its steps.
    app.tree()->clearSelection();
    folder->setSelected(true);
    settle();
    CHECK(app.host.selection().features() ==
          std::vector<FeatureId>{app.doc().featureAt(0).id(), app.doc().featureAt(1).id()});
    CHECK(app.tree()->topLevelItem(0)->child(0)->isSelected());

    // Turning the folder's steps off shows the folder as off.
    app.click("kh.modeling.turn_off");
    CHECK(app.tree()->topLevelItem(0)->text(2) == QStringLiteral("off"));

    // Renaming the folder row renames the folder.
    app.tree()->topLevelItem(0)->setText(0, QStringLiteral("Base"));
    settle();
    CHECK(app.doc().folders()[0].name == "Base");
    CHECK(app.shell->undoAction()->text() == QStringLiteral("Undo Rename folder Folder1 to Base"));

    // Undo three times: rename, turn off, then the folder itself.
    for (int i = 0; i < 3; ++i) app.shell->undoAction()->trigger();
    settle();
    CHECK(app.doc().folders().empty());
    CHECK(app.tree()->topLevelItemCount() == 3);
}

TEST_CASE("a closed folder stays closed when the tree is rebuilt", "[app][folders]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.box");
    app.pickRows({0});
    app.click("kh.modeling.new_folder");
    app.tree()->topLevelItem(0)->setExpanded(false);
    settle();
    app.click("kh.modeling.box");  // changes the design, so the tree is rebuilt
    CHECK(app.tree()->topLevelItem(0)->text(0) == QStringLiteral("Folder1"));
    CHECK_FALSE(app.tree()->topLevelItem(0)->isExpanded());
}

TEST_CASE("steps that aren't next to each other can't share a folder", "[app][folders]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.modeling.box");
    app.click("kh.modeling.box");
    app.pickRows({0, 2});
    app.click("kh.modeling.new_folder");
    CHECK(app.doc().folders().empty());
    REQUIRE(app.shell->lastProblems().size() == 1);
    CHECK(app.shell->lastProblems()[0].code == "folder.not_together");
}

TEST_CASE("Modify > Move adds a Move step, and Properties changes how far", "[app][move]") {
    App app;
    CHECK_FALSE(app.shell->action("kh.modeling.move")->isEnabled());  // nothing picked
    app.click("kh.modeling.box");                                     // Box1 is picked
    app.click("kh.modeling.move");
    REQUIRE(app.doc().featureCount() == 2);
    const auto& move = dynamic_cast<const model::MoveFeature&>(app.doc().featureAt(1));
    CHECK(move.name() == "Move1");
    CHECK(move.body() == app.doc().featureAt(0).id());
    CHECK(app.tree()->topLevelItem(1)->text(1) == QStringLiteral("Move"));
    // The new step is picked, so Properties shows its fields.
    CHECK(app.field<QLabel>("body")->text() == QStringLiteral("Box1"));
    auto* x = app.field<QDoubleSpinBox>("offset.x");
    REQUIRE(x);
    x->setValue(30);
    settle();
    CHECK(dynamic_cast<const model::MoveFeature&>(app.doc().featureAt(1)).motion().offset.x == 30.0);
    auto* turn = app.field<QDoubleSpinBox>("turn");
    REQUIRE(turn);
    CHECK(turn->suffix() == QStringLiteral("°"));
    turn->setValue(90);
    settle();
    CHECK(dynamic_cast<const model::MoveFeature&>(app.doc().featureAt(1)).motion().angle ==
          Approx(std::numbers::pi / 2));
    CHECK(app.tree()->topLevelItem(1)->text(2) == QStringLiteral("ok"));
}

TEST_CASE("a sketch shows in the tree, and Properties shows how free it still is", "[app][sketch]") {
    App app;
    auto& docs = app.host.documents();
    const auto doc = app.shell->activeDesign();
    REQUIRE(docs.run(doc, "feature.add_sketch", {}).empty());
    const auto sketchId = app.doc().featureAt(0).id();
    auto sk = [&](std::string_view name, model::Args args) {
        args["sketch"] = sketchId;
        return docs.run(doc, name, args);
    };
    REQUIRE(sk("sketch.add_line", {{"from", sketch::Point2{0, 0}}, {"to", sketch::Point2{30, 2}}}).empty());
    REQUIRE(sk("sketch.add_circle", {{"center", sketch::Point2{15, 10}}, {"radius", 4.0}}).empty());
    REQUIRE(
        sk("sketch.constrain", {{"kind", std::string("horizontal")}, {"on", std::string("Line1")}}).empty());
    settle();
    CHECK(app.tree()->topLevelItem(0)->text(1) == QStringLiteral("Sketch"));
    CHECK(app.tree()->topLevelItem(0)->text(2) == QStringLiteral("ok"));
    app.pickRows({0});
    CHECK(app.field<QLabel>("plane")->text().startsWith(QStringLiteral("XY")));
    CHECK(app.field<QLabel>("drawn")->text() == QStringLiteral("1 line(s), 1 circle(s)"));
    CHECK(app.field<QLabel>("constraints")->text() == QStringLiteral("1"));
    CHECK(app.field<QLabel>("freedom")->text() ==
          QStringLiteral("6"));                     // a horizontal line (3) and a circle (3)
    CHECK_FALSE(app.field<QComboBox>("material"));  // a sketch isn't a body

    REQUIRE(
        sk("sketch.constrain", {{"kind", std::string("fix")}, {"on", std::string("Line1.start")}}).empty());
    REQUIRE(sk("sketch.constrain",
               {{"kind", std::string("length")}, {"on", std::string("Line1")}, {"value", 30.0}})
                .empty());
    REQUIRE(sk("sketch.constrain",
               {{"kind", std::string("radius")}, {"on", std::string("Circle1")}, {"value", 4.0}})
                .empty());
    REQUIRE(sk("sketch.constrain",
               {{"kind", std::string("midpoint")}, {"on", std::string("Circle1.center,Line1")}})
                .empty());
    settle();
    CHECK(app.field<QLabel>("freedom")->text() == QStringLiteral("0: fully constrained"));
}

namespace {

// Clicks the flat view at a point of the design, in mm, as the student would.
void clickAt(view::FlatView* v, kernel::Vec3 at) {
    const QPointF p = v->toScreen(at);
    QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(v, &move);
    QMouseEvent press(QEvent::MouseButtonPress, p, v->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(v, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, p, v->mapToGlobal(p), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(v, &release);
    settle();
}

void pressEscape(QWidget* w) {
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(w, &key);
    settle();
}

const model::SketchFeature& sketchAt(App& app, std::size_t i) {
    return static_cast<const model::SketchFeature&>(app.doc().featureAt(i));
}

}  // namespace

TEST_CASE("Sketch > New sketch, then clicks draw a closed shape, one undo step per line", "[app][sketcher]") {
    App app;
    app.menu("kh.grid.snap")->trigger();  // so clicks land exactly on whole millimeters
    app.click("kh.sketcher.new_xy");
    REQUIRE(app.doc().featureCount() == 1);
    CHECK(app.doc().featureAt(0).type() == model::FeatureType::Sketch);
    auto* viewer = seenViewer;
    REQUIRE(viewer);
    CHECK(viewer->tool() != nullptr);  // drawing
    CHECK(app.shell->statusBar()->currentMessage() ==
          QStringLiteral("Drawing in Sketch1. Line: click where it starts."));

    auto* flat = app.flat();
    REQUIRE(flat);
    for (auto p : {kernel::Vec3{0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {0, 20, 0}, {0, 0, 0}}) clickAt(flat, p);

    const auto& s = sketchAt(app, 0).sketch();
    REQUIRE(s.entities.size() == 4);
    const auto& l1 = std::get<sketch::LineShape>(s.entities[0].shape);
    const auto& l4 = std::get<sketch::LineShape>(s.entities[3].shape);
    CHECK(l1.start.x == 0);
    CHECK(l1.end.x == 40);
    CHECK(l4.end.y == 0);
    // Each line joins the one before, and the last closes onto the first.
    REQUIRE(s.constraints.size() == 4);
    for (const auto& c : s.constraints) CHECK(c.kind == sketch::ConstraintKind::Coincident);
    CHECK(s.constraints[3].points[1].entity == s.entities[0].id);
    CHECK(app.doc().undoLabel() == "Add Line4");
    const auto* r =
        app.host.documents().lastRebuild(app.shell->activeDesign())->result(sketchAt(app, 0).id());
    REQUIRE(r->sketch);
    CHECK(r->sketch->freedom == 8);  // 4 lines (16) joined at 4 corners (8 taken)

    // Closing the shape ended the chain: the next click starts a new line.
    CHECK(app.shell->statusBar()->currentMessage() ==
          QStringLiteral("Drawing in Sketch1. Line: click where it starts."));

    // The viewer draws it: 4 lines, 4 corners.
    const auto* lines = app.host.overlays().find("kh.sketcher.edit.lines");
    REQUIRE(lines);
    CHECK(lines->points.size() == 8);
    CHECK(app.host.overlays().find("kh.sketcher.edit.points")->points.size() == 8);

    // One undo takes away one line.
    app.shell->undoAction()->trigger();
    settle();
    CHECK(sketchAt(app, 0).sketch().entities.size() == 3);
}

TEST_CASE("the Circle, Arc and Point tools draw with clicks", "[app][sketcher]") {
    App app;
    app.menu("kh.grid.snap")->trigger();
    app.click("kh.sketcher.new_xy");
    auto* flat = app.flat();
    app.click("kh.sketcher.circle");
    CHECK(app.shell->action("kh.sketcher.circle")->isChecked());
    clickAt(flat, {10, 10, 0});
    clickAt(flat, {20, 10, 0});
    app.click("kh.sketcher.arc");
    clickAt(flat, {30, 0, 0});   // center
    clickAt(flat, {40, 0, 0});   // start: radius 10, pointing right
    clickAt(flat, {30, 10, 0});  // end: straight up, a quarter turn counterclockwise
    app.click("kh.sketcher.point");
    clickAt(flat, {-10, -10, 0});
    const auto& s = sketchAt(app, 0).sketch();
    REQUIRE(s.entities.size() == 3);
    const auto& c = std::get<sketch::CircleShape>(s.entities[0].shape);
    CHECK(c.center.x == 10);
    CHECK(c.radius == Approx(10));
    const auto& a = std::get<sketch::ArcShape>(s.entities[1].shape);
    CHECK(a.radius == Approx(10));
    CHECK(a.startAngle == Approx(0).margin(1e-12));
    CHECK(a.endAngle == Approx(std::numbers::pi / 2));
    CHECK(std::get<sketch::PointShape>(s.entities[2].shape).at.x == -10);
    CHECK(s.entities[1].name == "Arc1");
}

TEST_CASE("an end close to a point snaps onto it and joins it", "[app][sketcher]") {
    App app;
    app.click("kh.sketcher.new_xy");
    auto* flat = app.flat();
    clickAt(flat, {0, 0, 0});
    clickAt(flat, {20, 0, 0});
    pressEscape(flat);  // stop the chain
    // Start a line 1 mm away from the first line's end: within 8 pixels, so it snaps.
    clickAt(flat, {21, 1, 0});
    clickAt(flat, {20, 15, 0});
    const auto& s = sketchAt(app, 0).sketch();
    REQUIRE(s.entities.size() == 2);
    const auto& l1 = std::get<sketch::LineShape>(s.entities[0].shape);
    const auto& l2 = std::get<sketch::LineShape>(s.entities[1].shape);
    CHECK(l2.start.x == l1.end.x);  // exactly on it
    CHECK(l2.start.y == l1.end.y);
    REQUIRE(s.constraints.size() == 1);
    CHECK(s.constraints[0].points[1].entity == s.entities[0].id);
    CHECK(s.constraints[0].points[1].role == sketch::PointRole::End);
}

TEST_CASE("Escape stops the shape being drawn, then stops editing", "[app][sketcher]") {
    App app;
    app.click("kh.sketcher.new_xy");
    auto* flat = app.flat();
    auto* viewer = seenViewer;
    clickAt(flat, {0, 0, 0});
    pressEscape(flat);  // the line that was started goes away
    CHECK(viewer->tool() != nullptr);
    clickAt(flat, {5, 5, 0});
    clickAt(flat, {10, 5, 0});
    CHECK(sketchAt(app, 0).sketch().entities.size() == 1);  // a new line, not one from 0, 0
    pressEscape(flat);
    pressEscape(flat);
    CHECK(viewer->tool() == nullptr);
    CHECK(app.shell->statusBar()->currentMessage() == QStringLiteral("Finished the sketch."));
    clickAt(flat, {20, 20, 0});  // not drawing any more
    CHECK(sketchAt(app, 0).sketch().entities.size() == 1);
}

TEST_CASE("Sketch menu items are grey when they can't be used", "[app][sketcher]") {
    App app;
    auto enabled = [&](const char* id) { return app.shell->action(id)->isEnabled(); };
    auto toolsOn = [&] {
        return enabled("kh.sketcher.line") && enabled("kh.sketcher.circle") && enabled("kh.sketcher.arc") &&
               enabled("kh.sketcher.point");
    };
    auto toolsOff = [&] {
        return !enabled("kh.sketcher.line") && !enabled("kh.sketcher.circle") &&
               !enabled("kh.sketcher.arc") && !enabled("kh.sketcher.point");
    };
    // Nothing picked, not editing: only the New sketch items work.
    CHECK(enabled("kh.sketcher.new_xy"));
    CHECK_FALSE(enabled("kh.sketcher.edit"));
    CHECK_FALSE(enabled("kh.sketcher.finish"));
    CHECK(toolsOff());

    app.click("kh.modeling.box");  // Box1 is picked: not a sketch
    CHECK_FALSE(enabled("kh.sketcher.edit"));

    app.click("kh.sketcher.new_xy");  // editing Sketch2
    CHECK(enabled("kh.sketcher.finish"));
    CHECK_FALSE(enabled("kh.sketcher.edit"));  // already editing
    CHECK(toolsOn());

    app.click("kh.sketcher.finish");
    CHECK_FALSE(enabled("kh.sketcher.finish"));
    CHECK(toolsOff());
    app.pickRows({1});  // Sketch1
    CHECK(enabled("kh.sketcher.edit"));
    app.pickRows({0, 1});  // two steps
    CHECK_FALSE(enabled("kh.sketcher.edit"));
    app.pickRows({0});  // Box1
    CHECK_FALSE(enabled("kh.sketcher.edit"));
}

TEST_CASE("the toolbar shows sketch buttons while editing, and 3D buttons otherwise", "[app][sketcher]") {
    App app;
    auto shown = [&](const char* id) {
        auto* b = app.shell->button(id);
        REQUIRE(b);
        return b->isVisible();
    };
    const char* sketchButtons[] = {"kh.sketcher.finish", "kh.sketcher.line", "kh.sketcher.circle",
                                   "kh.sketcher.arc", "kh.sketcher.point"};
    const char* usualButtons[] = {"kh.modeling.box", "kh.modeling.cylinder", "kh.modeling.move",
                                  "kh.sketcher.new_xy", "kh.sketcher.edit"};
    for (const char* id : usualButtons) CHECK(shown(id));
    for (const char* id : sketchButtons) CHECK_FALSE(shown(id));
    CHECK(app.shell->button("kh.sketcher.new_xy")->text() == QStringLiteral("New sketch"));
    CHECK(app.shell->button("kh.sketcher.new_xz") == nullptr);  // menu only

    app.click("kh.sketcher.new_xy");
    for (const char* id : usualButtons) CHECK_FALSE(shown(id));
    for (const char* id : sketchButtons) CHECK(shown(id));
    CHECK(app.shell->undoAction()->isVisible());               // Khan's own buttons stay
    CHECK(app.shell->action("kh.modeling.box")->isVisible());  // and the menus keep every item

    // A button does what its menu item does, and shows the same check mark.
    app.shell->button("kh.sketcher.circle")->trigger();
    settle();
    CHECK(app.shell->button("kh.sketcher.circle")->isChecked());
    CHECK(app.shell->action("kh.sketcher.circle")->isChecked());
    CHECK_FALSE(app.shell->button("kh.sketcher.line")->isChecked());

    app.shell->button("kh.sketcher.finish")->trigger();
    settle();
    for (const char* id : usualButtons) CHECK(shown(id));
    for (const char* id : sketchButtons) CHECK_FALSE(shown(id));
    CHECK(app.host.find("kh.sketcher")->issues.empty());  // sending ActionsChanged is allowed
}

TEST_CASE("Edit sketch draws in the picked sketch, and stops if the sketch goes away", "[app][sketcher]") {
    App app;
    app.click("kh.modeling.box");
    app.click("kh.sketcher.new_yz");
    auto* viewer = seenViewer;
    CHECK(viewer->tool() != nullptr);
    CHECK(sketchAt(app, 1).plane() == model::SketchPlane::YZ);
    app.shell->undoAction()->trigger();  // the sketch is gone
    settle();
    CHECK(viewer->tool() == nullptr);
    CHECK_FALSE(app.shell->action("kh.sketcher.finish")->isEnabled());

    app.pickRows({0});
    app.click("kh.sketcher.new_xy");
    app.click("kh.sketcher.finish");
    app.pickRows({1});
    app.click("kh.sketcher.edit");
    CHECK(viewer->tool() != nullptr);
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

TEST_CASE("a plugin item is enabled by its own rule, and updates on ActionsChanged", "[app][settings]") {
    tapeReady = false;
    App app;
    auto* a = app.shell->action("test.units.measure");
    REQUIRE(a);
    CHECK_FALSE(a->isEnabled());
    tapeReady = true;
    app.host.events().publish(plugin::ActionsChanged{});
    CHECK(a->isEnabled());
    tapeReady = false;
    app.host.events().publish(plugin::ActionsChanged{});
    CHECK_FALSE(a->isEnabled());
    // A rule that throws just leaves the item grey.
    CHECK_FALSE(app.shell->action("test.units.broken_rule")->isEnabled());
}

TEST_CASE("a plugin action that fails says so, without closing Khan", "[app][settings]") {
    App app;
    app.click("test.units.oops");
    auto* problems = app.shell->findChild<QLabel*>(QStringLiteral("kh.shell.problems"));
    REQUIRE(problems);
    CHECK(problems->text().contains(QStringLiteral("Oops stopped with an error")));
}
