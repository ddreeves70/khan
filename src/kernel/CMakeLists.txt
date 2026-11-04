# sb_kernel: the Kernel Abstraction Layer (KAL).
# Pure interface: it must never include or link OCCT.
sb_add_library(sb_kernel
  SOURCES
    ShapeHandle.cpp
  DEPS
    sb::core
)
