# kh_kernel: the Kernel Abstraction Layer (KAL).
# Pure interface: it must never include or link OCCT.
kh_add_library(kh_kernel
  SOURCES
    ShapeHandle.cpp
  DEPS
    kh::core
)
