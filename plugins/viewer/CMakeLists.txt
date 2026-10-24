# kh.viewer: the 3D viewer panel (Qt + kh_view). Built only with the desktop app.
kh_add_library(kh_plugin_viewer
  SOURCES
    ViewerPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
    kh::view
)
