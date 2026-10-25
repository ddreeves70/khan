#include "Shell.h"

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>

#include "AppUi.h"
#include "kh/core/Version.h"
#include "kh/io/KhanFile.h"
#include "kh/plugin/Events.h"

namespace kh::app {

namespace {

constexpr int kLayoutVersion = 2;  // bump when panel ids change, so old layouts are ignored
const char* kLastLayout = "layouts/last";
const char* kNamedLayouts = "layouts/named";
const char* kGeometry = "window/geometry";

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

ads::DockWidgetArea toAds(ui::PanelArea area) {
    switch (area) {
        case ui::PanelArea::Left:
            return ads::LeftDockWidgetArea;
        case ui::PanelArea::Right:
            return ads::RightDockWidgetArea;
        case ui::PanelArea::Bottom:
            return ads::BottomDockWidgetArea;
        case ui::PanelArea::Center:
            return ads::CenterDockWidgetArea;
    }
    return ads::LeftDockWidgetArea;
}

// The plugin a panel or action belongs to: the loaded plugin whose id, plus a
// dot, starts it. Panels of plugins that failed to load are dropped.
bool ownerLoaded(const plugin::PluginHost& host, const std::string& id) {
    for (const auto& p : host.plugins()) {
        const auto& pid = p.manifest.id;
        if (p.state == plugin::PluginState::Loaded && !pid.empty() && id.rfind(pid + ".", 0) == 0)
            return true;
    }
    return false;
}

}  // namespace

Shell::Shell(plugin::PluginHost& host, AppUi& ui, QSettings& settings, QWidget* parent)
    : QMainWindow(parent), host_(host), ui_(ui), settings_(settings) {
    setWindowTitle(QStringLiteral("Khan"));
    resize(1280, 800);

    askUnsaved = [this](const QString& name) {
        const auto answer = QMessageBox::question(
            this, tr("Save changes?"), tr("%1 has changes that aren't saved. Save them first?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        return answer == QMessageBox::Save      ? Unsaved::Save
               : answer == QMessageBox::Discard ? Unsaved::Discard
                                                : Unsaved::Cancel;
    };
    askSavePath = [this](const QString& suggested) {
        QString path =
            QFileDialog::getSaveFileName(this, tr("Save design"), suggested, tr("Khan designs (*.khan)"));
        if (!path.isEmpty() && !path.endsWith(QStringLiteral(".khan"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".khan");
        }
        return path;
    };

    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    docks_ = new ads::CDockManager(this);

    problemLabel_ = new QLabel(this);
    problemLabel_->setObjectName(QStringLiteral("kh.shell.problems"));
    problemLabel_->setStyleSheet(QStringLiteral("color: #c42b1c"));
    statusBar()->addWidget(problemLabel_, 1);
    const auto v = kh::version();
    statusBar()->addPermanentWidget(
        new QLabel(tr("Khan %1").arg(QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()))), this));
    ui_.setSinks([this](const Diagnostics& d) { showProblems(d); },
                 [this](const QString& m) { showMessage(m); });

    buildMenus();
    buildPanels();
    defaultLayout_ = saveLayout();

    using namespace plugin;
    auto later = [this](const auto&) {
        if (refreshPending_) return;
        refreshPending_ = true;
        QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    };
    subs_.push_back(host_.events().subscribe<DocumentChanged>([this, later](const DocumentChanged& e) {
        problemLabel_->clear();  // a change replaces the old problems
        later(e);
    }));
    subs_.push_back(host_.events().subscribe<RebuildFinished>(later));
    subs_.push_back(host_.events().subscribe<ActiveDocumentChanged>(later));
    subs_.push_back(host_.events().subscribe<SelectionChanged>(later));
    subs_.push_back(
        host_.events().subscribe<SettingChanged>([this](const SettingChanged&) { refreshActions(); }));

    // Start with an empty design, so Create works straight away.
    host_.documents().add(std::make_unique<model::Document>(model::DocKind::Part, "Untitled"));

    // Put the window and panels back where they were last time.
    restoreGeometry(settings_.value(QLatin1String(kGeometry)).toByteArray());
    const auto last = settings_.value(QLatin1String(kLastLayout)).toByteArray();
    if (!last.isEmpty()) restoreLayout(last);
    rebuildLayoutMenu();
    refresh();
}

Shell::~Shell() {
    subs_.clear();
    ui_.setSinks({}, {});
}

// ---------------------------------------------------------------------------
// Menus and panels
// ---------------------------------------------------------------------------

void Shell::buildMenus() {
    toolbar_ = addToolBar(tr("Tools"));
    toolbar_->setObjectName(QStringLiteral("kh.shell.toolbar"));
    toolbar_->setMovable(false);

    auto* file = menuBar()->addMenu(tr("&File"));
    auto* newAct = file->addAction(tr("&New"), this, [this] { newDesign(); });
    newAct->setShortcut(QKeySequence::New);
    auto* openAct = file->addAction(tr("&Open..."), this, [this] {
        if (!confirmLoseChanges()) return;
        const auto path =
            QFileDialog::getOpenFileName(this, tr("Open design"), QString(), tr("Khan designs (*.khan)"));
        if (!path.isEmpty()) openDesign(path.toStdWString());
    });
    openAct->setShortcut(QKeySequence::Open);
    save_ = file->addAction(tr("&Save"), this, [this] { save(); });
    save_->setShortcut(QKeySequence::Save);
    auto* saveAs = file->addAction(tr("Save &as..."), this, [this] {
        const auto path =
            askSavePath(qs(host_.documents().find(activeDesign())->name()) + QStringLiteral(".khan"));
        if (!path.isEmpty()) saveDesign(path.toStdWString());
    });
    saveAs->setShortcut(QKeySequence::SaveAs);
    file->addSeparator();
    file->addAction(tr("&Quit"), this, [this] { close(); })->setShortcut(QKeySequence::Quit);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    undo_ = edit->addAction(tr("Undo"), this, [this] { host_.documents().undo(activeDesign()); });
    undo_->setObjectName(QStringLiteral("kh.shell.undo"));
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(tr("Redo"), this, [this] { host_.documents().redo(activeDesign()); });
    redo_->setObjectName(QStringLiteral("kh.shell.redo"));
    redo_->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});

