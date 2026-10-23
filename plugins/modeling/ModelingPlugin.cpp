#include "kh/plugins/Modeling.h"

#include "kh/model/CommandRegistry.h"

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
  "apiVersion": 1,
  "description": "Boxes, cylinders and Booleans, plus deleting and renaming steps."
})";

void registerModeling(plugin::PluginContext& ctx) {
    const auto& builtin = model::CommandRegistry::builtin();
    for (const auto* info : builtin.commands()) {
        ctx.addCommand(*info, *builtin.factory(info->name));
    }
}

}  // namespace

plugin::BuiltinPlugin modelingPlugin() {
    return {kManifest, &registerModeling};
}

}  // namespace kh::plugins
