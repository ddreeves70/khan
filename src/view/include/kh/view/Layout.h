#pragma once

/// @file
/// How a viewer is split into panes, and the standard directions to look from.

#include <optional>
#include <string_view>
#include <vector>

#include "kh/kernel/Types.h"

namespace kh::view {

/// How a viewer panel is split into panes. Each pane has its own camera.
enum class PaneLayout {
    Single,      ///< One pane.
    SideBySide,  ///< Two panes, left and right.
    Stacked,     ///< Two panes, one above the other.
    Four,        ///< Four panes in a 2 by 2 square.
};

/// A direction to look at the design from. Khan is Z-up: the XY plane is the floor.
enum class StandardView {
    Front,   ///< From -Y, looking towards +Y. Z is up.
    Back,    ///< From +Y. Z is up.
    Left,    ///< From -X. Z is up.
    Right,   ///< From +X. Z is up.
    Top,     ///< From +Z, looking down. Y is up on the screen.
    Bottom,  ///< From -Z, looking up. -Y is up on the screen.
    Iso,     ///< From the front, right and above at once (+X, -Y, +Z). Z is up.
};

/// A camera's direction, as unit vectors.
struct ViewDirection {
    kernel::Vec3 toEye{};  ///< From the point looked at towards the camera.
    kernel::Vec3 up{};     ///< The direction that points up on the screen.
};

/// Gives the number of panes in a layout.
/// @param layout The layout.
/// @return 1, 2 or 4.
int paneCount(PaneLayout layout);

/// Gives the views a layout starts with, one per pane, in reading order
/// (left to right, then top to bottom).
///
/// Four panes follow a drawing sheet: Top above Front, Right to the right of
/// Front, and Iso in the free corner.
/// @param layout The layout.
/// @return paneCount(layout) views.
std::vector<StandardView> defaultViews(PaneLayout layout);

/// Gives the camera direction of a standard view.
/// @param view The view.
/// @return Unit vectors; toEye and up are at right angles.
ViewDirection direction(StandardView view);

/// Gives every standard view, in the order the toolbar lists them.
/// @return Front, Back, Left, Right, Top, Bottom, Iso.
const std::vector<StandardView>& allViews();

/// Gives a view's name, as the toolbar shows it.
/// @param view The view.
/// @return "Front", "Back", "Left", "Right", "Top", "Bottom" or "Iso".
std::string_view viewName(StandardView view);

/// Finds a view by name, ignoring upper and lower case.
/// @param name A name from viewName(), e.g. "top".
/// @return The view, or nothing if no view has that name.
std::optional<StandardView> parseView(std::string_view name);

/// Gives every layout, in the order the toolbar lists them.
/// @return Single, SideBySide, Stacked, Four.
const std::vector<PaneLayout>& allLayouts();

/// Gives a layout's name, as the toolbar shows it.
/// @param layout The layout.
/// @return "One pane", "Two side by side", "Two stacked" or "Four panes".
std::string_view layoutName(PaneLayout layout);

}  // namespace kh::view
