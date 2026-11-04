# sb_sketch_planegcs: ISketchSolver on PlaneGCS.
# The ONLY library allowed to include PlaneGCS headers (like sb_kernel_occt for OCCT).
sb_add_library(sb_sketch_planegcs
  SOURCES
    PlaneGcsSolver.cpp
  DEPS
    sb::sketch
  PRIVATE_DEPS
    sb::planegcs
)
