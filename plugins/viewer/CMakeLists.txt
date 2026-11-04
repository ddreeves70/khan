# sb.viewer: the 3D viewer panel (Qt + sb_view). Built only with the desktop app.
sb_add_library(sb_plugin_viewer
  SOURCES
    ViewerPlugin.cpp
  DEPS
    sb::ui
    sb::plugin
    sb::model
    sb::view
    sb::plugin_viewoptions
)
sb_embed_files(sb_plugin_viewer HEADER ViewerIcons.h NAMESPACE sb::icons::viewer
  FILES icons/orbit.svg icons/pan.svg icons/zoom.svg icons/fit.svg icons/views.svg icons/display.svg
        icons/grid.svg icons/panes.svg)
