#include "kh/plugins/Properties.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/model/Material.h"
#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.properties",
  "name": "Properties",
  "version": "0.1.0",
  "description": "The panel for changing the selected step's name and numbers.",
  "requires": ["kh.modeling"],
  "tier": "ui"
})";

constexpr qint64 kMergeMs = 800;                 // edits to one number this close together are one undo step
constexpr const char* kNewMaterial = "\x01new";  // the combo item that opens the New material dialog

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

// "0.18", "12.50", "1,234.00": money with two decimals.
QString money(double v) {
    return QStringLiteral("$") + QString::number(v, 'f', 2);
}

// Grams below 1 kg, kilograms above.
QString massText(double grams) {
    return grams < 1000 ? QObject::tr("%1 g").arg(grams, 0, 'f', 2)
                        : QObject::tr("%1 kg").arg(grams / 1000, 0, 'f', 3);
}

// Asks for a new material's numbers. Returns false if cancelled.
bool askNewMaterial(QWidget* parent, model::Material& m) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("New material"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    name->setPlaceholderText(QObject::tr("For example: Blue PETG"));
    auto number = [&dialog](double max, int decimals, const QString& suffix) {
        auto* s = new QDoubleSpinBox(&dialog);
        s->setRange(0, max);
        s->setDecimals(decimals);
        s->setSuffix(suffix);
        s->setSpecialValueText(QObject::tr("unknown"));  // shown for 0
        return s;
    };
    auto* density = number(25, 3, QObject::tr(" g/cm³"));
    density->setValue(1.24);
    auto* youngs = number(1000, 2, QObject::tr(" GPa"));
    auto* poisson = number(0.5, 3, QString());
    auto* yield = number(5000, 0, QObject::tr(" MPa"));
    auto* cost = number(100000, 2, QObject::tr(" per kg"));
    cost->setPrefix(QStringLiteral("$"));
    QColor color(120, 170, 220);
    auto* colorButton = new QPushButton(&dialog);
    auto showColor = [colorButton, &color] {
        colorButton->setText(color.name());
        colorButton->setStyleSheet(QStringLiteral("background: %1").arg(color.name()));
    };
    showColor();
    QObject::connect(colorButton, &QPushButton::clicked, &dialog, [&] {
        const QColor picked = QColorDialog::getColor(color, &dialog, QObject::tr("Material color"));
        if (picked.isValid()) color = picked;
        showColor();
    });
    form->addRow(QObject::tr("Name"), name);
    form->addRow(QObject::tr("Density"), density);
    form->addRow(QObject::tr("Color"), colorButton);
    form->addRow(QObject::tr("Stiffness (Young's modulus)"), youngs);
    form->addRow(QObject::tr("Poisson's ratio"), poisson);
    form->addRow(QObject::tr("Yield strength"), yield);
    form->addRow(QObject::tr("Price"), cost);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    m.name = name->text().trimmed().toStdString();
    m.density = density->value();
    m.color = {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
               static_cast<std::uint8_t>(color.blue())};
    m.youngsModulus = youngs->value();
    m.poissonsRatio = poisson->value();
    m.yieldStrength = yield->value();
    m.costPerKg = cost->value();
    return true;
}

// A copy of the step, changed, ready for EditFeatureCommand.
using Change = std::function<void(model::Feature&)>;

