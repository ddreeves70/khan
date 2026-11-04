# sb_add_plugin(<target> MANIFEST <plugin.json> SOURCES ... [DEPS ...] [FOLDER <dir>])
#
# Builds an outside-style C++ plugin: a shared library plus its plugin.json
# (with "apiVersion" filled in from Api.h),
# both placed in <FOLDER>/<plugin id>/ (default <build>/sketchbillet-plugins), ready for
# PluginHost::addFolder(<FOLDER>).
# The library's file name comes from "library" in plugin.json: hello.dll on
# Windows, hello.dylib on macOS, hello.so on Linux (see libraryFileName()).
# See docs/plugin-guide/hello-plugin.md.
# The plugin API version, read from Api.h so it lives in one place.
# Changing it re-runs CMake, so every plugin.json gets the new number.
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/../src/plugin/include/sb/plugin/Api.h")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../src/plugin/include/sb/plugin/Api.h" sb_api_line
  REGEX "inline constexpr int kPluginApiVersion = [0-9]+;")
string(REGEX MATCH "= ([0-9]+)" sb_api_match "${sb_api_line}")
set(SB_PLUGIN_API_VERSION "${CMAKE_MATCH_1}")
if(NOT SB_PLUGIN_API_VERSION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "SbPlugin.cmake: couldn't read kPluginApiVersion from Api.h")
endif()

function(sb_add_plugin target)
  cmake_parse_arguments(ARG "" "MANIFEST;FOLDER" "SOURCES;DEPS" ${ARGN})
  if(NOT ARG_FOLDER)
    set(ARG_FOLDER "${CMAKE_BINARY_DIR}/sketchbillet-plugins")
  endif()
  if(NOT ARG_MANIFEST)
    message(FATAL_ERROR "sb_add_plugin(${target}): MANIFEST plugin.json is required")
  endif()
  get_filename_component(manifest "${ARG_MANIFEST}" ABSOLUTE)
  file(READ "${manifest}" manifest_text)
  string(JSON plugin_id GET "${manifest_text}" id)
  string(JSON plugin_library GET "${manifest_text}" library)

  set(dir "${ARG_FOLDER}/${plugin_id}")
  add_library(${target} MODULE ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE sb::plugin ${ARG_DEPS})
  sb_set_warnings(${target})
  if(APPLE)
    set(suffix ".dylib")
  elseif(WIN32)
    set(suffix ".dll")
  else()
    set(suffix ".so")
  endif()
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${plugin_library}"
    PREFIX ""
    SUFFIX "${suffix}"
    # Export only what SB_PLUGIN_EXPORT marks.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # $<1:...> stops multi-config generators (Visual Studio) adding a Debug/ folder.
    LIBRARY_OUTPUT_DIRECTORY "$<1:${dir}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${dir}>")
  # The plugin.json next to the library gets this SketchBillet's apiVersion: the library
  # is compiled against this SketchBillet's headers, so the two always match, and nobody
  # edits the number by hand. A source plugin.json may leave it out.
  string(JSON manifest_text SET "${manifest_text}" apiVersion "${SB_PLUGIN_API_VERSION}")
  file(CONFIGURE OUTPUT "${dir}/plugin.json" CONTENT "${manifest_text}\n" @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
  set_property(TARGET ${target} PROPERTY SB_PLUGIN_DIR "${dir}")
endfunction()
