add_executable(kh_cli
  main.cpp
  Plugins.cpp
  Plugins.h
  Regression.cpp
  Regression.h
)
target_link_libraries(kh_cli PRIVATE
  kh::core kh::kernel kh::kernel_occt kh::model kh::io kh::plugin kh::plugin_modeling
  kh::sketch_planegcs nlohmann_json::nlohmann_json)
kh_set_warnings(kh_cli)
# Windows finds DLLs next to the .exe: copy Khan's own (kh_planegcs) there.
if(WIN32)
  add_custom_command(TARGET kh_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:kh_cli> $<TARGET_FILE_DIR:kh_cli>
    COMMAND_EXPAND_LISTS)
endif()
