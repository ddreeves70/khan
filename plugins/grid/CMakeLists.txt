# kh.grid: the grid's settings, View menu items, status-bar text, drawing (as a
# view overlay) and snapping. Qt for its status-bar label and spacing dialog,
# so it's built only with the desktop app.
kh_add_library(kh_plugin_grid
  SOURCES
    GridPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::kernel
)
