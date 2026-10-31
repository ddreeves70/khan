#include "SketchPanel.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
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

    // A picked spline's settings: shown only while one is picked.
    splineBox_ = new QWidget(this);
    splineBox_->setObjectName(QStringLiteral("spline"));
    auto* form = new QFormLayout(splineBox_);
    form->setContentsMargins(0, 4, 0, 4);
    splineTitle_ = new QLabel(splineBox_);
    QFont bold = splineTitle_->font();
    bold.setBold(true);
    splineTitle_->setFont(bold);
    form->addRow(splineTitle_);
    degree_ = new QSpinBox(splineBox_);
    degree_->setObjectName(QStringLiteral("spline.degree"));
    degree_->setRange(1, 5);
    degree_->setKeyboardTracking(false);
    degree_->setToolTip(
        tr("How smooth: 1 joins the control points with straight pieces, 3 is the usual. "
           "It needs one more control point than its degree."));
    form->addRow(tr("Degree"), degree_);
    closed_ = new QCheckBox(tr("Closed loop"), splineBox_);
    closed_->setObjectName(QStringLiteral("spline.closed"));
    closed_->setToolTip(
        tr("A smooth loop with no ends, instead of a curve from its first control point to its "
           "last."));
    form->addRow(closed_);
    weightLabel_ = new QLabel(splineBox_);
    weight_ = new NumberBox(splineBox_);
    weight_->setObjectName(QStringLiteral("spline.weight"));
    weight_->setRange(0.01, 100);
    weight_->setDecimals(3);
    weight_->setSingleStep(0.5);
    weight_->setKeyboardTracking(false);
    weight_->setToolTip(
        tr("How hard this control point pulls the curve: 1 is normal, bigger pulls it closer, "
           "smaller lets it go."));
    form->addRow(weightLabel_, weight_);
    auto* buttons = new QHBoxLayout;
    addPole_ = new QPushButton(tr("Add control point"), splineBox_);
    addPole_->setObjectName(QStringLiteral("spline.add_pole"));
    addPole_->setToolTip(
        tr("Add a control point after the picked one (halfway to the next), to shape the curve "
           "more finely."));
    removePole_ = new QPushButton(tr("Remove control point"), splineBox_);
    removePole_->setObjectName(QStringLiteral("spline.remove_pole"));
    removePole_->setToolTip(tr("Remove the picked control point, and the constraints on it."));
    buttons->addWidget(addPole_);
    buttons->addWidget(removePole_);
    form->addRow(buttons);
    // An imported spline: where from, and reading it again.
    auto* fromFile = new QHBoxLayout;
    source_ = new QLabel(splineBox_);
    source_->setObjectName(QStringLiteral("spline.source"));
    reload_ = new QPushButton(tr("Reload from file"), splineBox_);
    reload_->setObjectName(QStringLiteral("spline.reload"));
    reload_->setToolTip(
        tr("Read the file of points again, after you've changed it. Constraints on control points "
           "it doesn't have anymore are removed."));
    fromFile->addWidget(source_, 1);
    fromFile->addWidget(reload_);
    form->addRow(fromFile);
    connect(reload_, &QPushButton::clicked, this, [this] {
        const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
        const auto* e = step && spline_ ? step->sketch().find(spline_->spline) : nullptr;
        if (e) runSpline("sketch.reload_spline", {{"spline", e->name}});
    });
    splineBox_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(splineBox_);
    splineBox_->hide();

    connect(degree_, &QSpinBox::valueChanged, this, [this](int degree) {
        if (updating_ || !spline_) return;
        runSpline("sketch.set_spline", {{"degree", std::int64_t{degree}}});
    });
    connect(closed_, &QCheckBox::toggled, this, [this](bool closed) {
        if (updating_ || !spline_) return;
        runSpline("sketch.set_spline", {{"closed", closed}});
    });
    connect(weight_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (updating_ || !spline_ || !spline_->pole) return;
        const double value = weight_->value();
        const auto pick = *spline_;
        // Just after this returns, as for dimensions: the rebuild redraws the box.
        QMetaObject::invokeMethod(
            this,
            [this, value, pick] {
                const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
                const auto* e = step ? step->sketch().find(pick.spline) : nullptr;
                const auto* sp = e ? std::get_if<sketch::SplineShape>(&e->shape) : nullptr;
                if (!sp || !pick.pole) return;
                const auto i = static_cast<std::size_t>(*pick.pole);
                const double now = sp->weights.empty() ? 1.0 : sp->weights[i];
                if (std::abs(now - value) < 1e-9) return;
                runSpline("sketch.set_weight",
                          {{"point", e->name + ".pole" + std::to_string(*pick.pole + 1)}, {"weight", value}});
            },
            Qt::QueuedConnection);
    });
    connect(addPole_, &QPushButton::clicked, this, [this] {
        const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
        const auto* e = step && spline_ ? step->sketch().find(spline_->spline) : nullptr;
        const auto* sp = e ? std::get_if<sketch::SplineShape>(&e->shape) : nullptr;
        if (!sp) return;
        const int n = static_cast<int>(sp->poles.size());
        // After the picked control point (an open spline's last one: before it); the middle otherwise.
        int after = spline_->pole ? *spline_->pole + 1 : std::max(1, n / 2);
        if (!sp->closed) after = std::min(after, n - 1);
        const auto pick = *spline_;
        if (runSpline("sketch.add_pole", {{"spline", e->name}, {"after", std::int64_t{after}}}) &&
            hooks_.pickSpline)
            hooks_.pickSpline({pick.spline, after});  // the new one is picked, ready to drag
    });
    connect(removePole_, &QPushButton::clicked, this, [this] {
        const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
        const auto* e = step && spline_ ? step->sketch().find(spline_->spline) : nullptr;
        if (!e || !spline_->pole) return;
        const auto pick = *spline_;
        if (runSpline("sketch.remove_pole",
                      {{"point", e->name + ".pole" + std::to_string(*pick.pole + 1)}}) &&
            hooks_.pickSpline)
            hooks_.pickSpline({pick.spline, std::nullopt});
    });

    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("constraints"));
    table_->setHorizontalHeaderLabels({tr("Name"), tr("On"), tr("Number")});
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // When the panel is short, the list scrolls rather than squashing the boxes above it.
    table_->setMinimumHeight(40);
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
    showSpline(step ? step->sketch() : sketch::Sketch{});
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
        if (!c.expression.empty()) {
            // Its number comes from a formula: say so, and that typing a number replaces it.
            tip += tr("\nWorked out from %1. Change the parameters it uses to change it, or type a number "
                      "to use that instead.")
                       .arg(qs(c.expression));
            box->setProperty("formula", qs(c.expression));
        }
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

