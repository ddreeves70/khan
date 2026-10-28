# kh_app: the Qt desktop application. The shell (menus, panels, layouts, files)
# is a library so the app tests drive the same code; panels and menu items come
# from plugins. Panels dock through Qt ADS (cmake/KhAds.cmake).
qt_add_library(kh_app_core STATIC
  AppSettings.h
  AppUi.cpp
  AppUi.h
  Shell.cpp
  Shell.h
)
target_include_directories(kh_app_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kh_app_core PUBLIC
  Qt6::Widgets ads::qtadvanceddocking-qt6
  kh::core kh::kernel kh::model kh::io kh::plugin kh::ui)
kh_set_warnings(kh_app_core)

qt_add_executable(kh_app main.cpp)
target_link_libraries(kh_app PRIVATE
  kh_app_core kh::kernel_occt kh::view kh::sketch_planegcs
  kh::plugin_modeling kh::plugin_featuretree kh::plugin_properties kh::plugin_viewoptions kh::plugin_axes kh::plugin_grid kh::plugin_viewer)
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
  # windeployqt copies only the "windows" platform plugin, but the smoke test and
  # screenshots run with QT_QPA_PLATFORM=offscreen: copy that plugin too, so they work
  # on any computer, not just where QT_PLUGIN_PATH points into Qt.
  if(TARGET Qt6::QOffscreenIntegrationPlugin)
    add_custom_command(TARGET kh_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kh_app>/platforms"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
              "$<TARGET_FILE_DIR:kh_app>/platforms"
      VERBATIM)
  endif()
endif()

# Builds the main window without a screen and checks its panels and layouts.
if(KH_BUILD_TESTS)
  add_test(NAME app.smoke COMMAND kh_app --smoke-test --settings ${CMAKE_CURRENT_BINARY_DIR}/smoke-settings.ini)
  set_tests_properties(app.smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
