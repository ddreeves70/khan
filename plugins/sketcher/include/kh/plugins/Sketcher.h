#pragma once

/// @file
/// kh.sketcher: the built-in plugin that draws sketches and edits them with the mouse.

#include <vector>

#include "kh/model/Document.h"
#include "kh/plugin/Overlays.h"
#include "kh/plugin/PluginHost.h"

namespace kh::plugins {

/// Gives the kh.sketcher plugin.
///
/// It draws every sketch of the active design in the viewers, on its plane. The
/// Sketch menu makes new sketches (on XY, XZ or YZ) and edits one: the viewers
/// turn to face its plane, and the Line, Circle, Arc and Point tools draw in it
/// with the mouse. Ends snap to the sketch's points (joined with a Coincident
/// constraint) and to the grid when grid snapping is on. Each shape is one undo
/// step. Escape stops the shape being drawn; Escape again (or Finish sketch)
/// stops editing.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin sketcherPlugin();

/// Works out the lines that draw a sketch in the design: each line, and each
/// circle and arc as short straight pieces.
/// @param plane The sketch's plane.
/// @param entities Its entities (solved, or as drawn).
/// @return Pairs of points, in mm: each pair is one line.
std::vector<kernel::Vec3> sketchLines(model::SketchPlane plane, const std::vector<sketch::Entity>& entities);

}  // namespace kh::plugins
