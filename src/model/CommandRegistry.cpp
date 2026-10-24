#include "kh/model/CommandRegistry.h"

#include <algorithm>
#include <utility>

#include "kh/model/Commands.h"
#include "kh/model/Document.h"

namespace kh::model {

namespace {

Diagnostic error(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

const char* typeName(ArgType t) {
    switch (t) {
        case ArgType::Number:
            return "a number";
        case ArgType::Integer:
            return "a whole number";
        case ArgType::Bool:
            return "true or false";
        case ArgType::Text:
            return "text";
        case ArgType::Vec3:
            return "three numbers (x, y, z)";
        case ArgType::FeatureRef:
            return "a step in the design";
        case ArgType::Choice:
            return "one of the listed choices";
    }
    return "a value";
}

bool matches(ArgType t, const ArgValue& v) {
    switch (t) {
        case ArgType::Number:
            return std::holds_alternative<double>(v) || std::holds_alternative<std::int64_t>(v);
        case ArgType::Integer:
            return std::holds_alternative<std::int64_t>(v);
        case ArgType::Bool:
            return std::holds_alternative<bool>(v);
        case ArgType::Text:
        case ArgType::Choice:
            return std::holds_alternative<std::string>(v);
        case ArgType::Vec3:
            return std::holds_alternative<kernel::Vec3>(v);
        case ArgType::FeatureRef:
            return std::holds_alternative<FeatureId>(v);
    }
    return false;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

CommandRegistry makeBuiltin() {
    CommandRegistry r;

    r.add({"feature.add_box",
           "Add box",
           "Adds a rectangular block from a corner point and a size.",
           {{"size", ArgType::Vec3, "Width, depth and height in mm.", true, {}},
            {"corner", ArgType::Vec3, "Corner point in mm. Default 0, 0, 0.", false, {}},
            {"name", ArgType::Text, "Step name. Default Box1, Box2, ...", false, {}}}},
          [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<AddFeatureCommand>(std::make_unique<BoxFeature>(
                  argText(a, "name", doc.uniqueName("Box")), argVec3(a, "corner"), argVec3(a, "size")));
          });

    r.add({"feature.add_cylinder",
           "Add cylinder",
           "Adds a cylinder from its base center, axis, radius and height.",
           {{"radius", ArgType::Number, "Radius in mm.", true, {}},
            {"height", ArgType::Number, "Height in mm.", true, {}},
            {"base", ArgType::Vec3, "Center of the base in mm. Default 0, 0, 0.", false, {}},
            {"axis", ArgType::Vec3, "Direction of the axis. Default 0, 0, 1 (up).", false, {}},
            {"name", ArgType::Text, "Step name. Default Cylinder1, Cylinder2, ...", false, {}}}},
          [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<AddFeatureCommand>(std::make_unique<CylinderFeature>(
                  argText(a, "name", doc.uniqueName("Cylinder")), argVec3(a, "base"),
                  argVec3(a, "axis", {0, 0, 1}), argNumber(a, "radius"), argNumber(a, "height")));
          });

    r.add(
        {"feature.add_boolean",
         "Combine bodies",
         "Joins two bodies (union), removes one from another (cut) or keeps only their overlap (intersect).",
         {{"operation", ArgType::Choice, "union, cut or intersect.", true, {"union", "cut", "intersect"}},
          {"a", ArgType::FeatureRef, "The first body (for cut, the one that keeps its material).", true, {}},
          {"b", ArgType::FeatureRef, "The second body (for cut, the tool that is removed).", true, {}},
          {"name", ArgType::Text, "Step name. Default Union1, Cut1 or Intersect1.", false, {}}}},
        [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
            const auto opName = argText(a, "operation");
            const auto op = opName == "union" ? kernel::BoolOp::Union
                            : opName == "cut" ? kernel::BoolOp::Cut
                                              : kernel::BoolOp::Intersect;
            const std::string base = opName == "union" ? "Union" : opName == "cut" ? "Cut" : "Intersect";
            return std::make_unique<AddFeatureCommand>(std::make_unique<BooleanFeature>(
                argText(a, "name", doc.uniqueName(base)), op, argFeature(a, "a"), argFeature(a, "b")));
        });

    r.add({"feature.delete",
           "Delete step",
           "Deletes a step that no other step uses.",
           {{"feature", ArgType::FeatureRef, "The step to delete.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<DeleteFeatureCommand>(argFeature(a, "feature"));
          });

    r.add({"feature.rename",
           "Rename step",
           "Gives a step a new name.",
           {{"feature", ArgType::FeatureRef, "The step to rename.", true, {}},
            {"name", ArgType::Text, "The new name.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<RenameFeatureCommand>(argFeature(a, "feature"), argText(a, "name"));
          });

    r.add({"feature.set_material",
           "Set material",
           "Picks what a step's body is made of, for its mass, weight, cost and color.",
           {{"feature", ArgType::FeatureRef, "The step.", true, {}},
            {"material",
             ArgType::Text,
             "A material's name, like PLA or \"Aluminum 6061\" (see kh_cli materials). Leave it empty to "
             "use the material of the step it was made from (or PLA).",
             true,
             {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<SetMaterialCommand>(argFeature(a, "feature"), argText(a, "material"));
          });

    r.add({"material.define",
           "Define material",
           "Adds a material of your own to the design, or changes one you added.",
           {{"name", ArgType::Text, "The material's name, like \"Blue PETG\".", true, {}},
            {"density", ArgType::Number, "Density in g/cm³ (water is 1).", true, {}},
            {"color", ArgType::Text, "Color in the viewer, as #rrggbb. Default #9eb2cc.", false, {}},
            {"youngs_modulus",
             ArgType::Number,
             "Stiffness (Young's modulus) in GPa. Default 0 (unknown).",
             false,
             {}},
            {"poissons_ratio", ArgType::Number, "Poisson's ratio, 0 to 0.5. Default 0 (unknown).", false, {}},
            {"yield_strength", ArgType::Number, "Yield strength in MPa. Default 0 (unknown).", false, {}},
            {"cost_per_kg", ArgType::Number, "Price per kilogram. Default 0 (unknown).", false, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              Material m;
              m.name = argText(a, "name");
              m.density = argNumber(a, "density");
              const auto color = argText(a, "color", "#9eb2cc");
              m.color = parseColor(color).value_or(Color{});
              m.youngsModulus = argNumber(a, "youngs_modulus");
              m.poissonsRatio = argNumber(a, "poissons_ratio");
              m.yieldStrength = argNumber(a, "yield_strength");
              m.costPerKg = argNumber(a, "cost_per_kg");
              return std::make_unique<DefineMaterialCommand>(std::move(m),
                                                             parseColor(color) ? std::string() : color);
          });

    r.add({"material.delete",
           "Delete material",
           "Removes a material you added to the design. No step may be using it.",
           {{"name", ArgType::Text, "The material's name.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<DeleteMaterialCommand>(argText(a, "name"));
          });

    return r;
}

}  // namespace

void CommandRegistry::add(CommandInfo info, CommandFactory factory) {
    auto name = info.name;
    entries_.insert_or_assign(std::move(name), Entry{std::move(info), std::move(factory)});
}

const CommandInfo* CommandRegistry::info(std::string_view name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second.info;
}

std::vector<const CommandInfo*> CommandRegistry::commands() const {
    std::vector<const CommandInfo*> out;
    for (const auto& [name, entry] : entries_) out.push_back(&entry.info);
    return out;  // std::map keeps them sorted by name
}

const CommandFactory* CommandRegistry::factory(std::string_view name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second.factory;
}

CreateResult CommandRegistry::create(std::string_view name, const Args& args, const Document& doc) const {
    CreateResult result;
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        result.issues.push_back(
            error("command.unknown", "There is no command called " + std::string(name) + "."));
        return result;
    }
    const auto& info = it->second.info;

    for (const auto& [argName, value] : args) {
        const auto spec = std::find_if(info.args.begin(), info.args.end(),
                                       [&](const ArgSpec& s) { return s.name == argName; });
        if (spec == info.args.end()) {
            result.issues.push_back(
                error("command.arg_unknown", info.label + " has no setting called " + argName + "."));
            continue;
        }
        if (!matches(spec->type, value)) {
            result.issues.push_back(error("command.arg_type", argName + " for " + info.label + " must be " +
                                                                  typeName(spec->type) + "."));
            continue;
        }
        if (spec->type == ArgType::Choice) {
            const auto& s = std::get<std::string>(value);
            if (std::find(spec->choices.begin(), spec->choices.end(), s) == spec->choices.end()) {
                result.issues.push_back(
                    error("command.arg_choice", argName + " must be one of: " + joined(spec->choices) + "."));
            }
        }
        if (spec->type == ArgType::FeatureRef && !doc.find(std::get<FeatureId>(value))) {
            result.issues.push_back(
                error("command.arg_feature", argName + " points to a step that isn't in the design."));
        }
    }
    for (const auto& spec : info.args) {
        if (spec.required && !args.count(spec.name)) {
            result.issues.push_back(
                error("command.arg_missing", info.label + " needs " + spec.name + ": " + spec.description));
        }
    }
    if (!result.issues.empty()) return result;

    result.command = it->second.factory(doc, args);
    return result;
}

const CommandRegistry& CommandRegistry::builtin() {
    static const CommandRegistry registry = makeBuiltin();
    return registry;
}

double argNumber(const Args& args, const std::string& name, double fallback) {
    auto it = args.find(name);
    if (it == args.end()) return fallback;
    if (const auto* d = std::get_if<double>(&it->second)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&it->second)) return static_cast<double>(*i);
    return fallback;
}

std::string argText(const Args& args, const std::string& name, const std::string& fallback) {
    auto it = args.find(name);
    if (it == args.end()) return fallback;
    if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    return fallback;
}

kernel::Vec3 argVec3(const Args& args, const std::string& name, const kernel::Vec3& fallback) {
    auto it = args.find(name);
    if (it == args.end()) return fallback;
    if (const auto* v = std::get_if<kernel::Vec3>(&it->second)) return *v;
    return fallback;
}

FeatureId argFeature(const Args& args, const std::string& name) {
    auto it = args.find(name);
    if (it == args.end()) return {};
    if (const auto* id = std::get_if<FeatureId>(&it->second)) return *id;
    return {};
}

}  // namespace kh::model
