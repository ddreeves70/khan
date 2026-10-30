#include "kh/model/CommandRegistry.h"

#include <algorithm>
#include <cctype>
#include <numbers>
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
        case ArgType::Vec2:
            return "two numbers (x, y)";
        case ArgType::Vec2List:
            return "points (x, y), at least one";
        case ArgType::FeatureRef:
            return "a step in the design";
        case ArgType::FeatureList:
            return "one or more steps in the design";
        case ArgType::FolderRef:
            return "a folder in the design";
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
        case ArgType::Vec2:
            return std::holds_alternative<sketch::Point2>(v);
        case ArgType::Vec2List:
            return std::holds_alternative<std::vector<sketch::Point2>>(v);
        case ArgType::FeatureRef:
            return std::holds_alternative<FeatureId>(v);
        case ArgType::FeatureList:
            return std::holds_alternative<std::vector<FeatureId>>(v);
        case ArgType::FolderRef:
            return std::holds_alternative<FolderId>(v);
    }
    return false;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

// ---------------------------------------------------------------------------
// Sketch commands: helpers
// ---------------------------------------------------------------------------

constexpr double kDegrees = std::numbers::pi / 180.0;

// What a constraint kind needs, in order: 'P' a point, 'E' an entity; and an example.
struct Needs {
    const char* slots;
    const char* what;
    const char* example;
};

Needs needsOf(sketch::ConstraintKind k) {
    using K = sketch::ConstraintKind;
    switch (k) {
        case K::Fix:
            return {"P", "one point", "Line1.start"};
        case K::Coincident:
            return {"PP", "two points", "Line1.end,Line2.start"};
        case K::Horizontal:
        case K::Vertical:
        case K::Length:
            return {"E", "one line", "Line1"};
        case K::Parallel:
        case K::Perpendicular:
        case K::Angle:
            return {"EE", "two lines", "Line1,Line2"};
        case K::Equal:
            return {"EE", "two lines, or two circles or arcs", "Line1,Line2"};
        case K::PointOnLine:
            return {"PE", "a point and a line", "Point1,Line1"};
        case K::PointOnCircle:
            return {"PE", "a point and a circle or arc", "Line1.end,Circle1"};
        case K::Midpoint:
            return {"PE", "a point and a line", "Point1,Line1"};
        case K::Tangent:
            return {"EE", "a line and a circle or arc, or two circles or arcs", "Line1,Arc1"};
        case K::Distance:
        case K::HorizontalDistance:
        case K::VerticalDistance:
            return {"PP", "two points", "Line1.start,Line1.end"};
        case K::Radius:
        case K::Diameter:
            return {"E", "one circle or arc", "Circle1"};
    }
    return {"", "", ""};
}

