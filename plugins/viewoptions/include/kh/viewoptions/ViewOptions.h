#pragma once

/// @file
/// The view options contract: what other plugins may use from kh.viewoptions.
///
/// A plugin declares the things the viewer can show as options (on/off, one of
/// a list, a length), and kh.viewoptions remembers them, checks them, tells
/// everyone when they change, and shows them in the View menu and the View
/// options panel. See docs/design/view-options.md.
///
/// List "kh.viewoptions" in "requires" in your plugin.json, then declare your
/// options while registering:
///
/// @code
/// auto* options = ctx.service<kh::viewoptions::IViewOptions>();
/// kh::viewoptions::addOption(ctx, *options,
///     {.id = "sam.notes.dates", .text = "Show note dates", .group = "Notes", .initial = true});
/// // Later, when drawing:
/// if (options->flag("sam.notes.dates")) { /* ... */ }
/// @endcode

#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/plugin/PluginContext.h"

namespace kh::viewoptions {

/// What kind of value an option holds.
enum class OptionKind {
    Flag,    ///< On or off. Shown as a check mark.
    Choice,  ///< One of a list. Shown as a submenu.
    Length,  ///< A length in mm, between `min` and `max`. Shown as a submenu of presets plus Other...
    Number,  ///< A plain number, between `min` and `max`. Like Length, without "mm".
};

/// One item of a Choice option.
struct OptionChoice {
    std::string value;  ///< What's stored, e.g. "dots". Never changes.
    std::string text;   ///< What the student sees, e.g. "Dots".
};

/// An option's value: true/false for Flag, a number for Length and Number
/// (in mm for Length), and the choice's `value` for Choice.
using OptionValue = std::variant<bool, double, std::string>;

/// An option, described as plain data.
struct OptionSpec {
    std::string id{};     ///< Starts with the plugin's id and a dot, e.g. "kh.grid.spacing". Never changes.
    std::string text{};   ///< What the student sees, e.g. "Grid spacing".
    std::string group{};  ///< Where it's listed in the View options panel, e.g. "Grid".
    std::string tip{};    ///< One plain sentence shown when the mouse rests on it.
    OptionKind kind = OptionKind::Flag;   ///< What kind of value it holds.
    OptionValue initial = false;          ///< The value before the student changes it.
    double min = 0;                       ///< Length and Number: the smallest value (mm for Length).
    double max = 0;                       ///< Length and Number: the largest value (mm for Length).
    std::vector<double> presets{};        ///< Length and Number: values offered in the menu.
    std::vector<OptionChoice> choices{};  ///< Choice: the items, in menu order.
    std::string shortcut{};               ///< Flag: a keyboard shortcut for its menu item, e.g. "Ctrl+G".
};

/// Sent when an option's value changes.
struct ViewOptionChanged {
    static constexpr std::string_view kType = "kh.viewoptions.changed";  ///< Stable event type name.
    std::string option;                                                  ///< The option's id.
};

/// The view options, as other plugins see them.
class IViewOptions {
public:
    /// The service's name and version.
    static constexpr const char* kServiceName = "kh.viewoptions/1";

    /// Destroys the service.
    virtual ~IViewOptions() = default;

    /// Declares an option. Use addOption(), which also adds its menu items.
    /// @param option What it is.
    /// @return Problems (an id already used, a value of the wrong kind); nothing if it was added.
    virtual Diagnostics add(OptionSpec option) = 0;

    /// Finds an option.
    /// @param id Its id.
    /// @return It, or nullptr.
    virtual const OptionSpec* find(std::string_view id) const = 0;

    /// Lists every option, in the order they were added.
    /// @return The options.
    virtual std::vector<const OptionSpec*> options() const = 0;

    /// Gives an option's value.
    /// @param id Its id.
    /// @return The value; its `initial` value if never changed or saved damaged; false if there's no such
    /// option.
    virtual OptionValue value(std::string_view id) const = 0;

    /// Changes an option's value, for every pane. Sends ViewOptionChanged if it changed.
    /// @param id Its id.
    /// @param value The new value, of the option's kind (mm for a Length).
    /// @return A plain problem if the value is the wrong kind, outside the limits or not one of the choices.
    virtual Diagnostics set(std::string_view id, const OptionValue& value) = 0;

