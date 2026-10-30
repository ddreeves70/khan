#include "kh/plugins/ViewOptions.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
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

// The View options panel: every option, grouped, kept up to date. "Show
// options for" picks all panes or one pane; a pane's own values are bold, with a
// button to put them back to the all-panes value.
class Panel final : public QScrollArea {
public:
    Panel(ViewOptionsService& options, plugin::EventBus& bus) : options_(options) {
        setObjectName(QStringLiteral("kh.viewoptions.panel"));
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // narrow panels squeeze, not scroll
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout(body);

        auto* scopeRow = new QFormLayout;
        scope_ = new QComboBox;
        scope_->setObjectName(QStringLiteral("kh.viewoptions.scope"));
        scope_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        scope_->setMinimumContentsLength(8);
        scope_->setToolTip(tr("Change options for every pane, or for one pane only."));
        scopeRow->addRow(tr("Show options for"), scope_);
        layout->addLayout(scopeRow);
        connect(scope_, &QComboBox::currentIndexChanged, this, [this](int) { showAll(); });

        std::map<std::string, QFormLayout*> groups;
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
        fillScope();
        subs_.push_back(bus.subscribe<ViewOptionChanged>([this](const ViewOptionChanged& e) {
            if (e.pane.empty() || e.pane == pane()) show(e.option);
        }));
        subs_.push_back(bus.subscribe<viewoptions::PanesChanged>([this](const auto&) { fillScope(); }));
    }

private:
    struct Row {
        QWidget* widget = nullptr;
        QLabel* label = nullptr;  // for options with a label beside them
        QToolButton* reset = nullptr;
    };

    std::string pane() const { return scope_->currentData().toString().toStdString(); }

    // "All panes", then every pane, keeping the choice if that pane is still there.
    void fillScope() {
        const QString was = scope_->currentData().toString();
        const QSignalBlocker block(scope_);
        scope_->clear();
        scope_->addItem(tr("All panes"), QString());
        for (const auto& p : options_.panes()) scope_->addItem(qs(p.name), qs(p.key));
        const int at = scope_->findData(was);
        scope_->setCurrentIndex(at < 0 ? 0 : at);
        showAll();
    }

    void add(QFormLayout& form, const OptionSpec& o) {
        const std::string id = o.id;
        Row row;
        auto* line = new QWidget;
        auto* h = new QHBoxLayout(line);
        h->setContentsMargins(0, 0, 0, 0);
        switch (o.kind) {
            case OptionKind::Flag: {
                auto* box = new QCheckBox(qs(o.text));
                connect(box, &QCheckBox::toggled, this, [this, id](bool on) { change(id, on); });
                row.widget = box;
                break;
            }
            case OptionKind::Choice: {
                auto* combo = new QComboBox;
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(6);
                for (const auto& c : o.choices) combo->addItem(qs(c.text), qs(c.value));
                connect(combo, &QComboBox::currentIndexChanged, this, [this, id, combo](int) {
                    change(id, combo->currentData().toString().toStdString());
                });
                row.widget = combo;
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
                row.widget = spin;
                break;
            }
        }
        row.widget->setObjectName(qs(id));
        row.widget->setToolTip(qs(o.tip));
        h->addWidget(row.widget, 1);
        row.reset = new QToolButton;
        row.reset->setObjectName(qs(id + ".reset"));
        row.reset->setText(QStringLiteral("\u21BA"));  // an arrow going round
        row.reset->setToolTip(tr("This pane has its own value. Click to use the same as all panes."));
        row.reset->setAutoRaise(true);
        connect(row.reset, &QToolButton::clicked, this, [this, id] { options_.reset(id, pane()); });
        h->addWidget(row.reset);
        if (o.kind == OptionKind::Flag) {
            form.addRow(line);
        } else {
            row.label = new QLabel(qs(o.text));
            form.addRow(row.label, line);
        }
        rows_[id] = row;
        show(id);
    }

    void change(const std::string& id, const OptionValue& v) {
        if (showing_) return;
        const auto p = pane();
        if (p.empty()) {
            options_.set(id, v);
        } else {
            options_.setInPane(id, v, p);
        }
    }

    void showAll() {
        for (const auto& [id, row] : rows_) show(id);
    }

    // Puts an option's value, for the chosen panes, into its row.
    void show(const std::string& id) {
        auto it = rows_.find(id);
        const auto* o = options_.find(id);
        if (it == rows_.end() || !o) return;
        const auto p = pane();
        const auto& row = it->second;
        showing_ = true;
        {
            const QSignalBlocker block(row.widget);
            switch (o->kind) {
                case OptionKind::Flag:
                    static_cast<QCheckBox*>(row.widget)->setChecked(options_.flag(id, p));
                    break;
                case OptionKind::Choice: {
                    auto* combo = static_cast<QComboBox*>(row.widget);
                    combo->setCurrentIndex(combo->findData(qs(options_.choice(id, p))));
                    break;
                }
                case OptionKind::Length:
                case OptionKind::Number:
                    static_cast<QDoubleSpinBox*>(row.widget)->setValue(options_.number(id, p));
                    break;
            }
        }
        showing_ = false;
        const bool own = options_.hasOwn(id, p);
        row.reset->setVisible(own);
        QFont font = row.widget->font();
        font.setBold(own);
        (row.label ? static_cast<QWidget*>(row.label) : row.widget)->setFont(font);
    }

