#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/kernel/IKernel.h"
#include "kh/plugin/PluginHost.h"

class QAction;
class QLabel;
class QMenu;
class QSettings;
class QToolBar;

namespace ads {
class CDockManager;
class CDockWidget;
}  // namespace ads

namespace kh::app {

class AppUi;

// The main window. It owns no features of its own: panels, menu items and
// toolbar buttons come from plugins (through AppUi and PluginHost::actions()).
// The shell adds what every program needs: File (new, open, save), Edit (undo,
// redo), Window (panels and layouts), and one open design at a time.
class Shell : public QMainWindow {
    Q_OBJECT
public:
    // `host` must have loaded its plugins. `settings` keeps layouts and the
    // window's place between runs.
    Shell(plugin::PluginHost& host, AppUi& ui, kernel::IKernel& kernel, QSettings& settings,
          QWidget* parent = nullptr);
    ~Shell() override;

    // What to do with unsaved changes before they would be lost.
    enum class Unsaved { Save, Discard, Cancel };
    // Asks the student. Tests replace it so no dialog opens.
    std::function<Unsaved(const QString& name)> askUnsaved;
    // Asks where to save. Tests replace it. Empty means cancelled.
    std::function<QString(const QString& suggested)> askSavePath;

    // Designs. Each returns what went wrong, if anything (also shown in the status bar).
    bool newDesign();  // false if the student cancelled
    Diagnostics openDesign(const std::filesystem::path& path);
    Diagnostics saveDesign(const std::filesystem::path& path);
    bool save();  // to its file, or asks where; false if cancelled or failed
    DocId activeDesign() const;
    std::filesystem::path activePath() const;

    // Selects steps by name in the active design (for --select and tests).
    void selectSteps(const QStringList& names);

    // Plugin actions by id, e.g. "kh.modeling.box"; nullptr if there is none.
    QAction* action(const std::string& id) const;
    QAction* undoAction() const { return undo_; }
    QAction* redoAction() const { return redo_; }

    // Panels and layouts.
    QStringList dockIds() const;
    QWidget* panelWidget(const QString& id) const;
    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray& layout);
    void saveNamedLayout(const QString& name);
    bool openNamedLayout(const QString& name);
    QStringList layoutNames() const;
    void resetLayout();
    // Moves floating panels that are off every screen (a monitor was unplugged)
    // back onto the main screen. Returns how many it moved.
    int bringPanelsOnScreen();

    // The last problems shown, for tests.
    const Diagnostics& lastProblems() const { return lastProblems_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void buildPanels();
    void runAction(const plugin::ActionSpec& spec);
    void refresh();
    void refreshViewer();
    void showProblems(const Diagnostics& problems);
    void showMessage(const QString& message);
    bool confirmLoseChanges();
    void replaceActive(std::unique_ptr<model::Document> doc, std::filesystem::path path);
    void rebuildLayoutMenu();

    plugin::PluginHost& host_;
    AppUi& ui_;
    kernel::IKernel& kernel_;
    QSettings& settings_;
    ads::CDockManager* docks_ = nullptr;
    QLabel* viewer_ = nullptr;
    QLabel* problemLabel_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QMenu* windowMenu_ = nullptr;
    QMenu* layoutMenu_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
    QAction* save_ = nullptr;
    std::vector<std::pair<plugin::ActionSpec, QAction*>> actions_;
    std::map<DocId, std::filesystem::path> paths_;
    QByteArray defaultLayout_;
    Diagnostics lastProblems_;
    std::vector<plugin::Subscription> subs_;
    bool refreshPending_ = false;
};

}  // namespace kh::app
