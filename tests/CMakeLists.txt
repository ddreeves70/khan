add_executable(sb_tests
  core/test_id.cpp
  core/test_units.cpp
  model/test_document.cpp
  model/test_commands.cpp
  model/test_rebuild.cpp
  model/test_registry.cpp
  model/test_materials.cpp
  model/test_copy.cpp
  model/test_suppress.cpp
  model/test_folders.cpp
  model/test_move.cpp
  model/test_sketch_step.cpp
  model/test_extrude.cpp
  model/test_units_command.cpp
  model/test_parameters.cpp
  model/test_formulas.cpp
  io/test_sketchbillet_file.cpp
  plugin/test_event_bus.cpp
  plugin/test_manifest.cpp
  plugin/test_document_list.cpp
  plugin/test_plugin_host.cpp
  plugin/test_plugin_sharing.cpp
  plugin/test_selection.cpp
  plugin/test_settings.cpp
  plugin/test_overlays.cpp
  plugin/test_tiers.cpp
)
target_link_libraries(sb_tests PRIVATE Catch2::Catch2WithMain sb::core sb::model sb::io libzip::zip
  sb::plugin sb::plugin_modeling sb::sketch_planegcs)
if(WIN32)
  # sb_planegcs is a DLL: Windows finds it next to the .exe.
  add_custom_command(TARGET sb_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sb_tests> $<TARGET_FILE_DIR:sb_tests>
    COMMAND_EXPAND_LISTS)
endif()

# The sketch library and the PlaneGCS solver. PlaneGCS is a shared library
# (LGPL), so on Windows its DLL is copied next to the test program.
add_executable(sb_sketch_tests sketch/test_sketch.cpp sketch/test_planegcs.cpp sketch/test_profiles.cpp sketch/test_spline.cpp sketch/test_spline_constraints.cpp)
target_link_libraries(sb_sketch_tests PRIVATE Catch2::Catch2WithMain sb::sketch sb::sketch_planegcs)
sb_set_warnings(sb_sketch_tests)
if(WIN32)
  add_custom_command(TARGET sb_sketch_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sb_sketch_tests> $<TARGET_FILE_DIR:sb_sketch_tests>
    COMMAND_EXPAND_LISTS)
endif()
catch_discover_tests(sb_sketch_tests)

# Plugins built as shared libraries and loaded by the tests: the plugin guide's
# Hello plugin, and broken ones the host must refuse.
add_subdirectory(plugin/fixtures)
add_dependencies(sb_tests sb_test_plugin_noentry sb_test_plugin_oldapi sb_test_plugin_badname
  sb_test_plugin_warns)
target_compile_definitions(sb_tests PRIVATE SB_TEST_PLUGIN_FOLDER="${SB_TEST_PLUGIN_FOLDER}")
# The plugin guide's code blocks must match the files the tests build.
add_test(NAME docs.plugin_guide_code
  COMMAND ${CMAKE_COMMAND} -DGUIDE=${PROJECT_SOURCE_DIR}/docs/plugin-guide/hello-plugin.md
          -P ${PROJECT_SOURCE_DIR}/cmake/CheckGuideFiles.cmake)
add_test(NAME docs.plugin_guide_sharing_code
  COMMAND ${CMAKE_COMMAND} -DGUIDE=${PROJECT_SOURCE_DIR}/docs/plugin-guide/sharing.md
          -P ${PROJECT_SOURCE_DIR}/cmake/CheckGuideFiles.cmake)
if(TARGET sb_hello_plugin)
  target_sources(sb_tests PRIVATE plugin/test_hello_plugin.cpp plugin/test_sharing_example.cpp)
  add_dependencies(sb_tests sb_hello_plugin sb_example_ruler_plugin sb_example_tape_plugin)
  target_compile_definitions(sb_tests PRIVATE SB_PLUGIN_FOLDER="${CMAKE_BINARY_DIR}/sketchbillet-plugins")
endif()
# SketchBillet holds its own plugins to the rules: sb.modeling and the guide's example
# plugins load with no warnings at all, even with --strict. (The desktop app's
# built-in plugins are checked the same way in the app tests.)
if(TARGET sb_hello_plugin AND TARGET sketchbillet_cli)
  add_test(NAME cli.plugins_strict
    COMMAND sketchbillet_cli plugins --strict --plugins "${CMAKE_BINARY_DIR}/sketchbillet-plugins")
