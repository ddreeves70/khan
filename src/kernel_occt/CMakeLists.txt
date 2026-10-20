# kh_kernel_occt: IKernel implemented on Open CASCADE Technology.
# The ONLY library allowed to include OCCT headers.
kh_add_library(kh_kernel_occt
  SOURCES
    OcctKernel.cpp
  DEPS
    kh::kernel
  PRIVATE_DEPS
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_DataExchange_LIBRARIES}
)
target_include_directories(kh_kernel_occt SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
