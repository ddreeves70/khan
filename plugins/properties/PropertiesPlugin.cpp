#include "kh/plugins/Properties.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "kh/model/Commands.h"
#include "kh/model/Document.h"
#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.properties",
  "name": "Properties",
  "version": "0.1.0",
  "description": "The panel for changing the selected step's name and numbers.",
  "requires": ["kh.modeling"]
})";

constexpr qint64 kMergeMs = 800;  // edits to one number this close together are one undo step

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
        if (!f) {
            shown_ = FeatureId{};
            clearForm();
            title_->setText(count > 1 ? tr("%1 steps selected").arg(count)
                                      : tr("Pick a step in the feature tree to see its numbers."));
            status_->clear();
            return;
        }
        if (f->id() != shown_) {
            shown_ = f->id();
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

    void clearForm() {
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
                    QString());
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
        }
    }

    QDoubleSpinBox* spin(const QString& name, bool positive, const QString& suffix) {
        auto* s = new QDoubleSpinBox(form_);
        s->setObjectName(name);
        s->setDecimals(2);
        s->setRange(positive ? 0.01 : -10000.0, 10000.0);
        s->setSuffix(suffix);
        s->setKeyboardTracking(false);  // edit when Enter is pressed or focus leaves, not per key
        s->setAccelerated(true);
        return s;
    }

    void addNumber(QFormLayout* form, const QString& key, const QString& label,
                   std::function<double(const model::Feature&)> get,
                   std::function<void(model::Feature&, double)> set) {
        auto* s = spin(key, true, tr(" mm"));
        form->addRow(label, s);
        connect(s, &QDoubleSpinBox::valueChanged, this,
                [this, key, set](double v) { edit(key, [set, v](model::Feature& g) { set(g, v); }); });
        updaters_.push_back([s, get](const model::Feature& g) { s->setValue(get(g)); });
    }

    void addVec3(QFormLayout* form, const QString& key, const QString& label, bool positive,
                 std::function<kernel::Vec3(const model::Feature&)> get,
                 std::function<void(model::Feature&, kernel::Vec3)> set,
                 const QString& suffix = QStringLiteral(" mm")) {
        auto* row = new QWidget(form_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        QDoubleSpinBox* xyz[3];
        const char* axes[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            // Compact: "x 20.00", no arrows (type, or scroll the mouse wheel); the
            // unit is in the row's label.
            xyz[i] = spin(key + QLatin1Char('.') + QLatin1String(axes[i]), positive, QString());
            xyz[i]->setPrefix(QLatin1String(axes[i]) + QLatin1Char(' '));
            xyz[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
            xyz[i]->setMinimumWidth(72);
            h->addWidget(xyz[i]);
        }
        form->addRow(suffix.isEmpty() ? label : label + QStringLiteral(" (mm)"), row);
        for (int i = 0; i < 3; ++i) {
            connect(xyz[i], &QDoubleSpinBox::valueChanged, this, [this, key, i, get, set](double v) {
                const QString part = key + QLatin1Char('.') + QString::number(i);
                edit(part, [i, v, get, set](model::Feature& g) {
                    kernel::Vec3 p = get(g);
                    (i == 0 ? p.x : i == 1 ? p.y : p.z) = v;
                    set(g, p);
                });
            });
        }
        updaters_.push_back([xyz, get](const model::Feature& g) {
            const auto p = get(g);
            xyz[0]->setValue(p.x);
            xyz[1]->setValue(p.y);
            xyz[2]->setValue(p.z);
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

    plugin::PluginContext& ctx_;
    ui::UiRegistry& ui_;
    QLabel* title_ = nullptr;
    QWidget* form_ = nullptr;
    QLabel* status_ = nullptr;
    FeatureId shown_;
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
    ui->addPanel({"kh.properties.panel", QObject::tr("Properties"), ui::PanelArea::Right,
                  [&ctx, ui] { return new Properties(ctx, *ui); }});
}

}  // namespace

plugin::BuiltinPlugin propertiesPlugin() {
    return {kManifest, &registerProperties};
}

}  // namespace kh::plugins
