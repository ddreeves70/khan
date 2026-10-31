#include "kh/sketch/Profiles.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>

namespace kh::sketch {

namespace {

constexpr double kPi = std::numbers::pi;

// A line or arc of the sketch, with its ends.
struct Piece {
    const Entity* entity;
    Point2 start, end;
    int startVertex = -1, endVertex = -1;
};

Point2 arcPoint(const ArcShape& a, double t) {
    return {a.center.x + a.radius * std::cos(t), a.center.y + a.radius * std::sin(t)};
}

// The signed area an edge sweeps as seen from the origin (Green's theorem),
// so the sum around a loop is its signed area (positive counterclockwise).
double sweep(const Shape& shape, bool reversed) {
    double a = 0;
    if (const auto* l = std::get_if<LineShape>(&shape)) {
        a = (l->start.x * l->end.y - l->end.x * l->start.y) / 2;
    } else if (const auto* r = std::get_if<ArcShape>(&shape)) {
        const double t0 = r->startAngle, t1 = r->endAngle;
        a = (r->radius * r->center.x * (std::sin(t1) - std::sin(t0)) -
             r->radius * r->center.y * (std::cos(t1) - std::cos(t0)) + r->radius * r->radius * (t1 - t0)) /
            2;
    } else if (const auto* c = std::get_if<CircleShape>(&shape)) {
        a = kPi * c->radius * c->radius;
    } else if (const auto* s = std::get_if<SplineShape>(&shape)) {
        // (x dy - y dx) / 2 along the curve, by 8-point Gauss-Legendre on each piece
        // between knots: exact (to rounding) for an ordinary spline, whose pieces are
        // polynomials of degree 5 or less, and very close for a weighted one.
        static constexpr double kNode[4] = {0.1834346424956498, 0.5255324099163290, 0.7966664774136267,
                                            0.9602898564975363};
        static constexpr double kWeight[4] = {0.3626837833783620, 0.3137066458778873, 0.2223810344533745,
                                              0.1012285362903763};
        const int n = static_cast<int>(s->poles.size());
        const int spans = s->closed ? n : std::max(1, n - s->degree);
        const double h = 1e-7;  // for the slope, by central differences
        for (int i = 0; i < spans; ++i) {
            const double mid = (i + 0.5) / spans, half = 0.5 / spans;
            for (int k = 0; k < 8; ++k) {
                const double t = mid + (k < 4 ? -1 : 1) * kNode[k % 4] * half;
                const auto p = splinePoint(*s, t);
                const auto p0 = splinePoint(*s, t - h), p1 = splinePoint(*s, t + h);
                const double dx = (p1.x - p0.x) / (2 * h), dy = (p1.y - p0.y) / (2 * h);
                a += kWeight[k % 4] * half * (p.x * dy - p.y * dx) / 2;
            }
        }
    }
    return reversed ? -a : a;
}

// Points along an edge, in the loop's direction (for "is it inside?" tests).
void sample(const Shape& shape, bool reversed, std::vector<Point2>& out) {
    std::vector<Point2> pts;
    if (const auto* l = std::get_if<LineShape>(&shape)) {
        pts = {l->start, {(l->start.x + l->end.x) / 2, (l->start.y + l->end.y) / 2}};
    } else if (const auto* r = std::get_if<ArcShape>(&shape)) {
        const int n = std::max(4, static_cast<int>(std::ceil((r->endAngle - r->startAngle) / (kPi / 36))));
        for (int i = 0; i < n; ++i)
            pts.push_back(arcPoint(*r, r->startAngle + (r->endAngle - r->startAngle) * i / n));
    } else if (const auto* c = std::get_if<CircleShape>(&shape)) {
        for (int i = 0; i < 72; ++i)
            pts.push_back({c->center.x + c->radius * std::cos(2 * kPi * i / 72),
                           c->center.y + c->radius * std::sin(2 * kPi * i / 72)});
    } else if (const auto* s = std::get_if<SplineShape>(&shape)) {
        pts = splineSample(*s, 16);
        pts.pop_back();  // its end is the next edge's start (or, closed, its own start)
    }
    if (reversed) {
        // The same points, walked the other way (starting from the other end).
        if (const auto* l = std::get_if<LineShape>(&shape)) {
            pts = {l->end, {(l->start.x + l->end.x) / 2, (l->start.y + l->end.y) / 2}};
        } else if (const auto* r = std::get_if<ArcShape>(&shape)) {
            std::reverse(pts.begin(), pts.end());
            pts.insert(pts.begin(), arcPoint(*r, r->endAngle));
            pts.pop_back();
        } else {
            std::reverse(pts.begin(), pts.end());
        }
    }
    out.insert(out.end(), pts.begin(), pts.end());
}

// Whether a point is inside a polygon (even-odd rule).
bool inside(Point2 p, const std::vector<Point2>& poly) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[i];
        const auto& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// How far a point is from a polygon's outline.
double distanceTo(Point2 p, const std::vector<Point2>& poly) {
    double best = 1e300;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[j];
        const auto& b = poly[i];
        const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
        const double t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0;
        best = std::min(best, std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy)));
    }
    return best;
}