    toolbar_->addAction(newAct);
    toolbar_->addAction(openAct);
    toolbar_->addAction(save_);
    toolbar_->addSeparator();
    toolbar_->addAction(undo_);
    toolbar_->addAction(redo_);

    // Plugin actions, in the menus they name (made in the order first named).
    // View is made now, so plugins can add to it, and placed after their menus.
    viewMenu_ = new QMenu(tr("&View"), this);
    connect(viewMenu_, &QMenu::aboutToShow, this, [this] { refreshActions(); });
    std::map<std::string, QMenu*> menus{{"File", file}, {"Edit", edit}, {"View", viewMenu_}};
    // "View/Grid style" is the Grid style submenu of View.
    auto menuFor = [&](const std::string& path) {
        std::string sofar;
        QMenu* parent = nullptr;
        std::size_t from = 0;
        for (;;) {
            const auto slash = path.find('/', from);
            const std::string part =
                path.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
            sofar += (sofar.empty() ? "" : "/") + part;
            QMenu*& m = menus[sofar];
            if (!m) {
                m = parent ? parent->addMenu(qs(part)) : menuBar()->addMenu(qs(part));
                connect(m, &QMenu::aboutToShow, this, [this] { refreshActions(); });
            }
            parent = m;
            if (slash == std::string::npos) return m;
            from = slash + 1;
        }
    };
    bool toolbarSeparator = false;
    for (const auto& spec : host_.actions()) {
        QMenu* menu = menuFor(spec.menu);
        if (menu == edit && edit->actions().size() == 2) edit->addSeparator();
        auto* a = menu->addAction(qs(spec.text));
        a->setObjectName(qs(spec.id));
        a->setToolTip(qs(spec.tip));
        a->setStatusTip(qs(spec.tip));
        if (!spec.shortcut.empty()) a->setShortcut(QKeySequence(qs(spec.shortcut)));
        a->setCheckable(static_cast<bool>(spec.checked));
        connect(a, &QAction::triggered, this, [this, spec] { runAction(spec); });
        if (spec.toolbar) {
            if (!toolbarSeparator) toolbar_->addSeparator();
            toolbarSeparator = true;
            toolbar_->addAction(a);
        }
        actions_.emplace_back(spec, a);
    }

