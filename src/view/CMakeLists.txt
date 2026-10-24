# kh_view: the 3D view. OCCT's viewer (AIS and V3d) drawn inside Qt widgets, plus
# the flat view used when a computer can't start 3D graphics. With kh_kernel_occt,
# the only library allowed to include OCCT headers; none appear in its public headers.
kh_add_library(kh_view
  SOURCES
    FlatView.cpp
    Graphics.cpp
    Layout.cpp
    OcctGl.cpp
    OcctGl.h
    ViewWidget.cpp
    include/kh/view/FlatView.h
    include/kh/view/ViewWidget.h
  DEPS
    kh::core
    kh::kernel
    Qt6::Widgets
    Qt6::OpenGLWidgets
  PRIVATE_DEPS
    kh::kernel_occt
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_Visualization_LIBRARIES}
)
target_include_directories(kh_view SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
