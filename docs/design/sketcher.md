# The sketcher (Phase 1)

> **Draft.** Step 1 (the solver) is built. The rest is the plan, in the order it
> will be built. See the [roadmap](roadmap.md#phase-1-sketcher) for Phase 1's exit
> check.

A sketch is a flat drawing (points, lines, circles and arcs) with **constraints**
(this line is horizontal, these two points are the same point) and **dimensions**
(this line is 40 mm long). A solver moves the drawing until every constraint is
true. Later steps turn the closed shapes in a sketch into solids (extrude,
revolve).

## Order of work

| Step | What | Status |
| --- | --- | --- |
| 1. Solver | `kh_sketch` (sketch values, `ISketchSolver`), PlaneGCS as its own shared library, `kh_sketch_planegcs` | Built |
| 2. Sketch step | A `Sketch` step in the design: on a plane (XY, XZ, YZ), its entities and constraints, saved in the `.khan` file, changed only by commands (add line, add constraint, set dimension, ...), solved on rebuild | Next |
| 3. Drawing in the app | Edit a sketch in the viewer: draw lines, arcs and circles with the mouse, snap to the grid and to points, pick and drag | |
| 4. Constraints in the app | Add constraints and dimensions from a toolbar and by typing numbers; show them on the drawing; degrees of freedom shown by color (free blue, fixed black); conflicts highlighted with a plain message | |
| 5. Auto-constraints and profiles | Horizontal, vertical, coincident and tangent added while drawing; closed loops found as profiles | |
| 6. Extrude | Profiles become solids (Phase 2 starts properly here) | |
| 7. B-splines | Full spline support, as the roadmap lists | |

Each step is its own pull request, with tests and guide pages.

## Libraries

```text
kh_sketch            sketch values, check(), error(), ISketchSolver      (Apache-2.0, std only)
kh_sketch_planegcs   makePlaneGcsSolver(): ISketchSolver on PlaneGCS     (Apache-2.0)
kh_planegcs          FreeCAD's PlaneGCS, a shared library                (LGPL-2.1+, third_party/planegcs)
```

It's the same pattern as the geometry kernel (`kh_kernel` and `kh_kernel_occt`):
only `kh_sketch_planegcs` includes PlaneGCS headers, so the solver can be tested on
its own and replaced. `kh_model` will use `kh_sketch` for the Sketch step and get a
solver passed in, as it gets a kernel.

## Sketch values (`kh/sketch/Sketch.h`)

- **Entities:** `PointShape`, `LineShape` (start, end), `CircleShape` (center,
  radius), `ArcShape` (center, radius, start and end angles, counterclockwise).
  Each has an `EntityId` and a `construction` flag (helper geometry that isn't
  part of a profile).
- **Points of entities:** a `PointRef` is an entity plus a role: `Self` (a point),
  `Start`, `End`, `Center`. An arc's ends are worked out from its center, radius
  and angles.
- **Constraints:** Fix, Coincident, Horizontal, Vertical, Parallel, Perpendicular,
  Equal, Point on line, Point on circle, Midpoint, Tangent; and **dimensions**:
  Distance, Length, Horizontal distance, Vertical distance, Radius, Diameter,
  Angle. Each has a `ConstraintId`. Lengths are mm and angles radians inside the
  code; the app shows degrees.
- **`check()`** finds constraints that use the wrong things ("Horizontal needs one
  line.") before any solving.
- **`error()`** measures how far a constraint is from true with Khan's own math.
  The tests check every solver answer with it, and the PlaneGCS adapter refuses
  an answer that doesn't pass it.

## The solver (`ISketchSolver`)

`solve(sketch)` gives back the moved entities (same ids, kinds and order), a
status (Solved, Conflicting, Failed, Invalid), the **degrees of freedom** (how
many ways the drawing can still move; 0 is fully constrained), the ids of
**conflicting** constraints (they can't all be true) and of **redundant** ones
(they repeat others: the sketch still solves), and plain messages.

How the PlaneGCS adapter works:

1. Every number (a point's x and y, a radius, an arc's angles) becomes a PlaneGCS
   parameter; dimension values and fixed positions become constants. Khan's
   constraints are tagged 1, 2, 3, ... so PlaneGCS's answers map back to their ids.
2. **Solve** from where the geometry is now, with PlaneGCS's DogLeg method.
3. **Diagnose at the solved shape:** build the system again from the answer and
   ask for degrees of freedom, conflicting and redundant constraints. Asked at a
   rough starting drawing, two constraints that say the same thing (two
   horizontal lines and Parallel) can look different, and the counts come out
   wrong.
4. **Check** every constraint with `error()` (within 1e-6 mm or radians) and that
   no radius went to 0 or below. If any fails, the answer is refused.

**Tangent at a shared end.** When a Tangent's two curves are also joined end to
end by a Coincident (a line running smoothly into an arc), the adapter keeps their
directions equal at that point (PlaneGCS's angle-via-point), as FreeCAD's endpoint
tangency does. "Touching edges" plus "shared point" look like the same constraint
twice at the solved shape, which made the degrees of freedom wrong. The student
just adds Coincident and Tangent; the adapter picks the right form.

**Same answer every time.** PlaneGCS orders its unknowns by memory address (a
`std::set<double*>`), and that order sets the order of its sums, so numbers
scattered around memory could make the same sketch solve to answers that differ in
the last bits. (They did on Windows, Oct 28, 2026.) The adapter keeps every number
in one array, filled in a fixed order, so addresses rise in creation order on
every computer, and it lists conflicting and repeated constraints in the sketch's
order. The tests check that the same sketch gives the same numbers, bit for bit,
after scattering the heap between solves.

## Choices

- **A copy of PlaneGCS, not all of FreeCAD.** Only `src/Mod/Sketcher/App/planegcs/`
  is copied, with a few marked changes (no Boost, no FreeCAD headers; see
  `third_party/planegcs/README.md`). It's built as a shared library because it's
  LGPL.
- **Boost removed.** PlaneGCS used Boost.Graph only to find groups of constraints
  that affect each other; a dozen lines of union-find do the same.
- **Constraint set.** The core set above covers brackets, plates and slots.
  Symmetric, point-on-arc-only, ellipses and splines come later (splines are step 7).
- **Fix uses two coordinate constraints** (x and y), not removing the point's
  numbers from the solve, so a fixed point that fights a dimension shows up as a
  conflict with a name.