std::string roleWord(PointRole r) {
    return r == PointRole::Start ? "start" : r == PointRole::End ? "end" : "point";
}

}  // namespace

double Profile::area() const {
    double a = outer.area;
    for (const auto& h : holes) a -= h.area;
    return a;
}

Profiles findProfiles(const Sketch& sketch, double tolerance) {
    Profiles out;
    // Lines and arcs, and the places their ends meet ("vertices").
    std::vector<Piece> pieces;
    std::vector<const Entity*> circles;
    for (const auto& e : sketch.entities) {
        if (e.construction) continue;
        if (const auto* l = std::get_if<LineShape>(&e.shape)) {
            if (std::hypot(l->end.x - l->start.x, l->end.y - l->start.y) > tolerance)
                pieces.push_back({&e, l->start, l->end});
        } else if (const auto* a = std::get_if<ArcShape>(&e.shape)) {
            if (a->radius > tolerance)
                pieces.push_back({&e, arcPoint(*a, a->startAngle), arcPoint(*a, a->endAngle)});
        } else if (const auto* c = std::get_if<CircleShape>(&e.shape)) {
            if (c->radius > tolerance) circles.push_back(&e);
        } else if (const auto* s = std::get_if<SplineShape>(&e.shape)) {
            // A closed spline is a loop on its own, like a circle; an open one is a
            // piece from its first pole to its last.
            if (s->poles.size() < 2) continue;
            if (s->closed) {
                circles.push_back(&e);
            } else if (std::hypot(s->poles.back().x - s->poles.front().x,
                                  s->poles.back().y - s->poles.front().y) > tolerance) {
                pieces.push_back({&e, s->poles.front(), s->poles.back()});
            }
        }
    }
    std::vector<Point2> vertices;
    auto vertexAt = [&](Point2 p) {
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (std::hypot(vertices[i].x - p.x, vertices[i].y - p.y) <= tolerance) return static_cast<int>(i);
        }
        vertices.push_back(p);
        return static_cast<int>(vertices.size() - 1);
    };
    for (auto& p : pieces) {
        p.startVertex = vertexAt(p.start);
        p.endVertex = vertexAt(p.end);
    }
    std::vector<std::vector<std::size_t>> at(vertices.size());  // the pieces meeting at each vertex
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        at[static_cast<std::size_t>(pieces[i].startVertex)].push_back(i);
        if (pieces[i].endVertex != pieces[i].startVertex)
            at[static_cast<std::size_t>(pieces[i].endVertex)].push_back(i);
    }
    // Open ends, and junctions of three or more.
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        const auto& here = at[v];
        if (here.size() == 1) {
            const auto& p = pieces[here[0]];
            out.openEnds.push_back(
                {p.entity->id, p.startVertex == static_cast<int>(v) ? PointRole::Start : PointRole::End});
        } else if (here.size() >= 3) {
            const auto& p = pieces[here[0]];
            out.issues.push_back(
                {"sketch.junction",
                 Severity::Warning,
                 std::to_string(here.size()) + " lines and arcs meet at " + p.entity->name + "'s " +
                     roleWord(p.startVertex == static_cast<int>(v) ? PointRole::Start : PointRole::End) +
                     ", so Khan can't tell which closed shape they belong to. Delete the extra one, or make "
                     "it a "
                     "helper line.",
                 {}});
        }
    }
    // Walk each loop: every vertex on it must join exactly two pieces.
    std::vector<Loop> loops;
    std::vector<std::vector<Point2>> outlines;  // each loop as points, for "is it inside?"
    std::vector<std::size_t> firstEntity;       // each loop's first piece, for the sketch's order
    auto entityIndex = [&](const Entity* e) { return static_cast<std::size_t>(e - sketch.entities.data()); };
    std::vector<bool> used(pieces.size(), false);
    for (std::size_t start = 0; start < pieces.size(); ++start) {
        if (used[start]) continue;
        Loop loop;
        std::vector<std::size_t> walked;
        std::size_t piece = start;
        bool reversed = false;
        bool closed = false;
        while (true) {
            walked.push_back(piece);
            loop.edges.push_back({pieces[piece].entity->id, reversed});
            const int next = reversed ? pieces[piece].startVertex : pieces[piece].endVertex;
            const int first = pieces[start].startVertex;
            const auto& there = at[static_cast<std::size_t>(next)];
            if (there.size() != 2) break;  // an open end or a junction: not a loop
            const std::size_t other = there[0] == piece ? there[1] : there[0];
            if (next == first && other == start) {
                closed = true;
                break;
            }
            if (std::find(walked.begin(), walked.end(), other) != walked.end()) break;
            reversed = pieces[other].endVertex == next;
            piece = other;
        }
        if (!closed) continue;
        for (auto w : walked) used[w] = true;
        // Make it go counterclockwise.
        double area = 0;
        for (const auto& e : loop.edges) area += sweep(sketch.find(e.entity)->shape, e.reversed);
        if (area < 0) {
            std::reverse(loop.edges.begin(), loop.edges.end());
            for (auto& e : loop.edges) e.reversed = !e.reversed;
            area = -area;
        }
        loop.area = area;
        std::vector<Point2> outline;
        for (const auto& e : loop.edges) sample(sketch.find(e.entity)->shape, e.reversed, outline);
        std::size_t firstIndex = sketch.entities.size();
        for (auto w : walked) firstIndex = std::min(firstIndex, entityIndex(pieces[w].entity));
        loops.push_back(std::move(loop));
        outlines.push_back(std::move(outline));
        firstEntity.push_back(firstIndex);
    }
    for (const auto* c : circles) {
        // Counterclockwise: a closed spline drawn clockwise is walked backwards.
        const bool backwards = sweep(c->shape, false) < 0;
        Loop loop{{{c->id, backwards}}, std::abs(sweep(c->shape, false))};
        std::vector<Point2> outline;
        sample(c->shape, backwards, outline);
        loops.push_back(std::move(loop));
        outlines.push_back(std::move(outline));
        firstEntity.push_back(entityIndex(c));
    }
    // Which loop each one is directly inside: the smallest one around all of it.
    const std::size_t n = loops.size();
    std::vector<std::optional<std::size_t>> parent(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            std::size_t in = 0, out2 = 0;
            for (const auto& p : outlines[i]) {
                if (distanceTo(p, outlines[j]) <= tolerance * 10) continue;  // touching: says nothing
                (inside(p, outlines[j]) ? in : out2)++;
            }
            if (in > 0 && out2 > 0) {
                if (i > j) continue;  // found from the other loop too: say it once
                out.issues.push_back({"sketch.crossing",
                                      Severity::Warning,
                                      "Two closed shapes cross each other (one has " +
                                          sketch.find(loops[i].edges[0].entity)->name + ", the other " +
                                          sketch.find(loops[j].edges[0].entity)->name +
                                          "). Move them apart, or put one fully inside the other.",
                                      {}});
                continue;
            }
            if (in > 0 && out2 == 0 && loops[j].area > loops[i].area &&
                (!parent[i] || loops[j].area < loops[*parent[i]].area))
                parent[i] = j;
        }
    }
    auto depth = [&](std::size_t i) {
        int d = 0;
        for (auto p = parent[i]; p && d <= static_cast<int>(n); p = parent[*p]) ++d;
        return d;
    };
    // Outlines (even depth) become shapes; loops directly inside them are holes.
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return firstEntity[a] < firstEntity[b]; });
    for (auto i : order) {
        if (depth(i) % 2 != 0) continue;
        Profile p{loops[i], {}};
        for (auto h : order) {
            if (parent[h] == i) {
                Loop hole = loops[h];
                std::reverse(hole.edges.begin(), hole.edges.end());  // holes go clockwise
                for (auto& e : hole.edges) e.reversed = !e.reversed;
                p.holes.push_back(std::move(hole));
            }
        }
        out.shapes.push_back(std::move(p));
    }
    // The same crossing can be found from both loops: say it once.
    std::vector<Diagnostic> unique;
    for (auto& d : out.issues) {
        if (std::none_of(unique.begin(), unique.end(),
                         [&](const Diagnostic& u) { return u.code == d.code && u.message == d.message; }))
            unique.push_back(std::move(d));
    }
    out.issues = std::move(unique);
    return out;
}

std::optional<Loop> loopAround(const Sketch& sketch, Point2 point, double tolerance) {
    const auto found = findProfiles(sketch, tolerance);
    std::optional<Loop> best;
    auto consider = [&](const Loop& loop) {
        std::vector<Point2> outline;
        for (const auto& e : loop.edges) {
            if (const auto* entity = sketch.find(e.entity)) sample(entity->shape, e.reversed, outline);
        }
        if (outline.size() < 3 || !inside(point, outline)) return;
        if (!best || loop.area < best->area) best = loop;
    };
    for (const auto& shape : found.shapes) {
        consider(shape.outer);
        for (const auto& hole : shape.holes) consider(hole);
    }
    return best;
}

}  // namespace kh::sketch
