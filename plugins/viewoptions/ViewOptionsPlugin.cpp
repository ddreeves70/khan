#include "kh/plugins/ViewOptions.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "kh/plugin/Events.h"
#include "kh/ui/UiRegistry.h"

namespace kh::plugins {

namespace {

using viewoptions::OptionKind;
using viewoptions::OptionSpec;
using viewoptions::OptionValue;
using viewoptions::ViewOptionChanged;

constexpr const char* kManifest = R"({
  "id": "kh.viewoptions",
  "name": "View options",
  "version": "0.1.0",
  "description": "Keeps the options for what the viewer shows, with the View options panel.",
  "tier": "foundation"
})";

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

Diagnostic problem(std::string code, std::string message, std::string detail = {}) {
    return {std::move(code), Severity::Error, std::move(message), std::move(detail)};
}

bool sameKind(OptionKind kind, const OptionValue& v) {
    switch (kind) {
        case OptionKind::Flag:
            return std::holds_alternative<bool>(v);
        case OptionKind::Choice:
            return std::holds_alternative<std::string>(v);
        case OptionKind::Length:
        case OptionKind::Number:
            return std::holds_alternative<double>(v);
    }
    return false;
}

// Why a value can't be used for an option, in plain words; empty if it can.
std::string refuse(const OptionSpec& o, const OptionValue& v) {
    if (!sameKind(o.kind, v)) return o.text + " can't take that kind of value.";
    if (o.kind == OptionKind::Length || o.kind == OptionKind::Number) {
        const double d = std::get<double>(v);
        if (!std::isfinite(d) || d < o.min || d > o.max) {
            const std::string unit = o.kind == OptionKind::Length ? " mm" : "";
            std::string msg = o.text + " must be between " + viewoptions::numberText(o.min) + unit + " and " +
                              viewoptions::numberText(o.max) + unit + ".";
            if (o.presets.size() >= 3) {
                msg += " Pick one like " + viewoptions::numberText(o.presets[0]) + ", " +
                       viewoptions::numberText(o.presets[2]) + " or " +
                       viewoptions::numberText(o.presets[std::min<std::size_t>(3, o.presets.size() - 1)]) +
                       unit + ".";
            }
            return msg;
        }
    }
    if (o.kind == OptionKind::Choice) {
        const auto& s = std::get<std::string>(v);
        const bool known = std::any_of(o.choices.begin(), o.choices.end(),
                                       [&](const viewoptions::OptionChoice& c) { return c.value == s; });
        if (!known) return o.text + " has no choice called \"" + s + "\".";
    }
    return {};
}

// The View options panel: every option, grouped, kept up to date.
class Panel final : public QScrollArea {
public:
    Panel(ViewOptionsService& options, plugin::EventBus& bus) : options_(options) {
        setObjectName(QStringLiteral("kh.viewoptions.panel"));
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout(body);
        std::map<std::string, QFormLayout*> groups;
        std::vector<std::string> order;
        for (const auto* o : options_.options()) {
            if (!groups.count(o->group)) {
                auto* box = new QGroupBox(qs(o->group.empty() ? std::string("Other") : o->group));
                groups[o->group] = new QFormLayout(box);
                layout->addWidget(box);
            }
            add(*groups[o->group], *o);
        }
        if (groups.empty()) layout->addWidget(new QLabel(tr("No view options yet.")));
        layout->addStretch(1);
        setWidget(body);
        sub_ = bus.subscribe<ViewOptionChanged>([this](const ViewOptionChanged& e) { show(e.option); });
    }

private:
    void add(QFormLayout& form, const OptionSpec& o) {
        const std::string id = o.id;
        QWidget* w = nullptr;
        switch (o.kind) {
            case OptionKind::Flag: {
                auto* box = new QCheckBox(qs(o.text));
                connect(box, &QCheckBox::toggled, this, [this, id](bool on) { change(id, on); });
                form.addRow(box);
                w = box;
                break;
            }
            case OptionKind::Choice: {
                auto* combo = new QComboBox;
                for (const auto& c : o.choices) combo->addItem(qs(c.text), qs(c.value));
                connect(combo, &QComboBox::currentIndexChanged, this, [this, id, combo](int) {
                    change(id, combo->currentData().toString().toStdString());
                });
                form.addRow(qs(o.text), combo);
                w = combo;
                break;
            }
            case OptionKind::Length:
            case OptionKind::Number: {
                auto* spin = new QDoubleSpinBox;
                spin->setRange(o.min, o.max);
                spin->setDecimals(2);
                if (o.kind == OptionKind::Length) spin->setSuffix(tr(" mm"));
                spin->setKeyboardTracking(false);  // change once typing is done
                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id](double v) { change(id, v); });
                form.addRow(qs(o.text), spin);
                w = spin;
                break;
            }
        }
        w->setObjectName(qs(id));
        w->setToolTip(qs(o.tip));
        widgets_[id] = w;
        show(id);
    }

    void change(const std::string& id, const OptionValue& v) {
        if (showing_) return;
        options_.set(id, v);
    }

    // Puts an option's value into its widget.
    void show(const std::string& id) {
        auto it = widgets_.find(id);
        const auto* o = options_.find(id);
        if (it == widgets_.end() || !o) return;
        showing_ = true;
        const QSignalBlocker block(it->second);
        switch (o->kind) {
            case OptionKind::Flag:
                static_cast<QCheckBox*>(it->second)->setChecked(options_.flag(id));
                break;
            case OptionKind::Choice: {
                auto* combo = static_cast<QComboBox*>(it->second);
                combo->setCurrentIndex(combo->findData(qs(options_.choice(id))));
                break;
            }
            case OptionKind::Length:
            case OptionKind::Number:
                static_cast<QDoubleSpinBox*>(it->second)->setValue(options_.number(id));
                break;
        }
        showing_ = false;
    }

    ViewOptionsService& options_;
    std::map<std::string, QWidget*> widgets_;
    plugin::Subscription sub_;
    bool showing_ = false;
};

