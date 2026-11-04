# sb.parameters: the Parameters panel (Qt), where the student adds, changes,
# renames and deletes the design's named values. Built only with the desktop app.
sb_add_library(sb_plugin_parameters
  SOURCES
    ParametersPlugin.cpp
  DEPS
    sb::ui
    sb::plugin
    sb::model
)
