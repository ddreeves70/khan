// The Hello plugin: adds one command that makes a cube, and notices every change.
// Walked through step by step in docs/plugin-guide/hello-plugin.md.

#include <memory>

#include "sb/model/Commands.h"
#include "sb/model/Document.h"
#include "sb/plugin/Api.h"
#include "sb/plugin/Events.h"
#include "sb/plugin/PluginContext.h"

using namespace sb;

namespace {

// Builds the command when someone runs "example.hello.add_cube".
std::unique_ptr<model::Command> makeAddCube(const model::Document& doc, const model::Args& args) {
    const double size = model::argNumber(args, "size", 20.0);  // mm
    auto cube = std::make_unique<model::BoxFeature>(doc.uniqueName("Cube"), kernel::Vec3{0, 0, 0},
                                                    kernel::Vec3{size, size, size});
    return std::make_unique<model::AddFeatureCommand>(std::move(cube));
}

void registerHello(plugin::PluginContext& ctx) {
    // 1. A command: name, label, description, and its arguments.
    ctx.addCommand(
        {"example.hello.add_cube",
         "Add cube",
         "Adds a cube at the origin.",
         {{"size", model::ArgType::Number, "Length of each side, in mm. Default 20 mm.", false, {}}}},
        &makeAddCube);

    // 2. A menu item in the desktop app that runs the command.
    ctx.addAction({.id = "example.hello.cube",
                   .text = "Cube (Hello)",
                   .menu = "Create",
                   .command = "example.hello.add_cube",
                   .tip = "Add a 20 mm cube from the Hello plugin."});

    // 3. An event listener: runs after every change to any design.
    ctx.listen<plugin::DocumentChanged>(
        [&ctx](const plugin::DocumentChanged& e) { ctx.log("Hello saw a change: " + e.label); });
}

}  // namespace

SB_PLUGIN_ENTRY(registerHello)
