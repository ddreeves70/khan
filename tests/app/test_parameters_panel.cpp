// Tests for the Parameters panel (kh.parameters): what it lists, and adding,
// changing, renaming and deleting parameters in it.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <memory>

#include "AppSettings.h"
#include "AppUi.h"
#include "Shell.h"
#include "kh/kernel_occt/OcctKernel.h"
#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugins/Modeling.h"
#include "kh/plugins/Parameters.h"
#include "kh/sketch_planegcs/PlaneGcsSolver.h"

using namespace kh;

namespace {

void settle() {
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
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
    QTableWidget* table = nullptr;

    App() {
        host.provide<ui::UiRegistry>(ui);
        host.setSettings(pluginSettings);
        host.addBuiltin(plugins::modelingPlugin());
        host.addBuiltin(plugins::parametersPlugin());
        host.loadAll();
        shell = std::make_unique<app::Shell>(host, ui, settings);
        shell->askUnsaved = [](const QString&) { return app::Shell::Unsaved::Discard; };
        shell->show();
        settle();
        // Modify > Parameters opens it (it starts closed).
        auto* open = shell->action("kh.parameters.show");
        REQUIRE(open);
        open->trigger();
        settle();
        auto* panel = shell->panelWidget(QStringLiteral("kh.parameters.panel"));
        REQUIRE(panel);
        CHECK(panel->isVisible());
        table = panel->findChild<QTableWidget*>(QStringLiteral("kh.parameters.table"));
        REQUIRE(table);
    }

    const model::Document& doc() { return *host.documents().find(shell->activeDesign()); }
    Diagnostics set(std::string name, std::string value) {
        return host.documents().execute(shell->activeDesign(), std::make_unique<model::SetParameterCommand>(
                                                                   std::move(name), std::move(value)));
    }
    QString cell(int row, int column) {
        const auto* item = table->item(row, column);
        return item ? item->text() : QString();
    }
    // Types into a cell, as the student does.
    void type(int row, int column, const QString& text) {
        table->item(row, column)->setText(text);
        settle();
    }
    QPushButton* button(const char* name) {
        return shell->panelWidget(QStringLiteral("kh.parameters.panel"))
            ->findChild<QPushButton*>(QString::fromLatin1(name));
    }
};

}  // namespace

TEST_CASE("the Parameters panel lists each parameter with its value, what it works out to, and its note",
          "[app][parameters]") {
    App app;
    CHECK(app.table->rowCount() == 0);
    REQUIRE(app.set("wall", "3 mm").empty());
    REQUIRE(app.set("height", "wall * 4 + 1 in").empty());
    settle();
    REQUIRE(app.table->rowCount() == 2);
    CHECK(app.cell(0, 0) == "wall");
    CHECK(app.cell(0, 1) == "3 mm");
    CHECK(app.cell(0, 2) == "3 mm");
    CHECK(app.cell(1, 1) == "wall * 4 + 1 in");
    CHECK(app.cell(1, 2) == "37.4 mm");
    // What it works out to can't be typed over.
    CHECK_FALSE(app.table->item(1, 2)->flags() & Qt::ItemIsEditable);
}

TEST_CASE("typing in the Parameters panel changes, renames and notes a parameter, each one undo step",
          "[app][parameters]") {
    App app;
    REQUIRE(app.set("wall", "3 mm").empty());
    REQUIRE(app.set("height", "wall * 4").empty());
    settle();

    app.type(0, 1, "2 mm");
    CHECK(app.doc().parameters().find("wall")->expression == "2 mm");
    CHECK(app.cell(1, 2) == "8 mm");  // height followed
    CHECK(app.doc().undoLabel() == "Change wall to 2 mm");

    app.type(0, 0, "thickness");
    REQUIRE(app.doc().parameters().find("thickness"));
    CHECK(app.doc().parameters().find("height")->expression == "thickness * 4");
    CHECK(app.cell(1, 1) == "thickness * 4");

    app.type(0, 3, "How thick the walls are");
    CHECK(app.doc().parameters().find("thickness")->note == "How thick the walls are");
    CHECK(app.doc().parameters().find("thickness")->expression == "2 mm");  // kept

    // A change that can't be made is refused, said why, and the cell goes back.
    app.type(0, 1, "2");  // a plain number works: height becomes one too
    CHECK(app.doc().parameters().find("thickness")->expression == "2");
    app.type(0, 1, "2 kg");
    CHECK(app.doc().parameters().find("thickness")->expression == "2");
    REQUIRE_FALSE(app.shell->lastProblems().empty());
    CHECK(app.shell->lastProblems().front().code == "expression.unit");
    CHECK(app.cell(0, 1) == "2");
    app.type(1, 0, "thickness");  // a name that's taken
    CHECK(app.shell->lastProblems().front().code == "parameter.name_taken");
    CHECK(app.cell(1, 0) == "height");
}

TEST_CASE("Add adds a parameter ready to name; Delete deletes the picked ones", "[app][parameters]") {
    App app;
    auto* add = app.button("kh.parameters.add");
    auto* remove = app.button("kh.parameters.delete");
    REQUIRE(add);
    REQUIRE(remove);
    CHECK_FALSE(remove->isEnabled());  // nothing picked
    add->click();
    settle();
    REQUIRE(app.table->rowCount() == 1);
    CHECK(app.cell(0, 0) == "size1");
    CHECK(app.cell(0, 1) == "10 mm");
    add->click();
    settle();
    CHECK(app.cell(1, 0) == "size2");

    REQUIRE(app.set("double", "size1 * 2").empty());
    settle();
    app.table->selectRow(0);  // size1, which double uses
    CHECK(remove->isEnabled());
    remove->click();
    settle();
    CHECK(app.doc().parameters().find("size1"));  // refused
    CHECK(app.shell->lastProblems().front().code == "parameter.in_use");
    app.table->selectRow(1);  // size2
    remove->click();
    settle();
    CHECK_FALSE(app.doc().parameters().find("size2"));
    CHECK(app.table->rowCount() == 2);
}