class Properties final : public QWidget {
public:
    Properties(plugin::PluginContext& ctx, ui::UiRegistry& ui) : ctx_(ctx), ui_(ui) {
        setObjectName(QStringLiteral("kh.properties.widget"));
        setMinimumWidth(340);
        auto* outer = new QVBoxLayout(this);
        title_ = new QLabel(this);
        title_->setObjectName(QStringLiteral("kh.properties.title"));
        title_->setWordWrap(true);
        QFont bold = title_->font();
        bold.setBold(true);
        title_->setFont(bold);
        outer->addWidget(title_);
        form_ = new QWidget(this);
        outer->addWidget(form_);
        status_ = new QLabel(this);
        status_->setObjectName(QStringLiteral("kh.properties.status"));
        status_->setWordWrap(true);
        status_->setStyleSheet(QStringLiteral("color: #c42b1c"));
        outer->addWidget(status_);
        outer->addStretch(1);

        using namespace plugin;
        subs_.push_back(ctx_.events().subscribe<SelectionChanged>([this](const auto&) { schedule(); }));
        subs_.push_back(ctx_.events().subscribe<ActiveDocumentChanged>([this](const auto&) { schedule(); }));
        subs_.push_back(ctx_.events().subscribe<DocumentChanged>([this](const auto&) { schedule(); }));
        subs_.push_back(ctx_.events().subscribe<RebuildFinished>([this](const auto&) { schedule(); }));
        refresh();
    }

private:
    const model::Feature* selected() const {
        const auto& sel = ctx_.selection();
        if (sel.features().size() != 1) return nullptr;
        const auto* design = ctx_.documents().find(sel.doc());
        return design ? design->find(sel.features()[0]) : nullptr;
    }

    void schedule() {
        if (pending_) return;
        pending_ = true;
        QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    }

    // Shows the selected step. Rebuilds the form only when a different step is
    // picked, so the box being typed in keeps its focus.
    void refresh() {
        pending_ = false;
        const auto* f = selected();
        const auto count = ctx_.selection().features().size();
        if (ctx_.selection().designPicked()) {
            showDesign();
            return;
        }
        if (!f) {
            shown_ = FeatureId{};
            clearForm();
            title_->setText(count > 1 ? tr("%1 steps selected").arg(count)
                                      : tr("Pick a step in the feature tree to see its numbers."));
            status_->clear();
            return;
        }
        if (f->id() != shown_ || unitNow() != unit_) {
            shown_ = f->id();
            unit_ = unitNow();  // the numbers are shown in the design's unit
            buildForm(*f);
        }
        filling_ = true;
        for (auto& u : updaters_) u(*f);
        filling_ = false;
        title_->setText(QString::fromStdString(f->name()));

        QString problems;
        if (const auto* report = ctx_.documents().lastRebuild(ctx_.selection().doc())) {
            if (const auto* r = report->result(f->id())) {
                for (const auto& d : r->issues)
                    problems += QString::fromStdString(d.message) + QLatin1Char('\n');
            }
        }
        status_->setText(problems.trimmed());
    }

