# Installing SketchBillet, and making its installers (docs/user-guide/install.md
# says how people use them; packaging/README.md how they're made):
#
#   Windows  an .msi, made by CPack with WiX:        cpack --config build/ci/CPackConfig.cmake
#   macOS    a .dmg with SketchBillet.app, by CPack:  cpack --config build/ci/CPackConfig.cmake
#   Linux    an AppImage, made from `cmake --install` by packaging/linux/make-appimage.sh
#
# Everything a user gets is in the "app" component; the libraries SketchBillet builds
# from source (Qt ADS) bring install rules of their own for developers, which the
# installers leave out.
if(NOT (TARGET sketchbillet AND TARGET sketchbillet_cli))
  return()
endif()

include(GNUInstallDirs)
set(sb_pkg "${PROJECT_SOURCE_DIR}/packaging")
set(sb_ads_target qtadvanceddocking-qt6)  # Qt ADS, when built from source (cmake/SbAds.cmake)

# LICENSE without an extension, for Windows and the WiX license page.
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" COPYONLY)

if(APPLE)
  set(sb_docdir "SketchBillet.app/Contents/Resources")
elseif(WIN32)
  set(sb_docdir ".")
else()
  set(sb_docdir "${CMAKE_INSTALL_DOCDIR}")
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" "${sb_pkg}/THIRD-PARTY-NOTICES.txt"
  DESTINATION "${sb_docdir}" COMPONENT app)
install(DIRECTORY "${sb_pkg}/licenses" DESTINATION "${sb_docdir}" COMPONENT app)

