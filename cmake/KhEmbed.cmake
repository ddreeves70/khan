# kh_embed_files(): builds small text files (like SVG icons) into a target.
#
#   kh_embed_files(kh_plugin_modeling
#     HEADER ModelingIcons.h NAMESPACE kh::icons::modeling
#     FILES icons/box.svg icons/cylinder.svg)
#
# makes a header, included as "ModelingIcons.h", with one constant per file:
#
#   namespace kh::icons::modeling {
#   inline constexpr std::string_view box_svg = R"khembed(<svg ...>)khembed";
#   }
#
# The name is the file name with "." and "-" turned into "_". Changing a file
# re-runs CMake, which rewrites the header only if something changed.
function(kh_embed_files target)
  cmake_parse_arguments(ARG "" "HEADER;NAMESPACE" "FILES" ${ARGN})
  if(NOT ARG_HEADER OR NOT ARG_NAMESPACE OR NOT ARG_FILES)
    message(FATAL_ERROR "kh_embed_files(${target}) needs HEADER, NAMESPACE and FILES")
  endif()
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/kh_embedded/${target}")
  set(text "// Made by kh_embed_files() (cmake/KhEmbed.cmake). Don't edit: change the files instead.\n")
  string(APPEND text "#pragma once\n\n#include <string_view>\n\nnamespace ${ARG_NAMESPACE} {\n\n")
  foreach(file IN LISTS ARG_FILES)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
    get_filename_component(name "${file}" NAME)
    string(MAKE_C_IDENTIFIER "${name}" id)
    file(READ "${path}" body)
    if(body MATCHES "\\)khembed\"")
      message(FATAL_ERROR "${file} contains )khembed\", which kh_embed_files can't embed")
    endif()
    string(APPEND text "/// ${file}\ninline constexpr std::string_view ${id} = R\"khembed(${body})khembed\";\n\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
  endforeach()
  string(APPEND text "}  // namespace ${ARG_NAMESPACE}\n")
  set(out "${dir}/${ARG_HEADER}")
  set(old "")
  if(EXISTS "${out}")
    file(READ "${out}" old)
  endif()
  if(NOT old STREQUAL text)
    file(WRITE "${out}" "${text}")
  endif()
  target_include_directories(${target} PRIVATE "${dir}")
  target_sources(${target} PRIVATE ${ARG_FILES})  # so IDEs list them
endfunction()