void registerViewOptions(plugin::PluginContext& ctx) {
    // Kept alive by the listener below, which Khan keeps while the plugin is loaded.
    auto options = std::make_shared<ViewOptionsService>(ctx.settings(), ctx.events());
    ctx.provide<viewoptions::IViewOptions>(*options);
    ctx.listen<plugin::SettingChanged>([options](const plugin::SettingChanged& e) {
        if (e.plugin == "kh.viewoptions") options->settingChanged(e.key);
    });

    auto* ui = ctx.service<ui::UiRegistry>();
    if (!ui) return;  // kh_cli: options are kept, nothing is shown
    options->report = [ui](const Diagnostics& d) { ui->showProblems(d); };
    options->asker = [](const OptionSpec& o, double current) -> std::optional<double> {
        if (!qApp) return std::nullopt;
        bool ok = false;
        const QString unit = o.kind == OptionKind::Length ? QObject::tr(" (mm)") : QString();
        const double v = QInputDialog::getDouble(QApplication::activeWindow(), qs(o.text),
                                                 qs(o.text) + unit + ":", current, o.min, o.max, 2, &ok);
        return ok ? std::optional<double>(v) : std::nullopt;
    };
    auto& bus = ctx.events();
    ui::PanelSpec panel{"kh.viewoptions.panel", QObject::tr("View options"), ui::PanelArea::Right,
                        [options, &bus] { return new Panel(*options, bus); }};
    panel.startHidden = true;
    ui->addPanel(std::move(panel));
    ctx.addAction({.id = "kh.viewoptions.show",
                   .text = "View options...",
                   .menu = "View",
                   .tip = "Open the panel with every view option, like the grid and the axes.",
                   .run = [ui] { ui->showPanel("kh.viewoptions.panel"); }});
}

}  // namespace

ViewOptionsService::ViewOptionsService(plugin::PluginSettings& settings, plugin::EventBus& bus)
    : settings_(settings), bus_(bus) {}

Diagnostics ViewOptionsService::add(OptionSpec option) {
    if (find(option.id)) {
        return {problem("viewoptions.taken", "There's already a view option called " + option.id + ".",
                        option.id)};
    }
    if (const auto why = refuse(option, option.initial); !why.empty()) {
        return {problem("viewoptions.initial",
                        "The view option " + option.id + " starts with a value it can't take: " + why,
                        option.id)};
    }
    options_.push_back(std::move(option));
    return {};
}

const OptionSpec* ViewOptionsService::find(std::string_view id) const {
    for (const auto& o : options_) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

std::vector<const OptionSpec*> ViewOptionsService::options() const {
    std::vector<const OptionSpec*> out;
    for (const auto& o : options_) out.push_back(&o);
    return out;
}

OptionValue ViewOptionsService::value(std::string_view id) const {
    const auto* o = find(id);
    if (!o) return false;
    if (!settings_.has(o->id)) return o->initial;
    OptionValue v;
    switch (o->kind) {
        case OptionKind::Flag:
            v = settings_.flag(o->id, std::get<bool>(o->initial));
            break;
        case OptionKind::Choice:
            v = settings_.text(o->id);
            break;
        case OptionKind::Length:
        case OptionKind::Number:
            v = settings_.number(o->id, std::nan(""));
            break;
    }
    // A damaged saved value falls back to the option's first value.
    return refuse(*o, v).empty() ? v : o->initial;
}

Diagnostics ViewOptionsService::set(std::string_view id, const OptionValue& v) {
    const auto* o = find(id);
    if (!o) return {problem("viewoptions.unknown", "There's no view option called " + std::string(id) + ".")};
    if (const auto why = refuse(*o, v); !why.empty()) {
        return {problem("viewoptions.value", why, o->id)};
    }
    if (value(id) == v) return {};
    // Setting it sends SettingChanged, and settingChanged() turns that into ViewOptionChanged.
    std::visit([&](const auto& x) { settings_.set(o->id, x); }, v);
    return {};
}

void ViewOptionsService::settingChanged(const std::string& key) {
    if (find(key)) bus_.publish(ViewOptionChanged{key});
}

void ViewOptionsService::ask(std::string_view id) {
    const auto* o = find(id);
    if (!o || !asker || (o->kind != OptionKind::Length && o->kind != OptionKind::Number)) return;
    if (const auto typed = asker(*o, number(id))) {
        if (auto problems = set(id, *typed); !problems.empty() && report) report(problems);
    }
}

plugin::BuiltinPlugin viewOptionsPlugin() {
    return {kManifest, &registerViewOptions};
}

}  // namespace kh::plugins