if(APPLE)
  # ---------------------------------------------------------------- macOS
  # The build makes sketchbillet.app; people get SketchBillet.app, with the command-line
  # tool inside it, and Qt and the other libraries copied in by macdeployqt.
  install(DIRECTORY "$<TARGET_BUNDLE_DIR:sketchbillet>/" DESTINATION "SketchBillet.app"
    USE_SOURCE_PERMISSIONS COMPONENT app)
  install(PROGRAMS "$<TARGET_FILE:sketchbillet_cli>" DESTINATION "SketchBillet.app/Contents/MacOS"
    COMPONENT app)
  set(sb_libdirs "-libpath=$<TARGET_FILE_DIR:sb_planegcs>")
  if(TARGET ${sb_ads_target})
    list(APPEND sb_libdirs "-libpath=$<TARGET_FILE_DIR:${sb_ads_target}>")
  endif()
  string(JOIN " " sb_libdirs ${sb_libdirs})
  install(CODE "
    set(app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/SketchBillet.app\")
    message(STATUS \"Copying Qt and other libraries into \${app}\")
    execute_process(
      COMMAND \"$<TARGET_FILE:Qt6::macdeployqt>\" \"\${app}\"
              \"-executable=\${app}/Contents/MacOS/sketchbillet_cli\" ${sb_libdirs} -verbose=1
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR \"macdeployqt failed (\${result})\")
    endif()
    # Apple computers only run apps that are signed. Until SketchBillet has an Apple
    # Developer ID, it's signed by itself (ad hoc), so people allow it once in
    # System Settings (docs/user-guide/install.md).
    execute_process(COMMAND codesign --force --deep --sign - \"\${app}\" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR \"codesign failed (\${result})\")
    endif()
  " COMPONENT app)

elseif(WIN32)
  # ---------------------------------------------------------------- Windows
  install(TARGETS sketchbillet sketchbillet_cli sb_planegcs RUNTIME DESTINATION bin COMPONENT app)
  if(TARGET ${sb_ads_target})
    install(TARGETS ${sb_ads_target} RUNTIME DESTINATION bin COMPONENT app)
  endif()

  # The Visual C++ runtime, next to the programs, so nothing else needs installing.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT app)
  include(InstallRequiredSystemLibraries)

  # The other DLLs the programs use (Open CASCADE, libzip, ...), found where vcpkg and
  # Qt keep them, then Qt's plugins (windeployqt).
  set(sb_dll_dirs "$<TARGET_FILE_DIR:sb_planegcs>" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND sb_dll_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(TARGET ${sb_ads_target})
    list(APPEND sb_dll_dirs "$<TARGET_FILE_DIR:${sb_ads_target}>")
  endif()
  install(CODE "
    set(bin \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES \"\${bin}/sketchbillet.exe\" \"\${bin}/sketchbillet_cli.exe\"
      RESOLVED_DEPENDENCIES_VAR found
      UNRESOLVED_DEPENDENCIES_VAR missing
      DIRECTORIES ${sb_dll_dirs}
      PRE_EXCLUDE_REGEXES \"^api-ms-\" \"^ext-ms-\"
      POST_EXCLUDE_REGEXES \".*[Ss][Yy][Ss][Tt][Ee][Mm]32.*\")
    foreach(dll IN LISTS found)
      file(INSTALL \"\${dll}\" DESTINATION \"\${bin}\")
    endforeach()
    foreach(dll IN LISTS missing)
      message(WARNING \"Couldn't find \${dll}, which SketchBillet uses: the installer won't have it\")
    endforeach()
    execute_process(
      COMMAND \"$<TARGET_FILE:Qt6::windeployqt>\" --release --no-translations --no-compiler-runtime
              --no-system-d3d-compiler --no-opengl-sw \"\${bin}/sketchbillet.exe\"
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR \"windeployqt failed (\${result})\")
    endif()
  " COMPONENT app)

else()
  # ---------------------------------------------------------------- Linux
  # Installed under usr/ in an AppDir; linuxdeploy then copies in Qt and the rest.
  set(sb_rpath "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  set_target_properties(sketchbillet sketchbillet_cli PROPERTIES INSTALL_RPATH "${sb_rpath}")
  install(TARGETS sketchbillet sketchbillet_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT app)
  install(TARGETS sb_planegcs LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT app NAMELINK_SKIP)
  if(TARGET ${sb_ads_target})
    install(TARGETS ${sb_ads_target} LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT app NAMELINK_SKIP)
  endif()
  install(FILES "${sb_pkg}/linux/org.sketchbillet.SketchBillet.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" COMPONENT app)
  install(FILES "${sb_pkg}/icons/sketchbillet.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps" COMPONENT app)
  install(FILES "${sb_pkg}/icons/sketchbillet.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps" COMPONENT app)
endif()

# -------------------------------------------------------------------- CPack
set(CPACK_PACKAGE_NAME "SketchBillet")
set(CPACK_PACKAGE_VENDOR "SketchBillet")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Free CAD for parts you can make")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://sketchbillet.org")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SketchBillet")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
  set(sb_arch arm64)
else()
  set(sb_arch x64)
endif()
if(APPLE)
  set(sb_os macos)
elseif(WIN32)
  set(sb_os windows)
else()
  set(sb_os linux)
endif()
# For example SketchBillet-0.1.0-windows-x64.msi
set(CPACK_PACKAGE_FILE_NAME "SketchBillet-${PROJECT_VERSION}-${sb_os}-${sb_arch}")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}/packages")
# Only what users get: <build folder>, project, component, folder inside the package.
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};SketchBillet;app;/")

if(WIN32)
  set(CPACK_GENERATOR WIX)
  # WiX 4 or newer, installed with `dotnet tool install --global wix` (see packaging/README.md).
  set(CPACK_WIX_VERSION 4)
  # Never change this: it's how Windows knows a new SketchBillet replaces the old one.
  set(CPACK_WIX_UPGRADE_GUID "5B0C7B7E-3C4D-4E8A-9B2F-6A1D0E5C8F31")
  set(CPACK_WIX_INSTALL_SCOPE perMachine)  # for every user of the computer, as schools need
  set(CPACK_WIX_PRODUCT_ICON "${sb_pkg}/icons/sketchbillet.ico")
  set(CPACK_WIX_PROGRAM_MENU_FOLDER ".")    # Start menu > SketchBillet, not in a folder
  set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://sketchbillet.org")
  set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://sketchbillet.org/guide/")
  set(CPACK_PACKAGE_EXECUTABLES "sketchbillet;SketchBillet")  # the Start menu entry
  set(CPACK_CREATE_DESKTOP_LINKS "sketchbillet")
elseif(APPLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME "SketchBillet")
  set(CPACK_DMG_FORMAT UDZO)
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)  # no license to click through
else()
  set(CPACK_GENERATOR TGZ)  # unused by the AppImage, but `cpack` works
endif()
include(CPack)
