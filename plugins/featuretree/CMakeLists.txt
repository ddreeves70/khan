# kh.featuretree: the feature tree panel (Qt). Built only with the desktop app.
kh_add_library(kh_plugin_featuretree
  SOURCES
    FeatureTreePlugin.cpp
  DEPS
    kh::ui
    kh::plugin
    kh::model
)