Diagnostic sketchProblem(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

// Reads "Line1.end,Line2.start" or "Line1,Arc1" into a constraint's points and entities.
Diagnostics readOn(const sketch::Sketch& s, sketch::Constraint& c, const std::string& on) {
    const Needs needs = needsOf(c.kind);
    const std::string kind(sketch::kindName(c.kind));
    const std::string like = kind + " needs " + needs.what + ", like on=" + needs.example + ".";
    std::vector<std::string> items;
    std::size_t from = 0;
    while (true) {
        const auto comma = on.find(',', from);
        items.push_back(on.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    const std::string_view slots = needs.slots;
    if (on.empty() || items.size() != slots.size()) return {sketchProblem("sketch.on", like)};
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const auto dot = item.rfind('.');
        const std::string name = dot == std::string::npos ? item : item.substr(0, dot);
        const std::string roleText = dot == std::string::npos ? std::string() : item.substr(dot + 1);
        const sketch::Entity* e = s.findEntity(name);
        if (!e) {
            return {
                sketchProblem("sketch.on", "There's nothing called " + name + " in this sketch. " + like)};
        }
        const auto point = sketch::pointFromKey(e->id, roleText);
        if (!point) {
            return {sketchProblem("sketch.on",
                                  item +
                                      " isn't a point: use .start, .end, .center, or .pole1, .pole2, ... for "
                                      "a spline's control points. " +
                                      like)};
        }
        if (const auto* sp = std::get_if<sketch::SplineShape>(&e->shape);
            sp && point->role == sketch::PointRole::Pole &&
            point->index >= static_cast<int>(sp->poles.size())) {
            return {sketchProblem("sketch.on", name + " has " + std::to_string(sp->poles.size()) +
                                                   " control points: use " + name + ".pole1 to " + name +
                                                   ".pole" + std::to_string(sp->poles.size()) + ". " + like)};
        }
        const auto role = std::optional<sketch::PointRole>(point->role);
        if (slots[i] == 'E') {
            if (*role != sketch::PointRole::Self) {
                return {
                    sketchProblem("sketch.on", "Use " + name + " here, without ." + roleText + ". " + like)};
            }
            c.entities.push_back(e->id);
        } else {
            if (*role == sketch::PointRole::Self && !std::holds_alternative<sketch::PointShape>(e->shape)) {
                return {sketchProblem("sketch.on",
                                      name + " isn't a single point: say which of its points, like " + name +
                                          ".start or " + name + ".end.")};
            }
            c.points.push_back(*point);
        }
    }
    return {};
}

// A constraint's default name: "Horizontal distance" becomes "HorizontalDistance1".
std::string constraintBase(sketch::ConstraintKind k) {
    std::string base;
    bool upper = true;
    for (char ch : sketch::kindName(k)) {
        if (ch == ' ') {
            upper = true;
            continue;
        }
        base += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
        upper = false;
    }
    return base;
}

// Adds an entity to a sketch, named and checked, as an EditSketchCommand.
std::unique_ptr<Command> addEntity(FeatureId sketchId, sketch::Shape shape, bool construction,
                                   std::string name, std::string base) {
    const EntityId id = EntityId::generate();
    return std::make_unique<EditSketchCommand>(
        sketchId,
        [id, shape, construction, name, base](sketch::Sketch& s, std::string& label) -> Diagnostics {
            const std::string chosen = name.empty() ? uniqueSketchName(s, base) : name;
            if (s.findEntity(chosen) || s.findConstraint(chosen)) {
                return {sketchProblem("sketch.name_taken", "Something in this sketch is already called " +
                                                               chosen + ". Pick a different name.")};
            }
            s.entities.push_back({id, chosen, shape, construction});
            label = "Add " + chosen;
            return {};
        });
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

    r.add(
        {"feature.add_move",
         "Move body",
         "Turns a body about a line, then slides it. The body keeps its size and shape.",
         {{"body", ArgType::FeatureRef, "The step whose body is moved.", true, {}},
          {"by", ArgType::Vec3, "How far to slide it along x, y and z, in mm. Default 0, 0, 0.", false, {}},
          {"turn", ArgType::Number, "How far to turn it first, in degrees. Default 0.", false, {}},
          {"axis",
           ArgType::Vec3,
           "The direction of the line it turns about. Default 0, 0, 1 (up).",
           false,
           {}},
          {"about", ArgType::Vec3, "A point on the line it turns about, in mm. Default 0, 0, 0.", false, {}},
          {"name", ArgType::Text, "Step name. Default Move1, Move2, ...", false, {}}}},
        [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
            kernel::Motion m;
            m.offset = argVec3(a, "by");
            m.angle = argNumber(a, "turn") * std::numbers::pi / 180.0;  // degrees in, radians inside
            m.axis = argVec3(a, "axis", {0, 0, 1});
            m.center = argVec3(a, "about");
            return std::make_unique<AddFeatureCommand>(std::make_unique<MoveFeature>(
                argText(a, "name", doc.uniqueName("Move")), argFeature(a, "body"), m));
        });

    r.add({"feature.add_extrude",
           "Extrude",
           "Turns every closed shape of a sketch into a solid, by sweeping it straight out of the sketch. "
           "Holes in a shape go all the way through.",
           {{"sketch", ArgType::FeatureRef, "The sketch, like Sketch1.", true, {}},
            {"distance", ArgType::Number, "How far, in mm. Default 10.", false, {}},
            {"side",
             ArgType::Choice,
             "Which way: one (out of the sketch's front: up for XY), flip (the other way) or both (half "
             "each way). Default one.",
             false,
             {"one", "flip", "both"}},
            {"name", ArgType::Text, "Step name. Default Extrude1, Extrude2, ...", false, {}}}},
          [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
              const std::string side = argText(a, "side", "one");
              const auto s = side == "flip"   ? ExtrudeSide::Flip
                             : side == "both" ? ExtrudeSide::Both
                                              : ExtrudeSide::One;
              const double distance = a.count("distance") ? argNumber(a, "distance") : 10.0;
              return std::make_unique<AddFeatureCommand>(std::make_unique<ExtrudeFeature>(
                  argText(a, "name", doc.uniqueName("Extrude")), argFeature(a, "sketch"), distance, s));
          });

    r.add({"feature.delete",
           "Delete step",
           "Deletes a step that no other step uses.",
           {{"feature", ArgType::FeatureRef, "The step to delete.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<DeleteFeatureCommand>(argFeature(a, "feature"));
          });

    r.add({"feature.duplicate",
           "Duplicate step",
           "Copies a step, and every step it uses, as new steps at the end of the design.",
           {{"feature", ArgType::FeatureRef, "The step to copy.", true, {}}}},
          [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
              const FeatureId id = argFeature(a, "feature");
              const auto* f = doc.find(id);
              return std::make_unique<AddFeaturesCommand>(
                  copySteps(doc, {id}), std::vector<Material>{},
                  "Duplicate " + (f ? f->name() : std::string("step")));
          });

    r.add({"feature.suppress",
           "Turn steps off or on",
           "Turns steps off, so they aren't built, or back on. Steps that use a step that's off are blocked "
           "until it's on again.",
           {{"steps",
             ArgType::FeatureList,
             "The steps, as names separated by commas, like Box1,Hole.",
             true,
             {}},
            {"off",
             ArgType::Bool,
             "true to turn them off, false to turn them back on. Default true.",
             false,
             {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<SuppressFeaturesCommand>(argFeatures(a, "steps"),
                                                               argBool(a, "off", true));
          });

    r.add({"folder.add",
           "New folder",
           "Puts steps that are next to each other in the feature tree into a folder. Folders only keep the "
           "tree tidy; they don't change what's built.",
           {{"steps",
             ArgType::FeatureList,
             "The steps, as names separated by commas, like Hole,Slot.",
             true,
             {}},
            {"name", ArgType::Text, "The folder's name. Default Folder1, Folder2, ...", false, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<AddFolderCommand>(argFeatures(a, "steps"), argText(a, "name"));
          });

    r.add({"folder.rename",
           "Rename folder",
           "Gives a folder a new name.",
           {{"folder", ArgType::FolderRef, "The folder to rename.", true, {}},
            {"name", ArgType::Text, "Its new name. No other folder may have it.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<RenameFolderCommand>(argFolder(a, "folder"), argText(a, "name"));
          });

    r.add({"folder.delete",
           "Remove folder",
           "Removes a folder but keeps its steps.",
           {{"folder", ArgType::FolderRef, "The folder to remove.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<DeleteFolderCommand>(argFolder(a, "folder"));
          });

    // --- Sketches ------------------------------------------------------------

    r.add({"feature.add_sketch",
           "Add sketch",
           "Adds an empty sketch on a plane. Draw in it with sketch.add_line and the other sketch commands.",
           {{"plane",
             ArgType::Choice,
             "xy (the ground), xz (standing, facing the front) or yz (standing, facing "
             "the right). Default xy.",
             false,
             {"xy", "xz", "yz"}},
            {"name", ArgType::Text, "Step name. Default Sketch1, Sketch2, ...", false, {}}}},
          [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
              const auto p = argText(a, "plane", "xy");
              const auto plane = p == "xz" ? SketchPlane::XZ : p == "yz" ? SketchPlane::YZ : SketchPlane::XY;
              return std::make_unique<AddFeatureCommand>(
                  std::make_unique<SketchFeature>(argText(a, "name", doc.uniqueName("Sketch")), plane));
          });

    const ArgSpec sketchArg{"sketch", ArgType::FeatureRef, "The sketch, like Sketch1.", true, {}};
    const ArgSpec constructionArg{"construction",
                                  ArgType::Bool,
                                  "true for helper geometry that isn't part of the shape. Default false.",
                                  false,
                                  {}};

    r.add({"sketch.add_point",
           "Add point",
           "Adds a point to a sketch.",
           {sketchArg,
            {"at", ArgType::Vec2, "Where, in mm on the sketch's plane, like 10,5.", true, {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Point1, Point2, ...", false, {}}}},
          [](const Document&, const Args& a) {
              return addEntity(argFeature(a, "sketch"), sketch::PointShape{argVec2(a, "at")},
                               argBool(a, "construction", false), argText(a, "name"), "Point");
          });

    r.add({"sketch.add_line",
           "Add line",
           "Adds a straight line to a sketch.",
           {sketchArg,
            {"from", ArgType::Vec2, "Its first end, in mm, like 0,0.", true, {}},
            {"to", ArgType::Vec2, "Its second end, in mm, like 40,0.", true, {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Line1, Line2, ...", false, {}}}},
          [](const Document&, const Args& a) {
              return addEntity(argFeature(a, "sketch"),
                               sketch::LineShape{argVec2(a, "from"), argVec2(a, "to")},
                               argBool(a, "construction", false), argText(a, "name"), "Line");
          });

    r.add({"sketch.add_circle",
           "Add circle",
           "Adds a circle to a sketch.",
           {sketchArg,
            {"center", ArgType::Vec2, "Its center, in mm, like 20,10.", true, {}},
            {"radius", ArgType::Number, "Its radius, in mm.", true, {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Circle1, Circle2, ...", false, {}}}},
          [](const Document&, const Args& a) {
              return addEntity(argFeature(a, "sketch"),
                               sketch::CircleShape{argVec2(a, "center"), argNumber(a, "radius")},
                               argBool(a, "construction", false), argText(a, "name"), "Circle");
          });

    r.add({"sketch.add_arc",
           "Add arc",
           "Adds an arc (part of a circle) to a sketch. It goes counterclockwise from start to end.",
           {sketchArg,
            {"center", ArgType::Vec2, "The center of its circle, in mm.", true, {}},
            {"radius", ArgType::Number, "Its radius, in mm.", true, {}},
            {"start",
             ArgType::Number,
             "Where it starts, in degrees: 0 points right, 90 up. Default 0.",
             false,
             {}},
            {"end",
             ArgType::Number,
             "Where it ends, in degrees, counterclockwise from the start. Default 90.",
             false,
             {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Arc1, Arc2, ...", false, {}}}},
          [](const Document&, const Args& a) {
              return addEntity(
                  argFeature(a, "sketch"),
                  sketch::ArcShape{argVec2(a, "center"), argNumber(a, "radius"),
                                   argNumber(a, "start", 0) * kDegrees, argNumber(a, "end", 90) * kDegrees},
                  argBool(a, "construction", false), argText(a, "name"), "Arc");
          });

    r.add({"sketch.add_spline",
           "Add spline",
           "Adds a spline to a sketch: a smooth curve pulled towards its control points. An open one starts "
           "at the first point and ends at the last.",
           {sketchArg,
            {"poles",
             ArgType::Vec2List,
             "Its control points, in mm, split by /, like 0,0/0,30/40,30/40,0.",
             true,
             {}},
            {"degree",
             ArgType::Integer,
             "How smooth: 1 (straight pieces) to 5. Default 3, or less if there are only 2 or 3 points.",
             false,
             {}},
            {"closed", ArgType::Bool, "true for a closed loop with no ends. Default false.", false, {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Spline1, Spline2, ...", false, {}}}},
          [](const Document&, const Args& a) {
              auto poles = argVec2List(a, "poles");
              const int most = std::max(1, static_cast<int>(poles.size()) - 1);
              const int degree =
                  a.count("degree") ? static_cast<int>(argNumber(a, "degree")) : std::min(3, most);
              return addEntity(argFeature(a, "sketch"),
                               sketch::SplineShape{std::move(poles), {}, degree, argBool(a, "closed", false)},
                               argBool(a, "construction", false), argText(a, "name"), "Spline");
          });

    std::vector<std::string> kinds;
    for (auto k : sketch::allKinds()) kinds.emplace_back(sketch::kindKey(k));
    r.add(
        {"sketch.constrain",
         "Add constraint",
         "Adds a constraint (horizontal, tangent, ...) or a dimension (length, radius, ...) to a sketch. The "
         "sketch is solved again when the design rebuilds.",
         {sketchArg,
          {"kind", ArgType::Choice, "What to keep true.", true, kinds},
          {"on",
           ArgType::Text,
           "What it's on, separated by commas: lines and circles by name (Line1), points as Line1.start, "
           "Line1.end or Arc1.center.",
           true,
           {}},
          {"value", ArgType::Number, "For dimensions: mm, or degrees for an angle.", false, {}},
          {"name", ArgType::Text, "Its name. Default like Horizontal1 or Length1.", false, {}}}},
        [](const Document&, const Args& a) -> std::unique_ptr<Command> {
            const auto kind = *sketch::kindFromKey(argText(a, "kind"));
            const std::string on = argText(a, "on");
            const bool hasValue = a.count("value") > 0;
            const double value = argNumber(a, "value");
            const std::string name = argText(a, "name");
            const ConstraintId id = ConstraintId::generate();
            return std::make_unique<EditSketchCommand>(
                argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                    sketch::Constraint c{id, {}, kind, {}, {}, 0, {}};
                    if (auto problems = readOn(s, c, on); !problems.empty()) return problems;
                    const std::string kindText(sketch::kindName(kind));
                    if (sketch::isDimension(kind)) {
                        if (!hasValue) {
                            return {sketchProblem(
                                "sketch.value",
                                kindText + " needs a value, like value=" +
                                    (kind == sketch::ConstraintKind::Angle ? "90 (degrees)." : "40 (mm)."))};
                        }
                        c.value = kind == sketch::ConstraintKind::Angle ? value * kDegrees : value;
                    } else if (hasValue) {
                        return {sketchProblem("sketch.value", kindText + " has no number. Leave out value.")};
                    }
                    if (kind == sketch::ConstraintKind::Fix) c.at = *sketch::pointOf(s, c.points[0]);
                    c.name = name.empty() ? uniqueSketchName(s, constraintBase(kind)) : name;
                    if (s.findEntity(c.name) || s.findConstraint(c.name)) {
                        return {
                            sketchProblem("sketch.name_taken", "Something in this sketch is already called " +
                                                                   c.name + ". Pick a different name.")};
                    }
                    s.constraints.push_back(c);
                    label = "Add " + c.name;
                    return {};
                });
        });

    r.add({"sketch.set_value",
           "Change dimension",
           "Changes a dimension's number. The sketch is solved again when the design rebuilds.",
           {sketchArg,
            {"constraint", ArgType::Text, "The dimension, like Length1.", true, {}},
            {"value", ArgType::Number, "Its new number: mm, or degrees for an angle.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const std::string name = argText(a, "constraint");
              const double value = argNumber(a, "value");
              return std::make_unique<EditSketchCommand>(
                  argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                      auto it = std::find_if(s.constraints.begin(), s.constraints.end(),
                                             [&](const sketch::Constraint& c) { return c.name == name; });
                      if (it == s.constraints.end()) {
                          return {sketchProblem("sketch.not_found",
                                                "There's no constraint called " + name + " in this sketch.")};
                      }
                      if (!sketch::isDimension(it->kind)) {
                          return {sketchProblem("sketch.value", name + " has no number to change.")};
                      }
                      it->value = it->kind == sketch::ConstraintKind::Angle ? value * kDegrees : value;
                      label = "Change " + name;
                      return {};
                  });
          });

    r.add({"sketch.delete",
           "Delete from sketch",
           "Deletes a line, circle, arc or point (and the constraints on it), or a constraint.",
           {sketchArg, {"item", ArgType::Text, "What to delete, like Line1 or Length1.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const std::string name = argText(a, "item");
              return std::make_unique<EditSketchCommand>(
                  argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                      if (const auto* e = s.findEntity(name)) {
                          const EntityId id = e->id;
                          std::erase_if(s.constraints, [&](const sketch::Constraint& c) {
                              return std::ranges::find(c.entities, id) != c.entities.end() ||
                                     std::ranges::any_of(c.points,
                                                         [&](const auto& p) { return p.entity == id; });
                          });
                          std::erase_if(s.entities, [&](const sketch::Entity& x) { return x.id == id; });
                      } else if (s.findConstraint(name)) {
                          std::erase_if(s.constraints,
                                        [&](const sketch::Constraint& c) { return c.name == name; });
                      } else {
                          return {sketchProblem("sketch.not_found",
                                                "There's nothing called " + name + " in this sketch.")};
                      }
                      label = "Delete " + name;
                      return {};
                  });
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

    r.add({"document.set_units",
           "Set units",
           "Changes the units the design shows lengths in. Nothing changes size: Khan keeps every length in "
           "mm inside.",
           {{"unit",
             ArgType::Choice,
             "mm (millimeters), cm (centimeters), m (meters), in (inches) or ft (feet).",
             true,
             {"mm", "cm", "m", "in", "ft"}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              return std::make_unique<SetUnitsCommand>(
                  parseLengthUnit(argText(a, "unit")).value_or(LengthUnit::Millimeter));
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
        if (spec->type == ArgType::FolderRef && !doc.findFolder(std::get<FolderId>(value))) {
            result.issues.push_back(
                error("command.arg_folder", argName + " points to a folder that isn't in the design."));
        }
        if (spec->type == ArgType::FeatureList) {
            const auto& ids = std::get<std::vector<FeatureId>>(value);
            if (ids.empty()) {
                result.issues.push_back(error("command.arg_feature", argName + " needs at least one step."));
            } else if (std::any_of(ids.begin(), ids.end(),
                                   [&](const FeatureId& id) { return !doc.find(id); })) {
                result.issues.push_back(
                    error("command.arg_feature", argName + " points to a step that isn't in the design."));
            }
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

sketch::Point2 argVec2(const Args& args, const std::string& name, const sketch::Point2& fallback) {
    auto it = args.find(name);
    if (it == args.end()) return fallback;
    if (const auto* v = std::get_if<sketch::Point2>(&it->second)) return *v;
    return fallback;
}

std::vector<sketch::Point2> argVec2List(const Args& args, const std::string& name) {
    auto it = args.find(name);
    if (it == args.end()) return {};
    if (const auto* v = std::get_if<std::vector<sketch::Point2>>(&it->second)) return *v;
    return {};
}

FeatureId argFeature(const Args& args, const std::string& name) {
    auto it = args.find(name);
    if (it == args.end()) return {};
    if (const auto* id = std::get_if<FeatureId>(&it->second)) return *id;
    return {};
}

std::vector<FeatureId> argFeatures(const Args& args, const std::string& name) {
    auto it = args.find(name);
    if (it == args.end()) return {};
    if (const auto* ids = std::get_if<std::vector<FeatureId>>(&it->second)) return *ids;
    if (const auto* id = std::get_if<FeatureId>(&it->second)) return {*id};
    return {};
}

FolderId argFolder(const Args& args, const std::string& name) {
    auto it = args.find(name);
    if (it == args.end()) return {};
    if (const auto* id = std::get_if<FolderId>(&it->second)) return *id;
    return {};
}

bool argBool(const Args& args, const std::string& name, bool fallback) {
    auto it = args.find(name);
    if (it == args.end()) return fallback;
    if (const auto* b = std::get_if<bool>(&it->second)) return *b;
    return fallback;
}

}  // namespace kh::model
