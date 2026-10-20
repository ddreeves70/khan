kh_add_library(kh_core
  SOURCES
    Id.cpp
    Version.cpp
)
target_compile_definitions(kh_core PRIVATE
  KH_VERSION_STRING="${PROJECT_VERSION}")