endif()
# Checked-in .sb files that every future version must still open.
target_compile_definitions(sb_tests PRIVATE SB_TEST_FILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/files")

if(SB_WITH_OCCT)
  target_sources(sb_tests PRIVATE kernel/test_occt_kernel.cpp model/test_rebuild_occt.cpp model/test_slide.cpp)
  target_link_libraries(sb_tests PRIVATE sb::kernel_occt)
endif()

sb_set_warnings(sb_tests)
# Geometry regression corpus: every tests/corpus/*.json file runs as its own test
# through sketchbillet_cli. Re-run CMake configure after adding a case.
if(TARGET sketchbillet_cli)
  file(GLOB SB_CORPUS_CASES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/corpus/*.json")
  foreach(case_file IN LISTS SB_CORPUS_CASES)
    get_filename_component(case_name "${case_file}" NAME_WE)
    add_test(NAME "corpus.${case_name}" COMMAND sketchbillet_cli regress "${case_file}")
  endforeach()
  # Open the checked-in .sb file and rebuild it with the real kernel.
  # The user's guide, CLI reference and tutorials: every marked example must print
  # exactly what the page shows, and every command must be documented.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_test(NAME docs.guides
      COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/docs/check_docs.py
              --sketchbillet-cli $<TARGET_FILE:sketchbillet_cli> --build-dir ${CMAKE_BINARY_DIR} --repo ${PROJECT_SOURCE_DIR})
    add_test(NAME docs.checker
      COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/docs/test_check_docs.py)
  else()
    message(STATUS "Python 3 not found: the docs.guides test is off")
  endif()
  add_test(NAME cli.open_schema1 COMMAND sketchbillet_cli open "${CMAKE_CURRENT_SOURCE_DIR}/files/schema1-keychain-tag.khan")
  set_tests_properties(cli.open_schema1 PROPERTIES PASS_REGULAR_EXPRESSION "Tag +ok +volume 7121.46 mm\\^3")
endif()

# PRE_TEST discovery waits until ctest runs, after vcpkg has copied DLLs next to
# the test executable (needed on Windows).
catch_discover_tests(sb_tests DISCOVERY_MODE PRE_TEST)

# The 3D view: its plain logic everywhere, and its drawing where there is
# OpenGL. CI's Linux machine draws on a virtual screen (xvfb-run) with Mesa.
if(TARGET sb_view)
  add_executable(sb_view_tests view/test_layout.cpp view/test_handles.cpp)
  target_link_libraries(sb_view_tests PRIVATE Catch2::Catch2WithMain sb::view)
  sb_set_warnings(sb_view_tests)
  if(WIN32)
    # Qt's and OCCT's DLLs next to the .exe, so it starts.
    add_custom_command(TARGET sb_view_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sb_view_tests> $<TARGET_FILE_DIR:sb_view_tests>
      COMMAND_EXPAND_LISTS)
  endif()
  catch_discover_tests(sb_view_tests DISCOVERY_MODE PRE_TEST)

  find_program(SB_XVFB_RUN xvfb-run)
  if(SB_XVFB_RUN)
    add_executable(sb_view_gl_tests view/main_gl.cpp view/test_view_gl.cpp)
    target_link_libraries(sb_view_gl_tests PRIVATE Catch2::Catch2 sb::view sb::kernel_occt)
    sb_set_warnings(sb_view_gl_tests)
    add_test(NAME view.gl COMMAND ${SB_XVFB_RUN} -a -s "-screen 0 1280x1024x24" $<TARGET_FILE:sb_view_gl_tests>)
    set_tests_properties(view.gl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb;LIBGL_ALWAYS_SOFTWARE=1")
  else()
    message(STATUS "xvfb-run not found: the view.gl test (3D drawing) is off")
  endif()
endif()

# The desktop shell and its panels, driven without a screen.
if(TARGET sb_app_core)
  add_executable(sb_app_tests app/main.cpp app/test_shell.cpp app/test_grid_plugin.cpp app/test_axes.cpp
  app/test_view_options.cpp app/test_constrain.cpp app/test_movehandles.cpp app/test_parameters_panel.cpp)
  # The sketcher's constraint rules (Constrain.h) are tested on their own too.
  target_include_directories(sb_app_tests PRIVATE "${PROJECT_SOURCE_DIR}/plugins/sketcher")
  target_link_libraries(sb_app_tests PRIVATE Catch2::Catch2 sb_app_core sb::kernel_occt sb::view
    sb::plugin_modeling sb::plugin_featuretree sb::plugin_properties sb::plugin_viewoptions sb::plugin_axes sb::plugin_grid sb::plugin_viewer
    sb::plugin_sketcher sb::plugin_movehandles sb::plugin_parameters sb::sketch_planegcs)
  sb_set_warnings(sb_app_tests)
  if(WIN32)
    # Qt's DLLs and plugins (including the offscreen platform) and ADS, next to the .exe.
    add_custom_command(TARGET sb_app_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sb_app_tests> $<TARGET_FILE_DIR:sb_app_tests>
      COMMAND_EXPAND_LISTS)
    if(SB_WINDEPLOYQT)
      add_custom_command(TARGET sb_app_tests POST_BUILD
        COMMAND "${SB_WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:sb_app_tests>"
        VERBATIM)
    endif()
    # windeployqt copies only the "windows" platform plugin, but the tests run
    # with QT_QPA_PLATFORM=offscreen: copy that plugin too, so the tests work
    # on any computer, not just where QT_PLUGIN_PATH points into Qt.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
      add_custom_command(TARGET sb_app_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sb_app_tests>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:sb_app_tests>/platforms"
        VERBATIM)
    endif()
  endif()
  catch_discover_tests(sb_app_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
