#pragma once

/// @file
/// The grid: drawn in the views, and optionally snapped to.

#include <string_view>

#include "kh/core/Diagnostic.h"
#include "kh/kernel/Types.h"
#include "kh/plugin/EventBus.h"

namespace kh::plugin {

/// How the grid is drawn.
enum class GridStyle {
    Dots,   ///< A dot at every crossing: light, and the model stays easy to see.
    Lines,  ///< Full lines, like graph paper (the default).
};

/// Everything about the grid the student can change.
struct GridSettings {
    bool visible = true;                ///< Drawn in the views.
    double spacing = 10.0;              ///< Distance between grid lines, in mm.
    bool snap = false;                  ///< Points picked with the mouse jump to the nearest grid crossing.
    GridStyle style = GridStyle::Lines;  ///< Lines or dots.

    /// Compares two settings.
    /// @param other The settings to compare with.
    /// @return True if every field is the same.
    bool operator==(const GridSettings& other) const = default;
};

/// The grid settings changed. Views redraw the grid; tools that snap read the new settings.
struct GridChanged {
    static constexpr std::string_view kType = "kh.GridChanged";  ///< Stable event type name.
    GridSettings grid;                                           ///< The new settings.
};

/// The grid, shared by every view and tool in the desktop app.
///
/// The app provides it as a service, so plugins find it with
/// `ctx.service<kh::plugin::Grid>()` (nullptr in kh_cli, which has no views).
/// Every change sends GridChanged.
///
/// @code
/// if (auto* grid = ctx.service<kh::plugin::Grid>()) {
///     kernel::Vec3 p = grid->snap(pointUnderMouse);  // unchanged when snapping is off
/// }
/// @endcode
class Grid {
public:
    /// The name plugins use to find this service.
    static constexpr const char* kServiceName = "kh.grid";

    /// The smallest spacing allowed, in mm.
    static constexpr double kMinSpacing = 0.01;
    /// The largest spacing allowed, in mm.
    static constexpr double kMaxSpacing = 1000.0;

    /// Makes a grid with the default settings: shown, 10 mm, no snapping, lines.
    /// @param bus Where GridChanged is sent. Must outlive the grid.
    explicit Grid(EventBus& bus) : bus_(bus) {}

    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;

    /// Gives the current settings.
    /// @return The settings.
    const GridSettings& settings() const { return settings_; }

    /// Changes every setting at once, e.g. when restoring them. Sends GridChanged
    /// if anything differs.
    /// @param settings The new settings. A spacing outside the allowed range is refused.
    /// @return No diagnostics on success; otherwise why, and nothing changed.
    Diagnostics set(const GridSettings& settings);

    /// Shows or hides the grid.
    /// @param visible True to show it.
    void setVisible(bool visible);

    /// Changes the distance between grid lines.
    /// @param spacing The spacing in mm, from kMinSpacing to kMaxSpacing.
    /// @return No diagnostics on success; otherwise why, and nothing changed.
    Diagnostics setSpacing(double spacing);

    /// Turns snapping to the grid on or off.
    /// @param snap True to snap.
    void setSnap(bool snap);

    /// Draws the grid as dots or lines.
    /// @param style The style.
    void setStyle(GridStyle style);

    /// Moves a value to the nearest multiple of the spacing, when snapping is on.
    /// @param value A coordinate in mm.
    /// @return The snapped value, or @p value unchanged when snapping is off.
    double snap(double value) const;

    /// Moves a point to the nearest grid crossing, when snapping is on.
    /// @param point A point in mm.
    /// @return The snapped point (each of x, y and z), or @p point unchanged when snapping is off.
    kernel::Vec3 snap(const kernel::Vec3& point) const;

private:
    void changed(const GridSettings& next);

    EventBus& bus_;
    GridSettings settings_;
};

}  // namespace kh::plugin
