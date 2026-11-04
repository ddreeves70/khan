cmake_minimum_required(VERSION 3.28)

project(SketchBillet
  VERSION 0.0.1
  DESCRIPTION "Free, open-source parametric CAD"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(SB_WITH_OCCT  "Build the OCCT kernel adapter (sb_kernel_occt)" ON)
option(SB_BUILD_APP  "Build the Qt desktop app (sketchbillet, sb_view)"   ON)
option(SB_BUILD_CLI  "Build the headless command-line tool (sketchbillet_cli)" ON)
option(SB_BUILD_TESTS "Build unit tests"                            ON)
option(SB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"     OFF)
option(SB_BUILD_DOCS "Add the 'docs' target (API reference, needs Doxygen)" ON)
option(SB_BUILD_EXAMPLE_PLUGINS "Build the plugin guide's example plugins" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# SketchBillet's libraries are also linked into plugins, which are shared libraries.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SbTarget)
include(SbPlugin)
include(SbEmbed)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
if(SB_WITH_OCCT)
  find_package(OpenCASCADE CONFIG REQUIRED)
  message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} found")
endif()

# sb_io (the .sb file format) is always built: it needs a JSON and a zip library.
find_package(nlohmann_json 3 CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
# Linear algebra for the sketch solver (header-only, MPL-2.0).
find_package(Eigen3 3.3 CONFIG REQUIRED)

if(SB_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg)
  set(CMAKE_AUTOMOC ON)
  include(SbAds)
endif()

if(SB_BUILD_TESTS)
  find_package(Catch2 3 REQUIRED)
  include(CTest)
  include(Catch)
endif()

# ---------------------------------------------------------------------------
# Libraries, in dependency order (see CLAUDE.md for the layer rules)
# ---------------------------------------------------------------------------
add_subdirectory(src/core)        # sb_core: ids, units, diagnostics, version
add_subdirectory(src/kernel)      # sb_kernel: IKernel interface, no OCCT types
if(SB_WITH_OCCT)
  add_subdirectory(src/kernel_occt) # sb_kernel_occt: IKernel on OCCT
endif()
add_subdirectory(src/sketch)      # sb_sketch: sketch geometry, constraints, ISketchSolver
add_subdirectory(third_party/planegcs)  # sb_planegcs: FreeCAD's solver, LGPL, a shared library
add_subdirectory(src/sketch_planegcs)   # sb_sketch_planegcs: ISketchSolver on PlaneGCS
add_subdirectory(src/model)       # sb_model: document, commands (Phase 0, week 3)
add_subdirectory(src/io)          # sb_io: .sb files (zip + JSON)
add_subdirectory(src/plugin)      # sb_plugin: plugin host, event bus, open documents
if(SB_BUILD_APP)
  add_subdirectory(src/ui)        # sb_ui: Qt side of the plugin API (panels)
endif()
if(SB_BUILD_APP AND SB_WITH_OCCT)
  add_subdirectory(src/view)      # sb_view: the 3D view (Qt + OCCT)
endif()

# Built-in plugins, and the plugin guide's examples (built like outside plugins)
add_subdirectory(plugins/modeling)  # sb.modeling
if(SB_BUILD_APP)
  add_subdirectory(plugins/viewoptions)  # sb.viewoptions (Qt)
  add_subdirectory(plugins/axes)         # sb.axes
  add_subdirectory(plugins/featuretree)  # sb.featuretree (Qt)
  add_subdirectory(plugins/properties)   # sb.properties (Qt)
  add_subdirectory(plugins/grid)         # sb.grid (Qt)
endif()
if(TARGET sb_view)
  add_subdirectory(plugins/viewer)       # sb.viewer (Qt + OCCT)
  add_subdirectory(plugins/sketcher)     # sb.sketcher (Qt)
  add_subdirectory(plugins/movehandles)  # sb.movehandles
  add_subdirectory(plugins/parameters)   # sb.parameters (Qt)
endif()
if(SB_BUILD_EXAMPLE_PLUGINS)
  add_subdirectory(docs/plugin-guide/hello)
  add_subdirectory(docs/plugin-guide/sharing)
endif()
# Your own plugins, built with SketchBillet: -DSB_EXTRA_PLUGINS="C:/code/my-cube;C:/code/other"
# Each folder needs a CMakeLists.txt that calls sb_add_plugin(). See docs/plugin-guide.
set(SB_EXTRA_PLUGINS "" CACHE STRING "Folders of your own plugins to build with SketchBillet")
foreach(extra IN LISTS SB_EXTRA_PLUGINS)
  get_filename_component(extra_name "${extra}" NAME)
  add_subdirectory("${extra}" "${CMAKE_BINARY_DIR}/extra-plugins/${extra_name}")
endforeach()

# ---------------------------------------------------------------------------
# Applications
# ---------------------------------------------------------------------------
if(SB_BUILD_CLI AND SB_WITH_OCCT)
  add_subdirectory(apps/sketchbillet_cli)
endif()
if(SB_BUILD_APP AND SB_WITH_OCCT)  # the app rebuilds designs with OCCT
  add_subdirectory(apps/sketchbillet)
endif()

if(SB_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# API documentation: `cmake --build --preset dev --target docs`, then open
# build/<preset>/docs/html/index.html. Every public item must have a /// comment;
# the docs.api test fails if one is missing. See docs/Doxyfile.
# ---------------------------------------------------------------------------
if(SB_BUILD_DOCS)
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    set(SB_DOXYFILE "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    file(WRITE "${SB_DOXYFILE}"
      "@INCLUDE = ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile\n"
      "OUTPUT_DIRECTORY = ${CMAKE_CURRENT_BINARY_DIR}/docs\n"
      "PROJECT_NUMBER = ${PROJECT_VERSION}\n")
    add_custom_target(docs
      COMMAND Doxygen::doxygen "${SB_DOXYFILE}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMENT "Building the API reference in ${CMAKE_CURRENT_BINARY_DIR}/docs/html"
      VERBATIM)
    if(SB_BUILD_TESTS)
      add_test(NAME docs.api COMMAND Doxygen::doxygen "${SB_DOXYFILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
  else()
    message(STATUS "Doxygen not found: the 'docs' target and docs.api test are off")
  endif()
endif()
