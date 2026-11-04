# sb_ui: the Qt side of the plugin API (panels). Used by the desktop app and by
# plugins that add panels; sketchbillet_cli never links it.
sb_add_library(sb_ui
  DEPS
    sb::plugin
    Qt6::Widgets
)
