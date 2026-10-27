#pragma once

/// @file
/// The grid contract: what other plugins may use from the kh.grid plugin.
///
/// It has no code of its own, so a plugin can use the grid without linking to
/// kh.grid. List "kh.grid" in "uses" in your plugin.json, and look the grid up
/// in onStart:
///
/// @code
/// ctx.onStart([&ctx] {
///     if (auto* grid = ctx.service<kh::grid::IGrid>()) {
///         kh::kernel::Vec3 p = grid->snap(pointUnderMouse, pane);  // that pane's grid
///     }
/// });
/// ctx.listen<kh::grid::GridChanged>([](const kh::grid::GridChanged& e) { /* e.grid.spacing */ });
/// @endcode

#include <string>
#include <string_view>

#include "kh/kernel/Types.h"

namespace kh::grid {

/// How the grid is drawn.
enum class GridStyle {
    Dots,   ///< A dot at every crossing: light, and the model stays easy to see.
    Lines,  ///< Full lines, like graph paper (the default).
};

/// The grid's settings.
struct GridSettings {
    bool visible = true;                 ///< Drawn in the views.
    double spacing = 10.0;               ///< Distance between grid lines, in mm.
    bool snap = false;                   ///< Points picked with the mouse jump to the nearest grid crossing.
    GridStyle style = GridStyle::Lines;  ///< Lines or dots.

    /// Compares two sets of settings.
    /// @param other The other settings.
    /// @return True if every setting is the same.
    bool operator==(const GridSettings& other) const = default;
};

/// The smallest grid spacing, in mm.
inline constexpr double kMinSpacing = 0.01;
/// The largest grid spacing, in mm.
inline constexpr double kMaxSpacing = 1000.0;

/// Sent by kh.grid when the grid's settings change.
struct GridChanged {
    static constexpr std::string_view kType = "kh.grid.changed";  ///< Stable event type name.
    GridSettings grid;  ///< The new settings (for `pane`, if it's set).
    /// Empty: the all-panes grid changed. A pane's key: only that pane's own grid
    /// changed ("This pane only" in the View options).
    std::string pane;
};

/// The grid, as other plugins see it: its settings, and snapping. A pane can
/// have its own grid (a different spacing, say), so the pane-aware functions
/// take the pane the student is working in.
class IGrid {
public:
    /// The service's name and version. Version 2 added the pane-aware functions.
    static constexpr const char* kServiceName = "kh.grid/2";

    /// Destroys the grid.
    virtual ~IGrid() = default;

    /// Gives the grid's settings for every pane.
    /// @return The all-panes settings.
    virtual GridSettings settings() const = 0;

    /// Gives the grid's settings in one pane.
    /// @param pane The pane's key (from the view options), or empty for every pane.
    /// @return The pane's own settings where it has them, the all-panes ones otherwise.
    virtual GridSettings settings(std::string_view pane) const = 0;

    /// Snaps one coordinate to the nearest grid line, using the all-panes grid.
    /// @param value A coordinate, in mm.
    /// @return The nearest multiple of the spacing, in mm, or @p value unchanged when snapping is off.
    virtual double snap(double value) const = 0;

    /// Snaps a point to the nearest grid crossing, using the all-panes grid.
    /// @param point A point, in mm.
    /// @return The nearest crossing, in mm, or @p point unchanged when snapping is off.
    virtual kernel::Vec3 snap(const kernel::Vec3& point) const = 0;

    /// Snaps a point picked in a pane, to that pane's grid.
    /// @param point A point, in mm.
    /// @param pane The pane it was picked in.
    /// @return The nearest crossing of that pane's grid, in mm, or @p point unchanged when its snapping is
    /// off.
    virtual kernel::Vec3 snap(const kernel::Vec3& point, std::string_view pane) const = 0;
};

}  // namespace kh::grid
