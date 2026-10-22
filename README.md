# Khan

Free, open-source parametric CAD for classrooms, makers and small shops.
Works offline, owns no part of your data, and stays free forever.

> **Status:** Phase 0 (foundations). Nothing usable yet.

## What works today

- `kh_kernel`: the kernel interface (`IKernel`) that keeps OCCT behind one boundary.
- `kh_kernel_occt`: boxes, cylinders, Booleans, mass properties, topology counts,
  validity checks, meshing, STEP and STL export.
- `kh_cli demo out.step`: builds a test part (a tag with a hole) and exports it.
- `kh_cli regress tests/corpus`: runs the geometry regression corpus
  (see [tests/corpus/README.md](tests/corpus/README.md)).
- `kh_app`: an empty main window with the planned layout.

## Design

The full design, including architecture, UI mockups, class diagrams and the phased roadmap, is in **[docs/design](docs/design/README.md)**. It is a draft and changes as Khan is built.

## Building

You need a C++20 compiler (Visual Studio 2022, Clang 16+ or GCC 12+), CMake 3.28+,
Ninja, Qt 6.4+ and either vcpkg or system packages.

### Windows (main development platform)

Follow **[docs/windows-setup.md](docs/windows-setup.md)** step by step. In short:

1. Install an up-to-date **Visual Studio 2022** (C++ workload with CMake tools), **Git**,
   and **Qt 6 (MSVC 2022 64-bit)**, not the MinGW build.
2. Put vcpkg and the repository in `C:\dev`, not under Documents.
3. Add the `vsdev` function to your PowerShell profile and run it in each new window.
4. Copy `CMakeUserPresets.example.json` to `CMakeUserPresets.json` and check its paths.
5. Build:

```powershell
vsdev
cd C:\dev\khan
cmake --preset win
cmake --build --preset win
ctest --preset win
.\build\win\apps\kh_app\kh_app.exe
```

The first configure builds OCCT through vcpkg and takes 30-60 minutes; later builds
reuse it. Qt's runtime files are copied next to `kh_app.exe` automatically.

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
docs/              setup guides and the design (docs/design)
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and the architecture rules in [CLAUDE.md](CLAUDE.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).