    ViewOptionsService& options_;
    QComboBox* scope_ = nullptr;
    std::map<std::string, Row> rows_;
    std::vector<plugin::Subscription> subs_;
    bool showing_ = false;
};

// A pane's "This pane only" menu: every option, with the pane's values.
void paneMenu(ViewOptionsService& options, const std::string& pane, QPoint at) {
    QMenu menu;
    menu.setObjectName(QStringLiteral("kh.viewoptions.panemenu"));
    menu.addSection(QObject::tr("This pane only: %1").arg(qs(options.paneName(pane))));
    std::string group;
    for (const auto* o : options.options()) {
        if (!group.empty() && o->group != group) menu.addSeparator();
        group = o->group;
        const std::string id = o->id;
        switch (o->kind) {
            case OptionKind::Flag: {
                auto* a = menu.addAction(qs(o->text));
                a->setCheckable(true);
                a->setChecked(options.flag(id, pane));
                QObject::connect(a, &QAction::triggered,
                                 [&options, id, pane](bool on) { options.setInPane(id, on, pane); });
                break;
            }
            case OptionKind::Choice: {
                auto* sub = menu.addMenu(qs(o->text));
                for (const auto& c : o->choices) {
                    auto* a = sub->addAction(qs(c.text));
                    a->setCheckable(true);
                    a->setChecked(options.choice(id, pane) == c.value);
                    const std::string value = c.value;
                    QObject::connect(a, &QAction::triggered,
                                     [&options, id, value, pane] { options.setInPane(id, value, pane); });
                }
                break;
            }
            case OptionKind::Length:
            case OptionKind::Number: {
                auto* sub = menu.addMenu(qs(o->text));
                const QString unit = o->kind == OptionKind::Length ? QObject::tr(" mm") : QString();
                bool preset = false;
                for (double p : o->presets) {
                    auto* a = sub->addAction(qs(viewoptions::numberText(p)) + unit);
                    a->setCheckable(true);
                    a->setChecked(options.number(id, pane) == p);
                    preset = preset || a->isChecked();
                    QObject::connect(a, &QAction::triggered,
                                     [&options, id, p, pane] { options.setInPane(id, p, pane); });
                }
                sub->addSeparator();
                const QString other =
                    preset ? QObject::tr("Other...")
                           : QObject::tr("Other (%1)...")
                                 .arg(qs(viewoptions::numberText(options.number(id, pane))) + unit);
                QObject::connect(sub->addAction(other), &QAction::triggered,
                                 [&options, id, pane] { options.ask(id, pane); });
                break;
            }
        }
    }
    menu.addSeparator();
    auto* same = menu.addAction(QObject::tr("Use the same as all panes"));
    same->setEnabled(!options.ownOptions(pane).empty());
    QObject::connect(same, &QAction::triggered, [&options, pane] { options.resetPane(pane); });
    menu.exec(at);
}

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
    options->paneMenu = [options](const std::string& pane, int x, int y) {
        if (qApp) paneMenu(*options, pane, QPoint(x, y));
    };
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
    panel.floatsOverView = true;
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

namespace {

std::string paneKey(std::string_view pane, std::string_view id) {
    return "panes/" + std::string(pane) + "/" + std::string(id);
}

constexpr const char* kPaneIndex = "pane_keys";  // panes that have ever had their own values

}  // namespace

std::optional<OptionValue> ViewOptionsService::stored(const OptionSpec& o, const std::string& key) const {
    if (!settings_.has(key)) return std::nullopt;
    OptionValue v;
    switch (o.kind) {
        case OptionKind::Flag: {
            const auto text = settings_.text(key);
            if (text != "true" && text != "false") return std::nullopt;
            v = text == "true";
            break;
        }
        case OptionKind::Choice:
            v = settings_.text(key);
            break;
        case OptionKind::Length:
        case OptionKind::Number:
            v = settings_.number(key, std::nan(""));
            break;
    }
    // A damaged saved value is ignored.
    if (!refuse(o, v).empty()) return std::nullopt;
    return v;
}

