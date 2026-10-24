#pragma once

/// @file
/// Actions: menu items and toolbar buttons that run a command by name.

#include <string>
#include <vector>

#include "kh/model/CommandRegistry.h"

namespace kh::plugin {

/// A menu item or toolbar button that runs a command.
///
/// Actions are plain data, with no Qt, so any plugin can add them and every
/// front end shows them its own way: the desktop app as menu items and toolbar
/// buttons, `kh_cli plugins` as a list.
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
    std::string id{};       ///< Stable id; an outside plugin's must start with its plugin id and a dot.
    std::string text{};     ///< The words on the menu item or button, e.g. "Box".
    std::string menu{};     ///< The menu it goes in, e.g. "Create". Made if it doesn't exist.
    std::string command{};  ///< The command it runs, e.g. "feature.add_box".
    model::Args args{};     ///< Fixed arguments for the command.
    std::vector<std::string> fromSelection{};  ///< Arguments filled from the selected steps, in order.
                                               ///< The action is only enabled when exactly this many
                                               ///< steps are selected.
    std::string shortcut{};  ///< Keyboard shortcut, e.g. "Ctrl+B" or "Delete". May be empty.
    bool toolbar = false;    ///< True to also show it as a toolbar button.
    std::string tip{};       ///< One plain sentence shown when the mouse rests on it.
    std::string plugin{};    ///< Set by Khan: the id of the plugin that added it.
};

}  // namespace kh::plugin
