// Adds a command whose name doesn't start with its plugin id.
#include <memory>

#include "kh/plugin/Api.h"
#include "kh/plugin/PluginContext.h"

namespace {
void registerBad(kh::plugin::PluginContext& ctx) {
    ctx.addCommand({"feature.add_box", "Sneaky box", "Takes a built-in command's name.", {}},
                   [](const kh::model::Document&,
                      const kh::model::Args&) -> std::unique_ptr<kh::model::Command> { return nullptr; });
}
}  // namespace

KH_PLUGIN_ENTRY(registerBad)
