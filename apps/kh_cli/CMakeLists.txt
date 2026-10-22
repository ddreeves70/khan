add_executable(kh_cli
  main.cpp
  Regression.cpp
  Regression.h
)
target_link_libraries(kh_cli PRIVATE
  kh::core kh::kernel kh::kernel_occt kh::model
  nlohmann_json::nlohmann_json)
kh_set_warnings(kh_cli)
