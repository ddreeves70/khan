#pragma once

/// @file
/// sb.grid: the built-in plugin with the grid: its settings, View menu items,
/// status-bar text, drawing and snapping.

#include <vector>

#include "sb/core/Diagnostic.h"
#include "sb/grid/Grid.h"
#include "sb/plugin/EventBus.h"
#include "sb/plugin/Overlays.h"
#include "sb/plugin/PluginHost.h"
#include "sb/viewoptions/ViewOptions.h"

namespace sb::plugins {

/// Gives the sb.grid plugin. It offers the grid (sb.grid/2) to other plugins,
/// adds the grid's View menu items, remembers its settings, draws it in every
/// viewer with an overlay, and shows it in the status bar of the desktop app.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin gridPlugin();

/// Gives what sb.grid draws for some settings: a lattice on the XY plane,
/// reaching about 50 lines each way (at least 100 mm, at most 100 lines).
/// @param grid The grid's settings.
/// @return The overlay; hidden when the grid is off.
plugin::Overlay gridOverlay(const grid::GridSettings& grid);

/// Gives the grid's view options: sb.grid.show, sb.grid.snap, sb.grid.style
/// and sb.grid.spacing, in the order the View menu shows them.
/// @return The options, for sb.viewoptions.
std::vector<viewoptions::OptionSpec> gridOptions();

/// The grid itself, inside sb.grid. Its settings are sb.grid's view options
/// (see gridOptions()), so sb.viewoptions remembers and shows them.
class GridService final : public grid::IGrid {
public:
    /// Makes the grid from its view options, which must already be added.
    /// @param options The view options service. Must outlive the grid.
    /// @param bus Where GridChanged is sent, and ViewOptionChanged heard. Must outlive the grid.
    GridService(viewoptions::IViewOptions& options, plugin::EventBus& bus);

    grid::GridSettings settings() const override { return settings_; }
    grid::GridSettings settings(std::string_view pane) const override;
    double snap(double value) const override;
    kernel::Vec3 snap(const kernel::Vec3& point) const override;
    kernel::Vec3 snap(const kernel::Vec3& point, std::string_view pane) const override;

    /// Checks whether a pane has its own grid ("This pane only").
    /// @param pane The pane's key.
    /// @return True if any of the grid's options differ in that pane.
    bool hasOwn(std::string_view pane) const;

    /// Changes every setting at once. Sends GridChanged once if anything changed.
    /// @param settings The new settings.
    /// @return A problem if the spacing is outside 0.01 to 1000 mm; nothing changes then.
    Diagnostics set(const grid::GridSettings& settings);

    /// Changes the spacing.
    /// @param spacing Distance between lines, in mm.
    /// @return A problem if it is outside 0.01 to 1000 mm.
    Diagnostics setSpacing(double spacing);

private:
    grid::GridSettings read(std::string_view pane) const;
    void changed(const viewoptions::ViewOptionChanged& e);

    viewoptions::IViewOptions& options_;
    plugin::EventBus& bus_;
    grid::GridSettings settings_;
    plugin::Subscription sub_;
    bool writing_ = false;
};

}  // namespace sb::plugins
