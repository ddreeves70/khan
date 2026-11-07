#pragma once

/// @file
/// sb.viewer: the built-in plugin with the 3D viewer panel.

#include <vector>

#include "sb/plugin/DocumentList.h"
#include "sb/plugin/Overlays.h"
#include "sb/plugin/PluginHost.h"
#include "sb/plugin/Selection.h"
#include "sb/view/Scene.h"

namespace sb::plugins {

/// Gives the sb.viewer plugin: the 3D viewer in the middle of the window.
///
/// The viewer shows the active design's finished bodies, split into 1, 2 or 4
/// panes with their own cameras, and the student can open more viewers
/// (Window > New viewer). Clicking a body selects its step everywhere, and
/// selected steps are highlighted. It also draws every plugin's overlays (the
/// grid comes from sb.grid this way). Without 3D graphics it draws them flat,
/// seen from above, and says why.
/// @return Its manifest and register function, for PluginHost::addBuiltin().
plugin::BuiltinPlugin viewerPlugin();

/// Works out what the viewer shows of the active design: its finished bodies,
/// plus selected steps that built but are used by a later step, drawn see-through;
/// whether clicking picks edges, and the picked edges, found by name in the
/// latest rebuild (one that can't be found isn't drawn).
/// @param docs The open designs.
/// @param selection The shared selection.
/// @return The bodies and selection, empty if no design is open or it hasn't built.
view::SceneContent viewerScene(const plugin::DocumentList& docs, const plugin::Selection& selection);

/// Turns edges clicked in a view (numbers in the latest rebuild of the active
/// design) into lasting references, for Selection::setElements().
/// @param docs The open designs.
/// @param picks The clicked edges.
/// @return One reference per edge whose body has names; others are left out.
std::vector<plugin::PickedElement> pickedEdges(const plugin::DocumentList& docs,
                                               const std::vector<view::ScenePick>& picks);

/// Turns plugins' overlays into what a view draws, leaving out hidden ones.
/// @param overlays Every plugin's overlays, in order.
/// @return The visible ones, in the same order.
std::vector<view::ViewOverlay> viewOverlays(const std::vector<plugin::Overlay>& overlays);

}  // namespace sb::plugins
