add_executable(kh_tests
  core/test_id.cpp
  model/test_document.cpp
  model/test_commands.cpp
  model/test_rebuild.cpp
  model/test_registry.cpp
  model/test_materials.cpp
  model/test_copy.cpp
  model/test_suppress.cpp
  io/test_khan_file.cpp
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
target_link_libraries(kh_tests PRIVATE Catch2::Catch2WithMain kh::core kh::model kh::io libzip::zip
  kh::plugin kh::plugin_modeling)

# Plugins built as shared libraries and loaded by the tests: the plugin guide's
# Hello plugin, and broken ones the host must refuse.
add_subdirectory(plugin/fixtures)
add_dependencies(kh_tests kh_test_plugin_noentry kh_test_plugin_oldapi kh_test_plugin_badname
  kh_test_plugin_warns)
target_compile_definitions(kh_tests PRIVATE KH_TEST_PLUGIN_FOLDER="${KH_TEST_PLUGIN_FOLDER}")
# The plugin guide's code blocks must match the files the tests build.
add_test(NAME docs.plugin_guide_code
  COMMAND ${CMAKE_COMMAND} -DGUIDE=${PROJECT_SOURCE_DIR}/docs/plugin-guide/hello-plugin.md
          -P ${PROJECT_SOURCE_DIR}/cmake/CheckGuideFiles.cmake)
add_test(NAME docs.plugin_guide_sharing_code
  COMMAND ${CMAKE_COMMAND} -DGUIDE=${PROJECT_SOURCE_DIR}/docs/plugin-guide/sharing.md
          -P ${PROJECT_SOURCE_DIR}/cmake/CheckGuideFiles.cmake)
if(TARGET kh_hello_plugin)
  target_sources(kh_tests PRIVATE plugin/test_hello_plugin.cpp plugin/test_sharing_example.cpp)
  add_dependencies(kh_tests kh_hello_plugin kh_example_ruler_plugin kh_example_tape_plugin)
  target_compile_definitions(kh_tests PRIVATE KH_PLUGIN_FOLDER="${CMAKE_BINARY_DIR}/khan-plugins")
endif()
# Khan holds its own plugins to the rules: kh.modeling and the guide's example
# plugins load with no warnings at all, even with --strict. (The desktop app's
# built-in plugins are checked the same way in the app tests.)
if(TARGET kh_hello_plugin AND TARGET kh_cli)
  add_test(NAME cli.plugins_strict
    COMMAND kh_cli plugins --strict --plugins "${CMAKE_BINARY_DIR}/khan-plugins")
endif()
# Checked-in .khan files that every future version must still open.
target_compile_definitions(kh_tests PRIVATE KH_TEST_FILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/files")

if(KH_WITH_OCCT)
  target_sources(kh_tests PRIVATE kernel/test_occt_kernel.cpp model/test_rebuild_occt.cpp)
  target_link_libraries(kh_tests PRIVATE kh::kernel_occt)
endif()

kh_set_warnings(kh_tests)
# Geometry regression corpus: every tests/corpus/*.json file runs as its own test
# through kh_cli. Re-run CMake configure after adding a case.
if(TARGET kh_cli)
  file(GLOB KH_CORPUS_CASES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/corpus/*.json")
  foreach(case_file IN LISTS KH_CORPUS_CASES)
    get_filename_component(case_name "${case_file}" NAME_WE)
    add_test(NAME "corpus.${case_name}" COMMAND kh_cli regress "${case_file}")
  endforeach()
  # Open the checked-in .khan file and rebuild it with the real kernel.
  # The user's guide, CLI reference and tutorials: every marked example must print
  # exactly what the page shows, and every command must be documented.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_test(NAME docs.guides
      COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/docs/check_docs.py
              --kh-cli $<TARGET_FILE:kh_cli> --build-dir ${CMAKE_BINARY_DIR} --repo ${PROJECT_SOURCE_DIR})
    add_test(NAME docs.checker
      COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/docs/test_check_docs.py)
  else()
    message(STATUS "Python 3 not found: the docs.guides test is off")
  endif()
  add_test(NAME cli.open_schema1 COMMAND kh_cli open "${CMAKE_CURRENT_SOURCE_DIR}/files/schema1-keychain-tag.khan")
  set_tests_properties(cli.open_schema1 PROPERTIES PASS_REGULAR_EXPRESSION "Tag +ok +volume 7121.46 mm\\^3")
endif()

# PRE_TEST discovery waits until ctest runs, after vcpkg has copied DLLs next to
# the test executable (needed on Windows).
catch_discover_tests(kh_tests DISCOVERY_MODE PRE_TEST)

# The 3D view: its plain logic everywhere, and its drawing where there is
# OpenGL. CI's Linux machine draws on a virtual screen (xvfb-run) with Mesa.
if(TARGET kh_view)
  add_executable(kh_view_tests view/test_layout.cpp)
  target_link_libraries(kh_view_tests PRIVATE Catch2::Catch2WithMain kh::view)
  kh_set_warnings(kh_view_tests)
  if(WIN32)
    # Qt's and OCCT's DLLs next to the .exe, so it starts.
    add_custom_command(TARGET kh_view_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:kh_view_tests> $<TARGET_FILE_DIR:kh_view_tests>
      COMMAND_EXPAND_LISTS)
  endif()
  catch_discover_tests(kh_view_tests DISCOVERY_MODE PRE_TEST)

  find_program(KH_XVFB_RUN xvfb-run)
  if(KH_XVFB_RUN)
    add_executable(kh_view_gl_tests view/main_gl.cpp view/test_view_gl.cpp)
    target_link_libraries(kh_view_gl_tests PRIVATE Catch2::Catch2 kh::view kh::kernel_occt)
    kh_set_warnings(kh_view_gl_tests)
    add_test(NAME view.gl COMMAND ${KH_XVFB_RUN} -a -s "-screen 0 1280x1024x24" $<TARGET_FILE:kh_view_gl_tests>)
    set_tests_properties(view.gl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb;LIBGL_ALWAYS_SOFTWARE=1")
  else()
    message(STATUS "xvfb-run not found: the view.gl test (3D drawing) is off")
  endif()
endif()

# The desktop shell and its panels, driven without a screen.
if(TARGET kh_app_core)
  add_executable(kh_app_tests app/main.cpp app/test_shell.cpp app/test_grid_plugin.cpp app/test_axes.cpp
  app/test_view_options.cpp)
  target_link_libraries(kh_app_tests PRIVATE Catch2::Catch2 kh_app_core kh::kernel_occt kh::view
    kh::plugin_modeling kh::plugin_featuretree kh::plugin_properties kh::plugin_viewoptions kh::plugin_axes kh::plugin_grid kh::plugin_viewer)
  kh_set_warnings(kh_app_tests)
  if(WIN32)
    # Qt's DLLs and plugins (including the offscreen platform) and ADS, next to the .exe.
    add_custom_command(TARGET kh_app_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:kh_app_tests> $<TARGET_FILE_DIR:kh_app_tests>
      COMMAND_EXPAND_LISTS)
    if(KH_WINDEPLOYQT)
      add_custom_command(TARGET kh_app_tests POST_BUILD
        COMMAND "${KH_WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:kh_app_tests>"
        VERBATIM)
    endif()
    # windeployqt copies only the "windows" platform plugin, but the tests run
    # with QT_QPA_PLATFORM=offscreen: copy that plugin too, so the tests work
    # on any computer, not just where QT_PLUGIN_PATH points into Qt.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
      add_custom_command(TARGET kh_app_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:kh_app_tests>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:kh_app_tests>/platforms"
        VERBATIM)
    endif()
  endif()
  catch_discover_tests(kh_app_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
