sb_add_library(sb_core
  SOURCES
    Id.cpp
    Units.cpp
    Version.cpp
)
target_compile_definitions(sb_core PRIVATE
  SB_VERSION_STRING="${PROJECT_VERSION}")
