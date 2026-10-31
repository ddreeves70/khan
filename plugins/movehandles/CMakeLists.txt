# kh.movehandles: arrows on the picked body in the 3D view; dragging one slides
# the body (SlideBodyCommand), through the viewer's handles (kh.viewer/3).
# Built only with the desktop app.
kh_add_library(kh_plugin_movehandles
  SOURCES
    MoveHandlesPlugin.cpp
  DEPS
    kh::plugin
    kh::model
    kh::plugin_viewer
)
