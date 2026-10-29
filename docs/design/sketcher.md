# The sketcher (Phase 1)

> **Draft.** Steps 1 (the solver), 2 (the Sketch step), 3 (drawing in the app) and 4a (picking and dragging) are built. The rest is the plan, in the order it
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
| 2. Sketch step | A `Sketch` step in the design: on a plane (XY, XZ, YZ), its entities and constraints, saved in the `.khan` file, changed only by commands (add line, add constraint, set dimension, ...), solved on rebuild | Built |
| 3. Drawing in the app | Edit a sketch in the viewer: draw lines, arcs and circles with the mouse, snap to the grid and to points | Built |
| 4a. Picking and dragging | Pick sketch lines and points in the viewer (Select), drag them with the solver keeping every constraint true, delete them; what can still move shown by color (free blue, fixed black) | Built |
| 4b. Constraints in the app | Add constraints and dimensions from the toolbar and by typing numbers; show them on the drawing; conflicts highlighted with a plain message | Next |
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

## The Sketch step (step 2)

- `kh_model`: `SketchFeature` (`FeatureType::Sketch`) holds a plane and a
  `sketch::Sketch` **as drawn**. `Document::regenerate(kernel, solver)` solves it
  and puts the answer in `FeatureResult::sketch`. A sketch has no shape, so it's
  never a body; a Boolean that uses one is blocked with "a sketch, not a body".
  Without a solver, a sketch fails with a plain message.
- The solver is passed in like the kernel: `PluginHost(kernel, solver)` and
  `DocumentList`; `kh_cli` and `kh_app` make a PlaneGCS solver.
- Commands (all through `EditSketchCommand`, one undo step each, same ids on redo):
  `feature.add_sketch`, `sketch.add_point`, `sketch.add_line`,
  `sketch.add_circle`, `sketch.add_arc`, `sketch.constrain` (with `on=Line1.end,Arc1`
  references), `sketch.set_value`, `sketch.delete`. Angles are typed in degrees.
  Every edited sketch must pass `check()`.
- **Names.** Entities and constraints have names (`Line1`, `Length1`), unique in
  their sketch, so students and `kh_cli` can point at them; ids stay the
  references inside the file.
- **Not solved into the file.** The step keeps what was drawn, and every rebuild
  solves from there. The app will store solved positions back (with a command)
  once dragging exists, so the next solve starts from where things are.
- `kh_cli open --sketch` lists a solved sketch; `make_images.py` draws "as drawn"
  next to "solved" for the guide.
- File: a new step type, `"Sketch"` (schema 1). Plugin API 13 (`ArgType::Vec2`,
  `FeatureResult::sketch`, the solver parameter).

## Drawing in the app (step 3)

- **Viewer tools (`kh.viewer/1`, now `/2`).** `kh_view` has a drawing mode: given a plane, a
  left click reports the point on it (`planeClicked`) instead of orbiting or
  picking, moving reports it too (`planeMoved`), and Escape reports
  `planeCancelled`; pan and zoom still work. The point comes from OCCT's line of
  sight through the pixel (`ConvertWithProj`) meeting the plane, and the size of
  a pixel there from the next pixel over, so "close enough" works at any zoom.
  The flat view (no 3D graphics) does the same for flat planes, which is how the
  app tests click. `kh.viewer` offers this to plugins as the `kh.viewer/1`
  service: `startTool(tool, plane)`, `stopTool`, `face(plane)`.
- **`kh.sketcher`** (a new built-in plugin, tier `ui`) draws every sketch with
  overlays (the edited one blue with its points, others grey) and runs the Line,
  Circle, Arc and Point tools. A click snaps to a sketch point within 8 pixels
  (exactly onto it, plus a Coincident), or to the grid through `kh.grid/2` when
  snapping is on. Each shape, with its Coincidents, is one `EditSketchCommand`,
  so one undo step. Lines chain; closing onto a point ends the chain.
- **Overlay layers.** Lines, points and words are drawn in OCCT's Top layer and
  the grid in the normal one, in the overlays' order, so a sketch on the ground
  isn't lost in the grid lines it lies on.
- `kh_model`: `planeAxes()`, `toDesign()`, `toSketch()` turn sketch x, y into the
  design's x, y, z and back (also for extrude, later).

## Picking and dragging (step 4a)

- **Select tool.** A click picks the sketch point within 8 pixels (a point
  entity is picked whole, an end or center by its role), else the edge within
  6 pixels; Ctrl or Shift adds or removes. Picked parts are orange, the part under
  the mouse light orange. Edit sketch starts in Select; New sketch starts with
  Line. Escape steps back: stop the shape, then Select, then let go, then finish.
- **Dragging (`ISketchSolver::drag()`).** The adapter adds each pull as a
  PlaneGCS *temporary* constraint (tag -1). PlaneGCS solves the real constraints
  first and the temporary ones as a best fit after, so the dragged part follows
  the mouse only as far as the constraints let it. A point is pulled to where it
  was plus how far the mouse moved (no jump when it was grabbed slightly off); a
  line pulls both ends; a circle's or arc's edge is pulled through the mouse
  while its center is held where it is by a second temporary constraint (as
  FreeCAD does), so the size changes, not the place. Every answer is checked
  with Khan's own `error()`, like solve(). It doesn't diagnose, so it's fast
  enough for every mouse move. The drop is one `EditSketchCommand` ("Move Line1",
  "Move Line2's end"), only if something moved.
- **Free or fixed (`SolveResult::freePoints`, `freeEntities`).** After the
  diagnose step, PlaneGCS's *dependent* parameters are the numbers the
  constraints don't set. A point is free if its x or y is; an entity is free if
  any of its numbers is (a point, a radius, an angle). With no constraints at all
  (an empty diagnose matrix) everything is free. Free is drawn blue, fixed black.
- **Viewer drags (`kh.viewer/2`).** `IViewTool` gained `dragged(from, to)` and
  `dropped(from, to)` (empty by default), and `ToolPoint::add` for Ctrl/Shift. A
  press that moves 6 pixels or more is a drag, never a click; the flat view now
  reports a click on release, like the 3D view. `/2` replaces `/1` rather than
  sitting next to it, since no release has shipped `/1`.
- **Delete.** The Delete key deletes picked parts while a sketch is edited: an
  item with a `toolbarMode` wins a shared shortcut while its mode is on.

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
