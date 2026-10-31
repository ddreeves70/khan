#include "kh/view/Handles.h"

#include <algorithm>
#include <cmath>

namespace kh::view {

namespace {

using kernel::Vec3;

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 scale(const Vec3& a, double k) {
    return {a.x * k, a.y * k, a.z * k};
}
double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double norm(const Vec3& a) {
    return std::sqrt(dot(a, a));
}
Vec3 unit(const Vec3& a) {
    const double n = norm(a);
    return n > 0 ? scale(a, 1 / n) : a;
}

// How far a screen point is from a screen segment, in pixels.
double distanceToSegment(ScreenPoint p, ScreenPoint a, ScreenPoint b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Whether a screen point is inside a four-sided shape, its corners in order.
bool inside(ScreenPoint p, const ScreenPoint (&q)[4]) {
    bool in = false;
    for (int i = 0, j = 3; i < 4; j = i++) {
        if ((q[i].y > p.y) != (q[j].y > p.y) &&
            p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x) {
            in = !in;
        }
    }
    return in;
}

// The corners of a square handle, in order round it.
void squareCorners(const ViewHandle& h, Vec3 (&out)[4]) {
    const auto [u, v] = planeAxes(unit(h.direction));
    const double s = h.length / 2;
    out[0] = add(h.at, add(scale(u, -s), scale(v, -s)));
    out[1] = add(h.at, add(scale(u, s), scale(v, -s)));
    out[2] = add(h.at, add(scale(u, s), scale(v, s)));
    out[3] = add(h.at, add(scale(u, -s), scale(v, s)));
}

// The distance along a line (from `origin`, along the unit vector `along`) to
// the point on it nearest a line of sight; nothing if they're (nearly) parallel.
std::optional<double> nearestAlong(const Vec3& origin, const Vec3& along, const Ray& ray) {
    const Vec3 d = unit(ray.direction);
    const double b = dot(along, d);
    const double denominator = 1 - b * b;
    if (denominator < 1e-6) return std::nullopt;  // pointing (nearly) along the line of sight
    const Vec3 w = sub(origin, ray.from);
    return (b * dot(d, w) - dot(along, w)) / denominator;
}

std::optional<Vec3> onPlane(const Vec3& origin, const Vec3& normal, const Ray& ray) {
    const double facing = dot(normal, ray.direction);
    // Seen (nearly) edge on, a small mouse move would slide it a long way.
    if (std::abs(facing) < 0.05 * norm(ray.direction)) return std::nullopt;
    const double t = dot(normal, sub(origin, ray.from)) / facing;
    return add(ray.from, scale(ray.direction, t));
}

}  // namespace

std::pair<Vec3, Vec3> planeAxes(const Vec3& normal) {
    const Vec3 n = unit(normal);
    const Vec3 ref = std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 v = unit(cross(n, ref));
    return {cross(v, n), v};
}

std::vector<Vec3> handleLines(const ViewHandle& h) {
    std::vector<Vec3> out;
    if (h.kind == HandleKind::Square) {
        Vec3 c[4];
        squareCorners(h, c);
        for (int i = 0; i < 4; ++i) {
            out.push_back(c[i]);
            out.push_back(c[(i + 1) % 4]);
        }
        return out;
    }
    const Vec3 d = unit(h.direction);
    const Vec3 tip = add(h.at, scale(d, h.length));
    const Vec3 back = sub(tip, scale(d, h.length * 0.2));
    out.push_back(h.at);
    out.push_back(tip);
    // The head: four spokes from the tip back and out, so it reads as an arrow from any side.
    const auto [u, v] = planeAxes(d);
    const double w = h.length * 0.07;
    for (const Vec3& side : {u, scale(u, -1), v, scale(v, -1)}) {
        out.push_back(tip);
        out.push_back(add(back, scale(side, w)));
    }
    return out;
}

std::optional<std::size_t> handleAt(const std::vector<ViewHandle>& handles, const Projection& project,
                                    ScreenPoint mouse, double reach) {
    std::optional<std::size_t> best;
    double bestDistance = reach;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const auto& h = handles[i];
        if (h.kind != HandleKind::Arrow) continue;
        const Vec3 d = unit(h.direction);
        // From a quarter of the way out, so the middle (where a square may be) isn't the arrow's.
        const auto a = project(add(h.at, scale(d, h.length * 0.25)));
        const auto b = project(add(h.at, scale(d, h.length)));
        if (!a || !b) continue;
        if (std::hypot(b->x - a->x, b->y - a->y) < reach) continue;  // pointing at the camera
        const double distance = distanceToSegment(mouse, *a, *b);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    if (best) return best;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const auto& h = handles[i];
        if (h.kind != HandleKind::Square) continue;
        Vec3 c[4];
        squareCorners(h, c);
        ScreenPoint s[4];
        bool seen = true;
        for (int k = 0; k < 4; ++k) {
            const auto p = project(c[k]);
            if (!p) seen = false;
            if (p) s[k] = *p;
        }
        if (seen && inside(mouse, s)) return i;
    }
    return std::nullopt;
}

std::optional<Vec3> handleDrag(const ViewHandle& h, const Ray& start, const Ray& now) {
    const Vec3 d = unit(h.direction);
    if (h.kind == HandleKind::Arrow) {
        const auto a = nearestAlong(h.at, d, start);
        const auto b = nearestAlong(h.at, d, now);
        if (!a || !b) return std::nullopt;
        return scale(d, *b - *a);
    }
    const auto a = onPlane(h.at, d, start);
    const auto b = onPlane(h.at, d, now);
    if (!a || !b) return std::nullopt;
    Vec3 by = sub(*b, *a);
    // Exactly on the plane, even after rounding.
    by = sub(by, scale(d, dot(by, d)));
    return by;
}

}  // namespace kh::view
