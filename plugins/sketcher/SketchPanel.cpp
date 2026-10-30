#include "SketchPanel.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Constrain.h"
#include "kh/model/Feature.h"
#include "kh/sketch/Profiles.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins::sketcher {

namespace {

constexpr double kDegrees = std::numbers::pi / 180;

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

const model::SketchFeature* sketchStep(plugin::PluginContext& ctx, const std::optional<FeatureId>& id) {
    if (!id) return nullptr;
    const auto* doc = ctx.documents().find(ctx.documents().active());
    const auto* f = doc ? doc->find(*id) : nullptr;
    return f && f->type() == model::FeatureType::Sketch ? static_cast<const model::SketchFeature*>(f)
                                                        : nullptr;
}

const sketch::SolveResult* lastResult(plugin::PluginContext& ctx, const model::SketchFeature& step) {
    const auto* report = ctx.documents().lastRebuild(ctx.documents().active());
    const auto* r = report ? report->result(step.id()) : nullptr;
    return r && r->sketch ? &*r->sketch : nullptr;
}

// What a constraint is on, in words: "Line1", "Line1's end and Line2's start".
std::string onWhat(const sketch::Sketch& s, const sketch::Constraint& c) {
    std::string out;
    auto add = [&](const std::string& t) { out += (out.empty() ? "" : ", ") + t; };
    for (const auto& p : c.points) {
        const auto* e = s.find(p.entity);
        if (!e) continue;
        const std::string role = p.role == sketch::PointRole::Start    ? "'s start"
                                 : p.role == sketch::PointRole::End    ? "'s end"
                                 : p.role == sketch::PointRole::Center ? "'s center"
                                 : p.role == sketch::PointRole::Pole
                                     ? "'s control point " + std::to_string(p.index + 1)
                                     : std::string();
        add(e->name + role);
    }
    for (const auto& id : c.entities) {
        if (const auto* e = s.find(id)) add(e->name);
    }
    return out;
}

// A number box that shows "40 mm", not "40.000 mm", but keeps 3 decimals for typing.
class NumberBox final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    QString textFromValue(double value) const override {
        QString text = locale().toString(value, 'f', decimals());
        const QString point = locale().decimalPoint();
        if (text.contains(point)) {
            while (text.endsWith(QLatin1Char('0'))) text.chop(1);
            if (text.endsWith(point)) text.chop(point.size());
        }
        return text;
    }
};

// "1 hole", "2 holes".
QString count(std::size_t n, const QString& one, const QString& many) {
    return QString::number(n) + QLatin1Char(' ') + (n == 1 ? one : many);
}

}  // namespace

SketchPanel::SketchPanel(plugin::PluginContext& ctx, PanelHooks hooks, QWidget* parent)
    : QWidget(parent), ctx_(ctx), hooks_(std::move(hooks)) {
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("constraints"));
    table_->setHorizontalHeaderLabels({tr("Name"), tr("On"), tr("Number")});
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        const int row = table_->currentRow();
        if (row >= 0 && row < static_cast<int>(rows_.size()) && hooks_.pick) hooks_.pick(rows_[row]);
    });
    refresh();
}

void SketchPanel::refresh() {
    const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
    if (!step) {
        status_->setText(tr("Edit a sketch to see its constraints and dimensions here."));
        status_->setStyleSheet({});
        if (!shown_.empty()) {
            shown_.clear();
            rows_.clear();
            table_->setRowCount(0);
        }
        return;
    }
    // How free it is, or what's wrong.
    const auto* last = lastResult(ctx_, *step);
    if (last && last->status == sketch::SolveStatus::Conflicting && !last->issues.empty()) {
        status_->setText(qs(last->issues.front().message));
        status_->setStyleSheet(QStringLiteral("color: #cf222e;"));
    } else if (last && last->ok()) {
        status_->setText(last->freedom == 0
                             ? tr("%1 is fully constrained.").arg(qs(step->name()))
                             : tr("%1 can still move %2: blue parts are free.")
                                   .arg(qs(step->name()), count(static_cast<std::size_t>(last->freedom),
                                                                tr("way"), tr("ways"))));
        status_->setStyleSheet({});
    } else {
        status_->setText(last && !last->issues.empty() ? qs(last->issues.front().message)
                                                       : tr("%1 didn't solve.").arg(qs(step->name())));
        status_->setStyleSheet(QStringLiteral("color: #cf222e;"));
    }
    // Closed shapes, which extrude will turn into solids, and what stops others closing.
    if (last && last->ok() && !last->entities.empty()) {
        const auto found = sketch::findProfiles({last->entities, {}});
        std::size_t holes = 0;
        for (const auto& p : found.shapes) holes += p.holes.size();
        QString shapes = found.shapes.empty()
                             ? tr("No closed shapes yet.")
                             : count(found.shapes.size(), tr("closed shape"), tr("closed shapes")) +
                                   (holes ? tr(", with %1.").arg(count(holes, tr("hole"), tr("holes")))
                                          : QStringLiteral("."));
        if (!found.openEnds.empty())
            shapes += QLatin1Char(' ') +
                      (found.openEnds.size() == 1
                           ? tr("1 end isn't joined (marked in red).")
                           : tr("%1 ends aren't joined (marked in red).").arg(found.openEnds.size()));
        for (const auto& d : found.issues) shapes += QLatin1Char(' ') + qs(d.message);
        status_->setText(status_->text() + QLatin1Char('\n') + shapes);
    }
    // The list, made again only if the constraints or their numbers changed.
    std::string now = step->id().toString();
    for (const auto& c : step->sketch().constraints) {
        now += "|" + c.name + ":" + std::to_string(c.value);
        if (last && std::ranges::find(last->conflicting, c.id) != last->conflicting.end()) now += "!";
        if (last && std::ranges::find(last->redundant, c.id) != last->redundant.end()) now += "?";
    }
    if (now != shown_) {
        shown_ = now;
        rebuild();
    }
    // Rows of picked constraints are selected.
    updating_ = true;
    table_->clearSelection();
    const auto picked = hooks_.picked ? hooks_.picked() : std::vector<ConstraintId>{};
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (std::ranges::find(picked, rows_[i]) != picked.end()) table_->selectRow(static_cast<int>(i));
    }
    updating_ = false;
    // A new dimension: the cursor goes in its number.
    if (const auto id = hooks_.takeJustAdded ? hooks_.takeJustAdded() : ConstraintId{}; !id.isNil()) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i] != id) continue;
            if (auto* box = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(static_cast<int>(i), 2))) {
                updating_ = true;  // moving the cursor there isn't a click on the row
                table_->scrollToItem(table_->item(static_cast<int>(i), 0));
                box->setFocus(Qt::OtherFocusReason);
                box->selectAll();
                box->setProperty("justAdded", true);
                updating_ = false;
            }
        }
    }
}

