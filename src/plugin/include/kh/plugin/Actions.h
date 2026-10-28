#pragma once

/// @file
/// Actions: menu items and toolbar buttons that run a command by name.

#include <functional>
#include <string>
#include <vector>

#include "kh/model/CommandRegistry.h"

namespace kh::plugin {

/// A menu item or toolbar button that runs a command, or the plugin's own code.
///
/// Actions are plain data, with no Qt, so any plugin can add them and every
/// front end shows them its own way: the desktop app as menu items and toolbar
/// buttons, `kh_cli plugins` as a list.
///
/// Changes to a design always go through a `command`, so undo works. Things
/// that aren't changes to a design, like a setting, use `run` instead, often
/// with `checked` for a check mark:
/// @code
/// ctx.addAction({.id = "example.ruler.inches", .text = "Ruler shows inches", .menu = "View",
///                .run = [&ctx] { auto& s = ctx.settings(); s.set("unit", s.text("unit", "in") == "in" ? "mm"
///                : "in"); }, .checked = [&ctx] { return ctx.settings().text("unit", "in") == "in"; }});
/// @endcode
///
/// @code
/// ctx.addAction({.id = "kh.modeling.box", .text = "Box", .menu = "Create",
///                .command = "feature.add_box", .args = {{"size", kernel::Vec3{20, 20, 20}}},
///                .toolbar = true, .tip = "Add a 20 mm box. Change its size in Properties."});
/// ctx.addAction({.id = "kh.modeling.cut", .text = "Cut", .menu = "Combine",
///                .command = "feature.add_boolean", .args = {{"operation", std::string("cut")}},
///                .fromSelection = {"a", "b"}});
/// @endcode
struct ActionSpec {
    std::string id{};    ///< Stable id; an outside plugin's must start with its plugin id and a dot.
    std::string text{};  ///< The words on the menu item or button, e.g. "Box".
    /// The menu it goes in, e.g. "Create". Made if it doesn't exist. A "/" makes a
    /// submenu: "View/Grid style" is the Grid style submenu of View.
    std::string menu{};
    std::string command{};                     ///< The command it runs, e.g. "feature.add_box".
    model::Args args{};                        ///< Fixed arguments for the command.
    std::vector<std::string> fromSelection{};  ///< Arguments filled from the selected steps, in order.
                                               ///< The action is only enabled when exactly this many
                                               ///< steps are selected.
    /// An argument filled with every selected step, in the order picked (an
    /// ArgType::FeatureList argument). The action is enabled when one or more
    /// steps are selected. Use this or `fromSelection`, not both.
    std::string fromSelectionList{};
    std::string shortcut{};  ///< Keyboard shortcut, e.g. "Ctrl+B" or "Delete". May be empty.
    bool toolbar = false;    ///< True to also show it as a toolbar button.
    std::string tip{};       ///< One plain sentence shown when the mouse rests on it.
    /// Runs the plugin's own code instead of a command, for things that aren't
    /// changes to a design (a setting, a dialog). Give either `command` or `run`.
    std::function<void()> run{};
    /// If set, the item has a check mark, shown when this returns true. Khan asks
    /// when the menu opens and after every change (SettingChanged, design changes).
    std::function<bool()> checked{};
    /// If set, the words on the item come from this instead of `text`, asked at
    /// the same times as `checked`, e.g. "Other (12.5 mm)...".
    std::function<std::string()> currentText{};
    std::string plugin{};  ///< Set by Khan: the id of the plugin that added it.
};

}  // namespace kh::plugin
