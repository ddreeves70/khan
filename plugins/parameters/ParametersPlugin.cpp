#include "kh/plugins/Parameters.h"

#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "kh/model/Document.h"
#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.parameters",
  "name": "Parameters",
  "version": "0.1.0",
  "description": "The Parameters panel: the design's named values, like wall = 3 mm, that dimensions and steps can use.",
  "requires": ["kh.modeling"],
  "tier": "ui"
})";

constexpr const char* kPanelId = "kh.parameters.panel";

// The table's columns.
enum Column { kName = 0, kValue = 1, kIs = 2, kNote = 3 };

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

// The panel: one row per parameter, edited in place, with Add and Delete below.
class ParametersPanel final : public QWidget {
public:
    ParametersPanel(plugin::PluginContext& ctx, ui::UiRegistry& ui) : ctx_(ctx), ui_(ui) {
        setObjectName(QStringLiteral("kh.parameters.widget"));
        setMinimumWidth(320);
        auto* about =
            new QLabel(tr("Named values, like wall = 3 mm. Sketch dimensions and steps can use them in "
                          "formulas, like wall * 2. Double-click a cell to change it."),
                       this);
        about->setWordWrap(true);
        about->setContentsMargins(4, 2, 4, 2);
        table_ = new QTableWidget(0, 4, this);
        table_->setObjectName(QStringLiteral("kh.parameters.table"));
        table_->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Is"), tr("Note")});
        table_->verticalHeader()->hide();
        // As tall as its rows, so floating over the view it covers no more than it needs.
        table_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                                QAbstractItemView::AnyKeyPressed);
        auto* header = table_->horizontalHeader();
        header->setSectionResizeMode(kName, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(kValue, QHeaderView::Stretch);
        header->setSectionResizeMode(kIs, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(kNote, QHeaderView::Stretch);
        table_->horizontalHeaderItem(kValue)->setToolTip(
            tr("A number with its unit (3 mm, 1.5 in, 30 deg), or a formula that uses other parameters "
               "(wall * 2 + 1 mm)."));
        table_->horizontalHeaderItem(kIs)->setToolTip(
            tr("What the value works out to, in the design's unit."));

        add_ = new QPushButton(tr("Add"), this);
        add_->setObjectName(QStringLiteral("kh.parameters.add"));
        add_->setToolTip(tr("Add a parameter. Then type its name and value."));
        remove_ = new QPushButton(tr("Delete"), this);
        remove_->setObjectName(QStringLiteral("kh.parameters.delete"));
        remove_->setToolTip(tr("Delete the picked parameters. One that something uses can't be deleted."));
        auto* buttons = new QHBoxLayout;
        buttons->addWidget(add_);
        buttons->addWidget(remove_);
        buttons->addStretch(1);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 4);
        layout->addWidget(about);
        layout->addWidget(table_, 1);
        layout->addLayout(buttons);

        connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) { edited(item); });
        connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { showButtons(); });
        connect(add_, &QPushButton::clicked, this, [this] { addOne(); });
        connect(remove_, &QPushButton::clicked, this, [this] { deletePicked(); });

        using namespace plugin;
        auto later = [this](const auto&) { scheduleFill(); };
        subs_.push_back(ctx_.events().subscribe<DocumentChanged>(later));
        subs_.push_back(ctx_.events().subscribe<ActiveDocumentChanged>(later));
        subs_.push_back(ctx_.events().subscribe<DocumentClosed>(later));
        fill();
    }