    // The design's own settings (the feature tree's Document settings row).
    void showDesign() {
        const auto* design = ctx_.documents().find(ctx_.selection().doc());
        shown_ = FeatureId{};
        clearForm();
        status_->clear();
        title_->setText(tr("Document settings"));
        if (!design) return;
        auto* form = new QFormLayout(form_);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* name = new QLabel(QString::fromStdString(design->name()), form_);
        name->setObjectName(QStringLiteral("design"));
        form->addRow(tr("Design"), name);
        const auto count = static_cast<int>(design->featureCount());
        auto* steps = new QLabel(count == 1 ? tr("1 step") : tr("%1 steps").arg(count), form_);
        steps->setObjectName(QStringLiteral("steps"));
        form->addRow(tr("Steps"), steps);
        auto* units = new QComboBox(form_);
        units->setObjectName(QStringLiteral("units"));
        for (auto u : allLengthUnits()) {
            units->addItem(tr("%1 (%2)").arg(qs(std::string(unitName(u))), qs(std::string(unitSymbol(u)))),
                           qs(std::string(unitSymbol(u))));
        }
        units->setCurrentIndex(units->findData(qs(std::string(unitSymbol(design->lengthUnit())))));
        units->setToolTip(
            tr("The unit for every length you see and type in this design. Changing it "
               "doesn't change anything's size."));
        form->addRow(tr("Lengths"), units);
        connect(units, &QComboBox::activated, this, [this, units](int index) {
            const auto symbol = units->itemData(index).toString().toStdString();
            const auto doc = ctx_.selection().doc();
            const auto* d = ctx_.documents().find(doc);
            if (!d || symbol == unitSymbol(d->lengthUnit())) return;
            auto problems = ctx_.documents().run(doc, "document.set_units", {{"unit", symbol}});
            if (!problems.empty()) ui_.showProblems(problems);
        });
        auto* angles = new QLabel(tr("Degrees"), form_);
        angles->setObjectName(QStringLiteral("angles"));
        form->addRow(tr("Angles"), angles);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == wrapped_ && event->type() == QEvent::Resize) {
            const int needs = wrapped_->heightForWidth(wrapped_->width());
            if (needs > 0 && needs != wrapped_->minimumHeight()) wrapped_->setMinimumHeight(needs);
        }
        return QWidget::eventFilter(watched, event);
    }

    void clearForm() {
        wrapped_ = nullptr;
        updaters_.clear();
        delete form_->layout();
        for (auto* child : form_->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) delete child;
    }

    void buildForm(const model::Feature& f) {
        clearForm();
        auto* form = new QFormLayout(form_);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        auto* name = new QLineEdit(form_);
        name->setObjectName(QStringLiteral("name"));
        form->addRow(tr("Name"), name);
        connect(name, &QLineEdit::editingFinished, this, [this, name] { rename(name->text()); });
        updaters_.push_back([name](const model::Feature& g) {
            if (!name->hasFocus()) name->setText(QString::fromStdString(g.name()));
        });

        switch (f.type()) {
            case model::FeatureType::Box:
                addVec3(
                    form, QStringLiteral("corner"), tr("Corner"), false,
                    [](const model::Feature& g) { return static_cast<const model::BoxFeature&>(g).corner(); },
                    [](model::Feature& g, kernel::Vec3 v) {
                        static_cast<model::BoxFeature&>(g).setCorner(v);
                    });
                addVec3(
                    form, QStringLiteral("size"), tr("Size"), true,
                    [](const model::Feature& g) { return static_cast<const model::BoxFeature&>(g).size(); },
                    [](model::Feature& g, kernel::Vec3 v) { static_cast<model::BoxFeature&>(g).setSize(v); });
                break;
            case model::FeatureType::Cylinder:
                addNumber(
                    form, QStringLiteral("radius"), tr("Radius"),
                    [](const model::Feature& g) {
                        return static_cast<const model::CylinderFeature&>(g).radius();
                    },
                    [](model::Feature& g, double v) {
                        static_cast<model::CylinderFeature&>(g).setRadius(v);
                    });
                addNumber(
                    form, QStringLiteral("height"), tr("Height"),
                    [](const model::Feature& g) {
                        return static_cast<const model::CylinderFeature&>(g).height();
                    },
                    [](model::Feature& g, double v) {
                        static_cast<model::CylinderFeature&>(g).setHeight(v);
                    });
                addVec3(
                    form, QStringLiteral("base"), tr("Base"), false,
                    [](const model::Feature& g) {
                        return static_cast<const model::CylinderFeature&>(g).base();
                    },
                    [](model::Feature& g, kernel::Vec3 v) {
                        static_cast<model::CylinderFeature&>(g).setBase(v);
                    });
                addVec3(
                    form, QStringLiteral("axis"), tr("Axis"), false,
                    [](const model::Feature& g) {
                        return static_cast<const model::CylinderFeature&>(g).axis();
                    },
                    [](model::Feature& g, kernel::Vec3 v) {
                        static_cast<model::CylinderFeature&>(g).setAxis(v);
                    },
                    false);
                break;
            case model::FeatureType::Boolean: {
                auto* op = new QComboBox(form_);
                op->setObjectName(QStringLiteral("operation"));
                op->addItem(tr("Union: join them"), static_cast<int>(kernel::BoolOp::Union));
                op->addItem(tr("Cut: A minus B"), static_cast<int>(kernel::BoolOp::Cut));
                op->addItem(tr("Intersect: keep the overlap"), static_cast<int>(kernel::BoolOp::Intersect));
                form->addRow(tr("Operation"), op);
                connect(op, &QComboBox::currentIndexChanged, this, [this, op](int) {
                    const auto value = static_cast<kernel::BoolOp>(op->currentData().toInt());
                    edit(QStringLiteral("operation"),
                         [value](model::Feature& g) { static_cast<model::BooleanFeature&>(g).setOp(value); });
                });
                updaters_.push_back([op](const model::Feature& g) {
                    op->setCurrentIndex(
                        op->findData(static_cast<int>(static_cast<const model::BooleanFeature&>(g).op())));
                });
                auto* inputs = new QLabel(form_);
                inputs->setObjectName(QStringLiteral("inputs"));
                form->addRow(tr("Bodies"), inputs);
                updaters_.push_back([this, inputs](const model::Feature& g) {
                    const auto& b = static_cast<const model::BooleanFeature&>(g);
                    const auto* design = ctx_.documents().find(ctx_.selection().doc());
                    auto nameOf = [design](const FeatureId& id) {
                        const auto* s = design ? design->find(id) : nullptr;
                        return s ? QString::fromStdString(s->name()) : tr("(missing)");
                    };
                    inputs->setText(tr("A: %1, B: %2").arg(nameOf(b.a()), nameOf(b.b())));
                });
                break;
            }
            case model::FeatureType::Sketch: {
                auto* plane = new QLabel(form_);
                plane->setObjectName(QStringLiteral("plane"));
                form->addRow(tr("Plane"), plane);
                auto* drawn = new QLabel(form_);
                drawn->setObjectName(QStringLiteral("drawn"));
                drawn->setWordWrap(true);
                form->addRow(tr("Drawn"), drawn);
                auto* constraints = new QLabel(form_);
                constraints->setObjectName(QStringLiteral("constraints"));
                form->addRow(tr("Constraints"), constraints);
                auto* freedom = new QLabel(form_);
                freedom->setObjectName(QStringLiteral("freedom"));
                freedom->setWordWrap(true);
                freedom->setToolTip(
                    tr("How many ways the drawing can still move or change size. Add "
                       "constraints and dimensions until it's 0: fully constrained."));
                form->addRow(tr("Free to move"), freedom);
                updaters_.push_back([this, plane, drawn, constraints, freedom](const model::Feature& g) {
                    const auto& sk = static_cast<const model::SketchFeature&>(g);
                    switch (sk.plane()) {
                        case model::SketchPlane::XY:
                            plane->setText(tr("XY (the ground, seen from the top)"));
                            break;
                        case model::SketchPlane::XZ:
                            plane->setText(tr("XZ (standing, seen from the front)"));
                            break;
                        case model::SketchPlane::YZ:
                            plane->setText(tr("YZ (standing, seen from the right)"));
                            break;
                    }
                    int points = 0, lines = 0, circles = 0, arcs = 0, splines = 0;
                    for (const auto& e : sk.sketch().entities) {
                        points += std::holds_alternative<sketch::PointShape>(e.shape) ? 1 : 0;
                        lines += std::holds_alternative<sketch::LineShape>(e.shape) ? 1 : 0;
                        circles += std::holds_alternative<sketch::CircleShape>(e.shape) ? 1 : 0;
                        arcs += std::holds_alternative<sketch::ArcShape>(e.shape) ? 1 : 0;
                        splines += std::holds_alternative<sketch::SplineShape>(e.shape) ? 1 : 0;
                    }
                    QStringList parts;
                    if (lines) parts << tr("%n line(s)", nullptr, lines);
                    if (circles) parts << tr("%n circle(s)", nullptr, circles);
                    if (arcs) parts << tr("%n arc(s)", nullptr, arcs);
                    if (splines) parts << tr("%n spline(s)", nullptr, splines);
                    if (points) parts << tr("%n point(s)", nullptr, points);
                    drawn->setText(parts.isEmpty() ? tr("nothing yet") : parts.join(QStringLiteral(", ")));
                    constraints->setText(QString::number(sk.sketch().constraints.size()));
                    const auto* report = ctx_.documents().lastRebuild(ctx_.selection().doc());
                    const auto* r = report ? report->result(g.id()) : nullptr;
                    if (!r || !r->sketch || r->sketch->freedom < 0) {
                        freedom->setText(tr("unknown: the sketch didn't solve"));
                    } else if (r->sketch->freedom == 0) {
                        freedom->setText(tr("0: fully constrained"));
                    } else {
                        freedom->setText(QString::number(r->sketch->freedom));
                    }
                });
                break;
            }
            case model::FeatureType::Extrude:  // see addExtrude()
                break;
            case model::FeatureType::Move: {
                auto* body = new QLabel(form_);
                body->setObjectName(QStringLiteral("body"));
                form->addRow(tr("Body"), body);
                updaters_.push_back([this, body](const model::Feature& g) {
                    const auto* design = ctx_.documents().find(ctx_.selection().doc());
                    const auto* s =
                        design ? design->find(static_cast<const model::MoveFeature&>(g).body()) : nullptr;
                    body->setText(s ? QString::fromStdString(s->name()) : tr("(missing)"));
                });
                auto motion = [](const model::Feature& g) {
                    return static_cast<const model::MoveFeature&>(g).motion();
                };
                auto setMotion = [](model::Feature& g, auto change) {
                    auto& m = static_cast<model::MoveFeature&>(g);
                    auto value = m.motion();
                    change(value);
                    m.setMotion(value);
                };
                addVec3(
                    form, QStringLiteral("offset"), tr("Move"), false,
                    [motion](const model::Feature& g) { return motion(g).offset; },
                    [setMotion](model::Feature& g, kernel::Vec3 v) {
                        setMotion(g, [v](kernel::Motion& m) { m.offset = v; });
                    });
                // Turn: degrees for the student, radians inside.
                auto* turn = new QDoubleSpinBox(form_);
                turn->setObjectName(QStringLiteral("turn"));
                turn->setDecimals(2);
                turn->setRange(-360.0, 360.0);
                turn->setSuffix(QStringLiteral("°"));
                turn->setKeyboardTracking(false);
                turn->setToolTip(
                    tr("How far to turn the body before moving it, in degrees. Positive turns "
                       "counterclockwise, looking down the axis."));
                form->addRow(tr("Turn"), turn);
                connect(turn, &QDoubleSpinBox::valueChanged, this, [this, setMotion](double degrees) {
                    edit(QStringLiteral("turn"), [setMotion, degrees](model::Feature& g) {
                        setMotion(g, [degrees](kernel::Motion& m) {
                            m.angle = degrees * std::numbers::pi / 180.0;
                        });
                    });
                });
                updaters_.push_back([turn, motion](const model::Feature& g) {
                    turn->setValue(motion(g).angle * 180.0 / std::numbers::pi);
                });
                addVec3(
                    form, QStringLiteral("axis"), tr("Axis"), false,
                    [motion](const model::Feature& g) { return motion(g).axis; },
                    [setMotion](model::Feature& g, kernel::Vec3 v) {
                        setMotion(g, [v](kernel::Motion& m) { m.axis = v; });
                    },
                    false);
                addVec3(
                    form, QStringLiteral("center"), tr("Center"), false,
                    [motion](const model::Feature& g) { return motion(g).center; },
                    [setMotion](model::Feature& g, kernel::Vec3 v) {
                        setMotion(g, [v](kernel::Motion& m) { m.center = v; });
                    });
                break;
            }
        }
        if (f.type() == model::FeatureType::Extrude) addExtrude(form);
        if (f.type() != model::FeatureType::Sketch) addMaterial(form);  // a sketch isn't a body
    }

    const model::Document* design() const { return ctx_.documents().find(ctx_.selection().doc()); }

    // An Extrude: which sketch, how far, and which way.
    void addExtrude(QFormLayout* form) {
        auto* sketch = new QLabel(form_);
        sketch->setObjectName(QStringLiteral("sketch"));
        form->addRow(tr("Sketch"), sketch);
        updaters_.push_back([this, sketch](const model::Feature& g) {
            const auto* d = design();
            const auto* s = d ? d->find(static_cast<const model::ExtrudeFeature&>(g).sketch()) : nullptr;
            sketch->setText(s ? QString::fromStdString(s->name()) : tr("(missing)"));
        });
        addNumber(
            form, QStringLiteral("distance"), tr("Distance"),
            [](const model::Feature& g) { return static_cast<const model::ExtrudeFeature&>(g).distance(); },
            [](model::Feature& g, double v) { static_cast<model::ExtrudeFeature&>(g).setDistance(v); });
        auto* side = new QComboBox(form_);
        side->setObjectName(QStringLiteral("side"));
        side->addItem(tr("One side (out of the sketch's front)"), static_cast<int>(model::ExtrudeSide::One));
        side->addItem(tr("Flipped (the other way)"), static_cast<int>(model::ExtrudeSide::Flip));
        side->addItem(tr("Both sides (half each way)"), static_cast<int>(model::ExtrudeSide::Both));
        side->setToolTip(tr("Which way the shape goes from its sketch."));
        form->addRow(tr("Side"), side);
        connect(side, &QComboBox::activated, this, [this, side](int index) {
            const auto s = static_cast<model::ExtrudeSide>(side->itemData(index).toInt());
            edit(QStringLiteral("side"),
                 [s](model::Feature& g) { static_cast<model::ExtrudeFeature&>(g).setSide(s); });
        });
        updaters_.push_back([side](const model::Feature& g) {
            const int want = static_cast<int>(static_cast<const model::ExtrudeFeature&>(g).side());
            side->setCurrentIndex(side->findData(want));
        });
    }

    // The material, and what it makes of the body: mass, weight and cost.
    void addMaterial(QFormLayout* form) {
        auto* line = new QFrame(form_);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        form->addRow(line);

        auto* material = new QComboBox(form_);
        material->setObjectName(QStringLiteral("material"));
        material->setToolTip(
            tr("What this step's body is made of. It sets the mass, weight, cost and color."));
        form->addRow(tr("Material"), material);
        auto* info = new QLabel(form_);
        info->setObjectName(QStringLiteral("materialinfo"));
        info->setWordWrap(true);
        info->setStyleSheet(QStringLiteral("color: #57606a"));
        // A form lays out wrapped text at the height of its size hint, which
        // cuts off lines when it's narrower than that (floating over the view,
        // say): keep it as tall as its lines need at the width it has.
        info->installEventFilter(this);
        wrapped_ = info;
        form->addRow(QString(), info);
        auto* volume = new QLabel(form_);
        volume->setObjectName(QStringLiteral("volume"));
        form->addRow(tr("Volume"), volume);
        auto* mass = new QLabel(form_);
        mass->setObjectName(QStringLiteral("mass"));
        form->addRow(tr("Mass"), mass);
        auto* cost = new QLabel(form_);
        cost->setObjectName(QStringLiteral("cost"));
        form->addRow(tr("Material cost"), cost);

        connect(material, &QComboBox::activated, this, [this, material](int index) {
            const auto key = material->itemData(index).toString().toStdString();
            if (key == kNewMaterial) {
                newMaterial();
            } else {
                setMaterial(key);
            }
        });

        updaters_.push_back([this, material, info, volume, mass, cost](const model::Feature& g) {
            const auto* doc = design();
            if (!doc) return;
            // The first item: what the body gets when nothing is picked for it.
            QString inherited = tr("Default: %1").arg(qs(model::defaultMaterial().name));
            if (const auto inputs = g.dependencies(); !inputs.empty()) {
                const auto& a = inputs.front();  // a Boolean's A, a Move's body
                if (const auto* first = doc->find(a)) {
                    inherited = tr("Same as %1: %2").arg(qs(first->name()), qs(doc->materialOf(a).name));
                }
            }
            const QSignalBlocker block(material);
            material->clear();
            material->addItem(inherited, QString());
            material->insertSeparator(material->count());
            for (const auto& m : model::builtinMaterials()) material->addItem(qs(m.name), qs(m.key));
            if (!doc->customMaterials().empty()) {
                material->insertSeparator(material->count());
                for (const auto& m : doc->customMaterials()) material->addItem(qs(m.name), qs(m.key));
            }
            material->insertSeparator(material->count());
            material->addItem(tr("New material..."), QString::fromLatin1(kNewMaterial));
            const int at = material->findData(
                doc->findMaterial(g.material()) ? qs(doc->findMaterial(g.material())->key) : QString());
            material->setCurrentIndex(at < 0 ? 0 : at);

            const auto& m = doc->materialOf(g.id());
            auto known = [](double v, const QString& text) { return v > 0 ? text : tr("unknown"); };
            info->setText(
                tr("%1: density %2 g/cm³, stiffness %3, Poisson's ratio %4, yield strength %5, price %6")
                    .arg(qs(m.name), QString::number(m.density, 'g', 4),
                         known(m.youngsModulus, tr("%1 GPa").arg(m.youngsModulus)),
                         known(m.poissonsRatio, QString::number(m.poissonsRatio)),
                         known(m.yieldStrength, tr("%1 MPa").arg(m.yieldStrength)),
                         known(m.costPerKg, tr("%1 per kg").arg(money(m.costPerKg)))));

            const auto* report = ctx_.documents().lastRebuild(ctx_.selection().doc());
            const auto* shape = report ? report->shapeOf(g.id()) : nullptr;
            const auto* kernel = ctx_.documents().kernel();
            if (!shape || !kernel) {
                const QString none = tr("none: this step didn't build");
                volume->setText(none);
                mass->setText(none);
                cost->setText(none);
                return;
            }
            const auto b = model::bodyMass(*kernel, *shape, m);
            // "3748.67 mm³": the design's unit, cubed.
            QString vol = qs(volumeText(b.volume, unit_));
            vol.replace(QStringLiteral("^3"), QStringLiteral("³"));
            volume->setText(vol);
            mass->setText(tr("%1 (weighs %2 N)").arg(massText(b.grams)).arg(b.newtons, 0, 'f', 3));
            cost->setText(m.costPerKg <= 0 ? tr("unknown: the material has no price")
                          : b.cost < 0.005 ? tr("less than $0.01")
                                           : tr("about %1").arg(money(b.cost)));
        });
    }

    void setMaterial(const std::string& key) {
        if (filling_) return;
        const auto* f = selected();
        if (!f || key == f->material()) return;
        auto problems = ctx_.documents().run(ctx_.selection().doc(), "feature.set_material",
                                             {{"feature", f->id()}, {"material", key}});
        if (!problems.empty()) ui_.showProblems(problems);
        schedule();
    }

    void newMaterial() {
        model::Material m;
        if (!askNewMaterial(this, m)) {
            schedule();  // put the combo back
            return;
        }
        auto problems = ctx_.documents().execute(ctx_.selection().doc(),
                                                 std::make_unique<model::DefineMaterialCommand>(m));
        if (!problems.empty()) {
            ui_.showProblems(problems);
            schedule();
            return;
        }
        setMaterial(m.name);
    }

    // The unit the shown design's lengths are in (its Document settings).
    LengthUnit unitNow() const {
        const auto* d = design();
        return d ? d->lengthUnit() : LengthUnit::Millimeter;
    }

    // A box for a length in the design's unit (or, with `length` false, for a
    // plain number, like an axis direction).
    QDoubleSpinBox* spin(const QString& name, bool positive, const QString& suffix, bool length = true) {
        auto* s = new QDoubleSpinBox(form_);
        s->setObjectName(name);
        const double most = length ? toUnit(10000.0, unit_) : 10000.0;  // 10 m
        s->setDecimals(length ? unitDecimals(unit_) : 2);
        s->setRange(positive ? std::pow(10.0, -s->decimals()) : -most, most);
        s->setSuffix(suffix);
        s->setKeyboardTracking(false);  // edit when Enter is pressed or focus leaves, not per key
        s->setAccelerated(true);
        return s;
    }

    void addNumber(QFormLayout* form, const QString& key, const QString& label,
                   std::function<double(const model::Feature&)> get,
                   std::function<void(model::Feature&, double)> set) {
        auto* s = spin(key, true, QLatin1Char(' ') + qs(std::string(unitSymbol(unit_))));
        form->addRow(label, s);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, key, set](double v) {
            const double mm = fromUnit(v, unit_);
            edit(key, [set, mm](model::Feature& g) { set(g, mm); });
        });
        updaters_.push_back([this, s, get](const model::Feature& g) { s->setValue(toUnit(get(g), unit_)); });
    }

    void addVec3(QFormLayout* form, const QString& key, const QString& label, bool positive,
                 std::function<kernel::Vec3(const model::Feature&)> get,
                 std::function<void(model::Feature&, kernel::Vec3)> set, bool length = true) {
        auto* row = new QWidget(form_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        QDoubleSpinBox* xyz[3];
        const char* axes[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            // Compact: "x 20.00", no arrows (type, or scroll the mouse wheel); the
            // unit is in the row's label.
            xyz[i] = spin(key + QLatin1Char('.') + QLatin1String(axes[i]), positive, QString(), length);
            xyz[i]->setPrefix(QLatin1String(axes[i]) + QLatin1Char(' '));
            xyz[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
            xyz[i]->setMinimumWidth(72);
            h->addWidget(xyz[i]);
        }
        form->addRow(length ? tr("%1 (%2)").arg(label, qs(std::string(unitSymbol(unit_)))) : label, row);
        for (int i = 0; i < 3; ++i) {
            connect(xyz[i], &QDoubleSpinBox::valueChanged, this,
                    [this, key, i, get, set, length](double shown) {
                        const QString part = key + QLatin1Char('.') + QString::number(i);
                        const double v = length ? fromUnit(shown, unit_) : shown;
                        edit(part, [i, v, get, set](model::Feature& g) {
                            kernel::Vec3 p = get(g);
                            (i == 0 ? p.x : i == 1 ? p.y : p.z) = v;
                            set(g, p);
                        });
                    });
        }
        updaters_.push_back([this, xyz, get, length](const model::Feature& g) {
            const auto p = get(g);
            auto shown = [&](double v) { return length ? toUnit(v, unit_) : v; };
            xyz[0]->setValue(shown(p.x));
            xyz[1]->setValue(shown(p.y));
            xyz[2]->setValue(shown(p.z));
        });
    }

    // Edits the shown step through a command. Quick repeated changes to the same
    // number (holding an arrow key) become one undo step.
    void edit(const QString& field, const Change& change) {
        if (filling_) return;
        const auto* f = selected();
        if (!f) return;
        auto copy = f->clone();
        change(*copy);
        const bool merge = field == lastField_ && lastEdit_.isValid() && lastEdit_.elapsed() < kMergeMs;
        lastField_ = field;
        lastEdit_.start();
        auto problems = ctx_.documents().execute(
            ctx_.selection().doc(), std::make_unique<model::EditFeatureCommand>(std::move(copy), merge));
        if (!problems.empty()) {
            ui_.showProblems(problems);
            schedule();  // show the value that is really in the design
        }
    }

    void rename(const QString& text) {
        if (filling_) return;
        const auto* f = selected();
        const auto name = text.trimmed().toStdString();
        if (!f || name == f->name()) return;
        auto problems = ctx_.documents().run(ctx_.selection().doc(), "feature.rename",
                                             {{"feature", f->id()}, {"name", name}});
        if (!problems.empty()) {
            ui_.showProblems(problems);
            schedule();
        }
    }

    QLabel* wrapped_ = nullptr;  // the material's description, kept as tall as its lines
    plugin::PluginContext& ctx_;
    ui::UiRegistry& ui_;
    QLabel* title_ = nullptr;
    QWidget* form_ = nullptr;
    QLabel* status_ = nullptr;
    FeatureId shown_;
    LengthUnit unit_ = LengthUnit::Millimeter;  // the unit the form was built for
    std::vector<std::function<void(const model::Feature&)>> updaters_;
    bool filling_ = false;
    bool pending_ = false;
    QString lastField_;
    QElapsedTimer lastEdit_;
    std::vector<plugin::Subscription> subs_;
};

void registerProperties(plugin::PluginContext& ctx) {
    auto* ui = ctx.service<ui::UiRegistry>();
    if (!ui) {
        ctx.log("No desktop app here, so the Properties panel isn't shown.");
        return;
    }
    ui::PanelSpec spec{"kh.properties.panel", QObject::tr("Properties"), ui::PanelArea::Right,
                       [&ctx, ui] { return new Properties(ctx, *ui); }};
    spec.floatsOverView = true;  // at the view's top right
    ui->addPanel(std::move(spec));
}

}  // namespace

plugin::BuiltinPlugin propertiesPlugin() {
    return {kManifest, &registerProperties};
}

}  // namespace kh::plugins
