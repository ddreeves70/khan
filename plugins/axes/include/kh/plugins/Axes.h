#pragma once

/// @file
/// kh.axes: the built-in plugin that draws the x, y and z axes at the origin,
/// with their letters.

#include <vector>

#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginHost.h"

namespace kh::plugins {

/// Gives the kh.axes plugin. It draws the three axes through the origin in
/// every viewer (x red, y green, z blue), labeled X, Y and Z, and adds
/// View > Show axes and View > Show axis labels, which Khan remembers.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin axesPlugin();

/// What kh.axes draws.
struct AxesOverlay {
    const char* name;         ///< The overlay's short name, e.g. "x" or "x_label".
    plugin::Overlay overlay;  ///< What to draw.
};

/// Gives what kh.axes draws: a line for each axis, and its letter just past its end.
/// @param axes True to show the axes.
/// @param labels True to show their letters (only when the axes show).
/// @return Six overlays: x, y and z lines, then their labels; hidden ones have `visible` false.
std::vector<AxesOverlay> axesOverlays(bool axes, bool labels);

/// How long each axis is drawn, in mm.
inline constexpr double kAxisLength = 25.0;

}  // namespace kh::plugins
