# PlaneGCS

PlaneGCS is FreeCAD's 2D geometric constraint solver. Khan uses it to solve
sketches, through Khan's own `ISketchSolver` interface (`kh_sketch_planegcs`), so
it can be replaced.

- **From:** FreeCAD, `src/Mod/Sketcher/App/planegcs/`, commit
  `101dd78835dda2a8db2fe955e1afb209fb6a2193` (Oct 28, 2026),
  <https://github.com/FreeCAD/FreeCAD>.
- **License:** LGPL-2.1-or-later (see `COPYING.LIB` and each file's header).
  Khan builds it as its own **shared library**, `kh_planegcs`, so anyone can
  rebuild it with their own changes and swap it in, as the LGPL asks. Khan's own
  code stays Apache-2.0; see `docs/dependencies.md`.
- **Needs:** Eigen (header-only, MPL-2.0), built with `EIGEN_MPL2_ONLY`.

## What Khan changed

Every change in `src/planegcs/` is marked with a `// Khan:` comment.

| File | Change | Why |
| --- | --- | --- |
| `GCS.cpp` | Finds groups of constraints that affect each other with a small union-find instead of Boost.Graph's `connected_components`. The groups are numbered the same way | So Khan doesn't need Boost |
| `GCS.cpp` | `std::unreachable()` in a `switch` default became `return SolveStatus::Failed;` | `std::unreachable` needs C++23; Khan builds as C++20 |
| `Constraints.cpp`, `Geo.h` | Two unused Boost includes replaced with `<cassert>` and `<cmath>`, which they brought in | Same |

Files outside `src/planegcs/` are Khan's stand-ins for FreeCAD headers that
PlaneGCS includes:

- `SketcherGlobal.h`: the export macro (`SketcherExport`) for the shared library.
- `compat/Base/Console.h`: FreeCAD's log, which PlaneGCS only uses for debugging
  text. Khan drops the text; problems reach the student as diagnostics instead.
- `compat/Base/Tools.h`, `compat/FCConfig.h`: empty; included but not needed.

## Updating

1. Copy `src/Mod/Sketcher/App/planegcs/` from a newer FreeCAD into `src/planegcs/`.
2. Apply the changes above again (search the old copy for `// Khan:`).
3. Update the commit here and in `docs/dependencies.md`.
4. Run all the tests, including `kh_sketch_tests`, on all three systems.

Changes that make PlaneGCS better for everyone (full B-spline support is planned
for Phase 1) are offered back to FreeCAD.
