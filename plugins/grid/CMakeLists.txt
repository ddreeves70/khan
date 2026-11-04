# sb.grid: the grid's settings, View menu items, status-bar text, drawing (as a
# view overlay) and snapping. Qt for its status-bar label and spacing dialog,
# so it's built only with the desktop app.
sb_add_library(sb_plugin_grid
  SOURCES
    GridPlugin.cpp
  DEPS
    sb::ui
    sb::plugin_viewoptions
    sb::plugin
    sb::kernel
)