    menuBar()->addMenu(viewMenu_);  // after the plugins' own menus
    windowMenu_ = menuBar()->addMenu(tr("&Window"));
    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Khan"), this, [this] {
        const auto ver = kh::version();
        QMessageBox::about(this, tr("About Khan"),
                           tr("Khan %1\nFree, open-source CAD.\nhttps://github.com/ddreeves70/khan")
                               .arg(QString::fromUtf8(ver.data(), static_cast<qsizetype>(ver.size()))));
    });
}

void Shell::buildPanels() {
    // The middle of the window is the first Center panel (kh.viewer's viewer).
    std::vector<const ui::PanelSpec*> specs;
    for (const auto& spec : ui_.panels()) {
        if (ownerLoaded(host_, spec.id) && spec.create) specs.push_back(&spec);
    }
    const auto centerSpec = std::find_if(specs.begin(), specs.end(),
                                         [](const auto* s) { return s->area == ui::PanelArea::Center; });
    auto makeDock = [this](const ui::PanelSpec& spec, const QString& id, const QString& title) {
        auto* dock = new ads::CDockWidget(docks_, title);
        dock->setObjectName(id);
        // Panels are as wide as their content needs (their minimum width), not squeezed.
        dock->setWidget(spec.create(), ads::CDockWidget::ForceNoScrollArea);
        dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
        return dock;
    };
    if (centerSpec != specs.end()) {
        centerArea_ =
            docks_->setCentralWidget(makeDock(**centerSpec, qs((*centerSpec)->id), (*centerSpec)->title));
    } else {
        auto* missing =
            new QLabel(tr("The 3D viewer plugin (kh.viewer) didn't load, so there's nothing to show "
                          "here. Reinstall Khan to get it back."));
        missing->setAlignment(Qt::AlignCenter);
        missing->setWordWrap(true);
        auto* dock = new ads::CDockWidget(docks_, tr("Viewer"));
        dock->setObjectName(QStringLiteral("kh.shell.noviewer"));
        dock->setWidget(missing);
        centerArea_ = docks_->setCentralWidget(dock);
    }

    for (const auto* spec : specs) {
        if (centerSpec != specs.end() && spec == *centerSpec) continue;
        auto* dock = makeDock(*spec, qs(spec->id), spec->title);
        if (spec->area == ui::PanelArea::Center) {
            docks_->addDockWidgetTabToArea(dock, centerArea_);
        } else {
            docks_->addDockWidget(toAds(spec->area), dock);
        }
    }

    for (const auto& id : dockIds()) {
        if (auto* dock = docks_->findDockWidget(id)) windowMenu_->addAction(dock->toggleViewAction());
    }
    windowMenu_->addSeparator();
    for (const auto* spec : specs) {
        if (!spec->allowMultiple) continue;
        const QString id = qs(spec->id);
        auto* a = windowMenu_->addAction(tr("New %1").arg(spec->title.toLower()), this,
                                         [this, id] { openAnotherPanel(id); });
        a->setObjectName(QStringLiteral("kh.shell.new.") + id);
        a->setStatusTip(tr("Open another %1 next to the others.").arg(spec->title.toLower()));
    }
    windowMenu_->addSeparator();
    layoutMenu_ = windowMenu_->addMenu(tr("&Layouts"));

    // Plugins' status-bar items, at the bottom right, before Khan's own.
    int at = 1;  // after the problems label
    for (const auto& item : ui_.statusItems()) {
        if (!ownerLoaded(host_, item.id) || !item.create) continue;
        if (auto* w = item.create()) {
            w->setObjectName(qs(item.id));
            statusBar()->insertPermanentWidget(at++, w);
        }
    }
}

