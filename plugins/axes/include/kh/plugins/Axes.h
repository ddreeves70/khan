#pragma once

/// @file
/// kh.axes: the built-in plugin that draws the x, y and z axes at the origin,
/// with their letters.

#include <vector>

#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginHost.h"
#include "kh/viewoptions/ViewOptions.h"

namespace kh::plugins {

/// Gives the kh.axes plugin. It draws the three axes through the origin in
/// every viewer (x red, y green, z blue), labeled X, Y and Z, with two view
/// options: View > Show axes and View > Show axis labels.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin axesPlugin();

/// Gives the axes' view options: kh.axes.show and kh.axes.labels.
/// @return The options, for kh.viewoptions.
std::vector<viewoptions::OptionSpec> axesOptions();

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
