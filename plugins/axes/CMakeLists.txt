# sb.axes: the x, y and z axes at the origin, with their letters, drawn as view
# overlays, with its two view options (from sb.viewoptions).
sb_add_library(sb_plugin_axes
  SOURCES
    AxesPlugin.cpp
  DEPS
    sb::plugin
    sb::plugin_viewoptions
    sb::kernel
)
