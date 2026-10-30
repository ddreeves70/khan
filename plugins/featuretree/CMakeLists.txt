# kh.featuretree: the feature tree panel (Qt). Built only with the desktop app.
kh_add_library(kh_plugin_featuretree
  SOURCES
    FeatureTreePlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
)
kh_embed_files(kh_plugin_featuretree HEADER FeatureTreeIcons.h NAMESPACE kh::icons::featuretree
  FILES icons/settings.svg)
