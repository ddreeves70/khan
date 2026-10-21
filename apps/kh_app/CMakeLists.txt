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

# On Windows, copy Qt's DLLs and plugins next to kh_app.exe after each build,
# so the app runs without adding Qt to PATH.
if(WIN32)
  find_program(KH_WINDEPLOYQT windeployqt
    HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/bin")
  if(KH_WINDEPLOYQT)
    add_custom_command(TARGET kh_app POST_BUILD
      COMMAND "${KH_WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:kh_app>"
      COMMENT "Copying Qt runtime files next to kh_app.exe"
      VERBATIM)
  else()
    message(WARNING "windeployqt not found; run it on kh_app.exe by hand to launch the app")
  endif()
endif()
