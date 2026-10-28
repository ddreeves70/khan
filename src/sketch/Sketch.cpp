#include "kh/sketch/Sketch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

namespace kh::sketch {

namespace {

constexpr double kHuge = std::numeric_limits<double>::max();

bool isLine(const Entity* e) {
    return e && std::holds_alternative<LineShape>(e->shape);
}

bool isRound(const Entity* e) {
    return e && (std::holds_alternative<CircleShape>(e->shape) || std::holds_alternative<ArcShape>(e->shape));
}

// A circle's or arc's center and radius.
bool roundOf(const Entity* e, Point2& center, double& radius) {
    if (!e) return false;
    if (const auto* c = std::get_if<CircleShape>(&e->shape)) {
        center = c->center;
        radius = c->radius;
        return true;
    }
    if (const auto* a = std::get_if<ArcShape>(&e->shape)) {
        center = a->center;
        radius = a->radius;
        return true;
    }
    return false;
}

double distance(Point2 a, Point2 b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

// Distance from p to the infinite line through a and b.
double lineDistance(Point2 p, Point2 a, Point2 b) {
    const double len = distance(a, b);
    if (len == 0) return distance(p, a);
    return std::abs((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / len;
}

double direction(const LineShape& l) {
    return std::atan2(l.end.y - l.start.y, l.end.x - l.start.x);
}

// An angle brought into -pi..pi.
double wrap(double a) {
    a = std::remainder(a, 2 * std::numbers::pi);
    return a;
}

bool finite(Point2 p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

Diagnostic problem(std::string code, std::string message) {
    return {std::move(code), Severity::Error, std::move(message), {}};
}

}  // namespace

const Entity* Sketch::find(const EntityId& id) const {
    for (const auto& e : entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const Constraint* Sketch::find(const ConstraintId& id) const {
    for (const auto& c : constraints) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

std::string_view kindName(ConstraintKind kind) {
    switch (kind) {
        case ConstraintKind::Fix:
            return "Fix";
        case ConstraintKind::Coincident:
            return "Coincident";
        case ConstraintKind::Horizontal:
            return "Horizontal";
        case ConstraintKind::Vertical:
            return "Vertical";
        case ConstraintKind::Parallel:
            return "Parallel";
        case ConstraintKind::Perpendicular:
            return "Perpendicular";
        case ConstraintKind::Equal:
            return "Equal";
        case ConstraintKind::PointOnLine:
            return "Point on line";
        case ConstraintKind::PointOnCircle:
            return "Point on circle";
        case ConstraintKind::Midpoint:
            return "Midpoint";
        case ConstraintKind::Tangent:
            return "Tangent";
        case ConstraintKind::Distance:
            return "Distance";
        case ConstraintKind::Length:
            return "Length";
        case ConstraintKind::HorizontalDistance:
            return "Horizontal distance";
        case ConstraintKind::VerticalDistance:
            return "Vertical distance";
        case ConstraintKind::Radius:
            return "Radius";
        case ConstraintKind::Diameter:
            return "Diameter";
        case ConstraintKind::Angle:
            return "Angle";
    }
    return "Constraint";
}

bool isDimension(ConstraintKind kind) {
    switch (kind) {
        case ConstraintKind::Distance:
        case ConstraintKind::Length:
        case ConstraintKind::HorizontalDistance:
        case ConstraintKind::VerticalDistance:
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter:
        case ConstraintKind::Angle:
            return true;
        default:
            return false;
    }
}

std::optional<Point2> pointOf(const Sketch& sketch, const PointRef& ref) {
    const Entity* e = sketch.find(ref.entity);
    if (!e) return std::nullopt;
    if (const auto* p = std::get_if<PointShape>(&e->shape)) {
        if (ref.role == PointRole::Self) return p->at;
    } else if (const auto* l = std::get_if<LineShape>(&e->shape)) {
        if (ref.role == PointRole::Start) return l->start;
        if (ref.role == PointRole::End) return l->end;
    } else if (const auto* c = std::get_if<CircleShape>(&e->shape)) {
        if (ref.role == PointRole::Center) return c->center;
    } else if (const auto* a = std::get_if<ArcShape>(&e->shape)) {
        if (ref.role == PointRole::Center) return a->center;
        if (ref.role == PointRole::Start) {
            return Point2{a->center.x + a->radius * std::cos(a->startAngle),
                          a->center.y + a->radius * std::sin(a->startAngle)};
        }
        if (ref.role == PointRole::End) {
            return Point2{a->center.x + a->radius * std::cos(a->endAngle),
                          a->center.y + a->radius * std::sin(a->endAngle)};
        }
    }
    return std::nullopt;
}

Diagnostics check(const Sketch& sketch) {
    Diagnostics out;
    std::set<EntityId> entityIds;
    for (const auto& e : sketch.entities) {
        if (!entityIds.insert(e.id).second) {
            out.push_back(problem("sketch.duplicate",
                                  "Two things in the sketch have the same id. The sketch is damaged."));
        }
        if (const auto* p = std::get_if<PointShape>(&e.shape)) {
            if (!finite(p->at))
                out.push_back(problem("sketch.number", "A point has a position that isn't a number."));
        } else if (const auto* l = std::get_if<LineShape>(&e.shape)) {
            if (!finite(l->start) || !finite(l->end)) {
                out.push_back(problem("sketch.number", "A line has an end that isn't a number."));
            } else if (distance(l->start, l->end) < 1e-9) {
                out.push_back(
                    problem("sketch.line_length",
                            "A line needs two different ends. Drag one end away, or delete the line."));
            }
        } else {
            Point2 center;
            double radius = 0;
            roundOf(&e, center, radius);
            if (!finite(center) || !std::isfinite(radius)) {
                out.push_back(problem("sketch.number", "A circle or arc has a size that isn't a number."));
            } else if (radius <= 0) {
                out.push_back(problem("sketch.radius", "A circle or arc needs a radius greater than 0 mm."));
            }
            if (const auto* a = std::get_if<ArcShape>(&e.shape)) {
                if (!std::isfinite(a->startAngle) || !std::isfinite(a->endAngle)) {
                    out.push_back(problem("sketch.number", "An arc has an angle that isn't a number."));
                }
            }
        }
    }

    std::set<ConstraintId> constraintIds;
    for (const auto& c : sketch.constraints) {
        const std::string name(kindName(c.kind));
        if (!constraintIds.insert(c.id).second) {
            out.push_back(
                problem("sketch.duplicate", "Two constraints have the same id. The sketch is damaged."));
        }
        // Everything it uses must be in the sketch, and each point must exist.
        bool missing = false;
        for (const auto& id : c.entities) missing = missing || !sketch.find(id);
        for (const auto& p : c.points) missing = missing || !sketch.find(p.entity);
        if (missing) {
            out.push_back(problem("sketch.missing",
                                  name + " uses something that isn't in the sketch anymore. Delete the " +
                                      name + " constraint."));
            continue;
        }
        bool badPoint = false;
        for (const auto& p : c.points) badPoint = badPoint || !pointOf(sketch, p);
        if (badPoint) {
            out.push_back(problem(
                "sketch.point",
                name + " uses a point that its line, circle or arc doesn't have. The sketch is damaged."));
            continue;
        }
        const auto np = c.points.size();
        const auto ne = c.entities.size();
        const Entity* e0 = ne > 0 ? sketch.find(c.entities[0]) : nullptr;
        const Entity* e1 = ne > 1 ? sketch.find(c.entities[1]) : nullptr;
        const bool twoDifferent = ne == 2 && c.entities[0] != c.entities[1];
        std::string need;  // what it needs, if it doesn't have it
        switch (c.kind) {
            case ConstraintKind::Fix:
                if (np != 1 || ne != 0)
                    need = "one point";
                else if (!finite(c.at))
                    need = "a position that's a number";
                break;
            case ConstraintKind::Coincident:
                if (np != 2 || ne != 0 ||
                    (c.points[0].entity == c.points[1].entity && c.points[0].role == c.points[1].role)) {
                    need = "two different points";
                }
                break;
            case ConstraintKind::Horizontal:
            case ConstraintKind::Vertical:
            case ConstraintKind::Length:
                if (np != 0 || ne != 1 || !isLine(e0)) need = "one line";
                break;
            case ConstraintKind::Parallel:
            case ConstraintKind::Perpendicular:
            case ConstraintKind::Angle:
                if (np != 0 || !twoDifferent || !isLine(e0) || !isLine(e1)) need = "two different lines";
                break;
            case ConstraintKind::Equal:
                if (np != 0 || !twoDifferent ||
                    !((isLine(e0) && isLine(e1)) || (isRound(e0) && isRound(e1)))) {
                    need = "two lines, or two circles or arcs";
                }
                break;
            case ConstraintKind::PointOnLine:
            case ConstraintKind::Midpoint:
                if (np != 1 || ne != 1 || !isLine(e0)) need = "one point and one line";
                break;
            case ConstraintKind::PointOnCircle:
                if (np != 1 || ne != 1 || !isRound(e0)) need = "one point and one circle or arc";
                break;
            case ConstraintKind::Tangent:
                if (np != 0 || !twoDifferent || !(isRound(e0) || isRound(e1)) ||
                    !(isLine(e0) || isRound(e0)) || !(isLine(e1) || isRound(e1))) {
                    need = "a line and a circle or arc, or two circles or arcs";
                }
                break;
            case ConstraintKind::Distance:
            case ConstraintKind::HorizontalDistance:
            case ConstraintKind::VerticalDistance:
                if (np != 2 || ne != 0) need = "two points";
                break;
            case ConstraintKind::Radius:
            case ConstraintKind::Diameter:
                if (np != 0 || ne != 1 || !isRound(e0)) need = "one circle or arc";
                break;
        }
        if (!need.empty()) {
            out.push_back(problem("sketch.constraint", name + " needs " + need + "."));
            continue;
        }
        if (isDimension(c.kind)) {
            if (!std::isfinite(c.value)) {
                out.push_back(
                    problem("sketch.number", "The " + name + " dimension isn't a number. Type a number."));
            } else if ((c.kind == ConstraintKind::Distance || c.kind == ConstraintKind::Length ||
                        c.kind == ConstraintKind::Radius || c.kind == ConstraintKind::Diameter) &&
                       c.value <= 0) {
                out.push_back(problem("sketch.dimension",
                                      "A " + name + " must be more than 0 mm. Type a bigger number."));
            }
        }
    }
    return out;
}

double error(const Sketch& sketch, const Constraint& c) {
    auto pt = [&](std::size_t i) {
        return i < c.points.size() ? pointOf(sketch, c.points[i]) : std::nullopt;
    };
    auto ent = [&](std::size_t i) { return i < c.entities.size() ? sketch.find(c.entities[i]) : nullptr; };
    auto line = [&](std::size_t i) -> const LineShape* {
        const Entity* e = ent(i);
        return e ? std::get_if<LineShape>(&e->shape) : nullptr;
    };
    switch (c.kind) {
        case ConstraintKind::Fix: {
            const auto p = pt(0);
            return p ? distance(*p, c.at) : kHuge;
        }
        case ConstraintKind::Coincident: {
            const auto a = pt(0), b = pt(1);
            return a && b ? distance(*a, *b) : kHuge;
        }
        case ConstraintKind::Horizontal: {
            const auto* l = line(0);
            return l ? std::abs(l->end.y - l->start.y) : kHuge;
        }
        case ConstraintKind::Vertical: {
            const auto* l = line(0);
            return l ? std::abs(l->end.x - l->start.x) : kHuge;
        }
        case ConstraintKind::Parallel:
        case ConstraintKind::Perpendicular: {
            const auto* a = line(0);
            const auto* b = line(1);
            if (!a || !b) return kHuge;
            // How far the angle between them is from 0 (or 90 degrees), either way round.
            double d = wrap(direction(*b) - direction(*a));
            if (c.kind == ConstraintKind::Perpendicular) d -= std::numbers::pi / 2;
            return std::abs(std::remainder(d, std::numbers::pi));
        }
        case ConstraintKind::Equal: {
            if (const auto* a = line(0)) {
                const auto* b = line(1);
                return b ? std::abs(distance(a->start, a->end) - distance(b->start, b->end)) : kHuge;
            }
            Point2 ca, cb;
            double ra = 0, rb = 0;
            return roundOf(ent(0), ca, ra) && roundOf(ent(1), cb, rb) ? std::abs(ra - rb) : kHuge;
        }
        case ConstraintKind::PointOnLine: {
            const auto p = pt(0);
            const auto* l = line(0);
            return p && l ? lineDistance(*p, l->start, l->end) : kHuge;
        }
        case ConstraintKind::PointOnCircle: {
            const auto p = pt(0);
            Point2 center;
            double r = 0;
            return p && roundOf(ent(0), center, r) ? std::abs(distance(*p, center) - r) : kHuge;
        }
        case ConstraintKind::Midpoint: {
            const auto p = pt(0);
            const auto* l = line(0);
            if (!p || !l) return kHuge;
            return distance(*p, Point2{(l->start.x + l->end.x) / 2, (l->start.y + l->end.y) / 2});
        }
        case ConstraintKind::Tangent: {
            Point2 c0, c1;
            double r0 = 0, r1 = 0;
            const bool round0 = roundOf(ent(0), c0, r0);
            const bool round1 = roundOf(ent(1), c1, r1);
            if (round0 && round1) {
                // Touching from outside or from inside, whichever is closer.
                const double d = distance(c0, c1);
                return std::min(std::abs(d - (r0 + r1)), std::abs(d - std::abs(r0 - r1)));
            }
            const auto* l = round0 ? line(1) : line(0);
            if (!l || !(round0 || round1)) return kHuge;
            const Point2 center = round0 ? c0 : c1;
            const double r = round0 ? r0 : r1;
            return std::abs(lineDistance(center, l->start, l->end) - r);
        }
        case ConstraintKind::Distance: {
            const auto a = pt(0), b = pt(1);
            return a && b ? std::abs(distance(*a, *b) - c.value) : kHuge;
        }
        case ConstraintKind::Length: {
            const auto* l = line(0);
            return l ? std::abs(distance(l->start, l->end) - c.value) : kHuge;
        }
        case ConstraintKind::HorizontalDistance: {
            const auto a = pt(0), b = pt(1);
            return a && b ? std::abs((b->x - a->x) - c.value) : kHuge;
        }
        case ConstraintKind::VerticalDistance: {
            const auto a = pt(0), b = pt(1);
            return a && b ? std::abs((b->y - a->y) - c.value) : kHuge;
        }
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter: {
            Point2 center;
            double r = 0;
            if (!roundOf(ent(0), center, r)) return kHuge;
            return std::abs((c.kind == ConstraintKind::Diameter ? 2 * r : r) - c.value);
        }
        case ConstraintKind::Angle: {
            const auto* a = line(0);
            const auto* b = line(1);
            return a && b ? std::abs(wrap(direction(*b) - direction(*a) - c.value)) : kHuge;
        }
    }
    return kHuge;
}

}  // namespace kh::sketch
