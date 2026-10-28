# kh_sketch_planegcs: ISketchSolver on PlaneGCS.
# The ONLY library allowed to include PlaneGCS headers (like kh_kernel_occt for OCCT).
kh_add_library(kh_sketch_planegcs
  SOURCES
    PlaneGcsSolver.cpp
  DEPS
    kh::sketch
  PRIVATE_DEPS
    kh::planegcs
)
