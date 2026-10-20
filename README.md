# Khan

Free, open-source parametric CAD for classrooms, makers and small shops.
Works offline, owns no part of your data, and stays free forever.

> **Status:** Phase 0 (foundations). Nothing usable yet.

## What works today

- `kh_kernel`: the kernel interface (`IKernel`) that keeps OCCT behind one boundary.
- `kh_kernel_occt`: boxes, cylinders, Booleans, mass properties, meshing, STEP and STL export.
- `kh_cli demo out.step`: builds a test part (a tag with a hole) and exports it.
- `kh_app`: an empty main window with the planned layout.

## Building

You need a C++20 compiler (Visual Studio 2022, Clang 16+ or GCC 12+), CMake 3.28+,
Ninja, Qt 6.4+ and either vcpkg or system packages.

### Windows (main development platform)

One-time setup:

1. Install **Visual Studio 2022** (Community is fine) with the **Desktop development with C++**
   workload. It includes the compiler, CMake and Ninja.
2. Install **Git for Windows**.
3. Install **vcpkg** in a short path and point `VCPKG_ROOT` at it (PowerShell):

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   setx VCPKG_ROOT C:\dev\vcpkg
   ```

4. Install **Qt 6** with the Qt online installer, choosing the *MSVC 2022 64-bit* component,
   and point `QT_DIR` at it:

   ```powershell
   setx QT_DIR C:\Qt\6.8.3\msvc2022_64
   ```

5. Open a new **Developer PowerShell for VS 2022** (so the new variables and the compiler are found).

Build and test:

```powershell
git clone <your repo url> C:\dev\khan
cd C:\dev\khan
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
.\build\dev\apps\kh_cli\kh_cli.exe demo tag.step
```

The first configure builds OCCT through vcpkg and can take 30-60 minutes; later builds
reuse it. To run the desktop app, add `%QT_DIR%\bin` to `PATH` (or run `windeployqt`
on `build\dev\apps\kh_app\kh_app.exe`).

You can also open the folder directly in Visual Studio 2022 (**File > Open > Folder**);
it reads `CMakePresets.json` and lets you pick the `dev` preset.

### macOS and Linux with vcpkg

Same steps with your platform's compiler and Ninja: set `VCPKG_ROOT` and `QT_DIR`, then run
`cmake --preset dev`, `cmake --build --preset dev` and `ctest --preset dev`.

### Linux with system packages (Ubuntu 24.04)

```bash
sudo apt install cmake ninja-build g++ catch2 qt6-base-dev libtbb-dev \
  libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
  libocct-data-exchange-dev
cmake --preset system
cmake --build --preset system
ctest --preset system
```

Build options: `-DKH_BUILD_APP=OFF` skips the Qt app, `-DKH_WITH_OCCT=OFF` builds only
the kernel-independent libraries.

## Repository layout

```
apps/kh_app        Qt desktop application
apps/kh_cli        headless tool (smoke tests, regression corpus)
src/core           ids, diagnostics, version
src/kernel         IKernel interface, no OCCT types
src/kernel_occt    IKernel on Open CASCADE
src/model          document, commands, feature graph (growing in Phase 0-2)
tests/             Catch2 unit tests
cmake/             build helpers
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and the architecture rules in [CLAUDE.md](CLAUDE.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).
