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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KhTarget)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
if(KH_WITH_OCCT)
  find_package(OpenCASCADE CONFIG REQUIRED)
  message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} found")
endif()

if(KH_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
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
