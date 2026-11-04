# kh.parameters: the Parameters panel (Qt), where the student adds, changes,
# renames and deletes the design's named values. Built only with the desktop app.
kh_add_library(kh_plugin_parameters
  SOURCES
    ParametersPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
)
