# sb_view: the 3D view. OCCT's viewer (AIS and V3d) drawn inside Qt widgets, plus
# the flat view used when a computer can't start 3D graphics. With sb_kernel_occt,
# the only library allowed to include OCCT headers; none appear in its public headers.
sb_add_library(sb_view
  SOURCES
    FlatView.cpp
    Graphics.cpp
    Handles.cpp
    Layout.cpp
    OcctGl.cpp
    OcctGl.h
    ViewWidget.cpp
    include/sb/view/FlatView.h
    include/sb/view/ViewWidget.h
  DEPS
    sb::core
    sb::kernel
    Qt6::Widgets
    Qt6::OpenGLWidgets
  PRIVATE_DEPS
    sb::kernel_occt
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_Visualization_LIBRARIES}
)
target_include_directories(sb_view SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
