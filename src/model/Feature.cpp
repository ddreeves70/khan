#include "sb/model/Feature.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "sb/sketch/Profiles.h"

namespace sb::model {

namespace {

// A result's names, or nullptr if it has none.
const ShapeNames* namesOf(const ResultLookup& earlier, const FeatureId& id) {
    const FeatureResult* r = earlier.resultOf(id);
    return r ? r->names.get() : nullptr;
}

// Names a shape made from inputs' shapes, from the kernel's history.
Built carried(kernel::IKernel& kernel, kernel::OpResult op, const std::vector<const ShapeNames*>& inputs,
              const FeatureId& creator, const OriginNamer& other = {}) {
    Built out;
    if (op.ok()) {
        out.names = std::make_shared<const ShapeNames>(
            carryNames(kernel.elements(op.shape), op.history, inputs, shortId(creator.toString()), other));
    }
    out.op = std::move(op);
    return out;
}

// Names a new shape's faces by which way they face: a box's are "left" (smallest
// x), "right", "front" (smallest y), "back", "bottom" and "top"; a cylinder's
// are "bottom", "top" (the end its axis points to) and "side" (the round one).
Built byDirection(kernel::IKernel& kernel, kernel::OpResult op, const FeatureId& creator,
                  const kernel::Vec3& up = {0, 0, 1}) {
    Built out;
    if (op.ok()) {
        auto elements = kernel.elements(op.shape);
        const std::string id = shortId(creator.toString());
        std::vector<std::string> faces;
        for (const auto& f : elements.faces) {
            const auto& n = f.direction;
            std::string role = "side";  // a cylinder's round face
            if (f.geom == kernel::GeomKind::Plane) {
                const double along = n.x * up.x + n.y * up.y + n.z * up.z;
                const bool box = up.x == 0 && up.y == 0 && up.z == 1 && std::abs(along) < 0.5;
                if (box) {
                    role = std::abs(n.x) > std::abs(n.y) ? (n.x > 0 ? "right" : "left")
                                                         : (n.y > 0 ? "back" : "front");
                } else {
                    role = along > 0 ? "top" : "bottom";
                }
            }
            faces.push_back(id + "/" + role);
        }
        out.names = std::make_shared<const ShapeNames>(nameFromFaces(std::move(elements), std::move(faces)));
    }
    out.op = std::move(op);
    return out;
}

Built missingInput() {
    Built b;
    b.op.issues.push_back({"feature.input_missing",
                           Severity::Error,
                           "This step uses another step that is missing or has a problem.",
                           {}});
    return b;
}

}  // namespace

BoxFeature::BoxFeature(std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : BoxFeature(FeatureId::generate(), std::move(name), corner, size) {}

BoxFeature::BoxFeature(FeatureId id, std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : Feature(id, std::move(name)), corner_(corner), size_(size) {}

Built BoxFeature::build(kernel::IKernel& kernel, const ResultLookup&) const {
    // Faces: left, right, front, back, top and bottom.
    return byDirection(kernel, kernel.makeBox(corner_, size_.x, size_.y, size_.z), id());
}

CylinderFeature::CylinderFeature(std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius,
                                 double height)
    : CylinderFeature(FeatureId::generate(), std::move(name), base, axis, radius, height) {}

CylinderFeature::CylinderFeature(FeatureId id, std::string name, kernel::Vec3 base, kernel::Vec3 axis,
                                 double radius, double height)
    : Feature(id, std::move(name)), base_(base), axis_(axis), radius_(radius), height_(height) {}

Built CylinderFeature::build(kernel::IKernel& kernel, const ResultLookup&) const {
    // Faces: side, top and bottom (top is the end the axis points to).
    return byDirection(kernel, kernel.makeCylinder(base_, axis_, radius_, height_), id(), axis_);
}

BooleanFeature::BooleanFeature(std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : BooleanFeature(FeatureId::generate(), std::move(name), op, a, b) {}

BooleanFeature::BooleanFeature(FeatureId id, std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : Feature(id, std::move(name)), op_(op), a_(a), b_(b) {}

Built BooleanFeature::build(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const auto* sa = earlier.shapeOf(a_);
    const auto* sb = earlier.shapeOf(b_);
    if (!sa || !sb) return missingInput();
    // Every face keeps the name it had on its body.
    return carried(kernel, kernel.boolean(op_, *sa, *sb), {namesOf(earlier, a_), namesOf(earlier, b_)}, id());
}

MoveFeature::MoveFeature(std::string name, FeatureId body, kernel::Motion motion)
    : MoveFeature(FeatureId::generate(), std::move(name), body, motion) {}

MoveFeature::MoveFeature(FeatureId id, std::string name, FeatureId body, kernel::Motion motion)
    : Feature(id, std::move(name)), body_(body), motion_(motion) {}

Built MoveFeature::build(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const auto* s = earlier.shapeOf(body_);
    if (!s) return missingInput();
    return carried(kernel, kernel.transform(*s, motion_), {namesOf(earlier, body_)}, id());
}

SketchFeature::SketchFeature(std::string name, SketchPlane plane)
    : SketchFeature(FeatureId::generate(), std::move(name), plane, {}) {}

SketchFeature::SketchFeature(FeatureId id, std::string name, SketchPlane plane, sketch::Sketch sketch)
    : Feature(id, std::move(name)), plane_(plane), sketch_(std::move(sketch)) {}

Built SketchFeature::build(kernel::IKernel&, const ResultLookup&) const {
    Built b;
    b.op.issues.push_back({"sketch.no_solver",
                           Severity::Error,
                           "Sketches are solved by the sketch solver, not the kernel.",
                           {}});
    return b;
}

PlaneAxes planeAxes(SketchPlane plane) {
    switch (plane) {
        case SketchPlane::XY:
            return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        case SketchPlane::XZ:
            return {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        case SketchPlane::YZ:
            return {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    }
    return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
}

kernel::Vec3 toDesign(SketchPlane plane, const sketch::Point2& p) {
    const auto a = planeAxes(plane);
    return {a.origin.x + p.x * a.x.x + p.y * a.y.x, a.origin.y + p.x * a.x.y + p.y * a.y.y,
            a.origin.z + p.x * a.x.z + p.y * a.y.z};
}

sketch::Point2 toSketch(SketchPlane plane, const kernel::Vec3& p) {
    const auto a = planeAxes(plane);
    const kernel::Vec3 d{p.x - a.origin.x, p.y - a.origin.y, p.z - a.origin.z};
    return {d.x * a.x.x + d.y * a.x.y + d.z * a.x.z, d.x * a.y.x + d.y * a.y.y + d.z * a.y.z};
}

}  // namespace sb::model

namespace sb::model {

ExtrudeFeature::ExtrudeFeature(std::string name, FeatureId sketch, double distance, ExtrudeSide side)
    : Feature(std::move(name)), sketch_(sketch), distance_(distance), side_(side) {}

ExtrudeFeature::ExtrudeFeature(FeatureId id, std::string name, FeatureId sketch, double distance,
                               ExtrudeSide side)
    : Feature(id, std::move(name)), sketch_(sketch), distance_(distance), side_(side) {}

namespace {

// A sketch loop as the kernel takes it. Each curve's entity is added to `curves`,
// in the same order (for naming the faces made from them).
kernel::Loop2 toKernel(const sketch::Loop& loop, const sketch::Sketch& s, std::vector<EntityId>& curves) {
    kernel::Loop2 out;
    for (const auto& e : loop.edges) {
        const auto* entity = s.find(e.entity);
        if (!entity) continue;  // findProfiles only lists entities of the sketch
        curves.push_back(e.entity);
        kernel::Edge2 edge;
        edge.reversed = e.reversed;
        if (const auto* l = std::get_if<sketch::LineShape>(&entity->shape)) {
            edge.curve = kernel::Line2{{l->start.x, l->start.y}, {l->end.x, l->end.y}};
        } else if (const auto* a = std::get_if<sketch::ArcShape>(&entity->shape)) {
            edge.curve = kernel::Arc2{{a->center.x, a->center.y}, a->radius, a->startAngle, a->endAngle};
        } else if (const auto* c = std::get_if<sketch::CircleShape>(&entity->shape)) {
            edge.curve = kernel::Circle2{{c->center.x, c->center.y}, c->radius};
        } else if (const auto* sp = std::get_if<sketch::SplineShape>(&entity->shape)) {
            kernel::Spline2 k{{}, sp->weights, sp->degree, sp->closed};
            for (const auto& p : sp->poles) k.poles.push_back({p.x, p.y});
            edge.curve = std::move(k);
        }
        out.push_back(edge);
    }
    return out;
}

Built extrudeFailed(std::string code, std::string message) {
    Built b;
    b.op.issues.push_back({std::move(code), Severity::Error, std::move(message), {}});
    return b;
}

}  // namespace

Built ExtrudeFeature::build(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    if (!(distance_ > 0)) {
        return extrudeFailed("extrude.distance",
                             "Extrude needs a distance greater than 0 mm. Type one in Properties, like 10.");
    }
    const FeatureResult* r = earlier.resultOf(sketch_);
    if (!r || !r->sketch || !r->sketch->ok() || !r->plane) {
        // Rebuilds block this step first; this is only for callers that don't.
        return extrudeFailed("extrude.sketch", "Extrude needs a sketch that solved. Fix the sketch first.");
    }
    const sketch::Sketch solved{r->sketch->entities, {}};
    const auto found = sketch::findProfiles(solved);
    if (found.shapes.empty()) {
        std::string why = "The sketch has no closed shape to extrude.";
        if (!found.openEnds.empty()) {
            std::string ends;
            for (const auto& end : found.openEnds) {
                const auto* e = solved.find(end.entity);
                ends += (ends.empty() ? "" : ", ") + (e ? e->name : std::string("?")) + "'s " +
                        (end.role == sketch::PointRole::Start ? "start" : "end");
            }
            why += " These ends aren't joined: " + ends + ". Join them so the lines go all the way round.";
        } else if (!found.issues.empty()) {
            why += " " + found.issues.front().message;
        } else {
            why += " Draw a closed shape in it, like a rectangle or a circle.";
        }
        return extrudeFailed("extrude.no_shape", why);
    }

    // Which way: the sketch's front (the side it's seen from) unless flipped; for
    // both sides, start half the distance behind the sketch.
    const PlaneAxes axes = planeAxes(*r->plane);
    const kernel::Vec3 n{axes.x.y * axes.y.z - axes.x.z * axes.y.y, axes.x.z * axes.y.x - axes.x.x * axes.y.z,
                         axes.x.x * axes.y.y - axes.x.y * axes.y.x};
    const double sign = side_ == ExtrudeSide::Flip ? -1 : 1;
    const kernel::Vec3 sweep{n.x * distance_ * sign, n.y * distance_ * sign, n.z * distance_ * sign};
    const double back = side_ == ExtrudeSide::Both ? -distance_ / 2 : 0;
    const kernel::Plane plane{
        {axes.origin.x + n.x * back, axes.origin.y + n.y * back, axes.origin.z + n.z * back}, axes.x, axes.y};

    // Names: each side for the sketch curve it was swept from ("side:<curve>"),
    // and each shape's ends for its outline ("start:<curve>", "end:<curve>", with
    // the outline curve whose id comes first).
    const std::string me = shortId(id().toString());
    Built out;
    for (const auto& shape : found.shapes) {
        std::vector<EntityId> curves;
        kernel::Profile2 profile{toKernel(shape.outer, solved, curves), {}};
        const std::string outline = shortId(std::min_element(curves.begin(), curves.end())->toString());
        for (const auto& hole : shape.holes) profile.holes.push_back(toKernel(hole, solved, curves));
        auto face = kernel.makeFace(profile, plane);
        if (!face.ok()) return {face, nullptr};
        auto solid = kernel.extrude(face.shape, sweep);
        if (!solid.ok()) return {solid, nullptr};
        const auto& edgeCurves = face.history.edges;
        auto named = carried(kernel, std::move(solid), {}, id(), [&](const kernel::Origin& o) -> std::string {
            switch (o.kind) {
                case kernel::OriginKind::Start:
                    return me + "/start:" + outline;
                case kernel::OriginKind::End:
                    return me + "/end:" + outline;
                case kernel::OriginKind::Edge: {
                    const auto e = static_cast<std::size_t>(o.index);
                    if (e >= edgeCurves.size() || edgeCurves[e].empty()) return {};
                    const auto c = static_cast<std::size_t>(edgeCurves[e].front().index);
                    return c < curves.size() ? me + "/side:" + shortId(curves[c].toString()) : std::string();
                }
                default:
                    return {};
            }
        });
        // Several shapes make one body.
        if (out.op.shape.valid()) {
            auto joined = kernel.boolean(kernel::BoolOp::Union, out.op.shape, named.op.shape);
            if (!joined.ok()) return {joined, nullptr};
            named = carried(kernel, std::move(joined), {out.names.get(), named.names.get()}, id());
        }
        out = std::move(named);
    }
    // Pieces that weren't in any shape (three lines meeting, shapes crossing) are worth knowing about.
    for (const auto& d : found.issues)
        out.op.issues.push_back({d.code, Severity::Warning, d.message, d.detail});
    return out;
}

// ---------------------------------------------------------------------------
// Numbers by name
// ---------------------------------------------------------------------------

namespace {

// The x, y or z of a vector by a key's ending, like "size_x".
double* axisOf(kernel::Vec3& v, std::string_view key, std::string_view start) {
    if (key.size() != start.size() + 2 || !key.starts_with(start) || key[start.size()] != '_') return nullptr;
    switch (key.back()) {
        case 'x':
            return &v.x;
        case 'y':
            return &v.y;
        case 'z':
            return &v.z;
        default:
            return nullptr;
    }
}

void addAxes(std::vector<StepNumber>& out, const std::string& start, bool positive) {
    for (const char* axis : {"_x", "_y", "_z"}) out.push_back({start + axis, kLength, positive});
}

}  // namespace

std::vector<StepNumber> BoxFeature::numbers() const {
    std::vector<StepNumber> out;
    addAxes(out, "corner", false);
    addAxes(out, "size", true);
    return out;
}

std::optional<double> BoxFeature::number(std::string_view key) const {
    auto copy = *this;
    if (double* v = axisOf(copy.corner_, key, "corner")) return *v;
    if (double* v = axisOf(copy.size_, key, "size")) return *v;
    return std::nullopt;
}

bool BoxFeature::setNumber(std::string_view key, double value) {
    double* v = axisOf(corner_, key, "corner");
    if (!v) v = axisOf(size_, key, "size");
    if (!v) return false;
    *v = value;
    return true;
}

std::vector<StepNumber> CylinderFeature::numbers() const {
    std::vector<StepNumber> out;
    addAxes(out, "base", false);
    out.push_back({"radius", kLength, true});
    out.push_back({"height", kLength, true});
    return out;
}

std::optional<double> CylinderFeature::number(std::string_view key) const {
    if (key == "radius") return radius_;
    if (key == "height") return height_;
    auto base = base_;
    if (double* v = axisOf(base, key, "base")) return *v;
    return std::nullopt;
}

bool CylinderFeature::setNumber(std::string_view key, double value) {
    double* v = key == "radius" ? &radius_ : key == "height" ? &height_ : axisOf(base_, key, "base");
    if (!v) return false;
    *v = value;
    return true;
}

std::vector<StepNumber> MoveFeature::numbers() const {
    std::vector<StepNumber> out;
    addAxes(out, "offset", false);
    addAxes(out, "center", false);
    out.push_back({"angle", kAngle, false});
    return out;
}

std::optional<double> MoveFeature::number(std::string_view key) const {
    auto m = motion_;
    if (key == "angle") return m.angle;
    if (double* v = axisOf(m.offset, key, "offset")) return *v;
    if (double* v = axisOf(m.center, key, "center")) return *v;
    return std::nullopt;
}

bool MoveFeature::setNumber(std::string_view key, double value) {
    double* v = key == "angle" ? &motion_.angle : axisOf(motion_.offset, key, "offset");
    if (!v) v = axisOf(motion_.center, key, "center");
    if (!v) return false;
    *v = value;
    return true;
}

std::vector<StepNumber> ExtrudeFeature::numbers() const {
    return {{"distance", kLength, true}};
}

std::optional<double> ExtrudeFeature::number(std::string_view key) const {
    if (key == "distance") return distance_;
    return std::nullopt;
}

bool ExtrudeFeature::setNumber(std::string_view key, double value) {
    if (key != "distance") return false;
    distance_ = value;
    return true;
}

// ---------------------------------------------------------------------------
// Fillet and Chamfer
// ---------------------------------------------------------------------------

std::vector<StepNumber> EdgeFeature::numbers() const {
    return {{sizeKey(), kLength, true}};
}

std::optional<double> EdgeFeature::number(std::string_view key) const {
    if (key == sizeKey()) return size_;
    return std::nullopt;
}

bool EdgeFeature::setNumber(std::string_view key, double value) {
    if (key != sizeKey()) return false;
    size_ = value;
    return true;
}

void EdgeFeature::remapInputs(const std::map<FeatureId, FeatureId>& map) {
    if (auto it = map.find(body_); it != map.end()) body_ = it->second;
    // Edge names hold the ids of the steps that made their faces: a copy's edges
    // are on the copied steps' faces.
    for (auto& ref : edges_) {
        for (const auto& [from, to] : map) {
            const std::string a = shortId(from.toString()), b = shortId(to.toString());
            for (auto at = ref.name.find(a); at != std::string::npos; at = ref.name.find(a, at + b.size())) {
                ref.name.replace(at, a.size(), b);
            }
        }
    }
}

Built EdgeFeature::build(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const bool fillet = type() == FeatureType::Fillet;
    auto failed = [](std::string code, std::string message) {
        Built b;
        b.op.issues.push_back({std::move(code), Severity::Error, std::move(message), {}});
        return b;
    };
    if (!(size_ > 0)) {
        return failed(fillet ? "fillet.radius" : "chamfer.distance",
                      fillet
                          ? "A fillet needs a radius greater than 0 mm. Type one in Properties, like 2."
                          : "A chamfer needs a distance greater than 0 mm. Type one in Properties, like 1.");
    }
    if (edges_.empty()) {
        return failed("edges.none",
                      name() + " has no edges. Pick the edges to " + (fillet ? "round." : "bevel."));
    }
    const auto* shape = earlier.shapeOf(body_);
    if (!shape) return missingInput();
    const ShapeNames* names = namesOf(earlier, body_);

    // Find each edge again. Anything less than sure stops the step.
    std::vector<int> found;
    Diagnostics notes;
    for (const auto& ref : edges_) {
        const auto where = names ? resolve(ref, *names) : Resolution{};
        if (where.status == ResolveStatus::Ambiguous) {
            return failed("edges.ambiguous", name() + " can't tell which edge to " +
                                                 (fillet ? "round" : "bevel") + ": an earlier change made " +
                                                 std::to_string(where.candidates.size()) +
                                                 " edges that look alike where one was. Edit " + name() +
                                                 " and pick its edges again.");
        }
        if (!where.found()) {
            return failed("edges.missing", name() + " can't find an edge it " +
                                               (fillet ? "rounds" : "bevels") +
                                               ": an earlier change took it away. Edit " + name() +
                                               " and pick its edges again.");
        }
        if (where.status == ResolveStatus::Matched && notes.empty()) {
            notes.push_back({"edges.matched",
                             Severity::Info,
                             name() + " found an edge by where it is, because an earlier change renamed it. "
                                      "Check it's the right one.",
                             {}});
        }
        if (std::find(found.begin(), found.end(), where.index) == found.end()) found.push_back(where.index);
    }

    const std::string me = shortId(id().toString());
    const std::string what = role();
    auto out = carried(kernel, apply(kernel, *shape, found), {names}, id(), [&](const kernel::Origin& o) {
        // A round or bevel is named for the edge it replaced; a corner where
        // several meet, for the vertex.
        const auto i = static_cast<std::size_t>(o.index);
        if (o.kind == kernel::OriginKind::Edge && names && i < names->edges.size()) {
            return me + "/" + what + ":" + names->edges[i];
        }
        if (o.kind == kernel::OriginKind::Vertex && names && i < names->vertices.size()) {
            return me + "/corner:" + names->vertices[i];
        }
        return std::string();
    });
    if (out.op.ok()) out.op.issues.insert(out.op.issues.end(), notes.begin(), notes.end());
    return out;
}

}  // namespace sb::model
