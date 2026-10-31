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
    // A spline's control points, and the spline itself, made of them (for points
    // on it, and tangency at its ends). Its ends are its first and last pole.
    std::vector<GCS::Point> poles;
    GCS::BSpline bspline;
};

// How many numbers a sketch's entities can need: 9 for most (an arc: center,
// radius, two angles, two ends); for a spline, 2 per pole, a weight per pole and
// its knots (at most one more than its poles).
std::size_t entityNumbers(const std::vector<Entity>& entities) {
    std::size_t n = 0;
    for (const auto& e : entities) {
        const auto* s = std::get_if<SplineShape>(&e.shape);
        n += s ? 4 * s->poles.size() + 2 : 9;
    }
    return n;
}

// Where two curves joined end to end bend equally (G2): the difference of their
// curvature vectors (see kh::sketch::curvatureVector()) across the first curve's
// direction there. PlaneGCS has no such constraint, so it's Khan's own: it reads
// the curves' numbers through pvec (which PlaneGCS may redirect while it solves)
// and works out its gradient by central differences.
// Curvature is in 1/mm, so a mismatch is tiny next to the millimeters the other
// constraints are off by: weighed as it is, PlaneGCS can stop with it still
// wrong, especially when a spline's bend has to turn around. Measured as mm at
// 100 mm (the size of a part a student makes), it counts like the others. The
// solved shape is the same; only the path to it changes.
constexpr double kCurvatureScale = 100;

class ConstraintSameCurvature final : public GCS::Constraint {
public:
    // One curve, at the joined end: a line (its two ends), an arc (its center,
    // radius and the end), or a spline (its poles, with fixed weights).
    struct Curve {
        enum Kind { Line, Arc, Spline } kind = Line;
        std::size_t first = 0;  // where its numbers start in pvec
        std::size_t count = 0;
        PointRef end{};  // which end is joined (for a spline: its start or end)
        int degree = 3;  // a spline's
        std::vector<double> weights{};
    };

    ConstraintSameCurvature(Curve a, std::vector<double*> aParams, Curve b, std::vector<double*> bParams)
        : a_(std::move(a)), b_(std::move(b)) {
        a_.first = 0;
        a_.count = aParams.size();
        b_.first = aParams.size();
        b_.count = bParams.size();
        pvec = std::move(aParams);
        pvec.insert(pvec.end(), bParams.begin(), bParams.end());
        origpvec = pvec;
        rescale();
    }

    void errorgrad(double* err, double* grad, double* param) override {
        if (err) *err = value();
        if (grad) {
            *grad = 0;
            const int i = findParamInPvec(param);
            if (i < 0) return;
            double* x = pvec[static_cast<std::size_t>(i)];
            const double was = *x;
            const double h = 1e-6 * std::max(1.0, std::abs(was));
            *x = was + h;
            const double up = value();
            *x = was - h;
            const double down = value();
            *x = was;
            *grad = (up - down) / (2 * h);
        }
    }

private:
    // The curve as it is now, at its joined end.
    std::optional<CurveEnd> endOf(const Curve& c) const {
        auto at = [&](std::size_t k) { return *pvec[c.first + k]; };
        switch (c.kind) {
            case Curve::Line:
                return curveEnd(LineShape{{at(0), at(1)}, {at(2), at(3)}}, c.end);
            case Curve::Arc: {
                // Center, radius, then the joined end: the bend is towards the center.
                const Point2 center{at(0), at(1)}, end{at(3), at(4)};
                const double r = at(2);
                const double angle = std::atan2(end.y - center.y, end.x - center.x);
                return curveEnd(ArcShape{center, r, angle, angle + 1}, {{}, PointRole::Start});
            }
            case Curve::Spline: {
                SplineShape s;
                for (std::size_t k = 0; k + 1 < c.count; k += 2) s.poles.push_back({at(k), at(k + 1)});
                s.weights = c.weights;
                s.degree = c.degree;
                return curveEnd(s, c.end);
            }
        }
        return std::nullopt;
    }

    double value() const {
        const auto ea = endOf(a_), eb = endOf(b_);
        if (!ea || !eb) return 0;
        const auto ka = curvatureVector(*ea), kb = curvatureVector(*eb);
        const double speed = std::hypot(ea->d1.x, ea->d1.y);
        if (speed <= 0) return 0;
        // Across the first curve: its direction turned a quarter turn.
        const Point2 across{-ea->d1.y / speed, ea->d1.x / speed};
        return kCurvatureScale * ((ka.x - kb.x) * across.x + (ka.y - kb.y) * across.y);
    }

