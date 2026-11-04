# PlaneGCS, FreeCAD's sketch constraint solver (LGPL-2.1-or-later).
#
# Built as its own shared library, so the user could swap it for their own
# build, as the LGPL asks (see docs/dependencies.md). Only sb_sketch_planegcs
# uses it. See README.md for where it came from and what SketchBillet changed.

add_library(sb_planegcs SHARED
  src/planegcs/Constraints.cpp
  src/planegcs/GCS.cpp
  src/planegcs/Geo.cpp
  src/planegcs/SubSystem.cpp
  src/planegcs/qp_eq.cpp)
add_library(sb::planegcs ALIAS sb_planegcs)

target_compile_definitions(sb_planegcs PRIVATE SB_PLANEGCS_BUILD
  # Eigen's MPL-2.0 files only: no LGPL parts of Eigen are used.
  PUBLIC EIGEN_MPL2_ONLY)
# SketchBillet's code includes these as <planegcs/GCS.h>; SYSTEM keeps PlaneGCS's own
# warnings out of SketchBillet's warnings-as-errors builds.
target_include_directories(sb_planegcs SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(sb_planegcs PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/planegcs"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat")
target_link_libraries(sb_planegcs PUBLIC Eigen3::Eigen)
set_target_properties(sb_planegcs PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON)
# Upstream code: build it as it is, without SketchBillet's warning rules.
if(MSVC)
  target_compile_options(sb_planegcs PRIVATE /W0 /bigobj /utf-8)
else()
  target_compile_options(sb_planegcs PRIVATE -w)
endif()
