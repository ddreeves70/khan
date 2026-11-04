# sb.featuretree: the feature tree panel (Qt). Built only with the desktop app.
sb_add_library(sb_plugin_featuretree
  SOURCES
    FeatureTreePlugin.cpp
  DEPS
    sb::ui
    sb::plugin
    sb::model
)
sb_embed_files(sb_plugin_featuretree HEADER FeatureTreeIcons.h NAMESPACE sb::icons::featuretree
  FILES icons/settings.svg)
