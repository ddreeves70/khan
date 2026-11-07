#include "sb/plugins/Modeling.h"

#include <string>
#include <string_view>

#include "ModelingIcons.h"
#include "sb/kernel/Types.h"
#include "sb/model/CommandRegistry.h"
#include "sb/plugin/Actions.h"

// sb.modeling adds SketchBillet's modeling commands through the same PluginContext an
// outside plugin uses. The commands themselves are still defined in sb_model
// (CommandRegistry::builtin()); they move here when feature types become an
// extension point, because saving and opening them lives in sb_io for now.

namespace sb::plugins {

namespace {

constexpr const char* kManifest = R"({
  "id": "sb.modeling",
  "name": "Modeling",
  "version": "0.1.0",
  "description": "Boxes, cylinders and Booleans, plus deleting and renaming steps.",
  "tier": "foundation"
})";

void registerModeling(plugin::PluginContext& ctx) {
    const auto& builtin = model::CommandRegistry::builtin();
    for (const auto* info : builtin.commands()) {
        ctx.addCommand(*info, *builtin.factory(info->name));
    }

    // Menu items and toolbar buttons. New shapes start at a sensible size; the
    // student changes the numbers in the Properties panel.
    using plugin::ActionSpec;
    ctx.addAction(ActionSpec{.id = "sb.modeling.box",
                             .text = "Box",
                             .menu = "Create",
                             .command = "feature.add_box",
                             .args = {{"size", kernel::Vec3{20, 20, 20}}},
                             .shortcut = "Ctrl+Shift+B",
                             .toolbar = true,
                             .icon = std::string(icons::modeling::box_svg),
                             .tip = "Add a 20 mm box. Change its size in Properties."});
    ctx.addAction(ActionSpec{.id = "sb.modeling.cylinder",
                             .text = "Cylinder",
                             .menu = "Create",
                             .command = "feature.add_cylinder",
                             .args = {{"radius", 10.0}, {"height", 20.0}},
                             .shortcut = "Ctrl+Shift+C",
                             .toolbar = true,
                             .icon = std::string(icons::modeling::cylinder_svg),
                             .tip = "Add a cylinder 20 mm across and 20 mm tall. Change it in Properties."});
    struct Combine {
        const char* id;
        const char* text;
        const char* op;
        const char* tip;
        std::string_view icon;
    };
    for (const auto& c :
         {Combine{"sb.modeling.union", "Union", "union", "Join the two selected bodies into one.",
                  icons::modeling::union_svg},
          Combine{"sb.modeling.cut", "Cut", "cut", "Cut the second selected body out of the first.",
                  icons::modeling::cut_svg},
          Combine{"sb.modeling.intersect", "Intersect", "intersect",
                  "Keep only where the two selected bodies overlap.", icons::modeling::intersect_svg}}) {
        ctx.addAction(ActionSpec{.id = c.id,
                                 .text = c.text,
                                 .menu = "Combine",
                                 .command = "feature.add_boolean",
                                 .args = {{"operation", std::string(c.op)}},
                                 .fromSelection = {"a", "b"},
                                 .toolbar = true,
                                 .icon = std::string(c.icon),
                                 .tip = c.tip});
    }
    ctx.addAction(ActionSpec{.id = "sb.modeling.move",
                             .text = "Move",
                             .menu = "Modify",
                             .command = "feature.add_move",
                             .fromSelection = {"body"},
                             .shortcut = "Ctrl+M",
                             .toolbar = true,
                             .icon = std::string(icons::modeling::move_svg),
                             .tip = "Move or turn the picked body. Then type how far in Properties."});
    // Extrude: the one sketch picked, swept 10 mm out of its plane.
    ctx.addAction(ActionSpec{
        .id = "sb.modeling.extrude",
        .text = "Extrude",
        .menu = "Create",
        .command = "feature.add_extrude",
        .args = {{"distance", 10.0}},
        .fromSelection = {"sketch"},
        .shortcut = "Ctrl+Shift+E",
        .toolbar = true,
        .icon = std::string(icons::modeling::extrude_svg),
        .tip = "Make a 10 mm solid from the picked sketch's closed shapes. Change how far in "
               "Properties.",
        .enabled = [&ctx] {
            const auto& sel = ctx.selection();
            const auto* doc = ctx.documents().find(sel.doc());
            const auto* f = doc && sel.features().size() == 1 ? doc->find(sel.features()[0]) : nullptr;
            return f && f->type() == model::FeatureType::Sketch;
        }});
    // Fillet and Chamfer: the picked edges of one body. With none picked, the
    // first click starts picking edges.
    ctx.addAction(ActionSpec{
        .id = "sb.modeling.fillet",
        .text = "Fillet",
        .menu = "Modify",
        .command = "feature.add_fillet",
        .args = {{"radius", 1.0}},
        .fromEdges = "edges",
        .fromEdgesBody = "body",
        .toolbar = true,
        .icon = std::string(icons::modeling::fillet_svg),
        .tip = "Round the picked edges by 1 mm. Change the radius in Properties. With no edges picked, "
               "click the edges first."});
    ctx.addAction(ActionSpec{
        .id = "sb.modeling.chamfer",
        .text = "Chamfer",
        .menu = "Modify",
        .command = "feature.add_chamfer",
        .args = {{"distance", 1.0}},
        .fromEdges = "edges",
        .fromEdgesBody = "body",
        .toolbar = true,
        .icon = std::string(icons::modeling::chamfer_svg),
        .tip = "Bevel the picked edges by 1 mm. Change the distance in Properties. With no edges picked, "
               "click the edges first."});
    // What clicking a body picks: the body (the usual) or one of its edges.
    ctx.addAction(ActionSpec{
        .id = "sb.modeling.pick_edges",
        .text = "Pick edges",
        .menu = "Edit",
        .tip = "Click edges of bodies instead of whole bodies, for a fillet or chamfer.",
        .run =
            [&ctx] {
                auto& sel = ctx.selection();
                sel.setPickMode(sel.pickMode() == plugin::PickMode::Edges ? plugin::PickMode::Bodies
                                                                          : plugin::PickMode::Edges);
            },
        .checked = [&ctx] { return ctx.selection().pickMode() == plugin::PickMode::Edges; }});
    ctx.addAction(ActionSpec{.id = "sb.modeling.duplicate",
                             .text = "Duplicate step",
                             .menu = "Edit",
                             .command = "feature.duplicate",
                             .fromSelection = {"feature"},
                             .shortcut = "Ctrl+D",
                             .tip = "Copy the selected step, and the steps it uses, as new steps."});
    ctx.addAction(ActionSpec{.id = "sb.modeling.turn_off",
                             .text = "Turn off",
                             .menu = "Edit",
                             .command = "feature.suppress",
                             .args = {{"off", true}},
                             .fromSelectionList = "steps",
                             .tip = "Turn the picked steps off, so they aren't built. Nothing is deleted."});
    ctx.addAction(ActionSpec{.id = "sb.modeling.turn_on",
                             .text = "Turn on",
                             .menu = "Edit",
                             .command = "feature.suppress",
                             .args = {{"off", false}},
                             .fromSelectionList = "steps",
                             .tip = "Turn the picked steps back on."});
    ctx.addAction(ActionSpec{.id = "sb.modeling.new_folder",
                             .text = "New folder",
                             .menu = "Edit",
                             .command = "folder.add",
                             .fromSelectionList = "steps",
                             .tip = "Put the picked steps in a folder, to keep the feature tree tidy."});
    ctx.addAction(ActionSpec{.id = "sb.modeling.delete",
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

}  // namespace sb::plugins
