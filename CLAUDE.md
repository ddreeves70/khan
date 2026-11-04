# SketchBillet: guide for Claude and contributors

SketchBillet is a free, open-source parametric CAD application in C++20,
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

# Linux with system packages (OCCT, Catch2, TBB, Qt 6, nlohmann-json, libzip and Eigen from apt)
cmake --preset system
cmake --build --preset system
ctest --preset system

# smoke test
./build/dev/apps/sketchbillet_cli/sketchbillet_cli demo out.step
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
| `sketchbillet`, `sketchbillet_cli` | Applications (`sketchbillet` docks panels with Qt ADS, see `cmake/SbAds.cmake`) | anything below |
| Built-in plugins (`plugins/`) | `sb.modeling`, `sb.viewoptions`, `sb.featuretree`, `sb.properties`, `sb.axes`, `sb.grid`, `sb.viewer`, `sb.sketcher`, `sb.movehandles`, `sb.parameters` | `sb_plugin`, anything below it |
| `sb_view` | Qt + OCCT 3D view, and its flat fallback | `sb_kernel_occt`, `sb_kernel`, `sb_core`, Qt, OCCT |
| `sb_ui` | Qt side of the plugin API: panels | `sb_plugin`, Qt |
| `sb_plugin` | Plugin host, manifests, event bus, open documents | `sb_model`, `sb_kernel`, `sb_core` |
| `sb_io` | `.sb` files: save and open (zip + JSON) | `sb_model`, `sb_core`, libzip, nlohmann_json |
| `sb_model` | Document, parameters, feature graph, commands, naming, Sketch steps | `sb_sketch`, `sb_kernel`, `sb_core` (a sketch solver is passed in, like the kernel) |
| `sb_sketch_planegcs` | `ISketchSolver` on PlaneGCS | `sb_sketch`, `sb_planegcs` (LGPL, shared library, `third_party/planegcs`) |
| `sb_sketch` | 2D sketch geometry, constraints, `ISketchSolver` interface | `sb_core` |
| `sb_kernel_occt` | `IKernel` on OCCT | `sb_kernel`, OCCT |
| `sb_kernel` | `IKernel` interface and value types | `sb_core` |
| `sb_core` | Ids, units, diagnostics, version | standard library only |

1. **Only `sb_kernel_occt` and `sb_view` may include OCCT headers**, and never in
   a public header (`sb/kernel_occt/OcctShape.h` only forward-declares `TopoDS_Shape`). Nothing
   in `sb_kernel` or `sb_model` may mention an OCCT type. Shapes cross the boundary
   as `ShapeHandle`. Likewise, **only `sb_sketch_planegcs` may include PlaneGCS
   headers**, never in a public header; sketches cross as `sb::sketch` values.
2. **Only Commands change a Document.** UI, Python scripts, lessons and the AI
   assistant all go through the command stack, so undo and history always work.
3. **No exceptions across library boundaries.** Report problems as `sb::Diagnostic`
   values. Catch OCCT's `Standard_Failure` inside the adapter.
4. **Every persistent object has a UUID** (`sb::Id<Tag>`), never an index or pointer
   that could change between rebuilds. References to geometry use `GeometryRef`
   (Phase 2), never raw kernel sub-shapes.
5. **Kernel operations are deterministic.** Same inputs, same output.
6. **Qt types stay in `sketchbillet`, `sb_view`, `sb_ui` and the plugins that add panels.** Core
   libraries use the standard library; actions are plain data, so model plugins add them
   without Qt.
7. **User-facing messages are for a 14-year-old:** plain words, units on every
   number, and say how to fix the problem.

## Code style

- C++20, formatted with the repo's `.clang-format`. Namespaces `sb`, `sb::kernel`,
  `sb::model`, `sb::app`.
- Public headers live in `src/<lib>/include/sb/<lib>/`; include them as `"sb/<lib>/X.h"`.
- New libraries use `sb_add_library()` from `cmake/SbTarget.cmake`.
- Prefer value types and `std::unique_ptr` ownership; no raw `new`/`delete`.
- Lengths are millimeters, angles radians, inside the code.
- Toolbar icons are SVG files in `<lib or plugin>/icons/`, built in with
  `sb_embed_files()` (`cmake/SbEmbed.cmake`); the style is in the plugin guide, under "Icons".

## API documentation

Everything public gets a Doxygen doc comment (the C++ equivalent of Javadoc), so
it appears in the generated API reference. That means every class, struct, enum
and enum value, function, public data member, type alias and constant in a public
header (`src/<lib>/include/sb/<lib>/`), including public and protected members.

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

SketchBillet's guides grow with the code. Any pull request that adds or changes something a
user can see or type updates them in the same PR:

- `docs/user-guide/`: explains every feature for a student, with pictures. Each
  page leads with what the feature does, then an example, then tips.
- `docs/cli/`: one page per `sketchbillet_cli` command (all options, examples, errors), and
  `commands.md` with a section and a tested example for every modeling command.
- `docs/tutorials/`: step-by-step projects that build a printable part. Add or
  extend a tutorial when a feature changes how parts are made.

Rules:

- Examples are marked `<!-- cli-example -->` (or `exit=N`) above a ```` ```console ````
  block. The `docs.guides` test runs every one and fails if `sketchbillet_cli` prints anything
  different; it also fails if a command has no page or a link or picture is broken.
  Fix the docs or the code, never weaken the example.
- Pictures are rendered from real designs: add a recipe to `docs/pictures.json`, run
  `python3 tools/docs/make_images.py --sketchbillet-cli <path to sketchbillet_cli>`, look at the result,
  and commit the PNG. Once the desktop app exists, add screenshots the same way.
- Same writing rules as the app: plain words for a 14-year-old, units on every
  number, say how to fix problems.

## Plugin developer guide

SketchBillet's own features are plugins built on the public plugin API (see the design
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
  `sketchbillet_cli regress` as part of `ctest` (see `tests/corpus/README.md`). Every new kernel
  operation or geometry bug fix adds a corpus case.
- Never update expected corpus values just to make a test pass. Use `--update` only for
  intended behavior changes, check the new values by hand, and explain them in the commit.

## Dependencies

`docs/dependencies.md` lists every library, tool and service SketchBillet uses or plans to
use, with its license, status and long-term plan. Any pull request that adds,
removes, upgrades or changes how SketchBillet uses a dependency updates it in the same PR;
`docs.guides` fails if a `vcpkg.json` package or a `FetchContent` download isn't
listed. Check the license rules on that page before adding anything: LGPL only as
a shared library, GPL never linked into SketchBillet.

## Review rules

- Changes to `sb_model`, `sb_kernel`, naming or the file format need a line-by-line
  human review before merge.
- Keep changes small and focused; one task per branch.
- Commits need a DCO sign-off (`git commit -s`).
