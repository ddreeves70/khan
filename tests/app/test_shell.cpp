// Tests for the desktop shell and its built-in panels, driven like a student would:
// menu items, picking rows, typing numbers.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <memory>

#include "AppUi.h"
#include "GridView.h"
#include "Shell.h"
#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/Feature.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/FeatureTree.h"
#include "kh/plugins/Modeling.h"
#include "kh/plugins/Properties.h"
#include "kh/ui/UiRegistry.h"

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

struct App {
    QTemporaryDir dir;
    QSettings settings{dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
    std::unique_ptr<kernel::IKernel> kernel = kernel::makeOcctKernel();
    app::AppUi ui;
    plugin::PluginHost host{kernel.get()};
    plugin::Grid grid{host.events()};
    std::unique_ptr<app::Shell> shell;

    App() {
        host.provide<ui::UiRegistry>(ui);
        host.provide(grid);
        host.addBuiltin(plugins::modelingPlugin());
        host.addBuiltin(plugins::featureTreePlugin());
        host.addBuiltin(plugins::propertiesPlugin());
        host.addBuiltin(
            {R"({"id":"test.broken","name":"Broken","version":"1","apiVersion":2})", &panelThenFail});
        host.loadAll();
        open();
    }

    void open() {
        shell = std::make_unique<app::Shell>(host, ui, *kernel, grid, settings);
        shell->askUnsaved = [](const QString&) { return app::Shell::Unsaved::Discard; };
        shell->show();
        settle();
    }

    QAction* menu(const char* id) { return shell->findChild<QAction*>(QString::fromLatin1(id)); }

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
    CHECK(app.shell->dockIds() == QStringList({"kh.featuretree.panel", "kh.properties.panel",
                                               "kh.viewer.main"}));  // no test.broken.panel
    CHECK(app.shell->action("kh.modeling.box"));
    CHECK(app.shell->action("kh.modeling.cut"));
    CHECK_FALSE(app.shell->action("kh.modeling.cut")->isEnabled());  // needs two steps picked
    CHECK(app.doc().featureCount() == 0);
    CHECK(app.shell->windowTitle() == QStringLiteral("Untitled[*] - Khan"));
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

TEST_CASE("the grid starts on, with dots and no snapping", "[app][grid]") {
    App app;
    const auto& g = app.grid.settings();
    CHECK(g.visible);
    CHECK(g.style == plugin::GridStyle::Dots);
    CHECK_FALSE(g.snap);
    CHECK(g.spacing == 10.0);
    CHECK(app.menu("kh.shell.grid.show")->isChecked());
    CHECK(app.menu("kh.shell.grid.dots")->isChecked());
    CHECK_FALSE(app.menu("kh.shell.grid.snap")->isChecked());
    CHECK(app.menu("kh.shell.grid.spacing.10")->isChecked());
    CHECK(app.shell->findChild<QLabel*>(QStringLiteral("kh.shell.gridstatus"))->text() ==
          QStringLiteral("Grid 10 mm, dots, snap off"));
    auto* view = app.shell->findChild<app::GridView*>(QStringLiteral("kh.shell.viewer"));
    REQUIRE(view);
    CHECK(view->grid() == g);
}

TEST_CASE("the View menu changes the grid, and Khan remembers it", "[app][grid]") {
    App app;
    app.menu("kh.shell.grid.lines")->trigger();
    app.menu("kh.shell.grid.snap")->trigger();
    app.menu("kh.shell.grid.spacing.5")->trigger();
    app.menu("kh.shell.grid.show")->trigger();  // off
    settle();
    const auto& g = app.grid.settings();
    CHECK(g.style == plugin::GridStyle::Lines);
    CHECK(g.snap);
    CHECK(g.spacing == 5.0);
    CHECK_FALSE(g.visible);
    CHECK(app.shell->findChild<QLabel*>(QStringLiteral("kh.shell.gridstatus"))->text() ==
          QStringLiteral("Grid off, snap on"));

    // Next time Khan starts, the grid is as it was left.
    app.shell.reset();
    app.grid.set(plugin::GridSettings{});
    app.open();
    CHECK(app.grid.settings() == g);
    CHECK(app.menu("kh.shell.grid.lines")->isChecked());
    CHECK(app.menu("kh.shell.grid.spacing.5")->isChecked());
}

TEST_CASE("a spacing that isn't a preset shows in the Other item", "[app][grid]") {
    App app;
    REQUIRE(app.grid.setSpacing(12.5).empty());
    settle();
    CHECK(app.menu("kh.shell.grid.spacing.other")->text() == QStringLiteral("Other (12.5 mm)..."));
    CHECK_FALSE(app.menu("kh.shell.grid.spacing.10")->isChecked());
}

TEST_CASE("a damaged saved spacing falls back to the default", "[app][grid]") {
    App app;
    app.shell.reset();
    app.settings.setValue(QStringLiteral("grid/spacing"), -3.0);
    app.grid.set(plugin::GridSettings{});
    app.open();
    CHECK(app.grid.settings().spacing == 10.0);
}

TEST_CASE("a dense grid is drawn every few lines, so it doesn't turn grey", "[app][grid]") {
    App app;
    auto* view = app.shell->findChild<app::GridView*>(QStringLiteral("kh.shell.viewer"));
    REQUIRE(app.grid.setSpacing(10.0).empty());
    settle();
    CHECK(view->pixelsPerSpace() == 40.0);  // 10 mm at 4 px per mm
    REQUIRE(app.grid.setSpacing(0.5).empty());
    settle();
    CHECK(view->pixelsPerSpace() >= 8.0);  // every 5th line: 2.5 mm = 10 px
}
