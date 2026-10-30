#pragma once

/// @file
/// kh.axes: the built-in plugin that draws the x, y and z axes on the view cube
/// (and at the origin, if the student turns that on), with their letters.

#include <string>
#include <vector>

#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginHost.h"
#include "kh/viewoptions/ViewOptions.h"

namespace kh::plugins {

/// Gives the kh.axes plugin. It draws the three axes on the view cube in every
/// viewer (x red, y green, z blue), labeled X, Y and Z, with three view options:
/// View > Show axes, Show axis labels and Show axes at the origin.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin axesPlugin();

/// Gives the axes' view options: kh.axes.show, kh.axes.labels and kh.axes.origin.
/// @return The options, for kh.viewoptions.
std::vector<viewoptions::OptionSpec> axesOptions();

/// What kh.axes draws.
struct AxesOverlay {
    std::string name;         ///< The overlay's short name, e.g. "x", "x_label" or "origin_x".
    plugin::Overlay overlay;  ///< What to draw.
};

/// Gives what kh.axes draws: a line for each axis, and its letter just past its
/// end, on the view cube and at the origin.
/// @param onCube True to show the axes on the view cube.
/// @param labels True to show their letters (only where the axes show).
/// @param atOrigin True to show the axes at the origin too.
/// @return Twelve overlays: the cube's x, y and z lines (named "x", "y", "z"), their
///         labels ("x_label", ...), then the same at the origin ("origin_x", ...,
///         "origin_x_label", ...); hidden ones have `visible` false.
std::vector<AxesOverlay> axesOverlays(bool onCube, bool labels, bool atOrigin);

/// How long each axis is drawn at the origin, in mm.
inline constexpr double kAxisLength = 25.0;

/// How far outside the view cube's bevelled edges its axes are drawn, in screen
/// pixels, so they don't flicker with the cube.
inline constexpr double kCubeAxisGap = 1.0;

/// How far past the view cube each of its axes reaches, in screen pixels.
inline constexpr double kCubeAxisOvershoot = 14.0;

}  // namespace kh::plugins
