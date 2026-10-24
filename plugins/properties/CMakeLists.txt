# kh.properties: the Properties panel (Qt). Built only with the desktop app.
kh_add_library(kh_plugin_properties
  SOURCES
    PropertiesPlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
)
