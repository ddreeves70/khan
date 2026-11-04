# sb_io: the .sb file format (zip container + JSON). Import and export of other
# formats (STEP, STL, 3MF) moves here as file-format plugins in the plugin shell.
sb_add_library(sb_io
  SOURCES
    SketchBilletFile.cpp
  DEPS
    sb::core
    sb::model
  PRIVATE_DEPS
    nlohmann_json::nlohmann_json
    libzip::zip
)
