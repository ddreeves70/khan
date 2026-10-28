# kh.sketcher: draws sketches in the viewers, and edits them with the mouse
# (Line, Circle, Arc, Point) through the viewer's drawing tools (kh.viewer/1).
# Built only with the desktop app.
kh_add_library(kh_plugin_sketcher
  SOURCES
    SketcherPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
    kh::plugin_viewer
    kh::plugin_grid
)
