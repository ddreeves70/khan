# Broken plugins that the host must refuse, each with a plain message.
set(SB_TEST_PLUGIN_FOLDER "${CMAKE_BINARY_DIR}/test-plugins" PARENT_SCOPE)
foreach(name noentry oldapi badname warns)
  sb_add_plugin(sb_test_plugin_${name}
    MANIFEST ${name}/plugin.json
    SOURCES ${name}/${name}.cpp
    FOLDER "${CMAKE_BINARY_DIR}/test-plugins")
endforeach()
