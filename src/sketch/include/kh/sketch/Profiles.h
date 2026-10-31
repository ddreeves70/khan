#pragma once

/// @file
/// Closed shapes in a sketch: loops of lines and arcs joined end to end, and
/// circles, sorted into outlines and the holes inside them.

#include <optional>
#include <vector>

#include "kh/core/Diagnostic.h"
#include "kh/core/Id.h"
#include "kh/sketch/Sketch.h"

namespace kh::sketch {

/// One piece of a loop: a line or arc, and which way the loop goes along it.
struct LoopEdge {
    EntityId entity{};      ///< The line, arc or circle.
    bool reversed = false;  ///< True if the loop goes from its end to its start.
};

/// A closed loop. An outline goes counterclockwise, a hole clockwise (seen with
/// the sketch's x to the right and y up).
struct Loop {
    std::vector<LoopEdge> edges{};  ///< In order around the loop; one edge for a circle.
    double area = 0;                ///< The area it encloses, in mm^2 (never negative).
};

/// A closed shape: an outline, and the holes in it. Extrude turns each into a solid.
struct Profile {
    Loop outer{};               ///< The outline.
    std::vector<Loop> holes{};  ///< Loops directly inside it, cut out of it.

    /// Gives the shape's area: its outline's, less its holes'.
    /// @return mm^2.
    double area() const;
};

/// The closed shapes of a sketch, and what stops other pieces from closing.
struct Profiles {
    std::vector<Profile> shapes{};  ///< In the sketch's order (of each outline's first piece).
    /// Ends of lines and arcs that no other line or arc meets, so they can't be
    /// part of a closed shape yet.
    std::vector<PointRef> openEnds{};
    /// Why some pieces aren't in a closed shape even though their ends meet: three
    /// or more meeting at one point, or loops that cross each other. Warnings, in
    /// plain words.
    Diagnostics issues{};
};

/// Finds the closed shapes in a sketch.
///
/// Lines and arcs whose ends are at the same place (within `tolerance`) are joined;
/// a circle is a loop on its own. Helper (construction) geometry and points are
/// left out. Pass solved geometry: ends are joined by where they are, not by
/// Coincident constraints. A loop inside another is a hole in it; a loop inside a
/// hole is a shape of its own.
///
/// @code
/// const auto found = kh::sketch::findProfiles(kh::sketch::Sketch{solved.entities, {}});
/// for (const auto& p : found.shapes) { ... p.area() mm^2 ... }
/// @endcode
/// @param sketch The sketch, with its geometry where it is (solved).
/// @param tolerance How close two ends must be to count as joined, in mm.
/// @return The shapes, the open ends, and any problems.
Profiles findProfiles(const Sketch& sketch, double tolerance = 1e-6);

/// Finds the smallest closed loop around a point: the shape a click inside it
/// (not on its lines) is in. A point in a hole gets the hole's loop; a point in
/// a shape but outside its holes gets the shape's outline.
/// @param sketch The sketch, with its geometry where it is (solved).
/// @param point The point, in the sketch's x and y (mm).
/// @param tolerance How close two ends must be to count as joined, in mm.
/// @return The loop, or nothing if the point isn't inside one.
std::optional<Loop> loopAround(const Sketch& sketch, Point2 point, double tolerance = 1e-6);

}  // namespace kh::sketch
