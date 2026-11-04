# sb_sketch: 2D sketch geometry, constraints and the ISketchSolver interface.
# Standard library and sb_core only: no solver (PlaneGCS) or kernel types.
sb_add_library(sb_sketch
  SOURCES
    Sketch.cpp
    Profiles.cpp
  DEPS
    sb::core
)
