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
/// A B-spline: a smooth curve pulled towards its control points (poles), like
/// a flexible ruler bent by pins. It starts at its first pole and ends at its
/// last (unless it's closed), and passes near the ones between.
///
/// Its knots aren't stored: they're spread evenly. An open spline is clamped
/// (its knots are `degree + 1` zeros, then 1, 2, ..., up to `degree + 1` copies
/// of `poles - degree`), so it touches its first and last poles. A closed one is
/// periodic, with knots 0, 1, ..., `poles`: a smooth loop, with no corner.
struct SplineShape {
    std::vector<Point2> poles{};    ///< The control points (mm), in order: at least `degree + 1`.
    std::vector<double> weights{};  ///< One per pole, each more than 0; bigger pulls the curve closer to
                                    ///< that pole. Empty means every weight is 1 (an ordinary B-spline).
    int degree = 3;                 ///< 1 (straight pieces, a polyline) to 5. 3 (cubic) is the usual.
    bool closed = false;            ///< True for a closed loop with no start or end.
};

/// What an entity is: a point, line, circle, arc or spline.
using Shape = std::variant<PointShape, LineShape, CircleShape, ArcShape, SplineShape>;

/// One thing drawn in a sketch.
struct Entity {
    EntityId id{};              ///< Never changes, even when it's moved or resized.
    std::string name{};         ///< What the student sees and types, e.g. "Line1". Unique in its sketch.
    Shape shape{};              ///< What it is and where.
    bool construction = false;  ///< True for helper geometry that isn't part of a profile.
};

/// Which point of an entity a constraint uses.
enum class PointRole {
    Self,    ///< A point entity itself.
    Start,   ///< A line's or arc's first end.
    End,     ///< A line's or arc's second end.
    Center,  ///< A circle's or arc's center.
    Pole,    ///< One of a spline's control points: PointRef::index says which.
};

/// A point of an entity, for constraints.
struct PointRef {
    EntityId entity{};                 ///< The entity.
    PointRole role = PointRole::Self;  ///< Which of its points.
    int index = 0;                     ///< For PointRole::Pole: which pole, from 0. Otherwise 0.

    /// Checks whether two references name the same point.
    /// @return True if the entity, role and index all match.
    bool operator==(const PointRef&) const = default;
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
    ConstraintId id{};   ///< Never changes.
    std::string name{};  ///< What the student sees and types, e.g. "Length1". Unique in its sketch.
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

    /// Finds an entity by name.
    /// @param name For example "Line1".
    /// @return It, or nullptr.
    const Entity* findEntity(std::string_view name) const;

    /// Finds a constraint by name.
    /// @param name For example "Length1".
    /// @return It, or nullptr.
    const Constraint* findConstraint(std::string_view name) const;
};

/// Gives a constraint kind's name as the student sees it, e.g. "Horizontal".
/// @param kind The kind.
/// @return The name.
std::string_view kindName(ConstraintKind kind);

/// Gives a constraint kind's key, as files and kh_cli spell it, e.g. "point_on_line".
/// Keys never change.
/// @param kind The kind.
/// @return The key.
std::string_view kindKey(ConstraintKind kind);

/// Finds a constraint kind from its key.
/// @param key For example "horizontal" or "point_on_line".
/// @return The kind, or nothing if no kind has that key.
std::optional<ConstraintKind> kindFromKey(std::string_view key);

/// Every constraint kind, in the order ConstraintKind lists them.
/// @return The kinds.
const std::vector<ConstraintKind>& allKinds();

/// Gives a point role's key, as files and kh_cli spell it: "", "start", "end" or "center".
/// @param role The role.
/// @return The key ("" for Self).
std::string_view roleKey(PointRole role);

/// Finds a point role from its key.
/// @param key "", "start", "end", "center" or "pole".
/// @return The role, or nothing if no role has that key.
std::optional<PointRole> roleFromKey(std::string_view key);

/// Gives the name a student types after an entity's name for one of its points:
/// "start", "end", "center", or "pole1", "pole2", ... (counting from 1); "" for a
/// point entity itself. "Spline1.pole2" is Spline1's second control point.
/// @param ref The point.
/// @return Its name.
std::string pointKey(const PointRef& ref);

/// Reads a point's name (see pointKey()).
/// @param entity The entity it belongs to.
/// @param key "", "start", "end", "center", or "pole" and a number from 1.
/// @return The point, or nothing if the name isn't one.
std::optional<PointRef> pointFromKey(const EntityId& entity, std::string_view key);

/// Gives the points of an entity a constraint can use, in a fixed order: Self,
/// Start, End, Center, then each pole (a spline's start and end are its first
/// and last pole, so only its poles are listed).
/// @param entity The entity.
/// @return Its points.
std::vector<PointRef> pointsOf(const Entity& entity);

/// Finds a point on a spline.
/// @param spline The spline (see SplineShape for its knots).
/// @param t Where along it, from 0 (its start) to 1 (its end, or back at the
///        start for a closed one).
/// @return The point (mm).
Point2 splinePoint(const SplineShape& spline, double t);

/// Gives points along a spline, close enough together to draw it or measure it
/// as straight pieces: `perSpan` per piece between knots, and the end.
/// @param spline The spline.
/// @param perSpan How many points per piece between knots, at least 1.
/// @return The points, from its start to its end (a closed one ends where it starts).
std::vector<Point2> splineSample(const SplineShape& spline, int perSpan = 16);

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
