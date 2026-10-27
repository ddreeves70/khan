# kh.viewoptions: keeps every view option (the grid's, the axes', any
# plugin's), builds their View menu items through addOption(), and adds the
# View options panel. Qt for the panel, so it's built only with the desktop app.
kh_add_library(kh_plugin_viewoptions
  SOURCES
    ViewOptionsPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
)
