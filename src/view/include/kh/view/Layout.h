#pragma once

/// @file
/// How a viewer is split into panes, the standard directions to look from, and
/// how the mouse and the bodies' drawing can be set (the view bar's choices).

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

/// What dragging with the left mouse button does (the view bar's Orbit, Pan and
/// Zoom buttons). The other buttons and the wheel always do what they do.
enum class LeftDrag {
    Orbit,  ///< Turn the camera around the part. The usual choice.
    Pan,    ///< Slide the camera sideways.
    Zoom,   ///< Move closer (drag right) or farther away (drag left).
};

/// How bodies are drawn.
enum class DisplayStyle {
    ShadedWithEdges,  ///< Solid colors with the edges drawn in dark lines. The usual choice.
    Shaded,           ///< Solid colors only.
    Wireframe,        ///< Just the edges, so you can see through the part.
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

/// Gives every display style, in the order the view bar lists them.
/// @return ShadedWithEdges, Shaded, Wireframe.
const std::vector<DisplayStyle>& allDisplayStyles();

/// Gives a display style's name, as the view bar shows it.
/// @param style The style.
/// @return "Shaded with edges", "Shaded" or "Wireframe".
std::string_view displayStyleName(DisplayStyle style);

/// Finds a display style by name, ignoring upper and lower case (for settings).
/// @param name A name from displayStyleName(), e.g. "wireframe".
/// @return The style, or nothing if no style has that name.
std::optional<DisplayStyle> parseDisplayStyle(std::string_view name);

}  // namespace kh::view
