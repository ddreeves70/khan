add_executable(kh_tests
  core/test_id.cpp
  model/test_document.cpp
  model/test_commands.cpp
  model/test_rebuild.cpp
  model/test_registry.cpp
  io/test_khan_file.cpp
  plugin/test_event_bus.cpp
  plugin/test_manifest.cpp
  plugin/test_document_list.cpp
  plugin/test_plugin_host.cpp
)
target_link_libraries(kh_tests PRIVATE Catch2::Catch2WithMain kh::core kh::model kh::io libzip::zip
  kh::plugin kh::plugin_modeling)

# Plugins built as shared libraries and loaded by the tests: the plugin guide's
# Hello plugin, and broken ones the host must refuse.
add_subdirectory(plugin/fixtures)
add_dependencies(kh_tests kh_test_plugin_noentry kh_test_plugin_oldapi kh_test_plugin_badname)
target_compile_definitions(kh_tests PRIVATE KH_TEST_PLUGIN_FOLDER="${KH_TEST_PLUGIN_FOLDER}")
# The plugin guide's code blocks must match the files the tests build.
add_test(NAME docs.plugin_guide_code
  COMMAND ${CMAKE_COMMAND} -DGUIDE=${PROJECT_SOURCE_DIR}/docs/plugin-guide/hello-plugin.md
          -P ${PROJECT_SOURCE_DIR}/cmake/CheckGuideFiles.cmake)
if(TARGET kh_hello_plugin)
  target_sources(kh_tests PRIVATE plugin/test_hello_plugin.cpp)
  add_dependencies(kh_tests kh_hello_plugin)
  target_compile_definitions(kh_tests PRIVATE KH_PLUGIN_FOLDER="${CMAKE_BINARY_DIR}/khan-plugins")
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
