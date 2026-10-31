#pragma once

/// @file
/// A 2D sketch: points, lines, circles and arcs, and the constraints and
/// dimensions between them. Plain values, with no solver types.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/core/Units.h"

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
/// Where a spline's points came from, when it was imported from a file of points,
/// so it can be read again (see sketch.reload_spline).
struct SplineSource {
    std::string file{};   ///< The file's path, as it was when imported (UTF-8).
    bool through = true;  ///< True: the curve goes through the points; false: they're its poles.
    LengthUnit unit = LengthUnit::Millimeter;  ///< The unit of the file's numbers.
    /// Compares two sources.
    /// @param other The other source.
    /// @return True if every part is the same.
    bool operator==(const SplineSource& other) const = default;
};

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
    std::optional<SplineSource> source{};  ///< The file it was imported from, if it was.
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
    PointOnSpline,  ///< points[0] is on entities[0], a spline.
    Midpoint,       ///< points[0] is in the middle of entities[0], a line.
    Symmetric,      ///< points[0] and points[1] are mirror images across the line through entities[0].
    Tangent,        ///< entities[0] touches entities[1] without crossing: a line and a circle or arc, or two
                    ///< circles or arcs. Or, where a spline's end is joined to the end of a line, arc or
                    ///< spline (see jointOf()), they head the same way there, so the join has no corner.
    Smooth,    ///< Where entities[0] and entities[1] are joined end to end (see jointOf()), at least one a
               ///< spline: Tangent, and they bend equally there too (the same curvature, G2), so the join
               ///< can't be seen.
    Distance,  ///< points[0] and points[1] are `value` mm apart.
    Length,    ///< entities[0], a line, is `value` mm long.
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

/// Whether a point is an end of a curve: a line's or arc's start or end, or an
/// open spline's first or last control point.
/// @param sketch The sketch.
/// @param ref The point.
/// @return True for an end.
bool isEnd(const Sketch& sketch, const PointRef& ref);

/// Finds where two curves are joined end to end: a Coincident between an end
/// (see isEnd()) of each.
/// @param sketch The sketch.
/// @param a One curve.
/// @param b The other.
/// @return The joined ends, `a`'s first; nothing if they aren't joined.
std::optional<std::pair<PointRef, PointRef>> jointOf(const Sketch& sketch, const EntityId& a,
                                                     const EntityId& b);

/// A curve at one of its ends: the point and how the curve moves there.
struct CurveEnd {
    Point2 at{};  ///< The end (mm).
    Point2 d1{};  ///< The first derivative there, along the curve from its start to its end (mm per unit).
    Point2 d2{};  ///< The second derivative there.
};

/// Measures a line, arc or open spline at one of its ends.
/// @param shape The curve.
/// @param end Its start or end (for a spline, also its first or last pole).
/// @return The end, or nothing if `end` isn't an end of this shape.
std::optional<CurveEnd> curveEnd(const Shape& shape, const PointRef& end);

/// Gives a curve's curvature vector at an end: towards the inside of the bend,
/// as long as 1 / the radius of the circle that fits the bend there (0 for a
/// straight line). The same whichever way along the curve it's measured.
/// @param end The curve at the end.
/// @return The curvature vector (1/mm).
Point2 curvatureVector(const CurveEnd& end);

/// Finds the point of a spline nearest to a point.
/// @param spline The spline.
/// @param p The point (mm).
/// @return Where along the spline it is, from 0 to 1 (as for splinePoint()).
double nearestOnSpline(const SplineShape& spline, Point2 p);

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

/// Finds the control points of a spline that passes through given points, in
/// order: one control point for each. An open spline starts at the first point
/// and ends at the last.
///
/// @code
/// // A smooth arch through three points, ready for a SplineShape.
/// auto poles = kh::sketch::splinePolesThrough({{0, 0}, {20, 15}, {40, 0}}, 2, false);
/// @endcode
/// @param through The points it passes through (mm): at least `degree + 1`.
/// @param degree 1 to 5.
/// @param closed True for a closed loop through them, back to the first.
/// @return The control points (mm), or nothing if there are too few points or
///         the degree is out of range.
std::vector<Point2> splinePolesThrough(const std::vector<Point2>& through, int degree, bool closed);

/// The points read from a file of points (see readPoints()).
struct PointsFile {
    std::vector<Point2> points{};  ///< The points, in mm, in the file's order.
    Diagnostics issues{};          ///< What's wrong with the file, in plain words; empty if nothing.
};

/// Reads the text of a file of points: one point per line, x then y, split by
/// commas, spaces, tabs or semicolons (so a spreadsheet's CSV works). A third
/// number on a line (z) is left out. Blank lines are skipped, `#` starts a
/// comment, and a first line that isn't numbers (a heading like "x,y") is
/// skipped too.
///
/// @code
/// auto read = kh::sketch::readPoints("x,y\n0,0\n20,15\n40,0\n", "arch.csv");
/// // read.points: (0, 0), (20, 15), (40, 0)
/// @endcode
/// @param text The file's text.
/// @param fileName The file's name, for messages like "Line 4 of arch.csv ...".
/// @param unit The unit of its numbers; the points come back in mm.
/// @return The points, or what's wrong: a line that isn't 2 or 3 numbers, fewer
///         than 2 points, or more than kMostPoints.
PointsFile readPoints(std::string_view text, std::string_view fileName,
                      LengthUnit unit = LengthUnit::Millimeter);

/// The most points readPoints() takes from one file, so a spline through them
/// still solves quickly.
inline constexpr std::size_t kMostPoints = 500;

/// Says where along a spline made by splinePolesThrough() it passes through each point.
/// @param count How many points it passes through.
/// @param degree Its degree.
/// @param closed True for a closed loop.
/// @return For each point, where along the spline it is, from 0 to 1 (as for
///         splinePoint()); empty if splinePolesThrough() would give nothing.
std::vector<double> splineThroughAt(int count, int degree, bool closed);

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
