#include "kh/sketch_planegcs/PlaneGcsSolver.h"

#include <planegcs/GCS.h>

#include <cmath>
#include <exception>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace kh::sketch {

namespace {

// How close Khan's own measure of every constraint must be to 0 after a solve
// (mm, or radians for angles).
constexpr double kTolerance = 1e-6;

// One entity as PlaneGCS sees it. PlaneGCS holds pointers to the numbers, so
// they live in a deque, which never moves what it already holds.
struct Built {
    GCS::Point point;
    GCS::Line line;
    GCS::Circle circle;
    GCS::Arc arc;
};

// Every number lives in one array, filled in a fixed order, so the numbers' memory
// addresses rise in the order they were made, on every computer. PlaneGCS orders
// its unknowns by address (std::set<double*> in SubSystem::initialize), and that
// order sets the order of its sums: with numbers scattered around memory, the same
// sketch could solve to answers that differ in the last bits.
class Builder {
public:
    // Room for every number the sketch can need; the array never grows, so the
    // addresses PlaneGCS holds stay valid.
    explicit Builder(std::size_t capacity) { numbers_.reserve(capacity); }

    // A number the solver may change.
    double* unknown(double value) {
        double* p = add(value);
        unknowns_.push_back(p);
        return p;
    }

    // A number the solver must keep (a dimension's value, a fixed position).
    double* constant(double value) { return add(value); }

    GCS::Point point(Point2 p) {
        GCS::Point g;
        g.x = unknown(p.x);
        g.y = unknown(p.y);
        return g;
    }

    GCS::VEC_pD& unknowns() { return unknowns_; }

private:
    double* add(double value) {
        if (numbers_.size() == numbers_.capacity()) throw std::logic_error("sketch number array is full");
        numbers_.push_back(value);
        return &numbers_.back();
    }

