#pragma once

/// @file
/// kh.grid: the built-in plugin with the grid: its settings, View menu items,
/// status-bar text, drawing and snapping.

#include "kh/core/Diagnostic.h"
#include "kh/grid/Grid.h"
#include "kh/plugin/EventBus.h"
#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginHost.h"
#include "kh/plugin/Settings.h"

namespace kh::plugins {

/// Gives the kh.grid plugin. It offers the grid (kh.grid/1) to other plugins,
/// adds the grid's View menu items, remembers its settings, draws it in every
/// viewer with an overlay, and shows it in the status bar of the desktop app.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin gridPlugin();

/// Gives what kh.grid draws for some settings: a lattice on the XY plane,
/// reaching about 50 lines each way (at least 100 mm, at most 100 lines).
/// @param grid The grid's settings.
/// @return The overlay; hidden when the grid is off.
plugin::Overlay gridOverlay(const grid::GridSettings& grid);

/// The grid itself, inside kh.grid. Its settings are kept in the plugin's
/// settings, so they last between runs.
class GridService final : public grid::IGrid {
public:
    /// Makes the grid from what was saved; a damaged spacing falls back to 10 mm.
    /// @param settings kh.grid's settings. Must outlive the grid.
    /// @param bus Where GridChanged is sent. Must outlive the grid.
    GridService(plugin::PluginSettings& settings, plugin::EventBus& bus);

    grid::GridSettings settings() const override { return settings_; }
    double snap(double value) const override;
    kernel::Vec3 snap(const kernel::Vec3& point) const override;

    /// Changes every setting at once. Sends GridChanged once if anything changed.
    /// @param settings The new settings.
    /// @return A problem if the spacing is outside 0.01 to 1000 mm; nothing changes then.
    Diagnostics set(const grid::GridSettings& settings);

    /// Changes the spacing.
    /// @param spacing Distance between lines, in mm.
    /// @return A problem if it is outside 0.01 to 1000 mm.
    Diagnostics setSpacing(double spacing);

    /// Reads the settings again, after someone changed them in the store.
    void reload();

private:
    grid::GridSettings read() const;

    plugin::PluginSettings& store_;
    plugin::EventBus& bus_;
    grid::GridSettings settings_;
    bool writing_ = false;
};

}  // namespace kh::plugins
