# sb_kernel_occt: IKernel implemented on Open CASCADE Technology.
# The ONLY library allowed to include OCCT headers.
sb_add_library(sb_kernel_occt
  SOURCES
    OcctKernel.cpp
  DEPS
    sb::kernel
  PRIVATE_DEPS
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_DataExchange_LIBRARIES}
)
target_include_directories(sb_kernel_occt SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