QString Shell::openAnotherPanel(const QString& id) {
    for (const auto& spec : ui_.panels()) {
        if (qs(spec.id) != id || !spec.allowMultiple || !spec.create || !ownerLoaded(host_, spec.id))
            continue;
        // Number copies 2, 3, ... skipping ids still open.
        int n = std::max(copies_[id], 1);
        QString newId;
        do {
            newId = QStringLiteral("%1.%2").arg(id).arg(++n);
        } while (docks_->findDockWidget(newId));
        copies_[id] = n;
        auto* dock = new ads::CDockWidget(docks_, QStringLiteral("%1 %2").arg(spec.title).arg(n));
        dock->setObjectName(newId);
        dock->setWidget(spec.create(), ads::CDockWidget::ForceNoScrollArea);
        // Copies aren't remembered between runs: closing one deletes it.
        dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
        docks_->addDockWidget(ads::RightDockWidgetArea, dock, centerArea_);
        return newId;
    }
    return {};
}

void Shell::rebuildLayoutMenu() {
    layoutMenu_->clear();
    for (const auto& name : layoutNames()) {
        layoutMenu_->addAction(name, this, [this, name] { openNamedLayout(name); });
    }
    if (!layoutNames().isEmpty()) layoutMenu_->addSeparator();
    layoutMenu_->addAction(tr("Save layout as..."), this, [this] {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("Save layout"), tr("Name for this layout:"),
                                                QLineEdit::Normal, tr("My layout"), &ok);
        if (ok && !name.trimmed().isEmpty()) saveNamedLayout(name.trimmed());
    });
    layoutMenu_->addAction(tr("Reset layout"), this, [this] { resetLayout(); });
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void Shell::refreshActions() {
    for (auto& [spec, a] : actions_) {
        try {
            if (spec.checked) a->setChecked(spec.checked());
            if (spec.currentText) a->setText(qs(spec.currentText()));
        } catch (...) {
            // A plugin's check-mark function failed: leave the item as it was.
        }
    }
}

void Shell::runAction(const plugin::ActionSpec& spec) {
    if (spec.run) {
        // The plugin's own code (a setting, a dialog), not a change to a design.
        try {
            spec.run();
        } catch (const std::exception& e) {
            showProblems({{"action.failed", Severity::Error,
                           tr("%1 stopped with an error (%2). Ask the plugin's author for help.")
                               .arg(qs(spec.text), QString::fromUtf8(e.what()))
                               .toStdString(),
                           spec.plugin}});
        } catch (...) {
            showProblems({{"action.failed", Severity::Error,
                           tr("%1 stopped with an error. Ask the plugin's author for help.")
                               .arg(qs(spec.text))
                               .toStdString(),
                           spec.plugin}});
        }
        refreshActions();
        return;
    }
    auto& docs = host_.documents();
    const DocId doc = activeDesign();
    model::Args args = spec.args;
    const auto& picked = host_.selection().features();
    if (!spec.fromSelection.empty()) {
        if (picked.size() != spec.fromSelection.size() || host_.selection().doc() != doc) {
            showProblems({{"action.selection",
                           Severity::Error,
                           tr("%1 needs %2 steps picked in the feature tree, in order.")
                               .arg(qs(spec.text))
                               .arg(spec.fromSelection.size())
                               .toStdString(),
                           {}}});
            return;
        }
        for (std::size_t i = 0; i < picked.size(); ++i) args[spec.fromSelection[i]] = picked[i];
    }
    const auto before = docs.find(doc)->featureCount();
    auto problems = docs.run(doc, spec.command, args);
    if (!problems.empty()) {
        showProblems(problems);
        return;
    }
    // Select the step the action made, so Properties shows its numbers.
    const auto* design = docs.find(doc);
    if (design && design->featureCount() > before) {
        host_.selection().set(doc, {design->featureAt(design->featureCount() - 1).id()});
    }
}

