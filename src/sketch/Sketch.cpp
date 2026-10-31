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

const Entity* Sketch::findEntity(std::string_view name) const {
    for (const auto& e : entities) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

const Constraint* Sketch::findConstraint(std::string_view name) const {
    for (const auto& c : constraints) {
        if (c.name == name) return &c;
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

const std::vector<ConstraintKind>& allKinds() {
    static const std::vector<ConstraintKind> kinds{ConstraintKind::Fix,
                                                   ConstraintKind::Coincident,
                                                   ConstraintKind::Horizontal,
                                                   ConstraintKind::Vertical,
                                                   ConstraintKind::Parallel,
                                                   ConstraintKind::Perpendicular,
                                                   ConstraintKind::Equal,
                                                   ConstraintKind::PointOnLine,
                                                   ConstraintKind::PointOnCircle,
                                                   ConstraintKind::Midpoint,
                                                   ConstraintKind::Tangent,
                                                   ConstraintKind::Distance,
                                                   ConstraintKind::Length,
                                                   ConstraintKind::HorizontalDistance,
                                                   ConstraintKind::VerticalDistance,
                                                   ConstraintKind::Radius,
                                                   ConstraintKind::Diameter,
                                                   ConstraintKind::Angle};
    return kinds;
}

std::string_view kindKey(ConstraintKind kind) {
    switch (kind) {
        case ConstraintKind::Fix:
            return "fix";
        case ConstraintKind::Coincident:
            return "coincident";
        case ConstraintKind::Horizontal:
            return "horizontal";
        case ConstraintKind::Vertical:
            return "vertical";
        case ConstraintKind::Parallel:
            return "parallel";
        case ConstraintKind::Perpendicular:
            return "perpendicular";
        case ConstraintKind::Equal:
            return "equal";
        case ConstraintKind::PointOnLine:
            return "point_on_line";
        case ConstraintKind::PointOnCircle:
            return "point_on_circle";
        case ConstraintKind::Midpoint:
            return "midpoint";
        case ConstraintKind::Tangent:
            return "tangent";
        case ConstraintKind::Distance:
            return "distance";
        case ConstraintKind::Length:
            return "length";
        case ConstraintKind::HorizontalDistance:
            return "horizontal_distance";
        case ConstraintKind::VerticalDistance:
            return "vertical_distance";
        case ConstraintKind::Radius:
            return "radius";
        case ConstraintKind::Diameter:
            return "diameter";
        case ConstraintKind::Angle:
            return "angle";
    }
    return "";
}

std::optional<ConstraintKind> kindFromKey(std::string_view key) {
    for (auto k : allKinds()) {
        if (kindKey(k) == key) return k;
    }
    return std::nullopt;
}

std::string_view roleKey(PointRole role) {
    switch (role) {
        case PointRole::Self:
            return "";
        case PointRole::Start:
            return "start";
        case PointRole::End:
            return "end";
        case PointRole::Center:
            return "center";
        case PointRole::Pole:
            return "pole";
    }
    return "";
}

std::optional<PointRole> roleFromKey(std::string_view key) {
    for (auto r : {PointRole::Self, PointRole::Start, PointRole::End, PointRole::Center, PointRole::Pole}) {
        if (roleKey(r) == key) return r;
    }
    return std::nullopt;
}

std::string pointKey(const PointRef& ref) {
    if (ref.role == PointRole::Pole) return "pole" + std::to_string(ref.index + 1);
    return std::string(roleKey(ref.role));
}

std::optional<PointRef> pointFromKey(const EntityId& entity, std::string_view key) {
    constexpr std::string_view pole = "pole";
    if (key.size() > pole.size() && key.substr(0, pole.size()) == pole) {
        int n = 0;
        for (const char ch : key.substr(pole.size())) {
            if (ch < '0' || ch > '9' || n > 100000) return std::nullopt;
            n = n * 10 + (ch - '0');
        }
        if (n < 1) return std::nullopt;
        return PointRef{entity, PointRole::Pole, n - 1};
    }
    const auto role = roleFromKey(key);
    if (!role || *role == PointRole::Pole) return std::nullopt;  // "pole" needs its number
    return PointRef{entity, *role, 0};
}

std::vector<PointRef> pointsOf(const Entity& entity) {
    const auto id = entity.id;
    if (std::holds_alternative<PointShape>(entity.shape)) return {{id, PointRole::Self}};
    if (std::holds_alternative<LineShape>(entity.shape))
        return {{id, PointRole::Start}, {id, PointRole::End}};
    if (std::holds_alternative<CircleShape>(entity.shape)) return {{id, PointRole::Center}};
    if (std::holds_alternative<ArcShape>(entity.shape))
        return {{id, PointRole::Start}, {id, PointRole::End}, {id, PointRole::Center}};
    std::vector<PointRef> out;
    const auto& s = std::get<SplineShape>(entity.shape);
    for (std::size_t i = 0; i < s.poles.size(); ++i)
        out.push_back({id, PointRole::Pole, static_cast<int>(i)});
    return out;
}

namespace {

// A spline as an ordinary (unclamped) B-spline: its poles (a closed one's first
// `degree` poles repeated at the end), their weights, its whole knot vector, and
// the range of the knots it's drawn over.
struct Flat {
    std::vector<Point2> poles;
    std::vector<double> weights;
    std::vector<double> knots;
    double from = 0, to = 0;
};

Flat flatten(const SplineShape& s) {
    Flat f;
    const int n = static_cast<int>(s.poles.size());
    const int p = s.degree;
    auto weight = [&](int i) { return s.weights.empty() ? 1.0 : s.weights[static_cast<std::size_t>(i)]; };
    if (s.closed) {
        for (int i = 0; i < n + p; ++i) {
            f.poles.push_back(s.poles[static_cast<std::size_t>(i % n)]);
            f.weights.push_back(weight(i % n));
        }
        for (int i = 0; i <= n + 2 * p; ++i) f.knots.push_back(i);
        f.from = p;
        f.to = n + p;
    } else {
        for (int i = 0; i < n; ++i) {
            f.poles.push_back(s.poles[static_cast<std::size_t>(i)]);
            f.weights.push_back(weight(i));
        }
        for (int i = 0; i <= p; ++i) f.knots.push_back(0);
        for (int i = 1; i < n - p; ++i) f.knots.push_back(i);
        for (int i = 0; i <= p; ++i) f.knots.push_back(n - p);
        f.from = 0;
        f.to = n - p;
    }
    return f;
}

// de Boor's algorithm, in homogeneous coordinates (x w, y w, w) for the weights.
Point2 deBoor(const Flat& f, int p, double u) {
    const int last = static_cast<int>(f.poles.size()) - 1;
    int k = p;  // the span: knots[k] <= u < knots[k + 1]
    while (k < last && f.knots[static_cast<std::size_t>(k + 1)] <= u) ++k;
    std::vector<double> x, y, w;
    for (int j = 0; j <= p; ++j) {
        const auto i = static_cast<std::size_t>(j + k - p);
        w.push_back(f.weights[i]);
        x.push_back(f.poles[i].x * f.weights[i]);
        y.push_back(f.poles[i].y * f.weights[i]);
    }
    for (int r = 1; r <= p; ++r) {
        for (int j = p; j >= r; --j) {
            const double a0 = f.knots[static_cast<std::size_t>(j + k - p)];
            const double a1 = f.knots[static_cast<std::size_t>(j + 1 + k - r)];
            const double a = a1 > a0 ? (u - a0) / (a1 - a0) : 0;
            const auto J = static_cast<std::size_t>(j);
            x[J] = (1 - a) * x[J - 1] + a * x[J];
            y[J] = (1 - a) * y[J - 1] + a * y[J];
            w[J] = (1 - a) * w[J - 1] + a * w[J];
        }
    }
    const auto P = static_cast<std::size_t>(p);
    return {x[P] / w[P], y[P] / w[P]};
}

}  // namespace

Point2 splinePoint(const SplineShape& spline, double t) {
    if (spline.poles.empty()) return {};
    if (spline.degree < 1 || static_cast<int>(spline.poles.size()) <= spline.degree)
        return spline.poles.front();
    const Flat f = flatten(spline);
    const double u = f.from + std::clamp(t, 0.0, 1.0) * (f.to - f.from);
    return deBoor(f, spline.degree, u);
}

std::vector<Point2> splineSample(const SplineShape& spline, int perSpan) {
    std::vector<Point2> out;
    if (spline.poles.empty()) return out;
    const int n = static_cast<int>(spline.poles.size());
    const int spans = spline.closed ? n : std::max(1, n - spline.degree);
    const int count = spans * std::max(1, perSpan);
    for (int i = 0; i <= count; ++i) out.push_back(splinePoint(spline, static_cast<double>(i) / count));
    return out;
}

namespace {

// Where on the flat knots (see Flat) a spline made by splinePolesThrough() passes
// through each point. For an open spline, each control point's Greville point
// (the average of the p knots it spans), which always gives a solvable system.
std::vector<double> throughKnots(int n, int p, bool closed) {
    std::vector<double> out;
    if (p < 1 || p > 5 || n < p + 1) return out;
    SplineShape s;
    s.poles.assign(static_cast<std::size_t>(n), {});
    s.degree = p;
    s.closed = closed;
    const Flat f = flatten(s);
    if (closed) {
        // Evenly spaced, so any start works as well as the Greville points: start
        // at the loop's start, so the first point is where the loop begins. The
        // Greville points' half step for an even degree is kept.
        const double half = (p % 2 == 0) ? 0.5 : 0.0;
        for (int i = 0; i < n; ++i) out.push_back(f.from + i + half);
        return out;
    }
    for (int i = 0; i < n; ++i) {
        double sum = 0;
        for (int j = 1; j <= p; ++j) sum += f.knots[static_cast<std::size_t>(i + j)];
        out.push_back(sum / p);
    }
    return out;
}

}  // namespace

std::vector<double> splineThroughAt(int count, int degree, bool closed) {
    std::vector<double> out;
    SplineShape s;
    s.poles.assign(static_cast<std::size_t>(std::max(count, 0)), {});
    s.degree = degree;
    s.closed = closed;
    const auto knots = throughKnots(count, degree, closed);
    if (knots.empty()) return out;
    const Flat f = flatten(s);
    for (double u : knots) out.push_back((u - f.from) / (f.to - f.from));
    return out;
}

std::vector<Point2> splinePolesThrough(const std::vector<Point2>& through, int degree, bool closed) {
    const int n = static_cast<int>(through.size());
    const auto knots = throughKnots(n, degree, closed);
    if (knots.empty()) return {};
    // A[k][i]: how much control point i counts at the k-th point. Found by putting
    // 1 at control point i (and 0 elsewhere) and reading the spline's x there.
    SplineShape unit;
    unit.poles.assign(static_cast<std::size_t>(n), {});
    unit.degree = degree;
    unit.closed = closed;
    const auto N = static_cast<std::size_t>(n);
    std::vector<std::vector<double>> a(N, std::vector<double>(N + 2, 0.0));
    for (std::size_t i = 0; i < N; ++i) {
        unit.poles[i] = {1, 0};
        const Flat f = flatten(unit);
        for (std::size_t k = 0; k < N; ++k) a[k][i] = deBoor(f, degree, knots[k]).x;
        unit.poles[i] = {0, 0};
    }
    for (std::size_t k = 0; k < N; ++k) {
        a[k][N] = through[k].x;
        a[k][N + 1] = through[k].y;
    }
    // Gaussian elimination with partial pivoting; the matrix is small and well behaved.
    for (std::size_t c = 0; c < N; ++c) {
        std::size_t best = c;
        for (std::size_t r = c + 1; r < N; ++r) {
            if (std::abs(a[r][c]) > std::abs(a[best][c])) best = r;
        }
        if (std::abs(a[best][c]) < 1e-12) return {};
        std::swap(a[c], a[best]);
        for (std::size_t r = 0; r < N; ++r) {
            if (r == c || a[r][c] == 0) continue;
            const double m = a[r][c] / a[c][c];
            for (std::size_t j = c; j < N + 2; ++j) a[r][j] -= m * a[c][j];
        }
    }
    std::vector<Point2> poles;
    for (std::size_t i = 0; i < N; ++i) poles.push_back({a[i][N] / a[i][i], a[i][N + 1] / a[i][i]});
    return poles;
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
    } else if (const auto* s = std::get_if<SplineShape>(&e->shape)) {
        if (ref.role == PointRole::Pole && ref.index >= 0 && ref.index < static_cast<int>(s->poles.size()))
            return s->poles[static_cast<std::size_t>(ref.index)];
        // An open spline starts at its first pole and ends at its last.
        if (!s->closed && !s->poles.empty()) {
            if (ref.role == PointRole::Start) return s->poles.front();
            if (ref.role == PointRole::End) return s->poles.back();
        }
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
        } else if (const auto* s = std::get_if<SplineShape>(&e.shape)) {
            const auto n = s->poles.size();
            bool numbers = std::all_of(s->poles.begin(), s->poles.end(), finite);
            for (double w : s->weights) numbers = numbers && std::isfinite(w);
            if (!numbers) {
                out.push_back(problem("sketch.number", "A spline has a control point that isn't a number."));
            } else if (s->degree < 1 || s->degree > 5) {
                out.push_back(problem("sketch.spline_degree",
                                      "A spline's degree must be 1 to 5. 3 is the usual: pick that."));
            } else if (n < static_cast<std::size_t>(s->degree) + 1) {
                out.push_back(
                    problem("sketch.spline_poles", "A spline of degree " + std::to_string(s->degree) +
                                                       " needs at least " + std::to_string(s->degree + 1) +
                                                       " control points. Add more, or lower its degree."));
            } else if (!s->weights.empty() && s->weights.size() != n) {
                out.push_back(
                    problem("sketch.spline_weights",
                            "A spline needs one weight for each control point. The sketch is damaged."));
            } else if (std::any_of(s->weights.begin(), s->weights.end(), [](double w) { return w <= 0; })) {
                out.push_back(
                    problem("sketch.spline_weights",
                            "A spline's weights must be more than 0. Type a bigger weight, like 1."));
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

    // Names are how students and kh_cli point at things, so no two may match.
    std::set<std::string> names;
    auto nameOnce = [&](const std::string& name) {
        if (!name.empty() && !names.insert(name).second) {
            out.push_back(problem("sketch.duplicate_name",
                                  "Two things in the sketch are called " + name + ". Rename one of them."));
        }
    };
    for (const auto& e : sketch.entities) nameOnce(e.name);
    for (const auto& c : sketch.constraints) nameOnce(c.name);

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
            out.push_back(problem("sketch.point", name + " uses a point that its line, circle, arc or spline "
                                                         "doesn't have. The sketch is damaged."));
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
                if (np != 2 || ne != 0 || c.points[0] == c.points[1]) {
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
