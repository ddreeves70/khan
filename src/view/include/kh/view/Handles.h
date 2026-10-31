#pragma once

/// @file
/// Handles: arrows and squares drawn on a picked body, dragged with the mouse to
/// move it. Plain data and the maths for drawing, picking and dragging them; no
/// Qt or OCCT.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "kh/kernel/Types.h"
#include "kh/view/Scene.h"

namespace kh::view {

/// What a handle looks like, and how it drags.
enum class HandleKind {
    Arrow,   ///< An arrow: drags along its direction only.
    Square,  ///< A small square: drags anywhere on its flat plane.
};

/// Something the student drags in the 3D view, like the arrows on a picked
/// body. The view draws it on top of everything and reports how far it was
/// dragged; it doesn't change the design itself.
struct ViewHandle {
    std::string id{};                     ///< Says which handle was dragged, like "x".
    HandleKind kind = HandleKind::Arrow;  ///< An arrow or a square.
    kernel::Vec3 at{};                    ///< Arrow: where it starts. Square: its middle. In mm.
    /// Arrow: which way it points (a unit vector), the only way it drags.
    /// Square: the direction square to its plane (a unit vector); it drags on that plane.
    kernel::Vec3 direction{1, 0, 0};
    double length =
        20;       ///< Arrow: how long it is. Square: how long each side is. In mm, or pixels (see `pixels`).
    Rgb color{};  ///< Its color.
    /// True if `length` is in screen pixels: the view keeps the handle the same
    /// size on the screen however far it zooms. The maths below always takes mm.
    bool pixels = false;
};

/// A line of sight through the view: from a point, in a direction.
struct Ray {
    kernel::Vec3 from{};               ///< A point on it, in mm.
    kernel::Vec3 direction{0, 0, -1};  ///< Its direction (any length but 0).
};

/// A point on the screen, in pixels from the view's top-left corner.
struct ScreenPoint {
    double x = 0;  ///< To the right.
    double y = 0;  ///< Down.
};

/// Finds where a point in the design is drawn on the screen.
/// @param point A point, in mm.
/// @return Where it's drawn, or nothing if it's behind the camera.
using Projection = std::function<std::optional<ScreenPoint>(const kernel::Vec3& point)>;

/// Gives two directions along a flat plane, square to each other and to its normal.
/// For the ground (normal z) they're x and y.
/// @param normal The direction square to the plane (a unit vector).
/// @return The two directions, as unit vectors.
std::pair<kernel::Vec3, kernel::Vec3> planeAxes(const kernel::Vec3& normal);

/// Works out the lines that draw a handle: an arrow's shaft and a head made of
/// four spokes, or a square's four sides.
/// @param handle The handle.
/// @return Pairs of points, in mm: each pair is one line.
std::vector<kernel::Vec3> handleLines(const ViewHandle& handle);

/// Finds which handle is under the mouse. Arrows come first (the part away
/// from where they start), then squares; an arrow pointing almost straight at
/// the camera can't be picked, since it can't be dragged.
/// @param handles The handles.
/// @param project Where points are drawn on the screen.
/// @param mouse Where the mouse is.
/// @param reach How close the mouse must be to an arrow, in pixels.
/// @return The handle's index, or nothing if none is there.
std::optional<std::size_t> handleAt(const std::vector<ViewHandle>& handles, const Projection& project,
                                    ScreenPoint mouse, double reach);

/// Works out how far a handle was dragged, from the line of sight through the
/// mouse when the drag started and now: along an arrow, the change in the point
/// on it nearest the line of sight; on a square, the change in where the line of
/// sight meets its plane.
/// @param handle The handle being dragged.
/// @param start The line of sight when the button went down.
/// @param now The line of sight now.
/// @return How far, along x, y and z (mm), or nothing if it can't be worked out
///         (an arrow pointing straight at the camera, a plane seen edge on).
std::optional<kernel::Vec3> handleDrag(const ViewHandle& handle, const Ray& start, const Ray& now);

}  // namespace kh::view
