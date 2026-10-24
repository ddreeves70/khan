# Dependencies

Every library, tool and service Khan uses or plans to use: what it's for, its
license, where it stands today, and the long-term plan. **Any pull request that
adds, removes, upgrades or changes how Khan uses a dependency updates this page**
(a CLAUDE.md rule), and the `docs.guides` test fails if a package in `vcpkg.json`
or a `FetchContent` download in CMake isn't listed here.

Last reviewed: Oct 24, 2026.

## License rules

Khan's own code is Apache-2.0, free forever. A dependency must keep it that way:

| License kind | Examples | Allowed how |
| --- | --- | --- |
| Permissive | MIT, BSD, Apache-2.0, BSL-1.0, zlib, MPL-2.0 | Any way, including built into Khan |
| LGPL | Qt, Open CASCADE, Qt ADS, PlaneGCS | Only as a **shared library** the user could swap for their own build, never copied into Khan's code or linked statically |
| GPL | CalculiX, Doxygen, libdxfrw | Never linked into Khan. Allowed as a **separate program** Khan runs (CalculiX) or as a **build tool** that doesn't ship (Doxygen) |
| Commercial-only | Open CASCADE's paid DXF importer, Parasolid | Not used |

## In use

### Linked into Khan

| Dependency | What Khan uses it for | Used by | License | How we get it | Version | Long-term plan |
| --- | --- | --- | --- | --- | --- | --- |
| **Open CASCADE Technology (OCCT)** | The geometry kernel: boxes, cylinders, Booleans, meshing, STEP and STL export, volumes. Also the 3D viewer (AIS/V3d) | `kh_kernel_occt`, `kh_view` | LGPL-2.1 with exception | vcpkg (`opencascade`); apt on Linux (`libocct-*-dev`) | vcpkg baseline; 7.6.3 from Ubuntu 24.04 | Stays the kernel. Hidden behind `IKernel`, so a second kernel could be added. The viewer part is a deliberate shortcut: replaced by Khan's own renderer later (see "Planned") |
| **Qt 6** (Widgets, OpenGL Widgets) | The desktop app: windows, menus, panels, dialogs, the OpenGL surface the 3D view draws in | `kh_app`, `kh_ui`, `kh_view`, panel plugins | LGPL-3 | Qt's installer (Windows, Mac), `jurplel/install-qt-action` (CI), apt (`qt6-base-dev`) | Needs 6.4+; CI uses 6.8.3 | Stays. Always dynamically linked. Translations (Qt Linguist) when Khan gets other languages |
| **Qt Advanced Docking System (ADS)** | Dockable, tabbed, floating panels and saved layouts | `kh_app` | LGPL-2.1 | CMake `FetchContent` (`qtads`), pinned commit; or an installed copy | v5.1.1 | Stays. Built as a shared library |
| **nlohmann/json** | Reading and writing the JSON inside `.khan` files and plugin manifests | `kh_io`, `kh_plugin` | MIT | vcpkg (`nlohmann-json`); apt | 3.x | Stays |
| **libzip** | The zip container of `.khan` files | `kh_io` | BSD-3-Clause | vcpkg (`libzip`, default features off); apt | 1.x | Stays |
| **zlib** | Compression for libzip | via libzip | zlib | vcpkg (pulled in by libzip); system | | Stays |
| **FreeType** | Fonts for labels OCCT draws (the view cube's FRONT, TOP, ...) | via OCCT | FreeType License (BSD-style) | vcpkg (pulled in by OCCT); system | | Stays while OCCT draws the view |
| **OpenGL** (system) | 3D drawing | `kh_view` via OCCT and Qt | Part of the graphics driver | The computer's graphics driver | 2.1 or newer (4.1 core on macOS) | Replaced by the WebGPU renderer, which draws through Vulkan, Metal or Direct3D |

### Build, test and docs tools (not shipped)

| Tool | What for | License | How we get it | Long-term plan |
| --- | --- | --- | --- | --- |
| **CMake** | Build system | BSD-3-Clause | Installer, Homebrew, apt | Stays; needs 3.28+ |
| **Ninja** | Fast builds | Apache-2.0 | Installer, Homebrew, apt, CI action | Stays |
| **vcpkg** | Builds the C++ libraries the same way on every system | MIT | `git clone`, pinned by `builtin-baseline` in `vcpkg.json` | Stays. Bump the baseline on purpose, with a full CI run |
| **Compilers** | MSVC (Visual Studio 2022), Apple Clang (Xcode 15+), GCC 12+ / Clang 16+ | Various | Visual Studio, Xcode tools, apt | Stays. C++20 |
| **Catch2 v3** | Unit tests | BSL-1.0 | vcpkg (`catch2`); apt | Stays |
| **Doxygen** | API reference, and the `docs.api` test that every public item is documented | GPL-2.0 (tool only) | Installer, Homebrew, apt | Stays |
| **clang-format** | Code formatting (`.clang-format`) | Apache-2.0 with LLVM exception | LLVM, Visual Studio, apt | Stays. A CI format check could be added |
| **Python 3** | The docs checker (`tools/docs/check_docs.py`) and picture maker | PSF | python.org, system | Stays. Later also embedded in Khan (see pybind11) |
| **NumPy, Matplotlib** | Rendering the guide's pictures from real designs (`tools/docs/render.py`) | BSD-3-Clause; Matplotlib license (PSF-style) | `pip` | Stays; only needed to remake pictures |
| **Xvfb, Mesa** | A virtual screen and software OpenGL, so CI on Linux can draw the 3D view (`view.gl` test, app screenshots) | MIT | apt (`xvfb`, `libgl1-mesa-dri`) | Stays |
| **GitHub Actions** | CI on Windows, macOS and Linux; actions `checkout`, `cache`, `upload-artifact`, `ilammy/msvc-dev-cmd`, `seanmiddleditch/gha-setup-ninja`, `jurplel/install-qt-action` | Free for public repos; actions are MIT | GitHub | Stays. Pin actions to versions; move off Node 20 actions when GitHub requires it |
| **autoconf, automake, libtool, pkg-config** | Needed by vcpkg to build some ports on Linux and macOS | GPL tools (build only) | Homebrew, apt | Stays while vcpkg needs them |

## Planned

| Dependency | What it's for | When | License | Plan and open questions |
| --- | --- | --- | --- | --- |
| **PlaneGCS** (from FreeCAD) | The sketch constraint solver | Phase 1 | LGPL-2.1+ | Extract from FreeCAD into its own shared library behind Khan's `ISolver` interface, so it can be replaced. Supports splines only partly; spline constraints may need our own work |
| **Eigen** | Linear algebra, used by PlaneGCS (and later our own math) | Phase 1 | MPL-2.0 | vcpkg (`eigen3`). Header-only |
| **Boost** (a few header-only parts) | PlaneGCS uses some Boost headers | Phase 1 | BSL-1.0 | To confirm when extracting PlaneGCS; take only what it needs from vcpkg |
| **pybind11** + embedded **CPython** | Python scripting, console, Python plugins; bindings generated from the command registry | Phase 5 | BSD-3-Clause; PSF | vcpkg (`pybind11`, `python3`). Ship Python with the installer |
| **lib3mf** | 3MF import and export for 3D printing | Phase 3 | BSD-2-Clause | vcpkg (`lib3mf`). Alternative: write 3MF ourselves with libzip and nlohmann (it's zip + XML), if lib3mf is heavy |
| **Our own DXF reader** | Importing splines, lines and arcs from DXF (Inkscape, other CAD) | Phase 1 or 2 | Apache-2.0 (ours) | The common DXF libraries (libdxfrw, dxflib) are GPL, and OCCT's DXF importer is commercial, so we write a small reader for the entities we need |
| **Installer tools** | Windows installer (MSI for schools), macOS `.dmg`, Linux AppImage or Flatpak | Phase 3 | To decide: WiX (MS-RL), NSIS (zlib), CPack (BSD-3) | Decide in Phase 3. The tools are free; **code signing** isn't. Windows: SignPath Foundation (free for open-source projects that build and sign in CI with a manual approval per release), or Azure Artifact Signing (about $9.99/month, US and Canada individuals), or a classic OV certificate (about $150–300/year; EV no longer worth the extra). macOS: Apple Developer Program, $99/year, needed to notarize. Linux: free. Cheapest: about $99/year |
| **Crash reporter** (opt-in) | Crash reports users choose to send | Phase 3 | To decide: Crashpad (BSD-3) or sentry-native (MIT) | Off by default; works offline; never sends designs |
| **Manifold** | Fast, robust mesh Booleans for the mesh workspace | Phase 6 | Apache-2.0 | vcpkg (`manifold`) |
| **Clipper2** | 2D polygon offsets for CAM toolpaths | Phase 6 | BSL-1.0 | vcpkg (`clipper2`) |
| **CalculiX** (`ccx`) | Finite-element strength simulation (FEA) | Phase 7 | GPL | **Decided Oct 24, 2026: a separate program, never built into Khan.** Khan writes CalculiX's input file, runs `ccx` and reads the results, so Khan stays Apache-2.0. The installer may ship the `ccx` program with its license and an offer of its source. Practical work: building `ccx` for Windows and macOS (Fortran plus solver libraries; PARDISO isn't free, so use SPOOLES or similar) and turning its text errors into plain words. Uses the material strength numbers Khan stores now. Have the exact license wording and the bundling checked before shipping |
| **Khan's own renderer** on **WebGPU** (Dawn or wgpu-native) | Replaces OCCT's viewer: faster, large assemblies, and the same code for a web viewer | After v0.2 | Dawn BSD-3-Clause; wgpu-native MIT/Apache-2.0 | Rendering stays behind `kh_view`, so the swap doesn't touch the rest of Khan. Choose Dawn or wgpu-native then |
| **OCCT compiled to WebAssembly** | The read-only web viewer | Later | LGPL-2.1 with exception | With the WebGPU renderer |

## Considered and not used

| What | Why not |
| --- | --- |
| libdxfrw, dxflib | GPL: can't be linked into Apache-2.0 Khan |
| OCCT's DXF importer | Commercial add-on |
| FreeCAD's whole sketcher | Tied to FreeCAD's document model and GPL/LGPL code; we take only the solver (PlaneGCS) |
| Static linking of Qt or OCCT | LGPL: users must be able to swap the library |

## Adding a dependency

1. Check the license against the rules above. When unsure, ask before writing code.
2. Prefer vcpkg; pin downloads to an exact commit or version.
3. Keep it behind a Khan interface if Khan might replace it (the kernel, the solver, the renderer).
4. Add it to this page, with what it's for, its license and the long-term plan.
5. Make sure CI still builds on all three systems.
