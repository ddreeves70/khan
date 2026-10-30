# kh.viewer: the 3D viewer panel (Qt + kh_view). Built only with the desktop app.
kh_add_library(kh_plugin_viewer
  SOURCES
    ViewerPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
    kh::view
    kh::plugin_viewoptions
)
kh_embed_files(kh_plugin_viewer HEADER ViewerIcons.h NAMESPACE kh::icons::viewer
  FILES icons/orbit.svg icons/pan.svg icons/zoom.svg icons/fit.svg icons/views.svg icons/display.svg
        icons/grid.svg icons/panes.svg)
