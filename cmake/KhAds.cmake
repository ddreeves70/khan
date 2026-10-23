# Qt Advanced Docking System (ADS): docking, tabbing, floating panels and saved
# layouts for kh_app. LGPL-2.1, so it is always built as a shared library.
#
# Uses an installed ADS if CMake finds one; otherwise downloads the pinned
# release below at configure time. To build offline, point CMake at a copy:
#   -DFETCHCONTENT_SOURCE_DIR_QTADS=<path to Qt-Advanced-Docking-System>
include(FetchContent)

find_package(qtadvanceddocking-qt6 CONFIG QUIET)
if(TARGET ads::qtadvanceddocking-qt6)
  message(STATUS "Qt Advanced Docking System: installed copy")
else()
  set(QT_VERSION_MAJOR 6)
  set(BUILD_STATIC OFF)    # LGPL: keep it a separate, replaceable library
  set(BUILD_EXAMPLES OFF)
  FetchContent_Declare(qtads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG 023ce95934fecfd4cf5c672c9c404fabe0f54923  # v5.1.1 (Aug 20, 2026)
    GIT_SHALLOW FALSE
    SYSTEM)  # its headers don't trigger our warnings
  FetchContent_MakeAvailable(qtads)
  message(STATUS "Qt Advanced Docking System: v5.1.1 from source")
endif()
