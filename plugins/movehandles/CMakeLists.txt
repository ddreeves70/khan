# sb.movehandles: arrows on the picked body in the 3D view; dragging one slides
# the body (SlideBodyCommand), through the viewer's handles (sb.viewer/3).
# Built only with the desktop app.
sb_add_library(sb_plugin_movehandles
  SOURCES
    MoveHandlesPlugin.cpp
  DEPS
    sb::plugin
    sb::model
    sb::plugin_viewer
)
