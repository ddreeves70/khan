# Packaging: SketchBillet's installers

How the downloads on [sketchbillet.org/download](https://sketchbillet.org/download/)
are made. How people install them is in `docs/user-guide/install.md`.

| System | Download | Made by |
| --- | --- | --- |
| Windows | `SketchBillet-<version>-windows-x64.msi` | CPack with WiX 5 |
| macOS | `SketchBillet-<version>-macos-arm64.dmg`, with `SketchBillet.app` | CPack (DragNDrop), macdeployqt |
| Linux | `SketchBillet-<version>-linux-x64.AppImage` | `linux/make-appimage.sh`, with linuxdeploy |

`cmake/SbPackaging.cmake` has the install rules and the CPack settings. Everything
a user gets is in the `app` install component.

## Where they're made

CI (`.github/workflows/ci.yml`) makes the installer on Windows, macOS and Linux in
every run, after the tests. Then it installs it and runs the installed app and
`sketchbillet_cli`, so a missing library fails the pull request instead of
reaching a student's computer. Each run's installers are under **Artifacts** on
the run's page, so anyone can try a pull request.

The `publish` job puts them on GitHub:

- **Every push to main** replaces the files of the **preview** pre-release, under
  fixed names like `SketchBillet-preview-windows-x64.msi`, and moves its tag to
  that commit.
- **A version tag** makes a release. To release SketchBillet 0.1.0:
  1. Set `VERSION 0.1.0` in `project()` in the top `CMakeLists.txt` (and
     `"version"` in `vcpkg.json`), in a pull request, and merge it.
  2. Tag the merge and push the tag:
     `git tag -a v0.1.0 -m "SketchBillet 0.1.0" && git push origin v0.1.0`
  3. CI builds, tests and packages it, then makes the release
     **SketchBillet 0.1.0**, with notes made from the pull requests since the
     last one. Edit the notes on GitHub afterwards if you like.

  If the tag and `VERSION` don't match, `publish` stops and says so.

After publishing, `publish` starts the website build, so the Download page
shows the new files.

## Making one on your own computer

Build the `ci` or `release` preset first (a Release build).

```bash
# Windows, in a Visual Studio developer prompt; WiX once:
#   dotnet tool install --global wix --version 5.0.2
#   wix extension add --global WixToolset.UI.wixext/5.0.2
cpack --config build/release/CPackConfig.cmake

# macOS
cpack --config build/release/CPackConfig.cmake

# Linux (QMAKE: the Qt that SketchBillet was built with)
QMAKE=~/Qt/6.8.3/gcc_64/bin/qmake packaging/linux/make-appimage.sh build/release
```

The installer is written to `build/<preset>/packages/`.

## Not signed yet

The installers aren't code-signed, so Windows SmartScreen and macOS Gatekeeper
warn the first time someone opens them (the install guide says what to click).
The macOS app is signed "ad hoc" (by itself), which Apple silicon Macs need to run
it at all. To sign them later:

- **Windows:** SignPath Foundation signs open-source projects free, from CI, or
  a code-signing certificate. Sign the `.msi` and the `.exe` files in it.
- **macOS:** an Apple Developer ID ($99 a year), then sign and notarize the app
  in the "macOS" part of `cmake/SbPackaging.cmake`, and notarize the `.dmg`.

See "Installer tools" in `docs/dependencies.md`.

## Other files here

| File | What it is |
| --- | --- |
| `icons/sketchbillet.svg` | The app icon. `python3 tools/packaging/make_icons.py` remakes the `.png`, `.ico` and `.icns` from it |
| `linux/org.sketchbillet.SketchBillet.desktop` | The Linux menu entry |
| `linux/AppRun` | What the AppImage runs: the app, or `sketchbillet_cli` when the first word is `cli` |
| `THIRD-PARTY-NOTICES.txt`, `licenses/` | The open-source libraries in SketchBillet and their licenses, which every installer carries |

## To do

- Lower the oldest macOS it runs on (now the version the CI Mac has, 15), with
  `CMAKE_OSX_DEPLOYMENT_TARGET` and a matching vcpkg triplet.
- Intel Macs, and Linux on ARM.
- Opening `.sb` files by double-clicking them (file associations).
- Build the AppImage on an older Linux, so it runs on more systems.
