#include "kh/model/Feature.h"

#include <utility>

#include "kh/sketch/Profiles.h"

namespace kh::model {

BoxFeature::BoxFeature(std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : BoxFeature(FeatureId::generate(), std::move(name), corner, size) {}

BoxFeature::BoxFeature(FeatureId id, std::string name, kernel::Vec3 corner, kernel::Vec3 size)
    : Feature(id, std::move(name)), corner_(corner), size_(size) {}

kernel::OpResult BoxFeature::execute(kernel::IKernel& kernel, const ResultLookup&) const {
    return kernel.makeBox(corner_, size_.x, size_.y, size_.z);
}

CylinderFeature::CylinderFeature(std::string name, kernel::Vec3 base, kernel::Vec3 axis, double radius,
                                 double height)
    : CylinderFeature(FeatureId::generate(), std::move(name), base, axis, radius, height) {}

CylinderFeature::CylinderFeature(FeatureId id, std::string name, kernel::Vec3 base, kernel::Vec3 axis,
                                 double radius, double height)
    : Feature(id, std::move(name)), base_(base), axis_(axis), radius_(radius), height_(height) {}

kernel::OpResult CylinderFeature::execute(kernel::IKernel& kernel, const ResultLookup&) const {
    return kernel.makeCylinder(base_, axis_, radius_, height_);
}

BooleanFeature::BooleanFeature(std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : BooleanFeature(FeatureId::generate(), std::move(name), op, a, b) {}

BooleanFeature::BooleanFeature(FeatureId id, std::string name, kernel::BoolOp op, FeatureId a, FeatureId b)
    : Feature(id, std::move(name)), op_(op), a_(a), b_(b) {}

kernel::OpResult BooleanFeature::execute(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const auto* sa = earlier.shapeOf(a_);
    const auto* sb = earlier.shapeOf(b_);
    if (!sa || !sb) {
        kernel::OpResult r;
        r.issues.push_back({"feature.input_missing",
                            Severity::Error,
                            "This step uses another step that is missing or has a problem.",
                            {}});
        return r;
    }
    return kernel.boolean(op_, *sa, *sb);
}

MoveFeature::MoveFeature(std::string name, FeatureId body, kernel::Motion motion)
    : MoveFeature(FeatureId::generate(), std::move(name), body, motion) {}

MoveFeature::MoveFeature(FeatureId id, std::string name, FeatureId body, kernel::Motion motion)
    : Feature(id, std::move(name)), body_(body), motion_(motion) {}

kernel::OpResult MoveFeature::execute(kernel::IKernel& kernel, const ResultLookup& earlier) const {
    const auto* s = earlier.shapeOf(body_);
    if (!s) {
        kernel::OpResult r;
        r.issues.push_back({"feature.input_missing",
                            Severity::Error,
                            "This step uses another step that is missing or has a problem.",
                            {}});
        return r;
    }
    return kernel.transform(*s, motion_);
}

SketchFeature::SketchFeature(std::string name, SketchPlane plane)
    : SketchFeature(FeatureId::generate(), std::move(name), plane, {}) {}

SketchFeature::SketchFeature(FeatureId id, std::string name, SketchPlane plane, sketch::Sketch sketch)
    : Feature(id, std::move(name)), plane_(plane), sketch_(std::move(sketch)) {}

kernel::OpResult SketchFeature::execute(kernel::IKernel&, const ResultLookup&) const {
    kernel::OpResult r;
    r.issues.push_back({"sketch.no_solver",
                        Severity::Error,
                        "Sketches are solved by the sketch solver, not the kernel.",
                        {}});
    return r;
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

}  // namespace kh::model

namespace kh::model {

ExtrudeFeature::ExtrudeFeature(std::string name, FeatureId sketch, double distance, ExtrudeSide side)
    : Feature(std::move(name)), sketch_(sketch), distance_(distance), side_(side) {}

ExtrudeFeature::ExtrudeFeature(FeatureId id, std::string name, FeatureId sketch, double distance,
                               ExtrudeSide side)
    : Feature(id, std::move(name)), sketch_(sketch), distance_(distance), side_(side) {}

namespace {

// A sketch loop as the kernel takes it.
kernel::Loop2 toKernel(const sketch::Loop& loop, const sketch::Sketch& s) {
    kernel::Loop2 out;
    for (const auto& e : loop.edges) {
        const auto* entity = s.find(e.entity);
        if (!entity) continue;  // findProfiles only lists entities of the sketch
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

kernel::OpResult extrudeFailed(std::string code, std::string message) {
    kernel::OpResult r;
    r.issues.push_back({std::move(code), Severity::Error, std::move(message), {}});
    return r;
}

}  // namespace

kernel::OpResult ExtrudeFeature::execute(kernel::IKernel& kernel, const ResultLookup& earlier) const {
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
    const kernel::Plane plane{axes.origin, axes.x, axes.y};

    kernel::OpResult out;
    for (const auto& shape : found.shapes) {
        kernel::Profile2 profile{toKernel(shape.outer, solved), {}};
        for (const auto& hole : shape.holes) profile.holes.push_back(toKernel(hole, solved));
        auto face = kernel.makeFace(profile, plane);
        if (!face.ok()) return face;
        if (side_ == ExtrudeSide::Both) {
            const double back = -distance_ / 2;
            face = kernel.transform(face.shape, kernel::Motion{{n.x * back, n.y * back, n.z * back}});
            if (!face.ok()) return face;
        }
        auto solid = kernel.extrude(face.shape, sweep);
        if (!solid.ok()) return solid;
        // Several shapes make one body.
        if (out.shape.valid()) {
            solid = kernel.boolean(kernel::BoolOp::Union, out.shape, solid.shape);
            if (!solid.ok()) return solid;
        }
        out.shape = solid.shape;
    }
    // Pieces that weren't in any shape (three lines meeting, shapes crossing) are worth knowing about.
    for (const auto& d : found.issues) out.issues.push_back({d.code, Severity::Warning, d.message, d.detail});
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

}  // namespace kh::model
