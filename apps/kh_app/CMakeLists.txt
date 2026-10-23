# kh_app: the Qt desktop application.
# Panels and viewers dock through the Qt Advanced Docking System (cmake/KhAds.cmake).
qt_add_executable(kh_app
  main.cpp
  MainWindow.cpp
  MainWindow.h
)
target_link_libraries(kh_app PRIVATE Qt6::Widgets ads::qtadvanceddocking-qt6 kh::core kh::model)
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

# Windows finds DLLs next to the .exe: copy the ones kh_app links (ADS, and Qt
# when it comes from a CMake package) there after each build.
if(WIN32)
  add_custom_command(TARGET kh_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:kh_app> $<TARGET_FILE_DIR:kh_app>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying DLLs next to kh_app.exe")
endif()

# Builds the main window without a screen and checks its panels and layouts.
if(KH_BUILD_TESTS)
  add_test(NAME app.smoke COMMAND kh_app --smoke-test)
  set_tests_properties(app.smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
