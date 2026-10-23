# kh.modeling: the built-in plugin with Khan's modeling commands.
kh_add_library(kh_plugin_modeling
  SOURCES
    ModelingPlugin.cpp
  DEPS
    kh::plugin
    kh::model
)