    /// Asks the student to type a value (the "Other..." menu item). Only in the
    /// desktop app; does nothing elsewhere.
    /// @param id A Length or Number option's id.
    virtual void ask(std::string_view id) = 0;

    /// Gives a Flag option's value.
    /// @param id Its id.
    /// @return True if it's on.
    bool flag(std::string_view id) const {
        const auto v = value(id);
        const auto* b = std::get_if<bool>(&v);
        return b && *b;
    }

    /// Gives a Length or Number option's value.
    /// @param id Its id.
    /// @return The number (mm for a Length), or 0.
    double number(std::string_view id) const {
        const auto v = value(id);
        const auto* d = std::get_if<double>(&v);
        return d ? *d : 0.0;
    }

    /// Gives a Choice option's value.
    /// @param id Its id.
    /// @return The chosen item's `value`, or an empty string.
    std::string choice(std::string_view id) const {
        const auto v = value(id);
        const auto* s = std::get_if<std::string>(&v);
        return s ? *s : std::string();
    }
};

/// Writes a number the way the menus show it: "10", "12.5", with "." on every computer.
/// @param value The number.
/// @return It as short text.
inline std::string numberText(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << value;
    return out.str();
}

/// Declares an option and adds its View menu items, which belong to your plugin:
/// a check mark for a Flag, a submenu for a Choice, and a submenu of presets
/// plus "Other..." for a Length or Number. Call it while registering.
/// @param ctx Your plugin's context.
/// @param options The view options service.
/// @param option The option; its id must start with your plugin's id and a dot.
/// @return Problems; nothing if the option was added.
inline Diagnostics addOption(plugin::PluginContext& ctx, IViewOptions& options, OptionSpec option) {
    const std::string prefix = ctx.manifest().id + ".";
    if (option.id.rfind(prefix, 0) != 0) {
        return {{"viewoptions.id", Severity::Error,
                 "The view option " + option.id + " must start with \"" + prefix + "\".", option.id}};
    }
    if (auto problems = options.add(option); !problems.empty()) return problems;
    IViewOptions* o = &options;
    const std::string id = option.id;
    switch (option.kind) {
        case OptionKind::Flag:
            ctx.addAction({.id = id,
                           .text = option.text,
                           .menu = "View",
                           .shortcut = option.shortcut,
                           .tip = option.tip,
                           .run = [o, id] { o->set(id, !o->flag(id)); },
                           .checked = [o, id] { return o->flag(id); }});
            break;
        case OptionKind::Choice:
            for (const auto& c : option.choices) {
                const std::string value = c.value;
                ctx.addAction({.id = id + "." + value,
                               .text = c.text,
                               .menu = "View/" + option.text,
                               .tip = option.tip,
                               .run = [o, id, value] { o->set(id, value); },
                               .checked = [o, id, value] { return o->choice(id) == value; }});
            }
            break;
        case OptionKind::Length:
        case OptionKind::Number: {
            const std::string unit = option.kind == OptionKind::Length ? " mm" : "";
            for (double p : option.presets) {
                std::string name = numberText(p);
                for (auto& ch : name) {
                    if (ch == '.' || ch == '-') ch = '_';  // ids can't have more dots
                }
                ctx.addAction({.id = id + "." + name,
                               .text = numberText(p) + unit,
                               .menu = "View/" + option.text,
                               .tip = option.tip,
                               .run = [o, id, p] { o->set(id, p); },
                               .checked = [o, id, p] { return o->number(id) == p; }});
            }
            const auto presets = option.presets;
            ctx.addAction({.id = id + ".other",
                           .text = "Other...",
                           .menu = "View/" + option.text,
                           .tip = option.tip,
                           .run = [o, id] { o->ask(id); },
                           .currentText =
                               [o, id, presets, unit] {
                                   const double v = o->number(id);
                                   for (double p : presets) {
                                       if (p == v) return std::string("Other...");
                                   }
                                   return "Other (" + numberText(v) + unit + ")...";
                               }});
            break;
        }
    }
    return {};
}

}  // namespace kh::viewoptions
