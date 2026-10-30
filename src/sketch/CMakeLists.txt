# kh_sketch: 2D sketch geometry, constraints and the ISketchSolver interface.
# Standard library and kh_core only: no solver (PlaneGCS) or kernel types.
kh_add_library(kh_sketch
  SOURCES
    Sketch.cpp
    Profiles.cpp
  DEPS
    kh::core
)
