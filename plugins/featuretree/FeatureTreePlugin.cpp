#include "kh/plugins/FeatureTree.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "kh/model/Document.h"
#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.featuretree",
  "name": "Feature tree",
  "version": "0.1.0",
  "description": "The panel that lists a design's steps.",
  "requires": ["kh.modeling"],
  "tier": "ui"
})";

// Where a folder row keeps its folder's id (a step row keeps its step's id in Qt::UserRole).
constexpr int kFolderRole = Qt::UserRole + 1;

const char* kindName(const model::Feature& f) {
    switch (f.type()) {
        case model::FeatureType::Box:
            return "Box";
        case model::FeatureType::Cylinder:
            return "Cylinder";
        case model::FeatureType::Boolean: {
            switch (static_cast<const model::BooleanFeature&>(f).op()) {
                case kernel::BoolOp::Union:
                    return "Union";
                case kernel::BoolOp::Cut:
                    return "Cut";
                case kernel::BoolOp::Intersect:
                    return "Intersect";
            }
            break;
        }
        case model::FeatureType::Move:
            return "Move";
        case model::FeatureType::Sketch:
            return "Sketch";
        case model::FeatureType::Extrude:
            return "Extrude";
    }
    return "Step";
}

// The panel. One row per step, in build order: name, kind, status.
class FeatureTree final : public QWidget {
public:
    FeatureTree(plugin::PluginContext& ctx, ui::UiRegistry& ui) : ctx_(ctx), ui_(ui) {
        setObjectName(QStringLiteral("kh.featuretree.widget"));
        setMinimumWidth(260);
        tree_ = new QTreeWidget(this);
        tree_->setObjectName(QStringLiteral("kh.featuretree.tree"));
        tree_->setColumnCount(3);
        tree_->setHeaderLabels({tr("Step"), tr("Kind"), tr("Status")});
        tree_->setRootIsDecorated(false);
        tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        tree_->header()->setStretchLastSection(false);
        tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        tree_->setContextMenuPolicy(Qt::CustomContextMenu);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(tree_);

        connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] { pickedRows(); });
        connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
            if (column == 0) renamed(item);
        });
        connect(tree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
            if (!filling_ && !folderIdOf(item).isNil()) collapsed_.insert(folderIdOf(item));
        });
        connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
            if (!filling_) collapsed_.erase(folderIdOf(item));
        });
        connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) { menu(at); });

        using namespace plugin;
        subs_.push_back(
            ctx_.events().subscribe<ActiveDocumentChanged>([this](const auto&) { scheduleRefill(); }));
        subs_.push_back(ctx_.events().subscribe<DocumentChanged>([this](const auto&) { scheduleRefill(); }));
        subs_.push_back(ctx_.events().subscribe<RebuildFinished>([this](const auto&) { scheduleRefill(); }));
        subs_.push_back(ctx_.events().subscribe<SelectionChanged>([this](const auto&) { showSelection(); }));
        refill();
    }