std::vector<std::string> ViewOptionsService::knownPanes() const {
    std::vector<std::string> out;
    std::string list = settings_.text(kPaneIndex);
    std::size_t from = 0;
    while (from <= list.size()) {
        const auto comma = list.find(',', from);
        const auto part = list.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
        if (!part.empty()) out.push_back(part);
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    return out;
}

OptionValue ViewOptionsService::value(std::string_view id, std::string_view pane) const {
    const auto* o = find(id);
    if (!o) return false;
    if (!pane.empty()) {
        if (auto own = stored(*o, paneKey(pane, o->id))) return *own;
    }
    if (auto all = stored(*o, o->id)) return *all;
    return o->initial;
}

Diagnostics ViewOptionsService::set(std::string_view id, const OptionValue& v) {
    const auto* o = find(id);
    if (!o) return {problem("viewoptions.unknown", "There's no view option called " + std::string(id) + ".")};
    if (const auto why = refuse(*o, v); !why.empty()) return {problem("viewoptions.value", why, o->id)};
    // For every pane: their own values for this option go.
    for (const auto& pane : knownPanes()) {
        if (settings_.has(paneKey(pane, o->id))) settings_.remove(paneKey(pane, o->id));
    }
    // Setting it sends SettingChanged, and settingChanged() turns that into ViewOptionChanged.
    if (value(id, {}) != v) std::visit([&](const auto& x) { settings_.set(o->id, x); }, v);
    return {};
}

Diagnostics ViewOptionsService::setInPane(std::string_view id, const OptionValue& v, std::string_view pane) {
    if (pane.empty()) return set(id, v);
    const auto* o = find(id);
    if (!o) return {problem("viewoptions.unknown", "There's no view option called " + std::string(id) + ".")};
    if (const auto why = refuse(*o, v); !why.empty()) return {problem("viewoptions.value", why, o->id)};
    if (hasOwn(id, pane) && value(id, pane) == v) return {};
    auto known = knownPanes();
    if (std::find(known.begin(), known.end(), pane) == known.end()) {
        std::string list = settings_.text(kPaneIndex);
        settings_.set(kPaneIndex, list.empty() ? std::string(pane) : list + "," + std::string(pane));
    }
    std::visit([&](const auto& x) { settings_.set(paneKey(pane, o->id), x); }, v);
    return {};
}

bool ViewOptionsService::hasOwn(std::string_view id, std::string_view pane) const {
    const auto* o = find(id);
    return o && !pane.empty() && stored(*o, paneKey(pane, o->id)).has_value();
}

std::vector<std::string> ViewOptionsService::ownOptions(std::string_view pane) const {
    std::vector<std::string> out;
    for (const auto& o : options_) {
        if (hasOwn(o.id, pane)) out.push_back(o.id);
    }
    return out;
}

void ViewOptionsService::reset(std::string_view id, std::string_view pane) {
    if (pane.empty()) return;
    const auto key = paneKey(pane, id);
    if (settings_.has(key)) settings_.remove(key);
}

void ViewOptionsService::resetPane(std::string_view pane) {
    for (const auto& o : options_) reset(o.id, pane);
}

void ViewOptionsService::settingChanged(const std::string& key) {
    if (key.rfind("panes/", 0) == 0) {
        const auto slash = key.find('/', 6);
        if (slash == std::string::npos) return;
        const std::string pane = key.substr(6, slash - 6);
        const std::string id = key.substr(slash + 1);
        if (find(id)) bus_.publish(ViewOptionChanged{id, pane});
        return;
    }
    if (find(key)) bus_.publish(ViewOptionChanged{key, {}});
}

void ViewOptionsService::ask(std::string_view id, std::string_view pane) {
    const auto* o = find(id);
    if (!o || !asker || (o->kind != OptionKind::Length && o->kind != OptionKind::Number)) return;
    if (const auto typed = asker(*o, number(id, pane))) {
        auto problems = pane.empty() ? set(id, *typed) : setInPane(id, *typed, pane);
        if (!problems.empty() && report) report(problems);
    }
}

void ViewOptionsService::addPane(viewoptions::PaneInfo pane) {
    for (auto& p : panes_) {
        if (p.key == pane.key) {
            if (p.name == pane.name) return;
            p.name = std::move(pane.name);
            bus_.publish(viewoptions::PanesChanged{});
            return;
        }
    }
    panes_.push_back(std::move(pane));
    if (active_.empty()) active_ = panes_.back().key;
    bus_.publish(viewoptions::PanesChanged{});
}

void ViewOptionsService::removePane(std::string_view key, bool forget) {
    if (forget) resetPane(key);
    const auto it = std::find_if(panes_.begin(), panes_.end(),
                                 [&](const viewoptions::PaneInfo& p) { return p.key == key; });
    if (it == panes_.end()) return;
    panes_.erase(it);
    if (active_ == key) active_ = panes_.empty() ? std::string() : panes_.front().key;
    bus_.publish(viewoptions::PanesChanged{});
}

void ViewOptionsService::setActivePane(std::string_view key) {
    if (active_ == key) return;
    active_ = std::string(key);
    bus_.publish(viewoptions::PanesChanged{});
}

std::string ViewOptionsService::paneName(std::string_view key) const {
    for (const auto& p : panes_) {
        if (p.key == key) return p.name;
    }
    return {};
}

void ViewOptionsService::showPaneMenu(std::string_view pane, int x, int y) {
    if (paneMenu) paneMenu(std::string(pane), x, y);
}

plugin::BuiltinPlugin viewOptionsPlugin() {
    return {kManifest, &registerViewOptions};
}

}  // namespace kh::plugins