    std::vector<double> numbers_;
    GCS::VEC_pD unknowns_;
};

std::string names(const Sketch& sketch, const std::vector<ConstraintId>& ids) {
    std::string out;
    for (const auto& id : ids) {
        const auto* c = sketch.find(id);
        if (!c) continue;
        out += (out.empty() ? "" : ", ") + std::string(kindName(c->kind));
    }
    return out;
}

// Which side of the line a point is on, as PlaneGCS measures it (its
// ConstraintP2LDistance::signed_value()).
double sideOf(const GCS::Point& p, const GCS::Line& l) {
    const double x0 = *p.x, y0 = *p.y, x1 = *l.p1.x, y1 = *l.p1.y, x2 = *l.p2.x, y2 = *l.p2.y;
    return -x0 * (y2 - y1) + y0 * (x2 - x1) + x1 * y2 - x2 * y1;
}

// If a Coincident joins an end of one entity to an end of the other, the end of
// the first one.
std::optional<PointRef> jointOf(const Sketch& sketch, const EntityId& a, const EntityId& b) {
    auto isEnd = [](const PointRef& p) { return p.role == PointRole::Start || p.role == PointRole::End; };
    for (const auto& c : sketch.constraints) {
        if (c.kind != ConstraintKind::Coincident || c.points.size() != 2) continue;
        const auto& p = c.points[0];
        const auto& q = c.points[1];
        if (!isEnd(p) || !isEnd(q)) continue;
        if (p.entity == a && q.entity == b) return p;
        if (q.entity == a && p.entity == b) return q;
    }
    return std::nullopt;
}

// A sketch set up in PlaneGCS: its numbers, its entities and its constraints.
// It holds pointers into itself, so it's made once and never moved.
class GcsModel {
public:
    // At most 9 numbers per entity (an arc: center, radius, two angles, two ends)
    // and 2 constants per constraint (a Fix's x and y).
    GcsModel(const std::vector<Entity>& entities, const Sketch& sketch)
        : builder(9 * entities.size() + 2 * sketch.constraints.size()), built(entities.size()) {
        Sketch whole{entities, sketch.constraints};
        auto& b = builder;

        // Entities. An arc's ends are kept on its circle by PlaneGCS's arc rules,
        // added with tag 0 (Khan's own constraints are tagged 1, 2, 3, ...).
        for (std::size_t i = 0; i < whole.entities.size(); ++i) {
            const auto& shape = whole.entities[i].shape;
            auto& g = built[i];
            if (const auto* p = std::get_if<PointShape>(&shape)) {
                g.point = b.point(p->at);
            } else if (const auto* l = std::get_if<LineShape>(&shape)) {
                g.line.p1 = b.point(l->start);
                g.line.p2 = b.point(l->end);
            } else if (const auto* c = std::get_if<CircleShape>(&shape)) {
                g.circle.center = b.point(c->center);
                g.circle.rad = b.unknown(c->radius);
            } else if (const auto* a = std::get_if<ArcShape>(&shape)) {
                g.arc.center = b.point(a->center);
                g.arc.rad = b.unknown(a->radius);
                g.arc.startAngle = b.unknown(a->startAngle);
                g.arc.endAngle = b.unknown(a->endAngle);
                g.arc.start = b.point({a->center.x + a->radius * std::cos(a->startAngle),
                                       a->center.y + a->radius * std::sin(a->startAngle)});
                g.arc.end = b.point({a->center.x + a->radius * std::cos(a->endAngle),
                                     a->center.y + a->radius * std::sin(a->endAngle)});
                sys.addConstraintArcRules(g.arc, 0);
            }
        }

        auto index = [&](const EntityId& id) {
            for (std::size_t i = 0; i < whole.entities.size(); ++i) {
                if (whole.entities[i].id == id) return i;
            }
            return std::size_t{0};  // check() already made sure it's there
        };
        auto shapeOf = [&](const EntityId& id) -> const Shape& { return whole.entities[index(id)].shape; };
        auto pointOfRef = [&](const PointRef& ref) -> GCS::Point& {
            auto& g = built[index(ref.entity)];
            const Shape& s = shapeOf(ref.entity);
            if (std::holds_alternative<PointShape>(s)) return g.point;
            if (std::holds_alternative<LineShape>(s))
                return ref.role == PointRole::Start ? g.line.p1 : g.line.p2;
            if (std::holds_alternative<CircleShape>(s)) return g.circle.center;
            if (ref.role == PointRole::Center) return g.arc.center;
            return ref.role == PointRole::Start ? g.arc.start : g.arc.end;
        };
        auto lineOf = [&](const EntityId& id) -> GCS::Line& { return built[index(id)].line; };
        auto isArc = [&](const EntityId& id) { return std::holds_alternative<ArcShape>(shapeOf(id)); };
        auto isLine = [&](const EntityId& id) { return std::holds_alternative<LineShape>(shapeOf(id)); };
        auto curveOf = [&](const EntityId& id) -> GCS::Curve& {
            auto& g = built[index(id)];
            if (isLine(id)) return g.line;
            if (isArc(id)) return g.arc;
            return g.circle;
        };
        // A circle, or an arc's circle.
        auto circleOf = [&](const EntityId& id) -> GCS::Circle& {
            auto& g = built[index(id)];
            if (isArc(id)) return g.arc;
            return g.circle;
        };

        // Constraints.
        for (std::size_t i = 0; i < whole.constraints.size(); ++i) {
            const auto& c = whole.constraints[i];
            const int tag = static_cast<int>(i) + 1;
            const auto& e = c.entities;
            switch (c.kind) {
                case ConstraintKind::Fix: {
                    auto& p = pointOfRef(c.points[0]);
                    sys.addConstraintCoordinateX(p, b.constant(c.at.x), tag);
                    sys.addConstraintCoordinateY(p, b.constant(c.at.y), tag);
                    break;
                }
                case ConstraintKind::Coincident:
                    sys.addConstraintP2PCoincident(pointOfRef(c.points[0]), pointOfRef(c.points[1]), tag);
                    break;
                case ConstraintKind::Horizontal:
                    sys.addConstraintHorizontal(lineOf(e[0]), tag);
                    break;
                case ConstraintKind::Vertical:
                    sys.addConstraintVertical(lineOf(e[0]), tag);
                    break;
                case ConstraintKind::Parallel:
                    sys.addConstraintParallel(lineOf(e[0]), lineOf(e[1]), tag);
                    break;
                case ConstraintKind::Perpendicular:
                    sys.addConstraintPerpendicular(lineOf(e[0]), lineOf(e[1]), tag);
                    break;
                case ConstraintKind::Equal:
                    if (isLine(e[0])) {
                        sys.addConstraintEqualLength(lineOf(e[0]), lineOf(e[1]), tag);
                    } else {
                        // Equal radius doesn't depend on order; PlaneGCS has no (arc, circle) version.
                        const bool arc0 = isArc(e[0]), arc1 = isArc(e[1]);
                        auto& g0 = built[index(e[0])];
                        auto& g1 = built[index(e[1])];
                        if (arc0 && arc1)
                            sys.addConstraintEqualRadius(g0.arc, g1.arc, tag);
                        else if (arc0)
                            sys.addConstraintEqualRadius(g1.circle, g0.arc, tag);
                        else if (arc1)
                            sys.addConstraintEqualRadius(g0.circle, g1.arc, tag);
                        else
                            sys.addConstraintEqualRadius(g0.circle, g1.circle, tag);
                    }
                    break;
                case ConstraintKind::PointOnLine:
                    sys.addConstraintPointOnLine(pointOfRef(c.points[0]), lineOf(e[0]), tag);
                    break;
                case ConstraintKind::PointOnCircle:
                    sys.addConstraintPointOnCircle(pointOfRef(c.points[0]), circleOf(e[0]), tag);
                    break;
                case ConstraintKind::Midpoint: {
                    auto& l = lineOf(e[0]);
                    sys.addConstraintP2PSymmetric(l.p1, l.p2, pointOfRef(c.points[0]), tag);
                    break;
                }
                case ConstraintKind::Tangent: {
                    // Joined end to end (a Coincident between an end of each)? Then keep
                    // their directions the same at that point, as FreeCAD's endpoint
                    // tangency does. Touching edges plus a shared point would look like
                    // the same constraint twice at the solved shape, and the degrees of
                    // freedom would come out wrong.
                    if (const auto joint = jointOf(whole, e[0], e[1])) {
                        GCS::Curve& c0 = curveOf(e[0]);
                        GCS::Curve& c1 = curveOf(e[1]);
                        GCS::Point& p = pointOfRef(*joint);
                        // Same direction (0) or opposite (pi), whichever it's closer to now.
                        const auto n0 = c0.CalculateNormal(p);
                        const auto n1 = c1.CalculateNormal(p);
                        const bool same = n0.x * n1.x + n0.y * n1.y >= 0;
                        sys.addConstraintAngleViaPoint(c0, c1, p, b.constant(same ? 0.0 : std::numbers::pi),
                                                       tag);
                        break;
                    }
                    if (isLine(e[0]) || isLine(e[1])) {
                        const EntityId lineId = isLine(e[0]) ? e[0] : e[1];
                        const EntityId roundId = isLine(e[0]) ? e[1] : e[0];
                        auto& l = lineOf(lineId);
                        auto& circle = circleOf(roundId);
                        // Keep the circle on the side of the line it's on now.
                        const bool ccw = sideOf(circle.center, l) >= 0;
                        sys.addConstraintP2LDistance(circle.center, l, circle.rad, ccw, tag);
                    } else {
                        const bool arc0 = isArc(e[0]), arc1 = isArc(e[1]);
                        auto& g0 = built[index(e[0])];
                        auto& g1 = built[index(e[1])];
                        if (arc0 && arc1)
                            sys.addConstraintTangent(g0.arc, g1.arc, tag);
                        else if (arc0)
                            sys.addConstraintTangent(g1.circle, g0.arc, tag);
                        else if (arc1)
                            sys.addConstraintTangent(g0.circle, g1.arc, tag);
                        else
                            sys.addConstraintTangent(g0.circle, g1.circle, tag);
                    }
                    break;
                }
                case ConstraintKind::Distance:
                    sys.addConstraintP2PDistance(pointOfRef(c.points[0]), pointOfRef(c.points[1]),
                                                 b.constant(c.value), tag);
                    break;
                case ConstraintKind::Length: {
                    auto& l = lineOf(e[0]);
                    sys.addConstraintP2PDistance(l.p1, l.p2, b.constant(c.value), tag);
                    break;
                }
                case ConstraintKind::HorizontalDistance:
                    sys.addConstraintDifference(pointOfRef(c.points[0]).x, pointOfRef(c.points[1]).x,
                                                b.constant(c.value), tag);
                    break;
                case ConstraintKind::VerticalDistance:
                    sys.addConstraintDifference(pointOfRef(c.points[0]).y, pointOfRef(c.points[1]).y,
                                                b.constant(c.value), tag);
                    break;
                case ConstraintKind::Radius:
                    if (isArc(e[0]))
                        sys.addConstraintArcRadius(built[index(e[0])].arc, b.constant(c.value), tag);
                    else
                        sys.addConstraintCircleRadius(built[index(e[0])].circle, b.constant(c.value), tag);
                    break;
                case ConstraintKind::Diameter:
                    if (isArc(e[0]))
                        sys.addConstraintArcDiameter(built[index(e[0])].arc, b.constant(c.value), tag);
                    else
                        sys.addConstraintCircleDiameter(built[index(e[0])].circle, b.constant(c.value), tag);
                    break;
                case ConstraintKind::Angle:
                    sys.addConstraintL2LAngle(lineOf(e[0]), lineOf(e[1]), b.constant(c.value), tag);
                    break;
            }
        }

        sys.declareUnknowns(builder.unknowns());
    }

