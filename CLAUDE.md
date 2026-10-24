# Khan: guide for Claude and contributors

Khan is a free, open-source parametric CAD application in C++20,
built on Open CASCADE Technology (OCCT) with a Qt 6 desktop UI. First users are high
school engineering classes, then makers, CNC users and small shops. Long-term goal:
SolidWorks-class capability, free forever. The full design lives in the project design
document (Design, Build plan, Design model, Class model tabs).

## Build and test

```bash
# with vcpkg (all platforms; set VCPKG_ROOT first)
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

# Linux with system packages (OCCT, Catch2, TBB, Qt 6, nlohmann-json and libzip from apt)
cmake --preset system
cmake --build --preset system
ctest --preset system

# smoke test
./build/dev/apps/kh_cli/kh_cli demo out.step
```

The main developer works on Windows with the `win` preset from a personal
`CMakeUserPresets.json`, after running the `vsdev` PowerShell function (see
docs/windows-setup.md):

```powershell
cmake --preset win
cmake --build --preset win
ctest --preset win
```

Write cross-platform code; CI also builds on macOS and Linux.

Run the tests after every change. A change is not done until the build has no
warnings and every test passes.

## Architecture rules (do not break these)

Libraries and what each may depend on. A library may only use libraries listed
below it.

| Library | Purpose | May depend on |
| --- | --- | --- |
| `kh_app`, `kh_cli` | Applications (`kh_app` docks panels with Qt ADS, see `cmake/KhAds.cmake`) | anything below |
| Built-in plugins (`plugins/`) | `kh.modeling`, `kh.featuretree`, `kh.properties`, `kh.viewer` | `kh_plugin`, anything below it |
| `kh_view` | Qt + OCCT 3D view, and its flat fallback | `kh_kernel_occt`, `kh_kernel`, `kh_core`, Qt, OCCT |
| `kh_ui` | Qt side of the plugin API: panels | `kh_plugin`, Qt |
| `kh_plugin` | Plugin host, manifests, event bus, open documents | `kh_model`, `kh_kernel`, `kh_core` |
| `kh_io` | `.khan` files: save and open (zip + JSON) | `kh_model`, `kh_core`, libzip, nlohmann_json |
| `kh_model` | Document, parameters, feature graph, commands, naming | `kh_kernel`, `kh_core` |
| `kh_kernel_occt` | `IKernel` on OCCT | `kh_kernel`, OCCT |
| `kh_kernel` | `IKernel` interface and value types | `kh_core` |
| `kh_core` | Ids, units, diagnostics, version | standard library only |

1. **Only `kh_kernel_occt` and `kh_view` may include OCCT headers**, and never in
   a public header (`kh/kernel_occt/OcctShape.h` only forward-declares `TopoDS_Shape`). Nothing
   in `kh_kernel` or `kh_model` may mention an OCCT type. Shapes cross the boundary
   as `ShapeHandle`.
2. **Only Commands change a Document.** UI, Python scripts, lessons and the AI
   assistant all go through the command stack, so undo and history always work.
3. **No exceptions across library boundaries.** Report problems as `kh::Diagnostic`
   values. Catch OCCT's `Standard_Failure` inside the adapter.
4. **Every persistent object has a UUID** (`kh::Id<Tag>`), never an index or pointer
   that could change between rebuilds. References to geometry use `GeometryRef`
   (Phase 2), never raw kernel sub-shapes.
5. **Kernel operations are deterministic.** Same inputs, same output.
6. **Qt types stay in `kh_app`, `kh_view`, `kh_ui` and the plugins that add panels.** Core
   libraries use the standard library; actions are plain data, so model plugins add them
   without Qt.
7. **User-facing messages are for a 14-year-old:** plain words, units on every
   number, and say how to fix the problem.

## Code style

- C++20, formatted with the repo's `.clang-format`. Namespaces `kh`, `kh::kernel`,
  `kh::model`, `kh::app`.
- Public headers live in `src/<lib>/include/kh/<lib>/`; include them as `"kh/<lib>/X.h"`.
- New libraries use `kh_add_library()` from `cmake/KhTarget.cmake`.
- Prefer value types and `std::unique_ptr` ownership; no raw `new`/`delete`.
- Lengths are millimeters, angles radians, inside the code.

## API documentation

