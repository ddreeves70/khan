# kh_ui: the Qt side of the plugin API (panels). Used by the desktop app and by
# plugins that add panels; kh_cli never links it.
kh_add_library(kh_ui
  DEPS
    kh::plugin
    Qt6::Widgets
)
