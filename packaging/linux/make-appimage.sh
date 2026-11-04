#!/usr/bin/env bash
# Makes SketchBillet's Linux AppImage from a build folder:
#
#   packaging/linux/make-appimage.sh build/ci
#
# writes build/ci/packages/SketchBillet-<version>-linux-x64.AppImage. It installs
# the app into build/ci/AppDir, then linuxdeploy copies in Qt and every other
# library the programs need, and packs it all into one file. Set QMAKE to the
# qmake of the Qt that SketchBillet was built with, if it isn't the first on PATH.
set -euo pipefail

build="$(cd "${1:?usage: make-appimage.sh <build folder>}" && pwd)"
here="$(cd "$(dirname "$0")" && pwd)"

# linuxdeploy and its Qt plugin, pinned and checked, kept in the build folder.
tools="$build/packaging-tools"
mkdir -p "$tools"
fetch() {  # url file sha256
  if [ ! -f "$tools/$2" ] || ! echo "$3  $tools/$2" | sha256sum -c --status; then
    curl -sSfL -o "$tools/$2" "$1"
    echo "$3  $tools/$2" | sha256sum -c --quiet || { echo "$2 isn't the file we expected" >&2; exit 1; }
    chmod +x "$tools/$2"
  fi
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
  linuxdeploy-x86_64.AppImage c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
  linuxdeploy-plugin-qt-x86_64.AppImage 15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
export PATH="$tools:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1  # so the tools run without FUSE (CI, containers)

# The same name as the other installers: SketchBillet-<version>-linux-x64
name="$(sed -n 's/^set(CPACK_PACKAGE_FILE_NAME "\(.*\)")$/\1/p' "$build/CPackConfig.cmake")"
[ -n "$name" ] || { echo "Couldn't read the package name from $build/CPackConfig.cmake: configure the build first" >&2; exit 1; }

appdir="$build/AppDir"
rm -rf "$appdir"
cmake --install "$build" --component app --prefix "$appdir/usr"

export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
mkdir -p "$build/packages"
export LDAI_OUTPUT="$build/packages/$name.AppImage"
rm -f "$LDAI_OUTPUT"
linuxdeploy-x86_64.AppImage --appdir "$appdir" \
  --executable "$appdir/usr/bin/sketchbillet" \
  --executable "$appdir/usr/bin/sketchbillet_cli" \
  --desktop-file "$appdir/usr/share/applications/org.sketchbillet.SketchBillet.desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/sketchbillet.png" \
  --custom-apprun "$here/AppRun" \
  --plugin qt --output appimage
echo "Made $LDAI_OUTPUT"
