cmake_minimum_required(VERSION 3.28)

project(Khan
  VERSION 0.0.1
  DESCRIPTION "Free, open-source parametric CAD"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(KH_WITH_OCCT  "Build the OCCT kernel adapter (kh_kernel_occt)" ON)
option(KH_BUILD_APP  "Build the Qt desktop app (kh_app, kh_view)"   ON)
option(KH_BUILD_CLI  "Build the headless command-line tool (kh_cli)" ON)
option(KH_BUILD_TESTS "Build unit tests"                            ON)
option(KH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"     OFF)
option(KH_BUILD_DOCS "Add the 'docs' target (API reference, needs Doxygen)" ON)
option(KH_BUILD_EXAMPLE_PLUGINS "Build the plugin guide's example plugins" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Khan's libraries are also linked into plugins, which are shared libraries.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KhTarget)
include(KhPlugin)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
if(KH_WITH_OCCT)
  find_package(OpenCASCADE CONFIG REQUIRED)
  message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} found")
endif()

# kh_io (the .khan file format) is always built: it needs a JSON and a zip library.
find_package(nlohmann_json 3 CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)

if(KH_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
  include(KhAds)
endif()

if(KH_BUILD_TESTS)
  find_package(Catch2 3 REQUIRED)
  include(CTest)
  include(Catch)
endif()

# ---------------------------------------------------------------------------
# Libraries, in dependency order (see CLAUDE.md for the layer rules)
# ---------------------------------------------------------------------------
add_subdirectory(src/core)        # kh_core: ids, units, diagnostics, version
add_subdirectory(src/kernel)      # kh_kernel: IKernel interface, no OCCT types
if(KH_WITH_OCCT)
  add_subdirectory(src/kernel_occt) # kh_kernel_occt: IKernel on OCCT
endif()
add_subdirectory(src/model)       # kh_model: document, commands (Phase 0, week 3)
add_subdirectory(src/io)          # kh_io: .khan files (zip + JSON)
add_subdirectory(src/plugin)      # kh_plugin: plugin host, event bus, open documents

# Built-in plugins, and the plugin guide's examples (built like outside plugins)
add_subdirectory(plugins/modeling)  # kh.modeling
if(KH_BUILD_EXAMPLE_PLUGINS)
  add_subdirectory(docs/plugin-guide/hello)
endif()
# Your own plugins, built with Khan: -DKH_EXTRA_PLUGINS="C:/code/my-cube;C:/code/other"
# Each folder needs a CMakeLists.txt that calls kh_add_plugin(). See docs/plugin-guide.
set(KH_EXTRA_PLUGINS "" CACHE STRING "Folders of your own plugins to build with Khan")
foreach(extra IN LISTS KH_EXTRA_PLUGINS)
  get_filename_component(extra_name "${extra}" NAME)
  add_subdirectory("${extra}" "${CMAKE_BINARY_DIR}/extra-plugins/${extra_name}")
endforeach()

# ---------------------------------------------------------------------------
# Applications
# ---------------------------------------------------------------------------
if(KH_BUILD_CLI AND KH_WITH_OCCT)
  add_subdirectory(apps/kh_cli)
endif()
if(KH_BUILD_APP)
  add_subdirectory(apps/kh_app)
endif()

if(KH_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# API documentation: `cmake --build --preset dev --target docs`, then open
# build/<preset>/docs/html/index.html. Every public item must have a /// comment;
# the docs.api test fails if one is missing. See docs/Doxyfile.
# ---------------------------------------------------------------------------
if(KH_BUILD_DOCS)
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    set(KH_DOXYFILE "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    file(WRITE "${KH_DOXYFILE}"
      "@INCLUDE = ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile\n"
      "OUTPUT_DIRECTORY = ${CMAKE_CURRENT_BINARY_DIR}/docs\n"
      "PROJECT_NUMBER = ${PROJECT_VERSION}\n")
    add_custom_target(docs
      COMMAND Doxygen::doxygen "${KH_DOXYFILE}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMENT "Building the API reference in ${CMAKE_CURRENT_BINARY_DIR}/docs/html"
      VERBATIM)
    if(KH_BUILD_TESTS)
      add_test(NAME docs.api COMMAND Doxygen::doxygen "${KH_DOXYFILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
  else()
    message(STATUS "Doxygen not found: the 'docs' target and docs.api test are off")
  endif()
endif()
