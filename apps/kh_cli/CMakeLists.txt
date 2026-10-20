add_executable(kh_cli main.cpp)
target_link_libraries(kh_cli PRIVATE kh::core kh::kernel kh::kernel_occt kh::model)
kh_set_warnings(kh_cli)
