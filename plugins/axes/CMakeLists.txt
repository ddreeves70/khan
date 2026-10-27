# kh.axes: the x, y and z axes at the origin, with their letters, drawn as view
# overlays, with its two view options (from kh.viewoptions).
kh_add_library(kh_plugin_axes
  SOURCES
    AxesPlugin.cpp
  DEPS
    kh::plugin
    kh::plugin_viewoptions
    kh::kernel
)