    Curve a_, b_;
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
        out += (out.empty() ? "" : ", ") + (c->name.empty() ? std::string(kindName(c->kind)) : c->name);
    }
    return out;
}

// Which side of the line a point is on, as PlaneGCS measures it (its
// ConstraintP2LDistance::signed_value()).
double sideOf(const GCS::Point& p, const GCS::Line& l) {
    const double x0 = *p.x, y0 = *p.y, x1 = *l.p1.x, y1 = *l.p1.y, x2 = *l.p2.x, y2 = *l.p2.y;
    return -x0 * (y2 - y1) + y0 * (x2 - x1) + x1 * y2 - x2 * y1;
}

// A sketch set up in PlaneGCS: its numbers, its entities and its constraints.
// It holds pointers into itself, so it's made once and never moved.
class GcsModel {
public:
    // The entities' numbers (see entityNumbers()), 2 constants per constraint (a
    // Fix's x and y) and 4 per pull (where it pulls to, and where a pulled edge's
    // center is now).
    GcsModel(const std::vector<Entity>& entities, const Sketch& sketch, const std::vector<Pull>& pulls = {})
        : builder(entityNumbers(entities) + 2 * sketch.constraints.size() + 4 * pulls.size()),
          built(entities.size()) {
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
            } else if (const auto* s = std::get_if<SplineShape>(&shape)) {
                for (const auto& pole : s->poles) g.poles.push_back(b.point(pole));
                // The same spline in PlaneGCS's terms: its poles (the same numbers),
                // fixed weights, and its knots as distinct values with how many times
                // each repeats (clamped: degree + 1 at the ends; periodic: once each).
                auto& bs = g.bspline;
                const int n = static_cast<int>(s->poles.size());
                bs.poles = g.poles;
                for (int k = 0; k < n; ++k)
                    bs.weights.push_back(
                        b.constant(s->weights.empty() ? 1.0 : s->weights[static_cast<std::size_t>(k)]));
                bs.degree = s->degree;
                bs.periodic = s->closed;
                const int last = s->closed ? n : n - s->degree;
                for (int k = 0; k <= last; ++k) {
                    bs.knots.push_back(b.constant(k));
                    bs.mult.push_back(!s->closed && (k == 0 || k == last) ? s->degree + 1 : 1);
                }
                bs.start = g.poles.front();
                bs.end = s->closed ? g.poles.front() : g.poles.back();
                bs.setupFlattenedKnots();
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
            if (std::holds_alternative<SplineShape>(s)) {
                // check() made sure the pole is there; an open spline's ends are its first and last pole.
                if (ref.role == PointRole::Start) return g.poles.front();
                if (ref.role == PointRole::End) return g.poles.back();
                return g.poles[static_cast<std::size_t>(ref.index)];
            }
            if (ref.role == PointRole::Center) return g.arc.center;
            return ref.role == PointRole::Start ? g.arc.start : g.arc.end;
        };
        auto lineOf = [&](const EntityId& id) -> GCS::Line& { return built[index(id)].line; };
        auto isArc = [&](const EntityId& id) { return std::holds_alternative<ArcShape>(shapeOf(id)); };
        auto isLine = [&](const EntityId& id) { return std::holds_alternative<LineShape>(shapeOf(id)); };
        auto isSpline = [&](const EntityId& id) { return std::holds_alternative<SplineShape>(shapeOf(id)); };
        auto curveOf = [&](const EntityId& id) -> GCS::Curve& {
            auto& g = built[index(id)];
            if (isLine(id)) return g.line;
            if (isArc(id)) return g.arc;
            if (isSpline(id)) return g.bspline;
            return g.circle;
        };
        // Two curves joined end to end head the same way at the join: same
        // direction (0) or opposite (pi), whichever it's closer to now. Measured
        // at a spline's own end where there is one: PlaneGCS knows a spline's
        // direction only at its ends.
        auto sameDirection = [&](const EntityId& e0, const EntityId& e1,
                                 const std::pair<PointRef, PointRef>& joint, int tag) {
            GCS::Curve& c0 = curveOf(e0);
            GCS::Curve& c1 = curveOf(e1);
            GCS::Point& p = pointOfRef(isSpline(e1) && !isSpline(e0) ? joint.second : joint.first);
            const auto n0 = c0.CalculateNormal(p);
            const auto n1 = c1.CalculateNormal(p);
            const bool same = n0.x * n1.x + n0.y * n1.y >= 0;
            sys.addConstraintAngleViaPoint(c0, c1, p, b.constant(same ? 0.0 : std::numbers::pi), tag);
        };
        // A curve at its joined end, for ConstraintSameCurvature.
        auto curvatureCurve = [&](const EntityId& id, const PointRef& end, std::vector<double*>& params) {
            ConstraintSameCurvature::Curve c;
            auto& g = built[index(id)];
            c.end = end;
            if (isLine(id)) {
                c.kind = ConstraintSameCurvature::Curve::Line;
                params = {g.line.p1.x, g.line.p1.y, g.line.p2.x, g.line.p2.y};
            } else if (isArc(id)) {
                c.kind = ConstraintSameCurvature::Curve::Arc;
                const auto& e = end.role == PointRole::Start ? g.arc.start : g.arc.end;
                params = {g.arc.center.x, g.arc.center.y, g.arc.rad, e.x, e.y};
            } else {
                const auto& sp = std::get<SplineShape>(shapeOf(id));
                c.kind = ConstraintSameCurvature::Curve::Spline;
                c.degree = sp.degree;
                c.weights = sp.weights;
                for (const auto& q : g.poles) {
                    params.push_back(q.x);
                    params.push_back(q.y);
                }
            }
            return c;
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
                case ConstraintKind::PointOnSpline: {
                    // PlaneGCS adds a number for where along the spline the point is,
                    // starting from the nearest place now (its knots run 0 to the
                    // number of spans, as Khan's do).
                    auto& g = built[index(e[0])];
                    const auto& sp = std::get<SplineShape>(shapeOf(e[0]));
                    auto& point = pointOfRef(c.points[0]);
                    const double t = nearestOnSpline(sp, {*point.x, *point.y});
                    const int spans = sp.closed ? static_cast<int>(sp.poles.size())
                                                : static_cast<int>(sp.poles.size()) - sp.degree;
                    sys.addConstraintPointOnBSpline(point, g.bspline, b.unknown(t * spans), tag);
                    break;
                }
                case ConstraintKind::Smooth: {
                    const auto joint = jointOf(whole, e[0], e[1]);  // check() made sure there is one
                    if (!joint) break;
                    sameDirection(e[0], e[1], *joint, tag);
                    std::vector<double*> pa, pb;
                    auto ca = curvatureCurve(e[0], joint->first, pa);
                    auto cb = curvatureCurve(e[1], joint->second, pb);
                    auto* same = new ConstraintSameCurvature(std::move(ca), std::move(pa), std::move(cb),
                                                             std::move(pb));
                    same->setTag(tag);
                    sys.addConstraint(same);  // PlaneGCS owns it from here
                    break;
                }
                case ConstraintKind::Symmetric:
                    sys.addConstraintP2PSymmetric(pointOfRef(c.points[0]), pointOfRef(c.points[1]),
                                                  lineOf(e[0]), tag);
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
                        sameDirection(e[0], e[1], *joint, tag);
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

        // Pulls, while dragging: temporary constraints, which PlaneGCS keeps only as
        // far as the real ones let it (it solves them second, as a best fit).
        for (const auto& pull : pulls) {
            if (!whole.find(pull.point.entity)) continue;
            GCS::Point to;
            to.x = b.constant(pull.to.x);
            to.y = b.constant(pull.to.y);
            if (pull.edge) {
                const auto& s = shapeOf(pull.point.entity);
                if (std::holds_alternative<CircleShape>(s) || std::holds_alternative<ArcShape>(s)) {
                    // The edge goes through the mouse, and the center tries to stay where
                    // it is (as in FreeCAD), so the size changes rather than the place.
                    auto& circle = circleOf(pull.point.entity);
                    GCS::Point here;
                    here.x = b.constant(*circle.center.x);
                    here.y = b.constant(*circle.center.y);
                    sys.addConstraintPointOnCircle(to, circle, GCS::DefaultTemporaryConstraint);
                    sys.addConstraintP2PCoincident(circle.center, here, GCS::DefaultTemporaryConstraint);
                }
            } else if (pointOf(whole, pull.point)) {
                sys.addConstraintP2PCoincident(pointOfRef(pull.point), to, GCS::DefaultTemporaryConstraint);
            }
        }

        sys.declareUnknowns(builder.unknowns());
    }

