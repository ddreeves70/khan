#include "kh/model/CommandRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
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
        case K::PointOnSpline:
            return {"PE", "a point and a spline", "Point1,Spline1"};
        case K::Midpoint:
            return {"PE", "a point and a line", "Point1,Line1"};
        case K::Tangent:
            return {
                "EE",
                "a line and a circle or arc, two circles or arcs, or a spline and a curve joined to its end",
                "Line1,Arc1"};
        case K::Smooth:
            return {"EE", "a spline and a line, arc or spline joined to its end", "Spline1,Line1"};
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

// An entity by name, to change it.
sketch::Entity* entityNamed(sketch::Sketch& s, std::string_view name) {
    for (auto& e : s.entities) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

// A command that only reports problems, for arguments that can't work together.
std::unique_ptr<Command> failingCommand(FeatureId sketchId, Diagnostics problems) {
    return std::make_unique<EditSketchCommand>(
        sketchId, [problems](sketch::Sketch&, std::string&) -> Diagnostics { return problems; });
}

// Finds a spline by name, for the spline commands.
sketch::SplineShape* findSpline(sketch::Sketch& s, const std::string& name, Diagnostics& problems) {
    auto* e = entityNamed(s, name);
    if (!e) {
        problems.push_back(
            sketchProblem("sketch.not_found", "There's nothing called " + name + " in this sketch."));
        return nullptr;
    }
    auto* sp = std::get_if<sketch::SplineShape>(&e->shape);
    if (!sp)
        problems.push_back(
            sketchProblem("sketch.not_spline", name + " isn't a spline. Pick a spline, like Spline1."));
    return sp;
}

// Finds a spline's control point from "Spline1.pole2": the spline and the pole's
// index (from 0).
sketch::Entity* findPole(sketch::Sketch& s, const std::string& text, int& index, Diagnostics& problems) {
    const auto dot = text.rfind('.');
    const std::string name = text.substr(0, dot);
    auto* e = entityNamed(s, name);
    const auto* sp = e ? std::get_if<sketch::SplineShape>(&e->shape) : nullptr;
    const auto ref = e && dot != std::string::npos ? sketch::pointFromKey(e->id, text.substr(dot + 1))
                                                   : std::optional<sketch::PointRef>();
    if (!sp || !ref || ref->role != sketch::PointRole::Pole) {
        problems.push_back(sketchProblem(
            "sketch.not_found", "Name one of a spline's control points, like Spline1.pole2. There's no " +
                                    text + " in this sketch."));
        return nullptr;
    }
    if (ref->index >= static_cast<int>(sp->poles.size())) {
        const auto n = std::to_string(sp->poles.size());
        problems.push_back(sketchProblem(
            "sketch.not_found",
            name + " has " + n + " control points: use " + name + ".pole1 to " + name + ".pole" + n + "."));
        return nullptr;
    }
    index = ref->index;
    return e;
}

// A path from UTF-8 text, and back, the same on every computer.
std::filesystem::path pathFrom(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
std::string utf8Of(const std::filesystem::path& path) {
    const auto u = path.u8string();
    return std::string(u.begin(), u.end());
}

// Reads a file of points (see sketch::readPoints()).
sketch::PointsFile readPointsFile(const std::string& file, LengthUnit unit) {
    const auto path = pathFrom(file);
    const std::string name = utf8Of(path.filename());
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        sketch::PointsFile out;
        out.issues.push_back(
            sketchProblem("sketch.points_file", "Khan can't open " + name + " (looked for it at " + file +
                                                    "). Check the file is there, and try again."));
        return out;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return sketch::readPoints(text.str(), name, unit);
}

// The control points for points read from a file: the points themselves, or
// ones the curve goes through them by. Empty if there are too few for the degree.
std::vector<sketch::Point2> polesFrom(const std::vector<sketch::Point2>& points, bool through, int degree,
                                      bool closed) {
    return through ? sketch::splinePolesThrough(points, degree, closed) : points;
}

// After a control point is added at `index` (by +1) or removed from it (by -1),
// constraints on the spline's later control points follow them.
void renumberPoles(sketch::Sketch& s, const EntityId& spline, int index, int by) {
    for (auto& c : s.constraints) {
        for (auto& p : c.points) {
            if (p.entity == spline && p.role == sketch::PointRole::Pole && p.index >= index) p.index += by;
        }
    }
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

    r.add(
        {"sketch.add_rectangle",
         "Add rectangle",
         "Adds a rectangle: four lines between two opposite corners, joined at the corners and kept level "
         "and "
         "upright, so only its place and size can change.",
         {sketchArg,
          {"from", ArgType::Vec2, "One corner, in mm, like 0,0.", true, {}},
          {"to", ArgType::Vec2, "The opposite corner, in mm, like 40,20.", true, {}},
          constructionArg}},
        [](const Document&, const Args& a) -> std::unique_ptr<Command> {
            const auto from = argVec2(a, "from"), to = argVec2(a, "to");
            const bool construction = argBool(a, "construction", false);
            return std::make_unique<EditSketchCommand>(
                argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                    if (from.x == to.x || from.y == to.y) {
                        return {sketchProblem("sketch.rectangle",
                                              "A rectangle needs a width and a height: put the corners apart "
                                              "both left to right and up and down, like from=0,0 to=40,20.")};
                    }
                    const auto lines = addRectangle(s, from, to, construction);
                    label = "Add rectangle (" + s.find(lines.lines[0])->name + " to " +
                            s.find(lines.lines[3])->name + ")";
                    return {};
                });
        });

    r.add({"sketch.round_corner",
           "Round corner",
           "Rounds the corner where two lines meet: cuts both back and joins them with an arc that's tangent "
           "to "
           "both (a fillet). Things on the sharp corner move to a helper point where it was.",
           {sketchArg,
            {"at",
             ArgType::Text,
             "The corner: a line's end that's joined to another line's end, like Line1.end.",
             true,
             {}},
            {"radius", ArgType::Number, "The arc's radius, in mm.", true, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const std::string at = argText(a, "at");
              const double radius = argNumber(a, "radius");
              return std::make_unique<EditSketchCommand>(
                  argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                      const auto dot = at.rfind('.');
                      const auto* e = s.findEntity(at.substr(0, dot));
                      const auto ref = e && dot != std::string::npos
                                           ? sketch::pointFromKey(e->id, at.substr(dot + 1))
                                           : std::optional<sketch::PointRef>();
                      if (!ref) {
                          return {sketchProblem(
                              "sketch.round_corner",
                              "Name the corner by a line's end, like at=Line1.end. There's no " + at +
                                  " in this sketch.")};
                      }
                      RoundedCorner made;
                      if (auto problems = roundCorner(s, *ref, radius, &made); !problems.empty())
                          return problems;
                      label = "Round the corner at " + at + " (" + s.find(made.arc)->name + ")";
                      return {};
                  });
          });

    r.add(
        {"sketch.trim",
         "Trim",
         "Cuts away the piece of a line, circle or arc between the places nearest a point where other lines, "
         "circles and arcs cross it (or from one of them to its end). Something nothing crosses is deleted.",
         {sketchArg,
          {"item", ArgType::Text, "The line, circle or arc, like Line1.", true, {}},
          {"near",
           ArgType::Vec2,
           "A point on (or near) the piece to cut away, in mm, like 35,0.",
           true,
           {}}}},
        [](const Document&, const Args& a) -> std::unique_ptr<Command> {
            const std::string name = argText(a, "item");
            const auto near = argVec2(a, "near");
            return std::make_unique<EditSketchCommand>(
                argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                    const auto* e = s.findEntity(name);
                    if (!e) {
                        return {sketchProblem("sketch.not_found",
                                              "There's nothing called " + name + " in this sketch.")};
                    }
                    const std::size_t before = s.entities.size();
                    if (auto problems = trim(s, e->id, near); !problems.empty()) return problems;
                    label = (s.entities.size() < before ? "Delete " : "Trim ") + name;
                    return {};
                });
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
           "Adds a spline to a sketch: a smooth curve pulled towards its control points, or one through "
           "points you give. An open one starts at the first point and ends at the last.",
           {sketchArg,
            {"poles",
             ArgType::Vec2List,
             "Its control points, in mm, split by /, like 0,0/0,30/40,30/40,0. Give this or through.",
             false,
             {}},
            {"through",
             ArgType::Vec2List,
             "Points it passes through, in mm, split by /, like 0,0/20,15/40,0. Khan works out the "
             "control points (one for each). Give this or poles.",
             false,
             {}},
            {"degree",
             ArgType::Integer,
             "How smooth: 1 (straight pieces) to 5. Default 3, or less if there are only 2 or 3 points.",
             false,
             {}},
            {"closed", ArgType::Bool, "true for a closed loop with no ends. Default false.", false, {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Spline1, Spline2, ...", false, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const bool byPoles = a.count("poles") > 0, byThrough = a.count("through") > 0;
              if (byPoles == byThrough) {
                  return failingCommand(
                      argFeature(a, "sketch"),
                      {sketchProblem(
                          "sketch.spline_points",
                          "Give a spline either its control points (poles=0,0/0,30/40,30/40,0) or points it "
                          "passes through (through=0,0/20,15/40,0), not both.")});
              }
              auto points = argVec2List(a, byPoles ? "poles" : "through");
              const bool closed = argBool(a, "closed", false);
              const int most = std::max(1, static_cast<int>(points.size()) - 1);
              const int degree =
                  a.count("degree") ? static_cast<int>(argNumber(a, "degree")) : std::min(3, most);
              if (byThrough) {
                  auto poles = sketch::splinePolesThrough(points, degree, closed);
                  // Too few points or a bad degree: keep the points, so check() says what's wrong.
                  if (!poles.empty()) points = std::move(poles);
              }
              return addEntity(argFeature(a, "sketch"),
                               sketch::SplineShape{std::move(points), {}, degree, closed},
                               argBool(a, "construction", false), argText(a, "name"), "Spline");
          });

    std::vector<std::string> unitChoices;
    for (auto u : {LengthUnit::Millimeter, LengthUnit::Centimeter, LengthUnit::Meter, LengthUnit::Inch,
                   LengthUnit::Foot})
        unitChoices.emplace_back(unitSymbol(u));
    r.add({"sketch.import_spline",
           "Import spline",
           "Adds a spline made from a file of points: one point per line, x then y (a spreadsheet's CSV "
           "works). Khan "
           "remembers the file, so sketch.reload_spline can read it again after it changes.",
           {sketchArg,
            {"file", ArgType::Text, "The file of points, like wave.csv.", true, {}},
            {"through",
             ArgType::Bool,
             "true: the curve goes through every point; false: the points are its control points. Default "
             "true.",
             false,
             {}},
            {"unit", ArgType::Choice, "The unit of the file's numbers. Default mm.", false, unitChoices},
            {"degree",
             ArgType::Integer,
             "How smooth: 1 (straight pieces) to 5. Default 3, or less if there are only 2 or 3 points.",
             false,
             {}},
            {"closed", ArgType::Bool, "true for a closed loop with no ends. Default false.", false, {}},
            constructionArg,
            {"name", ArgType::Text, "Its name. Default Spline1, Spline2, ...", false, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const auto sketchId = argFeature(a, "sketch");
              const std::string file = argText(a, "file");
              const auto unit = parseLengthUnit(argText(a, "unit", "mm")).value_or(LengthUnit::Millimeter);
              auto read = readPointsFile(file, unit);
              if (!read.issues.empty()) return failingCommand(sketchId, read.issues);
              const bool through = argBool(a, "through", true);
              const bool closed = argBool(a, "closed", false);
              const int most = std::max(1, static_cast<int>(read.points.size()) - 1);
              const int degree =
                  a.count("degree") ? static_cast<int>(argNumber(a, "degree")) : std::min(3, most);
              auto poles = polesFrom(read.points, through, degree, closed);
              if (poles.empty()) poles = read.points;  // too few for the degree: check() says so
              // The whole path, so reloading finds it from anywhere.
              std::error_code ec;
              const auto whole = std::filesystem::absolute(pathFrom(file), ec);
              sketch::SplineShape shape{std::move(poles), {}, degree, closed};
              shape.source = sketch::SplineSource{utf8Of(ec ? pathFrom(file) : whole), through, unit};
              return addEntity(sketchId, std::move(shape), argBool(a, "construction", false),
                               argText(a, "name"), "Spline");
          });

    r.add({"sketch.reload_spline",
           "Reload spline from file",
           "Reads an imported spline's file of points again, so the spline follows changes made to it. "
           "Constraints "
           "on control points it doesn't have anymore are removed.",
           {sketchArg, {"spline", ArgType::Text, "The imported spline, like Spline1.", true, {}}}},
          [](const Document& doc, const Args& a) -> std::unique_ptr<Command> {
              const auto sketchId = argFeature(a, "sketch");
              const std::string name = argText(a, "spline");
              // The file is read now, so undo and redo put back exactly these points.
              const auto* f = doc.find(sketchId);
              const auto* step =
                  f && f->type() == FeatureType::Sketch ? static_cast<const SketchFeature*>(f) : nullptr;
              const auto* e = step ? step->sketch().findEntity(name) : nullptr;
              const auto* sp = e ? std::get_if<sketch::SplineShape>(&e->shape) : nullptr;
              if (step && !e) {
                  return failingCommand(
                      sketchId, {sketchProblem("sketch.not_found",
                                               "There's no spline called " + name + " in this sketch.")});
              }
              if (!step) {
                  // Not a sketch (or not there): the edit itself says so, in its words.
                  return std::make_unique<EditSketchCommand>(
                      sketchId, [](sketch::Sketch&, std::string&) { return Diagnostics{}; });
              }
              if (!sp || !sp->source) {
                  return failingCommand(
                      sketchId,
                      {sketchProblem("sketch.not_imported",
                                     name + " wasn't imported from a file, so there's nothing to reload. "
                                            "Make one from a file with sketch.import_spline.")});
              }
              const auto source = *sp->source;
              auto read = readPointsFile(source.file, source.unit);
              if (!read.issues.empty()) return failingCommand(sketchId, read.issues);
              const std::string fileName = utf8Of(pathFrom(source.file).filename());
              auto points = std::move(read.points);
              return std::make_unique<EditSketchCommand>(
                  sketchId, [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                      auto* entity = entityNamed(s, name);
                      auto* shape = entity ? std::get_if<sketch::SplineShape>(&entity->shape) : nullptr;
                      if (!shape) {
                          return {sketchProblem("sketch.not_found",
                                                "There's no spline called " + name + " in this sketch.")};
                      }
                      auto poles = polesFrom(points, source.through, shape->degree, shape->closed);
                      if (poles.empty()) {
                          return {sketchProblem("sketch.spline_poles",
                                                fileName + " has only " + std::to_string(points.size()) +
                                                    " points now, too few for " + name + "'s degree " +
                                                    std::to_string(shape->degree) +
                                                    ". Add points to the file, or lower its degree first.")};
                      }
                      const int n = static_cast<int>(poles.size());
                      shape->poles = std::move(poles);
                      if (!shape->weights.empty()) shape->weights.resize(shape->poles.size(), 1.0);
                      // Constraints on control points it no longer has go.
                      const EntityId id = entity->id;
                      std::erase_if(s.constraints, [&](const sketch::Constraint& c) {
                          return std::ranges::any_of(c.points, [&](const sketch::PointRef& p) {
                              return p.entity == id && p.role == sketch::PointRole::Pole && p.index >= n;
                          });
                      });
                      label = "Reload " + name + " from " + fileName;
                      return {};
                  });
          });

    const ArgSpec splineArg{"spline", ArgType::Text, "The spline, like Spline1.", true, {}};
    const ArgSpec poleArg{"point", ArgType::Text, "The control point, like Spline1.pole2.", true, {}};

    r.add(
        {"sketch.set_spline",
         "Change spline",
         "Changes a spline's degree, or whether it's a closed loop. Its control points stay put.",
         {sketchArg,
          splineArg,
          {"degree", ArgType::Integer, "How smooth: 1 (straight pieces) to 5.", false, {}},
          {"closed", ArgType::Bool, "true for a closed loop, false for a curve with two ends.", false, {}}}},
        [](const Document&, const Args& a) -> std::unique_ptr<Command> {
            const std::string name = argText(a, "spline");
            const std::optional<int> degree =
                a.count("degree") ? std::optional<int>(static_cast<int>(argNumber(a, "degree")))
                                  : std::nullopt;
            const std::optional<bool> closed =
                a.count("closed") ? std::optional<bool>(argBool(a, "closed", false)) : std::nullopt;
            if (!degree && !closed) {
                return failingCommand(argFeature(a, "sketch"),
                                      {sketchProblem("sketch.nothing_to_change",
                                                     "Say what to change: degree=2, closed=true, or both.")});
            }
            return std::make_unique<EditSketchCommand>(
                argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                    Diagnostics problems;
                    auto* sp = findSpline(s, name, problems);
                    if (!sp) return problems;
                    if (degree) sp->degree = *degree;
                    if (closed) sp->closed = *closed;
                    label = "Change " + name;
                    return {};
                });
        });

    r.add(
        {"sketch.set_weight",
         "Change control point weight",
         "Changes how hard one of a spline's control points pulls the curve: 1 is normal, bigger pulls it "
         "closer, smaller lets it go.",
         {sketchArg, poleArg, {"weight", ArgType::Number, "The weight, more than 0. Default 1.", true, {}}}},
        [](const Document&, const Args& a) -> std::unique_ptr<Command> {
            const std::string point = argText(a, "point");
            const double weight = argNumber(a, "weight", 1);
            return std::make_unique<EditSketchCommand>(
                argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                    Diagnostics problems;
                    int index = 0;
                    auto* e = findPole(s, point, index, problems);
                    if (!e) return problems;
                    auto& sp = std::get<sketch::SplineShape>(e->shape);
                    if (sp.weights.empty()) sp.weights.assign(sp.poles.size(), 1.0);
                    sp.weights[static_cast<std::size_t>(index)] = weight;
                    // All 1 again: an ordinary spline, saved without weights.
                    if (std::ranges::all_of(sp.weights, [](double w) { return w == 1.0; }))
                        sp.weights.clear();
                    label = "Change " + point + "'s weight";
                    return {};
                });
        });

    r.add({"sketch.add_pole",
           "Add control point",
           "Adds a control point to a spline, between two that are next to each other, so you can shape it "
           "more finely. Constraints on later control points stay on them.",
           {sketchArg,
            splineArg,
            {"after",
             ArgType::Integer,
             "Which control point it goes after: 1 puts it between pole1 and pole2.",
             true,
             {}},
            {"at", ArgType::Vec2, "Where, in mm. Default halfway to the next control point.", false, {}}}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const std::string name = argText(a, "spline");
              const int after = static_cast<int>(argNumber(a, "after"));
              const std::optional<sketch::Point2> at =
                  a.count("at") ? std::optional<sketch::Point2>(argVec2(a, "at")) : std::nullopt;
              return std::make_unique<EditSketchCommand>(
                  argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                      Diagnostics problems;
                      auto* sp = findSpline(s, name, problems);
                      if (!sp) return problems;
                      const int n = static_cast<int>(sp->poles.size());
                      const int last = sp->closed ? n : n - 1;  // a closed one can go after its last
                      if (after < 1 || after > last) {
                          return {sketchProblem("sketch.pole_place",
                                                "after must be 1 to " + std::to_string(last) + " for " +
                                                    name + ": the new control point goes between pole" +
                                                    "N and the next one.")};
                      }
                      const auto from = sp->poles[static_cast<std::size_t>(after - 1)];
                      const auto to = sp->poles[static_cast<std::size_t>(after % n)];
                      const auto where =
                          at.value_or(sketch::Point2{(from.x + to.x) / 2, (from.y + to.y) / 2});
                      const EntityId id = s.findEntity(name)->id;
                      sp->poles.insert(sp->poles.begin() + after, where);
                      if (!sp->weights.empty()) sp->weights.insert(sp->weights.begin() + after, 1.0);
                      renumberPoles(s, id, after, 1);
                      label = "Add a control point to " + name;
                      return {};
                  });
          });

    r.add({"sketch.remove_pole",
           "Remove control point",
           "Removes one of a spline's control points, and the constraints on it. Constraints on later "
           "control points stay on them.",
           {sketchArg, poleArg}},
          [](const Document&, const Args& a) -> std::unique_ptr<Command> {
              const std::string point = argText(a, "point");
              return std::make_unique<EditSketchCommand>(
                  argFeature(a, "sketch"), [=](sketch::Sketch& s, std::string& label) -> Diagnostics {
                      Diagnostics problems;
                      int index = 0;
                      auto* e = findPole(s, point, index, problems);
                      if (!e) return problems;
                      auto& sp = std::get<sketch::SplineShape>(e->shape);
                      if (static_cast<int>(sp.poles.size()) <= sp.degree + 1) {
                          return {sketchProblem(
                              "sketch.spline_poles",
                              e->name + " has only " + std::to_string(sp.poles.size()) +
                                  " control points, the fewest a spline of degree " +
                                  std::to_string(sp.degree) +
                                  " can have. Lower its degree first (sketch.set_spline degree=" +
                                  std::to_string(sp.degree - 1) + "), or delete the whole spline.")};
                      }
                      const sketch::PointRef gone{e->id, sketch::PointRole::Pole, index};
                      std::erase_if(s.constraints, [&](const sketch::Constraint& c) {
                          return std::ranges::any_of(c.points, [&](const auto& p) { return p == gone; });
                      });
                      sp.poles.erase(sp.poles.begin() + index);
                      if (!sp.weights.empty()) sp.weights.erase(sp.weights.begin() + index);
                      renumberPoles(s, e->id, index + 1, -1);
                      label = "Remove " + point;
                      return {};
                  });
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
