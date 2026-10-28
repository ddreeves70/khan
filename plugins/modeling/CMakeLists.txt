# kh.modeling: the built-in plugin with Khan's modeling commands.
kh_add_library(kh_plugin_modeling
  SOURCES
    ModelingPlugin.cpp
  DEPS
    kh::plugin
    kh::model
)
kh_embed_files(kh_plugin_modeling HEADER ModelingIcons.h NAMESPACE kh::icons::modeling
  FILES icons/box.svg icons/cylinder.svg icons/union.svg icons/cut.svg icons/intersect.svg icons/move.svg)
