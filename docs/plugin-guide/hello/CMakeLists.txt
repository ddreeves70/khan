# The Hello plugin from docs/plugin-guide/hello-plugin.md.
sb_add_plugin(sb_hello_plugin
  MANIFEST plugin.json
  SOURCES hello.cpp
  DEPS sb::model
)
