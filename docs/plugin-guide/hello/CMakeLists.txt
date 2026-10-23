# The Hello plugin from docs/plugin-guide/hello-plugin.md.
kh_add_plugin(kh_hello_plugin
  MANIFEST plugin.json
  SOURCES hello.cpp
  DEPS kh::model
)
