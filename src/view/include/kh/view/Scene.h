#pragma once

/// @file
/// What a 3D view shows: bodies, the selection and the grid. Plain data, no Qt or OCCT.

#include <optional>
#include <string>
#include <vector>

#include "kh/core/Id.h"
#include "kh/kernel/Types.h"

namespace kh::view {

/// A color, 0 to 255 per channel (sRGB).
struct Rgb {
    int r = 158;  ///< Red, 0 to 255.
    int g = 178;  ///< Green, 0 to 255.
    int b = 204;  ///< Blue, 0 to 255.

    /// Compares two colors.
    /// @param other The color to compare with.
    /// @return True if all three channels are the same.
    bool operator==(const Rgb& other) const = default;
};

/// One shape to draw, belonging to one step of the design.
struct SceneBody {
    FeatureId feature{};          ///< The step the shape belongs to; picking it selects this step.
    kernel::ShapeHandle shape{};  ///< The shape, from the OCCT kernel.
    bool ghost = false;           ///< Drawn see-through: a step that is used up by a later step.
    Rgb color{};                  ///< Its color: its material's. The default is a light steel blue.
};

/// Everything a view draws of a design.
struct SceneContent {
    std::vector<SceneBody> bodies{};    ///< The shapes, in design order.
    std::vector<FeatureId> selected{};  ///< Selected steps, in picking order; drawn highlighted.
};

/// What an overlay draws.
enum class OverlayShape {
    Lines,    ///< Straight lines: each pair of `points` is one line.
    Points,   ///< A dot at each of `points`.
    Lattice,  ///< Evenly spaced lines, or dots where they cross, on a plane.
    Labels,   ///< Words: `texts[i]` is written at `points[i]`, facing the screen.
};

/// Something drawn over the design that isn't part of it, such as the grid. The
/// view draws it as given, never picks it, and leaves it out of Fit all.
struct ViewOverlay {
    OverlayShape shape = OverlayShape::Lines;  ///< What it draws.
    std::vector<kernel::Vec3> points{};        ///< For Lines, Points and Labels, in mm.
    std::vector<std::string> texts{};          ///< For Labels: the words, one for each point.
    kernel::Vec3 origin{};                     ///< Lattice: a crossing point, in mm.
    kernel::Vec3 xAxis{1, 0, 0};               ///< Lattice: one direction of lines (a unit vector).
    kernel::Vec3 yAxis{0, 1, 0};               ///< Lattice: the other direction (a unit vector).
    double spacing = 10;                       ///< Lattice: distance between lines, in mm.
    double halfSize = 100;                     ///< Lattice: how far it reaches each way, in mm.
    bool dots = false;                         ///< Lattice: dots at the crossings instead of lines.
    Rgb color{};                               ///< Its color.
    int alpha = 255;                           ///< How solid, 0 to 255.
    double size = 1;  ///< Line width, dot size or the height of a label's letters, in screen pixels.
};

/// A flat plane a drawing tool works on (a sketch's plane).
struct DrawingPlane {
    kernel::Vec3 origin{};         ///< A point on the plane, in mm.
    kernel::Vec3 normal{0, 0, 1};  ///< Its direction, pointing toward the side it's drawn from.
};

/// Where the mouse is on a DrawingPlane.
struct PlanePoint {
    kernel::Vec3 at{};  ///< The point on the plane, in mm.
    double pixel = 0;   ///< How many mm one screen pixel covers there, for "close enough" tests.
};

/// Finds where a line of sight meets a plane.
/// @param from A point on the line (mm).
/// @param direction The line's direction (any length but 0).
/// @param plane The plane.
/// @return The point (mm), or nothing if the line runs along the plane.
std::optional<kernel::Vec3> rayHitsPlane(const kernel::Vec3& from, const kernel::Vec3& direction,
                                         const DrawingPlane& plane);

/// Works out the lines of a lattice overlay, for drawing.
/// @param overlay A Lattice overlay.
/// @return Pairs of points, in mm: each pair is one line.
std::vector<kernel::Vec3> latticeLines(const ViewOverlay& overlay);

/// Works out the crossings of a lattice overlay, for drawing dots.
/// @param overlay A Lattice overlay.
/// @return The crossing points, in mm.
std::vector<kernel::Vec3> latticePoints(const ViewOverlay& overlay);

/// Works out the selection after a click in a view.
///
/// The view knows which steps are selected after the click, but not the order
/// they were picked in. Steps that stay selected keep their place; new ones are
/// added at the end, in the order given.
/// @param before The selection before the click, in picking order.
/// @param after The steps selected after the click, in any order.
/// @return @p after, in picking order.
std::vector<FeatureId> mergePick(const std::vector<FeatureId>& before, const std::vector<FeatureId>& after);

}  // namespace kh::view