    // Reads the numbers back into the entities.
    std::vector<Entity> read(std::vector<Entity> entities) {
        for (std::size_t i = 0; i < entities.size(); ++i) {
            auto& g = built[i];
            auto p2 = [](const GCS::Point& p) { return Point2{*p.x, *p.y}; };
            std::visit(
                [&](auto& s) {
                    using S = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<S, PointShape>) {
                        s.at = p2(g.point);
                    } else if constexpr (std::is_same_v<S, LineShape>) {
                        s.start = p2(g.line.p1);
                        s.end = p2(g.line.p2);
                    } else if constexpr (std::is_same_v<S, CircleShape>) {
                        s.center = p2(g.circle.center);
                        s.radius = *g.circle.rad;
                    } else {
                        s.center = p2(g.arc.center);
                        s.radius = *g.arc.rad;
                        s.startAngle = *g.arc.startAngle;
                        s.endAngle = *g.arc.endAngle;
                    }
                },
                entities[i].shape);
        }
        return entities;
    }

    Builder builder;
    GCS::System sys;
    std::vector<Built> built;
};

class PlaneGcsSolver final : public ISketchSolver {
public:
    SolveResult solve(const Sketch& sketch) override {
        SolveResult result;
        result.entities = sketch.entities;
        if (auto problems = check(sketch); !problems.empty()) {
            result.status = SolveStatus::Invalid;
            result.issues = std::move(problems);
            return result;
        }
        if (sketch.entities.empty()) {
            result.status = SolveStatus::Solved;
            result.freedom = 0;
            return result;
        }
        try {
            run(sketch, result);
        } catch (const std::exception& e) {
            fail(result, e.what());
        } catch (...) {
            fail(result, "unknown error");
        }
        return result;
    }

private:
    static void fail(SolveResult& result, const std::string& detail) {
        result.status = SolveStatus::Failed;
        result.issues.push_back(
            {"sketch.solve_failed", Severity::Error,
             "The sketch couldn't be solved. Undo the last change, or delete a constraint.", detail});
    }

