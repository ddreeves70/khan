#include "kh/plugins/FeatureTree.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
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
        }
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

    // Rebuilds the rows from the active design and its latest rebuild.
    void refill() {
        refillPending_ = false;
        filling_ = true;
        tree_->clear();
        const DocId doc = ctx_.documents().active();
        const auto* design = ctx_.documents().find(doc);
        const auto* report = ctx_.documents().lastRebuild(doc);
        if (design) {
            for (std::size_t i = 0; i < design->featureCount(); ++i) {
                const auto& f = design->featureAt(i);
                auto* item = new QTreeWidgetItem(tree_);
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
                item->setText(2, status);
                if (status != tr("ok")) {
                    const QColor c = status == tr("off") ? palette().color(QPalette::Disabled, QPalette::Text)
                                                         : QColor(196, 43, 28);
                    for (int col = 0; col < 3; ++col) item->setForeground(col, c);
                }
                if (!tip.isEmpty()) {
                    for (int col = 0; col < 3; ++col) item->setToolTip(col, tip.trimmed());
                }
            }
        }
        filling_ = false;
        showSelection();
    }

    // Shows the shared selection in the rows, without sending it back.
    void showSelection() {
        filling_ = true;
        const auto& sel = ctx_.selection();
        const bool here = sel.doc() == ctx_.documents().active();
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
            auto* item = tree_->topLevelItem(i);
            item->setSelected(here && sel.contains(idOf(item)));
        }
        picked_ = here ? sel.features() : std::vector<FeatureId>{};
        filling_ = false;
    }

    // The student picked rows: keep the order they were picked in.
    void pickedRows() {
        if (filling_) return;
        std::vector<FeatureId> now;
        for (const auto* item : tree_->selectedItems()) now.push_back(idOf(item));
        std::vector<FeatureId> ordered;
        for (const auto& id : picked_) {
            if (std::find(now.begin(), now.end(), id) != now.end()) ordered.push_back(id);
        }
        for (const auto& id : now) {
            if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) ordered.push_back(id);
        }
        picked_ = ordered;
        ctx_.selection().set(ctx_.documents().active(), ordered);
    }

    void renamed(QTreeWidgetItem* item) {
        if (filling_) return;
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
        QMenu m(this);
        m.addAction(tr("Rename"), this, [this, item] { tree_->editItem(item, 0); });
        m.addAction(tr("Delete step"), this, [this, item] {
            auto problems =
                ctx_.documents().run(ctx_.documents().active(), "feature.delete", {{"feature", idOf(item)}});
            if (!problems.empty()) ui_.showProblems(problems);
        });
        m.exec(tree_->viewport()->mapToGlobal(at));
    }

    plugin::PluginContext& ctx_;
    ui::UiRegistry& ui_;
    QTreeWidget* tree_ = nullptr;
    std::vector<FeatureId> picked_;
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
