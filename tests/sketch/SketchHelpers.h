#pragma once

// Small helpers for building test sketches.

#include "kh/sketch/Sketch.h"

namespace kh::test {

using namespace kh::sketch;

struct SketchBuilder {
    Sketch sketch;

    EntityId point(double x, double y) { return add(PointShape{{x, y}}); }
    EntityId line(double x1, double y1, double x2, double y2) { return add(LineShape{{x1, y1}, {x2, y2}}); }
    EntityId circle(double x, double y, double r) { return add(CircleShape{{x, y}, r}); }
    EntityId arc(double x, double y, double r, double a0, double a1) {
        return add(ArcShape{{x, y}, r, a0, a1});
    }

    EntityId add(Shape shape) {
        Entity e{EntityId::generate(), shape, false};
        sketch.entities.push_back(e);
        return e.id;
    }

    ConstraintId constrain(ConstraintKind kind, std::vector<PointRef> points, std::vector<EntityId> entities,
                           double value = 0, Point2 at = {}) {
        Constraint c{ConstraintId::generate(), kind, std::move(points), std::move(entities), value, at};
        sketch.constraints.push_back(c);
        return c.id;
    }
};

inline PointRef start(EntityId e) {
    return {e, PointRole::Start};
}
inline PointRef end(EntityId e) {
    return {e, PointRole::End};
}
inline PointRef center(EntityId e) {
    return {e, PointRole::Center};
}
inline PointRef self(EntityId e) {
    return {e, PointRole::Self};
}

}  // namespace kh::test