void SketchPanel::rebuild() {
    const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
    if (!step) return;
    const auto* last = lastResult(ctx_, *step);
    const auto& s = step->sketch();
    const auto* design = ctx_.documents().find(ctx_.documents().active());
    const auto unit = design ? design->lengthUnit() : LengthUnit::Millimeter;  // lengths are shown in it
    updating_ = true;
    table_->setRowCount(0);
    rows_.clear();
    for (const auto& c : s.constraints) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        rows_.push_back(c.id);
        const bool conflict = last && std::ranges::find(last->conflicting, c.id) != last->conflicting.end();
        const bool repeat = last && std::ranges::find(last->redundant, c.id) != last->redundant.end();
        auto* name = new QTableWidgetItem(qs(c.name));
        auto* on = new QTableWidgetItem(qs(onWhat(s, c)));
        QString tip = qs(std::string(sketch::kindName(c.kind)));
        if (conflict) tip += tr(": can't be true with the others in red. Delete one, or change a number.");
        if (repeat) tip += tr(": says what other constraints already say. You can delete it.");
        for (auto* item : {name, on}) {
            item->setToolTip(tip);
            if (conflict) item->setForeground(QColor(207, 34, 46));
            if (repeat) item->setForeground(QColor(176, 120, 0));
        }
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, on);
        if (!sketch::isDimension(c.kind)) {
            table_->setItem(row, 2, new QTableWidgetItem(qs(labelText(c, unit))));
            continue;
        }
        // A dimension: its number, ready to change. Enter (or clicking away) sets it.
        auto* box = new NumberBox(table_);
        box->setObjectName(qs(c.name));
        box->setKeyboardTracking(false);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setDecimals(3);
        const bool angle = c.kind == sketch::ConstraintKind::Angle;
        const bool signedValue = angle || c.kind == sketch::ConstraintKind::HorizontalDistance ||
                                 c.kind == sketch::ConstraintKind::VerticalDistance;
        box->setRange(signedValue ? -100000 : 0, 100000);
        box->setSuffix(angle ? QStringLiteral("°") : QLatin1Char(' ') + qs(std::string(unitSymbol(unit))));
        box->setValue(angle ? c.value / kDegrees : toUnit(c.value, unit));
        box->setToolTip(tip);
        const std::string constraint = c.name;
        connect(box, &QDoubleSpinBox::editingFinished, this, [this, box, constraint] {
            if (updating_) return;
            // Change the sketch just after this returns: the rebuild that follows makes
            // the list again, which deletes this box, and it must not be deleted while
            // it's still telling us about Enter.
            const double value = box->value();
            QMetaObject::invokeMethod(
                this, [this, constraint, value] { setValue(constraint, value); }, Qt::QueuedConnection);
        });
        table_->setCellWidget(row, 2, box);
    }
    table_->resizeColumnToContents(0);
    updating_ = false;
}

void SketchPanel::setValue(const std::string& name, double value) {
    const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
    if (!step) return;
    const auto* c = step->sketch().findConstraint(name);
    if (!c) return;
    // The box shows lengths in the design's unit; the command takes mm (and degrees).
    const bool angle = c->kind == sketch::ConstraintKind::Angle;
    const auto* design = ctx_.documents().find(ctx_.documents().active());
    const auto unit = design ? design->lengthUnit() : LengthUnit::Millimeter;
    const double now = angle ? c->value / kDegrees : toUnit(c->value, unit);
    if (std::abs(now - value) < 1e-9) return;  // nothing changed
    if (!angle) value = fromUnit(value, unit);
    const auto problems =
        ctx_.documents().run(ctx_.documents().active(), "sketch.set_value",
                             {{"sketch", step->id()}, {"constraint", name}, {"value", value}});
    if (!problems.empty()) {
        if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showProblems(problems);
    }
}

}  // namespace kh::plugins::sketcher
