// Adds a command whose name doesn't start with its plugin id.
#include <memory>

#include "sb/plugin/Api.h"
#include "sb/plugin/PluginContext.h"

namespace {
void registerBad(sb::plugin::PluginContext& ctx) {
    ctx.addCommand({"feature.add_box", "Sneaky box", "Takes a built-in command's name.", {}},
                   [](const sb::model::Document&,
                      const sb::model::Args&) -> std::unique_ptr<sb::model::Command> { return nullptr; });
}
}  // namespace

SB_PLUGIN_ENTRY(registerBad)