QAction* Shell::action(const std::string& id) const {
    for (const auto& [spec, a] : actions_) {
        if (spec.id == id) return a;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Designs
// ---------------------------------------------------------------------------

DocId Shell::activeDesign() const {
    return host_.documents().active();
}

std::filesystem::path Shell::activePath() const {
    auto it = paths_.find(activeDesign());
    return it == paths_.end() ? std::filesystem::path() : it->second;
}

bool Shell::confirmLoseChanges() {
    const auto* doc = host_.documents().find(activeDesign());
    if (!doc || !doc->isModified()) return true;
    switch (askUnsaved(qs(doc->name()))) {
        case Unsaved::Save:
            return save();
        case Unsaved::Discard:
            return true;
        case Unsaved::Cancel:
            return false;
    }
    return false;
}

void Shell::replaceActive(std::unique_ptr<model::Document> doc, std::filesystem::path path) {
    auto& docs = host_.documents();
    const DocId old = activeDesign();
    const DocId id = docs.add(std::move(doc));
    if (!path.empty()) paths_[id] = std::move(path);
    docs.setActive(id);
    if (!old.isNil() && old != id) {
        docs.close(old);
        paths_.erase(old);
    }
    refresh();
}

bool Shell::newDesign() {
    if (!confirmLoseChanges()) return false;
    replaceActive(std::make_unique<model::Document>(model::DocKind::Part, "Untitled"), {});
    return true;
}

Diagnostics Shell::openDesign(const std::filesystem::path& path) {
    auto opened = io::openFile(path);
    if (!opened.ok()) {
        showProblems(opened.issues);
        return opened.issues;
    }
    if (host_.documents().find(opened.document->id())) {
        // Already open (the same design): just show it.
        host_.documents().setActive(opened.document->id());
        return {};
    }
    replaceActive(std::move(opened.document), path);
    showMessage(tr("Opened %1").arg(QString::fromStdWString(path.filename().wstring())));
    return {};
}

Diagnostics Shell::saveDesign(const std::filesystem::path& path) {
    const DocId id = activeDesign();
    const auto* doc = host_.documents().find(id);
    if (!doc) return {};
    auto problems = io::writeFile(*doc, path);
    if (!problems.empty()) {
        showProblems(problems);
        return problems;
    }
    host_.documents().markSaved(id);
    paths_[id] = path;
    showMessage(tr("Saved %1").arg(QString::fromStdWString(path.filename().wstring())));
    refresh();
    return {};
}

bool Shell::save() {
    auto path = activePath();
    if (path.empty()) {
        const auto* doc = host_.documents().find(activeDesign());
        const auto chosen = askSavePath(qs(doc ? doc->name() : "Untitled") + QStringLiteral(".khan"));
        if (chosen.isEmpty()) return false;
        path = chosen.toStdWString();
    }
    return saveDesign(path).empty();
}

void Shell::selectSteps(const QStringList& names) {
    const auto* doc = host_.documents().find(activeDesign());
    if (!doc) return;
    std::vector<FeatureId> ids;
    for (const auto& name : names) {
        for (std::size_t i = 0; i < doc->featureCount(); ++i) {
            if (qs(doc->featureAt(i).name()) == name) ids.push_back(doc->featureAt(i).id());
        }
    }
    host_.selection().set(activeDesign(), ids);
}

// ---------------------------------------------------------------------------
// What the window shows
// ---------------------------------------------------------------------------

void Shell::refresh() {
    refreshPending_ = false;
    auto& docs = host_.documents();
    const auto* doc = docs.find(activeDesign());
    const QString name = doc ? qs(doc->name()) : tr("Untitled");
    setWindowTitle(tr("%1[*] - Khan").arg(name));
    setWindowModified(doc && doc->isModified());

    undo_->setEnabled(doc && doc->canUndo());
    redo_->setEnabled(doc && doc->canRedo());
    undo_->setText(doc && doc->canUndo() ? tr("Undo %1").arg(qs(doc->undoLabel())) : tr("Undo"));
    redo_->setText(doc && doc->canRedo() ? tr("Redo %1").arg(qs(doc->redoLabel())) : tr("Redo"));

    const auto& sel = host_.selection();
    for (auto& [spec, a] : actions_) {
        bool ok = doc != nullptr || static_cast<bool>(spec.run);
        if (!spec.fromSelection.empty()) {
            ok = ok && sel.doc() == activeDesign() && sel.features().size() == spec.fromSelection.size();
        }
        a->setEnabled(ok);
    }
    refreshActions();
}

void Shell::showProblems(const Diagnostics& problems) {
    lastProblems_ = problems;
    QStringList lines;
    for (const auto& d : problems) lines << qs(d.message);
    problemLabel_->setText(lines.join(QStringLiteral("  ")));
}

void Shell::showMessage(const QString& message) {
    problemLabel_->clear();
    statusBar()->showMessage(message, 5000);
}

// ---------------------------------------------------------------------------
// Layouts
// ---------------------------------------------------------------------------

QStringList Shell::dockIds() const {
    QStringList ids = docks_->dockWidgetsMap().keys();
    ids.sort();
    return ids;
}

QWidget* Shell::panelWidget(const QString& id) const {
    auto* dock = docks_->findDockWidget(id);
    return dock ? dock->widget() : nullptr;
}

QByteArray Shell::saveLayout() const {
    return docks_->saveState(kLayoutVersion);
}

bool Shell::restoreLayout(const QByteArray& layout) {
    if (!docks_->restoreState(layout, kLayoutVersion)) return false;
    bringPanelsOnScreen();
    return true;
}

void Shell::saveNamedLayout(const QString& name) {
    settings_.setValue(QLatin1String(kNamedLayouts) + QLatin1Char('/') + name, saveLayout());
    rebuildLayoutMenu();
    showMessage(tr("Saved the layout %1").arg(name));
}

bool Shell::openNamedLayout(const QString& name) {
    const auto saved = settings_.value(QLatin1String(kNamedLayouts) + QLatin1Char('/') + name).toByteArray();
    if (saved.isEmpty() || !restoreLayout(saved)) {
        showProblems(
            {{"layout.bad",
              Severity::Error,
              tr("The layout %1 was made by another version of Khan. Arrange the panels and save it again.")
                  .arg(name)
                  .toStdString(),
              {}}});
        return false;
    }
    return true;
}

QStringList Shell::layoutNames() const {
    settings_.beginGroup(QLatin1String(kNamedLayouts));
    QStringList names = settings_.childKeys();
    settings_.endGroup();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void Shell::resetLayout() {
    restoreLayout(defaultLayout_);
}

int Shell::bringPanelsOnScreen() {
    int moved = 0;
    for (auto* floating : docks_->floatingWidgets()) {
        const QRect frame = floating->frameGeometry();
        bool visible = false;
        for (const auto* screen : QGuiApplication::screens()) {
            if (screen->availableGeometry().intersects(frame)) visible = true;
        }
        if (!visible) {
            const QRect home = QGuiApplication::primaryScreen()->availableGeometry();
            floating->move(home.center() - QPoint(frame.width() / 2, frame.height() / 2));
            ++moved;
        }
    }
    return moved;
}

void Shell::closeEvent(QCloseEvent* event) {
    if (!confirmLoseChanges()) {
        event->ignore();
        return;
    }
    settings_.setValue(QLatin1String(kGeometry), saveGeometry());
    settings_.setValue(QLatin1String(kLastLayout), saveLayout());
    event->accept();
}

}  // namespace kh::app