private:
    static FeatureId idOf(const QTreeWidgetItem* item) {
        return FeatureId::parse(item->data(0, Qt::UserRole).toString().toStdString()).value_or(FeatureId{});
    }

    // Rows are rebuilt on the next turn of the event loop: a change often starts
    // inside one of the tree's own signals (a rename), where deleting its rows
    // would pull them out from under Qt. Several changes in a row refill once.
    void scheduleRefill() {
        if (refillPending_) return;
        refillPending_ = true;
        QMetaObject::invokeMethod(this, [this] { refill(); }, Qt::QueuedConnection);
    }

    // Rebuilds the rows from the active design and its latest rebuild. A folder
    // is a row with its steps under it.
    void refill() {
        refillPending_ = false;
        filling_ = true;
        tree_->clear();
        const DocId doc = ctx_.documents().active();
        const auto* design = ctx_.documents().find(doc);
        const auto* report = ctx_.documents().lastRebuild(doc);
        const auto folders = design ? design->folders() : std::vector<model::Folder>{};
        tree_->setRootIsDecorated(!folders.empty());
        if (design) {
            std::map<FolderId, QTreeWidgetItem*> folderRows;
            for (std::size_t i = 0; i < design->featureCount(); ++i) {
                const auto& f = design->featureAt(i);
                QTreeWidgetItem* parent = nullptr;
                if (const FolderId in = design->folderOf(f.id()); !in.isNil()) {
                    auto& row = folderRows[in];
                    if (!row) row = folderRow(*design->findFolder(in));
                    parent = row;
                }
                auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
                item->setText(0, QString::fromStdString(f.name()));
                item->setText(1, QString::fromLatin1(kindName(f)));
                item->setData(0, Qt::UserRole, QString::fromStdString(f.id().toString()));
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                const auto* r = report ? report->result(f.id()) : nullptr;
                QString status = tr("ok");
                QString tip;
                if (r) {
                    switch (r->status) {
                        case model::FeatureStatus::Ok:
                            break;
                        case model::FeatureStatus::Failed:
                            status = tr("failed");
                            break;
                        case model::FeatureStatus::Blocked:
                            status = tr("blocked");
                            break;
                        case model::FeatureStatus::Suppressed:
                            status = tr("off");
                            break;
                    }
                    for (const auto& d : r->issues)
                        tip += QString::fromStdString(d.message) + QLatin1Char('\n');
                }
                setStatus(item, status);
                if (!tip.isEmpty()) {
                    for (int col = 0; col < 3; ++col) item->setToolTip(col, tip.trimmed());
                }
            }
            // A folder's status: a problem inside it, or off if all its steps are off.
            for (auto& [id, row] : folderRows) {
                QString status = tr("ok");
                int off = 0;
                for (int c = 0; c < row->childCount(); ++c) {
                    const QString s = row->child(c)->text(2);
                    if (s == tr("off")) ++off;
                    if ((s == tr("failed") || s == tr("blocked")) && status == tr("ok")) status = s;
                }
                if (status == tr("ok") && off == row->childCount()) status = tr("off");
                setStatus(row, status);
                row->setExpanded(!collapsed_.count(id));
            }
        }
        filling_ = false;
        showSelection();
    }

    QTreeWidgetItem* folderRow(const model::Folder& folder) {
        auto* row = new QTreeWidgetItem(tree_);
        row->setText(0, QString::fromStdString(folder.name));
        row->setText(1, tr("Folder"));
        row->setData(0, kFolderRole, QString::fromStdString(folder.id.toString()));
        row->setFlags(row->flags() | Qt::ItemIsEditable);
        row->setToolTip(
            0, tr("A folder of %n step(s). It keeps the tree tidy; it doesn't change what's built.", nullptr,
                  static_cast<int>(folder.steps.size())));
        return row;
    }

    void setStatus(QTreeWidgetItem* item, const QString& status) {
        item->setText(2, status);
        if (status != tr("ok")) {
            const QColor c = status == tr("off") ? palette().color(QPalette::Disabled, QPalette::Text)
                                                 : QColor(196, 43, 28);
            for (int col = 0; col < 3; ++col) item->setForeground(col, c);
        }
    }

    static FolderId folderIdOf(const QTreeWidgetItem* item) {
        return FolderId::parse(item->data(0, kFolderRole).toString().toStdString()).value_or(FolderId{});
    }

    // Every row, folders' steps included.
    std::vector<QTreeWidgetItem*> rows() const {
        std::vector<QTreeWidgetItem*> out;
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) out.push_back(*it);
        return out;
    }

    // The steps in a folder row.
    static std::vector<FeatureId> stepsOf(const QTreeWidgetItem* folderRow) {
        std::vector<FeatureId> out;
        for (int c = 0; c < folderRow->childCount(); ++c) out.push_back(idOf(folderRow->child(c)));
        return out;
    }

    // Shows the shared selection in the rows, without sending it back. A folder
    // row is picked when all its steps are.
    void showSelection() {
        filling_ = true;
        const auto& sel = ctx_.selection();
        const bool here = sel.doc() == ctx_.documents().active();
        pickedFolders_.clear();
        for (auto* item : rows()) {
            const FolderId folder = folderIdOf(item);
            if (folder.isNil()) {
                item->setSelected(here && sel.contains(idOf(item)));
                continue;
            }
            const auto steps = stepsOf(item);
            const bool all =
                here && !steps.empty() &&
                std::all_of(steps.begin(), steps.end(), [&](const auto& id) { return sel.contains(id); });
            item->setSelected(all);
            if (all) pickedFolders_.insert(folder);
        }
        picked_ = here ? sel.features() : std::vector<FeatureId>{};
        filling_ = false;
    }

    // The student picked rows: keep the order they were picked in. Picking a
    // folder row picks all its steps; un-picking it un-picks them.
    void pickedRows() {
        if (filling_) return;
        std::vector<FeatureId> now;
        std::vector<FeatureId> dropped;
        for (auto* item : rows()) {
            const FolderId folder = folderIdOf(item);
            if (folder.isNil()) {
                if (item->isSelected()) now.push_back(idOf(item));
                continue;
            }
            const bool was = pickedFolders_.count(folder) > 0;
            bool anyChild = false;
            for (int c = 0; c < item->childCount(); ++c) anyChild = anyChild || item->child(c)->isSelected();
            // Newly picked, or clicked on its own (Qt un-picks the steps under it).
            if (item->isSelected() && (!was || !anyChild)) {
                for (const auto& id : stepsOf(item)) now.push_back(id);
            } else if (!item->isSelected() && was) {
                for (const auto& id : stepsOf(item)) dropped.push_back(id);
            }
        }
        auto has = [](const std::vector<FeatureId>& list, const FeatureId& id) {
            return std::find(list.begin(), list.end(), id) != list.end();
        };
        std::vector<FeatureId> ordered;
        for (const auto& id : picked_) {
            if (has(now, id) && !has(dropped, id)) ordered.push_back(id);
        }
        for (const auto& id : now) {
            if (!has(ordered, id) && !has(dropped, id)) ordered.push_back(id);
        }
        picked_ = ordered;
        ctx_.selection().set(ctx_.documents().active(), ordered);
        showSelection();  // folder rows follow their steps
    }

    void renamed(QTreeWidgetItem* item) {
        if (filling_) return;
        if (const FolderId folder = folderIdOf(item); !folder.isNil()) {
            const auto* design = ctx_.documents().find(ctx_.documents().active());
            const auto* f = design ? design->findFolder(folder) : nullptr;
            const auto name = item->text(0).trimmed().toStdString();
            if (!f || name == f->name) return;
            auto problems = ctx_.documents().run(ctx_.documents().active(), "folder.rename",
                                                 {{"folder", folder}, {"name", name}});
            if (!problems.empty()) {
                ui_.showProblems(problems);
                scheduleRefill();  // put the old name back
            }
            return;
        }
        const auto id = idOf(item);
        const auto* design = ctx_.documents().find(ctx_.documents().active());
        const auto* f = design ? design->find(id) : nullptr;
        const auto name = item->text(0).trimmed().toStdString();
        if (!f || name == f->name()) return;
        auto problems = ctx_.documents().run(ctx_.documents().active(), "feature.rename",
                                             {{"feature", id}, {"name", name}});
        if (!problems.empty()) {
            ui_.showProblems(problems);
            scheduleRefill();  // put the old name back
        }
    }

    void menu(const QPoint& at) {
        auto* item = tree_->itemAt(at);
        if (!item) return;
        const DocId doc = ctx_.documents().active();
        const auto* design = ctx_.documents().find(doc);
        if (!design) return;
        QMenu m(this);
        auto run = [this, doc](const char* command, model::Args args) {
            auto problems = ctx_.documents().run(doc, command, args);
            if (!problems.empty()) ui_.showProblems(problems);
        };
        auto addTurn = [&](const std::vector<FeatureId>& steps) {
            // Off if any is on; on if they're all off.
            const bool off = std::any_of(steps.begin(), steps.end(), [&](const auto& id) {
                const auto* f = design->find(id);
                return f && !f->suppressed();
            });
            const QString what = steps.size() == 1 ? QString() : tr(" %1 steps").arg(steps.size());
            m.addAction(off ? tr("Turn off%1").arg(what) : tr("Turn on%1").arg(what), this,
                        [run, steps, off] { run("feature.suppress", {{"steps", steps}, {"off", off}}); });
        };

        if (const FolderId folder = folderIdOf(item); !folder.isNil()) {
            m.addAction(tr("Rename folder"), this, [this, item] { tree_->editItem(item, 0); });
            addTurn(stepsOf(item));
            m.addAction(tr("Remove folder (keep its steps)"), this,
                        [run, folder] { run("folder.delete", {{"folder", folder}}); });
            m.exec(tree_->viewport()->mapToGlobal(at));
            return;
        }

        // The picked steps if this row is one of them, otherwise just this row.
        const auto clicked = idOf(item);
        std::vector<FeatureId> steps = picked_;
        if (std::find(steps.begin(), steps.end(), clicked) == steps.end()) steps = {clicked};
        m.addAction(tr("Rename"), this, [this, item] { tree_->editItem(item, 0); });
        addTurn(steps);
        if (design->folderOf(clicked).isNil()) {
            m.addAction(steps.size() == 1 ? tr("New folder") : tr("New folder of %1 steps").arg(steps.size()),
                        this, [run, steps] { run("folder.add", {{"steps", steps}}); });
        }
        m.addAction(tr("Delete step"), this,
                    [run, clicked] { run("feature.delete", {{"feature", clicked}}); });
        m.exec(tree_->viewport()->mapToGlobal(at));
    }

    plugin::PluginContext& ctx_;
    ui::UiRegistry& ui_;
    QTreeWidget* tree_ = nullptr;
    std::vector<FeatureId> picked_;
    std::set<FolderId> pickedFolders_;  // folder rows shown picked
    std::set<FolderId> collapsed_;      // folders the student closed
    bool filling_ = false;
    bool refillPending_ = false;
    std::vector<plugin::Subscription> subs_;
};

void registerFeatureTree(plugin::PluginContext& ctx) {
    auto* ui = ctx.service<ui::UiRegistry>();
    if (!ui) {
        ctx.log("No desktop app here, so the feature tree panel isn't shown.");
        return;
    }
    ui->addPanel({"kh.featuretree.panel", QObject::tr("Feature tree"), ui::PanelArea::Left,
                  [&ctx, ui] { return new FeatureTree(ctx, *ui); }});
}

}  // namespace

plugin::BuiltinPlugin featureTreePlugin() {
    return {kManifest, &registerFeatureTree};
}

}  // namespace kh::plugins
