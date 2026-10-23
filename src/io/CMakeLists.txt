# kh_io: the .khan file format (zip container + JSON). Import and export of other
# formats (STEP, STL, 3MF) moves here as file-format plugins in the plugin shell.
kh_add_library(kh_io
  SOURCES
    KhanFile.cpp
  DEPS
    kh::core
    kh::model
  PRIVATE_DEPS
    nlohmann_json::nlohmann_json
    libzip::zip
)
