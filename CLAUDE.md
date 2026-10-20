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

# Linux with system packages (OCCT, Catch2, TBB and Qt 6 from apt)
cmake --preset system
cmake --build --preset system
ctest --preset system

# smoke test
./build/dev/apps/kh_cli/kh_cli demo out.step
```

The main developer works on Windows: run commands from a "Developer PowerShell for
VS 2022" with `VCPKG_ROOT` and `QT_DIR` set (see README). Use forward-compatible,
cross-platform code; CI also builds on macOS and Linux.

Run the tests after every change. A change is not done until the build has no
warnings and every test passes.

## Architecture rules (do not break these)

Libraries and what each may depend on. A library may only use libraries listed
below it.

| Library | Purpose | May depend on |
| --- | --- | --- |
| `kh_app`, `kh_cli` | Applications | anything below |
| `kh_view` (Phase 0 wk 4) | Qt + OCCT viewer | `kh_model`, `kh_kernel`, Qt |
| `kh_model` | Document, parameters, feature graph, commands, naming | `kh_kernel`, `kh_core` |
| `kh_kernel_occt` | `IKernel` on OCCT | `kh_kernel`, OCCT |
| `kh_kernel` | `IKernel` interface and value types | `kh_core` |
| `kh_core` | Ids, units, diagnostics, version | standard library only |

1. **Only `kh_kernel_occt` (and later `kh_view`) may include OCCT headers.** Nothing
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
6. **Qt types stay in `kh_app` and `kh_view`.** Core libraries use the standard library.
7. **User-facing messages are for a 14-year-old:** plain words, units on every
   number, and say how to fix the problem.

## Code style

- C++20, formatted with the repo's `.clang-format`. Namespaces `kh`, `kh::kernel`,
  `kh::model`, `kh::app`.
- Public headers live in `src/<lib>/include/kh/<lib>/`; include them as `"kh/<lib>/X.h"`.
- New libraries use `kh_add_library()` from `cmake/KhTarget.cmake`.
- Prefer value types and `std::unique_ptr` ownership; no raw `new`/`delete`.
- Lengths are millimeters, angles radians, inside the code.

## Tests

- Catch2 v3, in `tests/<lib>/test_*.cpp`.
- Every bug fix adds a test. Every geometry routine gets a property test
  (volumes, bounds, validity), not just a "does not crash" test.
- From Phase 0 week 2, the geometry regression corpus runs through `kh_cli`.

## Review rules

- Changes to `kh_model`, `kh_kernel`, naming or the file format need a line-by-line
  human review before merge.
- Keep changes small and focused; one task per branch.
- Commits need a DCO sign-off (`git commit -s`).
