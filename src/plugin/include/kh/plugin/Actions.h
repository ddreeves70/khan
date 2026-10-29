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
    /// Shorter words for the toolbar button, e.g. "Sketch" for "New sketch on XY
    /// (the ground)". Empty means the button uses `text`.
    std::string buttonText{};
    /// Which toolbar mode the item belongs to (see PluginContext::setToolbarMode()).
    /// Empty: the usual toolbar, shown when no mode is on. A mode name, e.g.
    /// "kh.sketcher.editing": its button shows only while that mode is on, and its
    /// shortcut wins over any other item's with the same keys while the mode is on
    /// (the other item keeps its keys the rest of the time). The menu item is
    /// always there either way.
    std::string toolbarMode{};
    /// A picture for the toolbar button and menu item: SVG text, drawn in a 24 by 24
    /// box (`viewBox="0 0 24 24"`). Write `currentColor` for lines that should follow
    /// the theme (dark on a light theme, light on a dark one). Empty, or text that
    /// isn't SVG Khan can draw: words only. See "Icons" in the plugin guide.
    std::string icon{};
    std::string tip{};  ///< One plain sentence shown when the mouse rests on it.
    /// Runs the plugin's own code instead of a command, for things that aren't
    /// changes to a design (a setting, a dialog). Give either `command` or `run`.
    std::function<void()> run{};
    /// If set, the item has a check mark, shown when this returns true. Khan asks
    /// when the menu opens and after every change (SettingChanged, design changes).
    std::function<bool()> checked{};
    /// If set, the item is enabled only while this returns true (on top of the
    /// rules for `fromSelection`), and greyed out otherwise. Khan asks when a menu
    /// opens, after design, selection and setting changes, and when a plugin sends
    /// ActionsChanged. Without it, an item that runs your code is always enabled.
    std::function<bool()> enabled{};
    /// If set, the words on the item come from this instead of `text`, asked at
    /// the same times as `checked`, e.g. "Other (12.5 mm)...".
    std::function<std::string()> currentText{};
    std::string plugin{};  ///< Set by Khan: the id of the plugin that added it.
};

}  // namespace kh::plugin