void SketchPanel::showSpline(const sketch::Sketch& sketch) {
    spline_ = hooks_.pickedSpline ? hooks_.pickedSpline() : std::nullopt;
    const auto* e = spline_ ? sketch.find(spline_->spline) : nullptr;
    const auto* sp = e ? std::get_if<sketch::SplineShape>(&e->shape) : nullptr;
    if (!sp || (spline_->pole && *spline_->pole >= static_cast<int>(sp->poles.size()))) {
        spline_.reset();
        splineBox_->hide();
        return;
    }
    updating_ = true;
    const int n = static_cast<int>(sp->poles.size());
    splineTitle_->setText(
        spline_->pole ? tr("%1, control point %2 of %3").arg(qs(e->name)).arg(*spline_->pole + 1).arg(n)
                      : tr("%1, with %2 control points").arg(qs(e->name)).arg(n));
    degree_->setValue(sp->degree);
    closed_->setChecked(sp->closed);
    const bool pole = spline_->pole.has_value();
    weightLabel_->setVisible(pole);
    weight_->setVisible(pole);
    if (pole) {
        weightLabel_->setText(tr("Weight"));
        weight_->setValue(sp->weights.empty() ? 1.0 : sp->weights[static_cast<std::size_t>(*spline_->pole)]);
    }
    removePole_->setEnabled(pole && n > sp->degree + 1);
    source_->setVisible(sp->source.has_value());
    reload_->setVisible(sp->source.has_value());
    if (sp->source) {
        const QString path = QString::fromStdString(sp->source->file);
        source_->setText(tr("From %1").arg(QFileInfo(path).fileName()));
        source_->setToolTip(path);
    }
    removePole_->setToolTip(
        pole && n <= sp->degree + 1
            ? tr("%1 has the fewest control points degree %2 allows. Lower its degree first.")
                  .arg(qs(e->name))
                  .arg(sp->degree)
            : tr("Remove the picked control point, and the constraints on it."));
    updating_ = false;
    splineBox_->show();
}

bool SketchPanel::runSpline(const std::string& command, model::Args args) {
    const auto* step = sketchStep(ctx_, hooks_.editing ? hooks_.editing() : std::nullopt);
    if (!step || !spline_) return false;
    const auto* e = step->sketch().find(spline_->spline);
    if (!e) return false;
    args["sketch"] = step->id();
    if (command == "sketch.set_spline") args["spline"] = e->name;
    const auto problems = ctx_.documents().run(ctx_.documents().active(), command, std::move(args));
    if (problems.empty()) return true;
    if (auto* ui = ctx_.service<ui::UiRegistry>()) ui->showProblems(problems);
    refresh();  // put the box back as it was
    return false;
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
