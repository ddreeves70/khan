# kh_app: the Qt desktop application.
# Phase 0 week 4 embeds the OCCT 3D viewer (kh_view) in MainWindow.
qt_add_executable(kh_app
  main.cpp
  MainWindow.cpp
  MainWindow.h
)
target_link_libraries(kh_app PRIVATE Qt6::Widgets kh::core kh::model)
kh_set_warnings(kh_app)
set_target_properties(kh_app PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
)
