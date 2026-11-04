# sb_plugin: plugin manifests, the plugin host, the event bus and the list of
# open documents. No Qt, so sketchbillet_cli and Python can load model-layer plugins too.
sb_add_library(sb_plugin
  SOURCES
    DocumentList.cpp
    EventBus.cpp
    Library.cpp
    Library.h
    Manifest.cpp
    Overlays.cpp
    PluginHost.cpp
    Selection.cpp
    Settings.cpp
    Tiers.cpp
    Tiers.h
  DEPS
    sb::core
    sb::kernel
    sb::model
  PRIVATE_DEPS
    nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS}
)
