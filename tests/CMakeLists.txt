add_executable(kh_tests
  core/test_id.cpp
  model/test_document.cpp
)
target_link_libraries(kh_tests PRIVATE Catch2::Catch2WithMain kh::core kh::model)

if(KH_WITH_OCCT)
  target_sources(kh_tests PRIVATE kernel/test_occt_kernel.cpp)
  target_link_libraries(kh_tests PRIVATE kh::kernel_occt)
endif()

kh_set_warnings(kh_tests)
# PRE_TEST discovery waits until ctest runs, after vcpkg has copied DLLs next to
# the test executable (needed on Windows).
catch_discover_tests(kh_tests DISCOVERY_MODE PRE_TEST)
