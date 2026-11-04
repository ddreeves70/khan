# sketchbillet: the Qt desktop application. The shell (menus, panels, layouts, files)
# is a library so the app tests drive the same code; panels and menu items come
# from plugins. Panels dock through Qt ADS (cmake/SbAds.cmake).
qt_add_library(sb_app_core STATIC
  AppSettings.h
  AppUi.cpp
  AppUi.h
  Icons.cpp
  Icons.h
  Shell.cpp
  Shell.h
  ViewOverlay.cpp
  ViewOverlay.h
)
target_include_directories(sb_app_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sb_app_core PUBLIC
  Qt6::Widgets Qt6::Svg ads::qtadvanceddocking-qt6
  sb::core sb::kernel sb::model sb::io sb::plugin sb::ui)
sb_set_warnings(sb_app_core)
sb_embed_files(sb_app_core HEADER AppIcons.h NAMESPACE sb::icons::app
  FILES icons/new.svg icons/open.svg icons/save.svg icons/undo.svg icons/redo.svg
        icons/dock.svg)

# The app's icon: on the .exe on Windows (sketchbillet.rc), on the app in Finder on
# macOS. packaging/icons/ has how to remake them.
if(WIN32)
  set(sb_app_icon "${CMAKE_CURRENT_BINARY_DIR}/sketchbillet.rc")
  file(CONFIGURE OUTPUT "${sb_app_icon}"
    CONTENT "IDI_ICON1 ICON \"${PROJECT_SOURCE_DIR}/packaging/icons/sketchbillet.ico\"\n")
elseif(APPLE)
  set(sb_app_icon "${PROJECT_SOURCE_DIR}/packaging/icons/sketchbillet.icns")
  set_source_files_properties("${sb_app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
qt_add_executable(sketchbillet main.cpp ${sb_app_icon})
target_link_libraries(sketchbillet PRIVATE
  sb_app_core sb::kernel_occt sb::view sb::sketch_planegcs
  sb::plugin_modeling sb::plugin_featuretree sb::plugin_properties sb::plugin_viewoptions sb::plugin_axes sb::plugin_grid sb::plugin_viewer sb::plugin_sketcher sb::plugin_movehandles sb::plugin_parameters)
sb_set_warnings(sketchbillet)
sb_embed_files(sketchbillet HEADER AppLogo.h NAMESPACE sb::icons::app
  FILES ../../packaging/icons/sketchbillet.svg)

set_target_properties(sketchbillet PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME "SketchBillet"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.sketchbillet.SketchBillet"
  MACOSX_BUNDLE_ICON_FILE "sketchbillet.icns"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "SketchBillet contributors, Apache License 2.0"
)

# On Windows, copy Qt's DLLs and plugins next to sketchbillet.exe after each build,
# so the app runs without adding Qt to PATH.
if(WIN32)
  find_program(SB_WINDEPLOYQT windeployqt
    HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/bin")
  if(SB_WINDEPLOYQT)
    add_custom_command(TARGET sketchbillet POST_BUILD
      COMMAND "${SB_WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:sketchbillet>"
      COMMENT "Copying Qt runtime files next to sketchbillet.exe"
      VERBATIM)
  else()
    message(WARNING "windeployqt not found; run it on sketchbillet.exe by hand to launch the app")
  endif()
endif()

# Windows finds DLLs next to the .exe: copy the ones sketchbillet links (ADS, and Qt
# when it comes from a CMake package) there after each build.
if(WIN32)
  add_custom_command(TARGET sketchbillet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sketchbillet> $<TARGET_FILE_DIR:sketchbillet>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying DLLs next to sketchbillet.exe")
  # windeployqt copies only the "windows" platform plugin, but the smoke test and
  # screenshots run with QT_QPA_PLATFORM=offscreen: copy that plugin too, so they work
  # on any computer, not just where QT_PLUGIN_PATH points into Qt.
  if(TARGET Qt6::QOffscreenIntegrationPlugin)
    add_custom_command(TARGET sketchbillet POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sketchbillet>/platforms"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
              "$<TARGET_FILE_DIR:sketchbillet>/platforms"
      VERBATIM)
  endif()
endif()

# Builds the main window without a screen and checks its panels and layouts.
if(SB_BUILD_TESTS)
  add_test(NAME app.smoke COMMAND sketchbillet --smoke-test --settings ${CMAKE_CURRENT_BINARY_DIR}/smoke-settings.ini)
  set_tests_properties(app.smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  # --trigger waits for menu items to catch up: Box picks the new box, so
  # Duplicate step (which needs a step picked) can be clicked straight after.
  add_test(NAME app.trigger_after_change
           COMMAND sketchbillet --trigger sb.modeling.box,sb.modeling.duplicate --smoke-test
                   --settings ${CMAKE_CURRENT_BINARY_DIR}/trigger-settings.ini)
  # ... and refuses a greyed-out item instead of quietly doing nothing.
  add_test(NAME app.trigger_greyed_out
           COMMAND sketchbillet --trigger sb.modeling.cut --smoke-test
                   --settings ${CMAKE_CURRENT_BINARY_DIR}/trigger-settings.ini)
  set_tests_properties(app.trigger_after_change app.trigger_greyed_out PROPERTIES ENVIRONMENT
                       "QT_QPA_PLATFORM=offscreen")
  set_tests_properties(app.trigger_greyed_out PROPERTIES PASS_REGULAR_EXPRESSION
                       "sb.modeling.cut is greyed out")
endif()
