add_executable(sketchbillet_cli
  main.cpp
  Plugins.cpp
  Plugins.h
  Regression.cpp
  Regression.h
)
target_link_libraries(sketchbillet_cli PRIVATE
  sb::core sb::kernel sb::kernel_occt sb::model sb::io sb::plugin sb::plugin_modeling
  sb::sketch_planegcs nlohmann_json::nlohmann_json)
sb_set_warnings(sketchbillet_cli)
# Windows finds DLLs next to the .exe: copy SketchBillet's own (sb_planegcs) there.
if(WIN32)
  add_custom_command(TARGET sketchbillet_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sketchbillet_cli> $<TARGET_FILE_DIR:sketchbillet_cli>
    COMMAND_EXPAND_LISTS)
endif()
