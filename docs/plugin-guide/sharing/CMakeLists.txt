# The plugin guide's sharing example (docs/plugin-guide/sharing.md): Ruler offers
# a service, Tape uses it.

# The contract: headers only, so Tape can use the ruler without linking to Ruler.
add_library(example_ruler_api INTERFACE)
target_include_directories(example_ruler_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/ruler/include")

kh_add_plugin(kh_example_ruler_plugin
  MANIFEST ruler/plugin.json
  SOURCES ruler/ruler.cpp
  DEPS example_ruler_api
)
kh_add_plugin(kh_example_tape_plugin
  MANIFEST tape/plugin.json
  SOURCES tape/tape.cpp
  DEPS example_ruler_api
)