    // After sys.diagnose (initSolution diagnoses): which points and entities can
    // still move. A number is free if PlaneGCS lists it as "dependent" (not set
    // by the constraints); with no constraints at all, every number is free.
    void freeParts(const std::vector<Entity>& entities, SolveResult& result) const {
        GCS::VEC_pD dependent;
        sys.getDependentParams(dependent);
        const std::set<const double*> loose(dependent.begin(), dependent.end());
        const bool all = sys.isEmptyDiagnoseMatrix();
        auto isFree = [&](const double* p) { return all || loose.count(p) > 0; };
        auto pointFree = [&](const GCS::Point& p) { return isFree(p.x) || isFree(p.y); };
        for (std::size_t i = 0; i < entities.size(); ++i) {
            const auto& g = built[i];
            const auto id = entities[i].id;
            std::vector<std::pair<PointRole, bool>> points;
            bool size = false;  // a radius or an angle
            const auto& s = entities[i].shape;
            if (std::holds_alternative<SplineShape>(s)) {
                bool any = false;
                for (std::size_t k = 0; k < g.poles.size(); ++k) {
                    if (!pointFree(g.poles[k])) continue;
                    result.freePoints.push_back({id, PointRole::Pole, static_cast<int>(k)});
                    any = true;
                }
                if (any) result.freeEntities.push_back(id);
                continue;
            }
            if (std::holds_alternative<PointShape>(s)) {
                points = {{PointRole::Self, pointFree(g.point)}};
            } else if (std::holds_alternative<LineShape>(s)) {
                points = {{PointRole::Start, pointFree(g.line.p1)}, {PointRole::End, pointFree(g.line.p2)}};
            } else if (std::holds_alternative<CircleShape>(s)) {
                points = {{PointRole::Center, pointFree(g.circle.center)}};
                size = isFree(g.circle.rad);
            } else {
                points = {{PointRole::Start, pointFree(g.arc.start)},
                          {PointRole::End, pointFree(g.arc.end)},
                          {PointRole::Center, pointFree(g.arc.center)}};
                size = isFree(g.arc.rad) || isFree(g.arc.startAngle) || isFree(g.arc.endAngle);
            }
            bool any = size;
            for (const auto& [role, free] : points) {
                if (!free) continue;
                result.freePoints.push_back({id, role});
                any = true;
            }
            if (any) result.freeEntities.push_back(id);
        }
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
                    } else if constexpr (std::is_same_v<S, SplineShape>) {
                        for (std::size_t k = 0; k < s.poles.size(); ++k) s.poles[k] = p2(g.poles[k]);
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

    SolveResult drag(const Sketch& sketch, const std::vector<Pull>& pulls) override {
        SolveResult result;
        result.entities = sketch.entities;
        if (auto problems = check(sketch); !problems.empty()) {
            result.status = SolveStatus::Invalid;
            result.issues = std::move(problems);
            return result;
        }
        if (sketch.entities.empty()) {
            result.status = SolveStatus::Solved;
            return result;
        }
        try {
            GcsModel model(sketch.entities, sketch, pulls);
            model.sys.initSolution();
            model.sys.solve();
            model.sys.applySolution();
            auto moved = model.read(sketch.entities);
            if (const auto problem = checkAnswer(sketch, moved)) {
                result.status = SolveStatus::Failed;
                result.issues.push_back(*problem);
                return result;
            }
            result.entities = std::move(moved);
            result.status = SolveStatus::Solved;
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

    // Whether an answer really makes every constraint true, with no circle turned
    // inside out. If not, the problem, in plain words.
    static std::optional<Diagnostic> checkAnswer(const Sketch& sketch, const std::vector<Entity>& answer) {
        Sketch after{answer, sketch.constraints};
        std::vector<ConstraintId> off;
        for (const auto& c : sketch.constraints) {
            if (!(error(after, c) <= kTolerance)) off.push_back(c.id);
        }
        bool sizes = true;
        for (const auto& e : answer) {
            if (const auto* c = std::get_if<CircleShape>(&e.shape)) sizes = sizes && c->radius > 0;
            if (const auto* a = std::get_if<ArcShape>(&e.shape)) sizes = sizes && a->radius > 0;
        }
        if (off.empty() && sizes) return std::nullopt;
        return Diagnostic{"sketch.solve_failed",
                          Severity::Error,
                          off.empty()
                              ? std::string("The sketch couldn't be solved without turning a circle "
                                            "inside out. Undo the last change, or change a dimension.")
                              : "The sketch couldn't be solved: " + names(sketch, off) +
                                    " couldn't be made true. Undo the last change, or delete a constraint.",
                          {}};
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
        if (const auto problem = checkAnswer(sketch, solved)) {
            result.status = SolveStatus::Failed;
            result.issues.push_back(*problem);
            return;
        }
        second->freeParts(solved, result);
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
