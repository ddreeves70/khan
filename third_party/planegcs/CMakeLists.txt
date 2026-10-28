# PlaneGCS, FreeCAD's sketch constraint solver (LGPL-2.1-or-later).
#
# Built as its own shared library, so the user could swap it for their own
# build, as the LGPL asks (see docs/dependencies.md). Only kh_sketch_planegcs
# uses it. See README.md for where it came from and what Khan changed.

add_library(kh_planegcs SHARED
  src/planegcs/Constraints.cpp
  src/planegcs/GCS.cpp
  src/planegcs/Geo.cpp
  src/planegcs/SubSystem.cpp
  src/planegcs/qp_eq.cpp)
add_library(kh::planegcs ALIAS kh_planegcs)

target_compile_definitions(kh_planegcs PRIVATE KH_PLANEGCS_BUILD
  # Eigen's MPL-2.0 files only: no LGPL parts of Eigen are used.
  PUBLIC EIGEN_MPL2_ONLY)
# Khan's code includes these as <planegcs/GCS.h>; SYSTEM keeps PlaneGCS's own
# warnings out of Khan's warnings-as-errors builds.
target_include_directories(kh_planegcs SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(kh_planegcs PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/planegcs"
  "${CMAKE_CURRENT_SOURCE_DIR}/compat")
target_link_libraries(kh_planegcs PUBLIC Eigen3::Eigen)
set_target_properties(kh_planegcs PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON)
# Upstream code: build it as it is, without Khan's warning rules.
if(MSVC)
  target_compile_options(kh_planegcs PRIVATE /W0 /bigobj /utf-8)
else()
  target_compile_options(kh_planegcs PRIVATE -w)
endif()
