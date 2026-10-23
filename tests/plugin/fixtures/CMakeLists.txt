# Broken plugins that the host must refuse, each with a plain message.
set(KH_TEST_PLUGIN_FOLDER "${CMAKE_BINARY_DIR}/test-plugins" PARENT_SCOPE)
foreach(name noentry oldapi badname)
  kh_add_plugin(kh_test_plugin_${name}
    MANIFEST ${name}/plugin.json
    SOURCES ${name}/${name}.cpp
    FOLDER "${CMAKE_BINARY_DIR}/test-plugins")
endforeach()