    static void run(const Sketch& sketch, SolveResult& result) {
        // 1. Solve from where the geometry is now.
        auto first = std::make_unique<GcsModel>(sketch.entities, sketch);
        first->sys.initSolution();
        first->sys.solve();
        first->sys.applySolution();
        auto solved = first->read(sketch.entities);

        // 2. Look for conflicting and repeated constraints, and count the degrees
        // of freedom, at the solved shape. (At a rough starting drawing, two
        // constraints that say the same thing can look different to PlaneGCS.)
        auto second = std::make_unique<GcsModel>(solved, sketch);
        second->sys.initSolution();
        result.freedom = second->sys.dofsNumber();
        // In the sketch's order (PlaneGCS lists them in an order that depends on memory addresses).
        auto toIds = [&](const GCS::VEC_I& tags) {
            std::set<int> seen;
            for (int t : tags) {
                if (t >= 1 && t <= static_cast<int>(sketch.constraints.size())) seen.insert(t);
            }
            std::vector<ConstraintId> ids;
            for (int t : seen) ids.push_back(sketch.constraints[static_cast<std::size_t>(t - 1)].id);
            return ids;
        };
        GCS::VEC_I conflicting, redundant;
        second->sys.getConflicting(conflicting);
        second->sys.getRedundant(redundant);
        result.conflicting = toIds(conflicting);
        result.redundant = toIds(redundant);
        if (!result.conflicting.empty()) {
            result.status = SolveStatus::Conflicting;
            result.freedom = -1;
            result.issues.push_back(
                {"sketch.conflicting",
                 Severity::Error,
                 "These constraints can't all be true at once: " + names(sketch, result.conflicting) +
                     ". Delete one of them, or change a dimension.",
                 {}});
            return;
        }

        // 3. Check the answer with Khan's own measures: the solver must really
        // have made every constraint true.
        Sketch after{solved, sketch.constraints};
        std::vector<ConstraintId> off;
        for (const auto& c : sketch.constraints) {
            if (!(error(after, c) <= kTolerance)) off.push_back(c.id);
        }
        bool sizes = true;
        for (const auto& e : solved) {
            if (const auto* c = std::get_if<CircleShape>(&e.shape)) sizes = sizes && c->radius > 0;
            if (const auto* a = std::get_if<ArcShape>(&e.shape)) sizes = sizes && a->radius > 0;
        }
        if (!off.empty() || !sizes) {
            result.status = SolveStatus::Failed;
            result.issues.push_back(
                {"sketch.solve_failed",
                 Severity::Error,
                 off.empty()
                     ? std::string("The sketch couldn't be solved without turning a circle inside out. "
                                   "Undo the last change, or change a dimension.")
                     : "The sketch couldn't be solved: " + names(sketch, off) +
                           " couldn't be made true. Undo the last change, or delete a constraint.",
                 {}});
            return;
        }
        result.entities = std::move(solved);
        result.status = SolveStatus::Solved;
        if (!result.redundant.empty()) {
            result.issues.push_back(
                {"sketch.redundant",
                 Severity::Warning,
                 "Some constraints say what others already say: " + names(sketch, result.redundant) +
                     ". The sketch still works, but you can delete one of them.",
                 {}});
        }
    }
};

}  // namespace

std::unique_ptr<ISketchSolver> makePlaneGcsSolver() {
    return std::make_unique<PlaneGcsSolver>();
}

}  // namespace kh::sketch