Everything public gets a Doxygen doc comment (the C++ equivalent of Javadoc), so
it appears in the generated API reference. That means every class, struct, enum
and enum value, function, public data member, type alias and constant in a public
header (`src/<lib>/include/kh/<lib>/`), including public and protected members.

- Use `///` comments directly above the item (or `///<` after a data member or enum
  value). The first sentence is the summary.
- Functions: `@param` for every parameter, `@return` for anything not `void`, with
  units (mm, mm^3, radians) where they apply. Templates: `@tparam`.
- Each header starts with `/// @file` and a one-line summary.
- Overrides inherit the base class's docs; add a comment only if the behavior differs.
- Add a short `@code` example for the main entry points of a library.
- Private members and `.cpp` internals use ordinary `//` comments.

Build the reference with `cmake --build --preset dev --target docs` (output in
`build/<preset>/docs/html/index.html`). The `docs.api` test and the `api-docs` CI job
fail on any undocumented public item.

## User's guide, CLI reference and tutorials

Khan's guides grow with the code. Any pull request that adds or changes something a
user can see or type updates them in the same PR:

- `docs/user-guide/`: explains every feature for a student, with pictures. Each
  page leads with what the feature does, then an example, then tips.
- `docs/cli/`: one page per `kh_cli` command (all options, examples, errors), and
  `commands.md` with a section and a tested example for every modeling command.
- `docs/tutorials/`: step-by-step projects that build a printable part. Add or
  extend a tutorial when a feature changes how parts are made.

Rules:

- Examples are marked `<!-- cli-example -->` (or `exit=N`) above a ```` ```console ````
  block. The `docs.guides` test runs every one and fails if `kh_cli` prints anything
  different; it also fails if a command has no page or a link or picture is broken.
  Fix the docs or the code, never weaken the example.
- Pictures are rendered from real designs: add a recipe to `docs/pictures.json`, run
  `python3 tools/docs/make_images.py --kh-cli <path to kh_cli>`, look at the result,
  and commit the PNG. Once the desktop app exists, add screenshots the same way.
- Same writing rules as the app: plain words for a 14-year-old, units on every
  number, say how to fix problems.

## Plugin developer guide

Khan's own features are plugins built on the public plugin API (see the design
document, "UI and plugin architecture"). The guide for plugin authors lives in
`docs/plugin-guide/` and starts with the plugin shell (Phase 0, weeks 4 to 5).

- Any pull request that adds or changes a plugin extension point, the manifest
  format, the event types or the Python bindings updates the guide in the same PR.
- Every extension point in the guide has a short working example, and the guide's
  Hello plugin is built and loaded by a test, so the tutorial can't go stale.
- Write it for a student who knows a little C++ or Python: plain words, one idea
  per step, units on every number.
- Code shown in the guide is marked `<!-- guide-file: path -->` and must match that
  file exactly; the `docs.plugin_guide_code` test checks it.
- Built-in plugins live in `plugins/<name>/` and use only `PluginContext`, like an
  outside plugin. An outside plugin's commands must start with its plugin id.

## Tests

- Catch2 v3, in `tests/<lib>/test_*.cpp`.
- Every bug fix adds a test. Every geometry routine gets a property test
  (volumes, bounds, validity), not just a "does not crash" test.
- The geometry regression corpus lives in `tests/corpus/` and runs through
  `kh_cli regress` as part of `ctest` (see `tests/corpus/README.md`). Every new kernel
  operation or geometry bug fix adds a corpus case.
- Never update expected corpus values just to make a test pass. Use `--update` only for
  intended behavior changes, check the new values by hand, and explain them in the commit.

## Dependencies

`docs/dependencies.md` lists every library, tool and service Khan uses or plans to
use, with its license, status and long-term plan. Any pull request that adds,
removes, upgrades or changes how Khan uses a dependency updates it in the same PR;
`docs.guides` fails if a `vcpkg.json` package or a `FetchContent` download isn't
listed. Check the license rules on that page before adding anything: LGPL only as
a shared library, GPL never linked into Khan.

## Review rules

- Changes to `kh_model`, `kh_kernel`, naming or the file format need a line-by-line
  human review before merge.
- Keep changes small and focused; one task per branch.
- Commits need a DCO sign-off (`git commit -s`).
