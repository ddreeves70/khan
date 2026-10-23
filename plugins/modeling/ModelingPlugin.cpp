#include "kh/plugins/Modeling.h"

#include <string>

#include "kh/kernel/Types.h"
#include "kh/model/CommandRegistry.h"
#include "kh/plugin/Actions.h"

// kh.modeling adds Khan's modeling commands through the same PluginContext an
// outside plugin uses. The commands themselves are still defined in kh_model
// (CommandRegistry::builtin()); they move here when feature types become an
// extension point, because saving and opening them lives in kh_io for now.

namespace kh::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "kh.modeling",
  "name": "Modeling",
  "version": "0.1.0",
  "apiVersion": 2,
  "description": "Boxes, cylinders and Booleans, plus deleting and renaming steps."
})";

void registerModeling(plugin::PluginContext& ctx) {
    const auto& builtin = model::CommandRegistry::builtin();
    for (const auto* info : builtin.commands()) {
        ctx.addCommand(*info, *builtin.factory(info->name));
    }

    // Menu items and toolbar buttons. New shapes start at a sensible size; the
    // student changes the numbers in the Properties panel.
    using plugin::ActionSpec;
    ctx.addAction(ActionSpec{.id = "kh.modeling.box",
                             .text = "Box",
                             .menu = "Create",
                             .command = "feature.add_box",
                             .args = {{"size", kernel::Vec3{20, 20, 20}}},
                             .shortcut = "Ctrl+Shift+B",
                             .toolbar = true,
                             .tip = "Add a 20 mm box. Change its size in Properties."});
    ctx.addAction(ActionSpec{.id = "kh.modeling.cylinder",
                             .text = "Cylinder",
                             .menu = "Create",
                             .command = "feature.add_cylinder",
                             .args = {{"radius", 10.0}, {"height", 20.0}},
                             .shortcut = "Ctrl+Shift+C",
                             .toolbar = true,
                             .tip = "Add a cylinder 20 mm across and 20 mm tall. Change it in Properties."});
    struct Combine {
        const char* id;
        const char* text;
        const char* op;
        const char* tip;
    };
    for (const auto& c :
         {Combine{"kh.modeling.union", "Union", "union", "Join the two selected bodies into one."},
          Combine{"kh.modeling.cut", "Cut", "cut", "Cut the second selected body out of the first."},
          Combine{"kh.modeling.intersect", "Intersect", "intersect",
                  "Keep only where the two selected bodies overlap."}}) {
        ctx.addAction(ActionSpec{.id = c.id,
                                 .text = c.text,
                                 .menu = "Combine",
                                 .command = "feature.add_boolean",
                                 .args = {{"operation", std::string(c.op)}},
                                 .fromSelection = {"a", "b"},
                                 .toolbar = true,
                                 .tip = c.tip});
    }
    ctx.addAction(ActionSpec{.id = "kh.modeling.delete",
                             .text = "Delete step",
                             .menu = "Edit",
                             .command = "feature.delete",
                             .fromSelection = {"feature"},
                             .shortcut = "Delete",
                             .tip = "Delete the selected step."});
}

}  // namespace

plugin::BuiltinPlugin modelingPlugin() {
    return {kManifest, &registerModeling};
}

}  // namespace kh::plugins
