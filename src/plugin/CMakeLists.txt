# kh_plugin: plugin manifests, the plugin host, the event bus and the list of
# open documents. No Qt, so kh_cli and Python can load model-layer plugins too.
kh_add_library(kh_plugin
  SOURCES
    DocumentList.cpp
    EventBus.cpp
    Grid.cpp
    Library.cpp
    Library.h
    Manifest.cpp
    PluginHost.cpp
    Selection.cpp
  DEPS
    kh::core
    kh::kernel
    kh::model
  PRIVATE_DEPS
    nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS}
)
