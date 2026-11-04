# sb.viewoptions: keeps every view option (the grid's, the axes', any
# plugin's), builds their View menu items through addOption(), and adds the
# View options panel. Qt for the panel, so it's built only with the desktop app.
sb_add_library(sb_plugin_viewoptions
  SOURCES
    ViewOptionsPlugin.cpp
  DEPS
    sb::ui
    sb::plugin
)