private:
    const model::Document* design() const { return ctx_.documents().find(ctx_.documents().active()); }

    void scheduleFill() {
        if (fillPending_) return;
        fillPending_ = true;
        // After the change is done (an edit in a cell may be what changed it).
        QMetaObject::invokeMethod(this, [this] { fill(); }, Qt::QueuedConnection);
    }

    // One row per parameter: name, value as typed, what it works out to, note.
    void fill() {
        fillPending_ = false;
        filling_ = true;
        const auto* doc = design();
        const int keepRow = table_->currentRow();
        table_->setRowCount(0);
        if (doc) {
            const auto& list = doc->parameters().parameters();
            const auto values = doc->parameters().evaluate();
            for (const auto& p : list) {
                const int row = table_->rowCount();
                table_->insertRow(row);
                auto* name = new QTableWidgetItem(qs(p.name));
                name->setData(Qt::UserRole, qs(p.name));  // its name before an edit
                auto* value = new QTableWidgetItem(qs(p.expression));
                auto* is = new QTableWidgetItem;
                is->setFlags(is->flags() & ~Qt::ItemIsEditable);
                const auto* v = values.find(p.name);
                if (v && v->ok()) {
                    is->setText(qs(model::quantityText(*v->value, doc->lengthUnit())));
                } else {
                    // Only a design opened with a problem gets here: edits that cause one are refused.
                    is->setText(tr("problem"));
                    is->setForeground(QColor(207, 34, 46));
                    if (v && !v->issues.empty()) {
                        is->setToolTip(qs(v->issues.front().message));
                        value->setToolTip(qs(v->issues.front().message));
                    }
                }
                auto* note = new QTableWidgetItem(qs(p.note));
                note->setToolTip(qs(p.note));  // all of it, when the column is too narrow
                table_->setItem(row, kName, name);
                table_->setItem(row, kValue, value);
                table_->setItem(row, kIs, is);
                table_->setItem(row, kNote, note);
            }
        }
        if (keepRow >= 0 && keepRow < table_->rowCount()) table_->setCurrentCell(keepRow, kName);
        filling_ = false;
        add_->setEnabled(doc != nullptr);
        showButtons();
        table_->updateGeometry();
        updateGeometry();
    }

    void showButtons() { remove_->setEnabled(!pickedNames().empty()); }

    std::vector<std::string> pickedNames() const {
        std::set<int> rows;
        for (const auto* item : table_->selectedItems()) rows.insert(item->row());
        std::vector<std::string> out;
        for (int row : rows) {
            if (const auto* name = table_->item(row, kName))
                out.push_back(name->data(Qt::UserRole).toString().toStdString());
        }
        return out;
    }

    // Runs a command on the active design; a refusal is shown, and the table goes back.
    bool run(std::string_view command, const model::Args& args) {
        const auto problems = ctx_.documents().run(ctx_.documents().active(), command, args);
        if (problems.empty()) return true;
        ui_.showProblems(problems);
        scheduleFill();
        return false;
    }

    void edited(QTableWidgetItem* item) {
        if (filling_ || !item) return;
        const int row = item->row();
        const auto* nameItem = table_->item(row, kName);
        if (!nameItem) return;
        const std::string name = nameItem->data(Qt::UserRole).toString().toStdString();
        const std::string text = item->text().trimmed().toStdString();
        switch (item->column()) {
            case kName:
                if (text != name) run("parameter.rename", {{"name", name}, {"to", text}});
                break;
            case kValue:
                run("parameter.set", {{"name", name}, {"value", text}});
                break;
            case kNote: {
                const auto* value = table_->item(row, kValue);
                run("parameter.set", {{"name", name},
                                      {"value", value ? value->text().toStdString() : std::string()},
                                      {"note", text}});
                break;
            }
            default:
                break;
        }
    }

    // Adds "size1" (or the next free name) as 10 mm, and starts editing its name.
    void addOne() {
        const auto* doc = design();
        if (!doc) return;
        std::string name;
        for (int i = 1; name.empty() || doc->parameters().findIgnoringCase(name); ++i)
            name = "size" + std::to_string(i);
        if (!run("parameter.set", {{"name", name}, {"value", std::string("10 mm")}})) return;
        fill();
        const int row = table_->rowCount() - 1;
        table_->setCurrentCell(row, kName);
        table_->editItem(table_->item(row, kName));
    }

    void deletePicked() {
        for (const auto& name : pickedNames()) {
            if (!run("parameter.delete", {{"name", name}})) break;
        }
    }

    plugin::PluginContext& ctx_;
    ui::UiRegistry& ui_;
    QTableWidget* table_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    bool filling_ = false;
    bool fillPending_ = false;
    std::vector<plugin::Subscription> subs_;
};

void registerParameters(plugin::PluginContext& ctx) {
    auto* ui = ctx.service<ui::UiRegistry>();
    if (!ui) {
        ctx.log("No desktop app here, so the Parameters panel isn't shown.");
        return;
    }
    ui::PanelSpec spec{kPanelId, QObject::tr("Parameters"), ui::PanelArea::Left,
                       [&ctx, ui] { return new ParametersPanel(ctx, *ui); }};
    spec.startHidden = true;     // Modify > Parameters opens it
    spec.floatsOverView = true;  // see-through over the view, under the feature tree, like the other panels
    ui->addPanel(std::move(spec));
    ctx.addAction(plugin::ActionSpec{.id = "kh.parameters.show",
                                     .text = "Parameters",
                                     .menu = "Modify",
                                     .tip = "Open the Parameters panel: named values, like wall = 3 mm, that "
                                            "dimensions and steps can use.",
                                     .run = [ui] { ui->showPanel(kPanelId); }});
}

}  // namespace

plugin::BuiltinPlugin parametersPlugin() {
    return {kManifest, &registerParameters};
}

}  // namespace kh::plugins
