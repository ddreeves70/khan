# sb.modeling: the built-in plugin with SketchBillet's modeling commands.
sb_add_library(sb_plugin_modeling
  SOURCES
    ModelingPlugin.cpp
  DEPS
    sb::plugin
    sb::model
)
sb_embed_files(sb_plugin_modeling HEADER ModelingIcons.h NAMESPACE sb::icons::modeling
  FILES icons/box.svg icons/cylinder.svg icons/union.svg icons/cut.svg icons/intersect.svg icons/move.svg icons/extrude.svg)
