# kh.axes: the x, y and z axes at the origin, with their letters, drawn as view
# overlays. No Qt: it only uses settings, menu items and overlays.
kh_add_library(kh_plugin_axes
  SOURCES
    AxesPlugin.cpp
  DEPS
    kh::plugin
    kh::kernel
)
