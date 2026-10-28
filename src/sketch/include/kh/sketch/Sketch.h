#pragma once

/// @file
/// A 2D sketch: points, lines, circles and arcs, and the constraints and
/// dimensions between them. Plain values, with no solver types.

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"

namespace kh::sketch {

/// A point on the sketch's plane, in mm.
struct Point2 {
    double x = 0;  ///< Across (mm).
    double y = 0;  ///< Up (mm).
};

/// A single point.
struct PointShape {
    Point2 at{};  ///< Where it is (mm).
};

/// A straight line between two points.
struct LineShape {
    Point2 start{};  ///< First end (mm).
    Point2 end{};    ///< Second end (mm).
};

/// A full circle.
struct CircleShape {
    Point2 center{};    ///< Its center (mm).
    double radius = 0;  ///< Its radius (mm). More than 0.
};

/// Part of a circle, going counterclockwise from `startAngle` to `endAngle`.
/// Its ends are worked out from the center, radius and angles.
struct ArcShape {
    Point2 center{};        ///< The center of its circle (mm).
    double radius = 0;      ///< Its radius (mm). More than 0.
    double startAngle = 0;  ///< Where it starts (radians, 0 points along +x).
    double endAngle = 0;    ///< Where it ends (radians), counterclockwise from the start.
};

/// The geometry of one sketch entity.
using Shape = std::variant<PointShape, LineShape, CircleShape, ArcShape>;

/// One thing drawn in a sketch.
struct Entity {
    EntityId id{};              ///< Never changes, even when it's moved or resized.
    Shape shape{};              ///< What it is and where.
    bool construction = false;  ///< True for helper geometry that isn't part of a profile.
};

/// Which point of an entity a constraint uses.
enum class PointRole {
    Self,    ///< A point entity itself.
    Start,   ///< A line's or arc's first end.
    End,     ///< A line's or arc's second end.
    Center,  ///< A circle's or arc's center.
};

/// A point of an entity, for constraints.
struct PointRef {
    EntityId entity{};                 ///< The entity.
    PointRole role = PointRole::Self;  ///< Which of its points.
};

/// What a constraint keeps true. Dimensions (Distance and below) have a value.
enum class ConstraintKind {
    Fix,            ///< points[0] stays at `at`.
    Coincident,     ///< points[0] and points[1] are the same point.
    Horizontal,     ///< entities[0], a line, is horizontal.
    Vertical,       ///< entities[0], a line, is vertical.
    Parallel,       ///< entities[0] and entities[1], lines, are parallel.
    Perpendicular,  ///< entities[0] and entities[1], lines, meet at a right angle.
    Equal,          ///< entities[0] and entities[1] are the same length (lines) or radius (circles, arcs).
    PointOnLine,    ///< points[0] is on the line through entities[0].
    PointOnCircle,  ///< points[0] is on the circle of entities[0] (a circle or arc).
    Midpoint,       ///< points[0] is in the middle of entities[0], a line.
    Tangent,        ///< entities[0] touches entities[1] without crossing: a line and a circle or arc, or two
                    ///< circles or arcs.
    Distance,       ///< points[0] and points[1] are `value` mm apart.
    Length,         ///< entities[0], a line, is `value` mm long.
    HorizontalDistance,  ///< points[1] is `value` mm to the right of points[0] (negative: to the left).
    VerticalDistance,    ///< points[1] is `value` mm above points[0] (negative: below).
    Radius,              ///< entities[0], a circle or arc, has a radius of `value` mm.
    Diameter,            ///< entities[0], a circle or arc, has a diameter of `value` mm.
    Angle,  ///< entities[1], a line, is turned `value` radians counterclockwise from entities[0].
};

/// One constraint or dimension.
struct Constraint {
    ConstraintId id{};                                 ///< Never changes.
    ConstraintKind kind = ConstraintKind::Coincident;  ///< What it keeps true.
    std::vector<PointRef> points{};                    ///< The points it uses (see ConstraintKind).
    std::vector<EntityId> entities{};                  ///< The entities it uses (see ConstraintKind).
    double value = 0;                                  ///< For dimensions: mm, or radians for Angle.
    Point2 at{};                                       ///< For Fix: where the point stays (mm).
};

/// A whole sketch.
struct Sketch {
    std::vector<Entity> entities{};         ///< What's drawn, in the order it was drawn.
    std::vector<Constraint> constraints{};  ///< What's kept true, in the order it was added.

    /// Finds an entity.
    /// @param id Its id.
    /// @return It, or nullptr.
    const Entity* find(const EntityId& id) const;

    /// Finds a constraint.
    /// @param id Its id.
    /// @return It, or nullptr.
    const Constraint* find(const ConstraintId& id) const;
};

/// Gives a constraint kind's name as the student sees it, e.g. "Horizontal".
/// @param kind The kind.
/// @return The name.
std::string_view kindName(ConstraintKind kind);

/// Checks whether a kind is a dimension (it has a `value`).
/// @param kind The kind.
/// @return True for Distance, Length, HorizontalDistance, VerticalDistance, Radius, Diameter and Angle.
bool isDimension(ConstraintKind kind);

/// Finds where a point of an entity is.
/// @param sketch The sketch.
/// @param ref The point.
/// @return Where it is (mm), or nothing if the entity is missing or has no such point.
std::optional<Point2> pointOf(const Sketch& sketch, const PointRef& ref);

/// Checks that every entity's numbers make sense and every constraint uses the
/// right number and kind of things that are in the sketch. Solvers call this first.
/// @param sketch The sketch.
/// @return Plain problems ("Horizontal needs one line."); nothing if it's fine.
Diagnostics check(const Sketch& sketch);

/// Measures how far a constraint is from being true, with Khan's own math (not
/// the solver's), so tests can check a solver's answer.
/// @param sketch The sketch, with its geometry where the solver put it.
/// @param constraint One of its constraints.
/// @return 0 when it's exactly true; otherwise how far off, in mm (radians for
///         angles). A large number if the constraint can't be measured.
double error(const Sketch& sketch, const Constraint& constraint);

}  // namespace kh::sketch
