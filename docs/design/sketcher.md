# The sketcher (Phase 1)

> **Draft.** Steps 1 (the solver), 2 (the Sketch step), 3 (drawing in the app), 4 (picking, dragging, constraints and dimensions in the app) and 5 (constraints added while drawing, closed shapes) are built. The rest is the plan, in the order it
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
| 4b. Constraints in the app | Add constraints and dimensions from the toolbar and by typing numbers; show them on the drawing; conflicts highlighted with a plain message | Built |
| 5. Auto-constraints and profiles | Horizontal, vertical, coincident and tangent added while drawing; closed loops found as profiles | Built |
| 6. Extrude | Profiles become solids (Phase 2 starts properly here). A: the kernel's faces and extrude; B: the Extrude step | Built |
| 7. B-splines | Full spline support, as the roadmap lists. A: splines in the model, solver, files and kernel, through to extrude (built); B: drawing them in the app; C: spline constraints; D: import from a file of points | A built |

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
  "Move Line2's end"), only if something moved. A dragged point that comes within
  8 pixels of a point of another entity is pulled exactly onto it, and the drop
  adds a Coincident too ("Join Line2's start to Line1's end").
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

## Constraints and dimensions in the app (step 4b)

- **Buttons that fit what's picked.** `plugins/sketcher/Constrain.cpp` sorts what's
  picked into points, lines and round things, and `plan(rule, picks, sketch)`
  gives the constraints a button adds, or nothing if it doesn't fit. Buttons have
  `ActionSpec::buttonWhenEnabled` (plugin API 16), so only fitting ones show, and
  the shell wraps the rest onto a second toolbar row when they don't fit across
  the window. Several lines picked get one constraint each (Horizontal) or one per
  pair with the first (Parallel, Equal), in one undo step; a constraint the sketch
  already has (either way round) is refused with its name.
- **Dimensions start at today's size.** Length, Diameter (circles), Radius (arcs),
  Distance, Angle (`direction(b) - direction(a)`, as `error()` measures it),
  Horizontal and Vertical distance are measured from the drawing, so adding one
  moves nothing; the Sketch panel puts the cursor in its number.
- **The Sketch panel** (`kh.sketcher.panel`, right side, opens when editing
  starts): the sketch's freedom or its problem, and every constraint with what
  it's on; dimensions have a number box that runs `sketch.set_value` on Enter.
  The change runs just after the box's Enter handler returns, because the rebuild
  makes the list again and deletes the box (a crash the conflict test found).
  Right-side panels now share the side as tabs.
- **Labels** are a Labels overlay: short words (H, V, //, 90°, =, T, On, Mid, Fix)
  and numbers with units, in characters every system font has. Each goes beside
  what it's on, on the side away from the middle of the sketch, a gap of 6% of
  the sketch's size away; one that would overlap another (as a box: about 0.6 of
  a letter's height per letter) moves further out, or down beside an upright
  line. A click picks points first, then edges, then labels within 12 pixels.
- **Conflicts and repeats.** The last solve's `conflicting` constraints are
  red, with what they're on; `redundant` ones are amber. The solver's plain
  message shows in the status bar and the panel. A conflicting sketch is drawn as
  it was drawn, since there's no solved shape.

## Constraints while drawing and closed shapes (step 5)

- **Where a click lands** (`Sketcher::pick`, `where`): a sketch point within 8
  pixels (Coincident), else a line, circle or arc within 8 pixels, projected onto
  it (PointOnLine or PointOnCircle), else the grid. For a line's end that landed
  on neither: off the end of an arc and within 6 pixels of the arc's direction,
  it's put exactly on that direction (Tangent: with the Coincident at the shared
  end, the solver keeps their directions equal there); else within 6 pixels of
  level or upright, it's made so (Horizontal or Vertical). Each shape and its
  constraints are one `EditSketchCommand`. A hint label ("H", "V", "T", "On")
  shows beside the mouse first. `Sketch > Add constraints while drawing` (the
  plugin setting `auto_constraints_off`) turns the edge snaps and alignments off;
  point snaps always happen.
- **Closed shapes (`kh/sketch/Profiles.h`, `findProfiles`).** From solved
  geometry, not constraints: line and arc ends within a tolerance (1e-6 mm) are
  joined into vertices. Ends that meet nothing are `openEnds`; three or more
  meeting at a vertex is a `sketch.junction` warning, and those pieces aren't
  walked (no guessing which loop is meant). Loops are walked through vertices
  that join exactly two pieces, then turned counterclockwise. Areas use Green's
  theorem per edge (lines: the shoelace term; arcs: the exact integral), so a
  slot's area is exact. A loop is inside another if every sample point of it is
  (even-odd test on the other's outline, points touching the outline ignored);
  mixed in and out is a `sketch.crossing` warning. Even depth is an outline,
  the loops directly inside are its holes (clockwise), and islands in holes are
  shapes of their own. Circles are one-edge loops; helper geometry and points
  are left out. Extrude (step 6) builds faces from these.
- **Showing them:** the Sketch panel counts shapes, holes and open ends; open
  ends are red dots on the drawing; `kh_cli open --sketch` lists each shape with
  its area in mm^2.
- **The second toolbar row** stays while a mode's buttons don't all fit, so
  picking never moves the view; while none of its buttons show, it says "Pick
  something in the view to see what you can add here."

## Extrude (step 6)

Two pull requests, so each is small enough to review line by line:

**A. The kernel** (`kh_kernel`, `kh_kernel_occt`):

- Value types, OCCT-free: `Point2`, `Plane` (origin, x and y directions; normal
  x × y), `Line2`, `Arc2` (counterclockwise from startAngle to endAngle, angles
  from the plane's x), `Circle2`, `Edge2` (a curve and `reversed`), `Loop2`
  (edges end to end), `Profile2` (an outline, counterclockwise, and holes,
  clockwise, as `findProfiles()` gives them).
- `IKernel::makeFace(profile, plane)`: each loop becomes an OCCT wire. Neighbor
  edges share one vertex put halfway between their ends, with its tolerance
  raised to cover the gap, so ends a hair apart (up to 1e-5 mm, as the solver
  leaves them) join; farther apart is "the outline isn't closed". Arcs are
  `gp_Circ` on the plane's axes, so their angles mean the same as in a sketch. The
  face is checked with `BRepCheck_Analyzer`.
- `IKernel::extrude(face, vector)`: `BRepPrimAPI_MakePrism`. A vector of 0, 0, 0,
  or one lying in the face, is refused in plain words.
- New operations go at the end of `IKernel`, as before.

**B. The Extrude step** (`kh_model`, `kh_io`, commands, `kh_cli`, the app):

- `ExtrudeFeature`: which Sketch step it uses (a `FeatureId`), a distance (mm),
  and a side: one side (along the sketch plane's normal), flipped, or both sides
  (half each way). Every closed shape of the sketch becomes a solid; several are
  joined into one body. It fails with a plain message when the sketch has no
  closed shape (naming its open ends) or didn't solve.
- Saved in the `.khan` file as a new step type; the command
  `feature.add_extrude sketch=Sketch1 distance=10 side=one|flip|both`; the
  modeling toolbar gets **Extrude** (with a sketch picked), Properties edits the
  distance and side, and a corpus case pins the geometry.
- Built as planned. The sketch's result carries its plane, so the extrude needs
  nothing but the sketch's id. Both sides moves the face back half the distance
  first. Two corpus cases (`extrude-plate-hole`, `extrude-slot-xz`) and
  `invalid-face-open` pin the geometry through the corpus runner's new `face` and
  `extrude` operations.
- Join and cut keep going through Combine for now; an Extrude option for them
  can come with the Phase 2 feature work.

## B-splines (step 7)

Four pull requests:

**A. Splines in the model, through to extrude** (`kh_sketch`, `kh_sketch_planegcs`,
`kh_kernel`, `kh_kernel_occt`, `kh_model`, `kh_io`, `kh_cli`; built):

- `sketch::SplineShape`: control points (poles), optional weights, a degree (1 to
  5) and closed or open. Knots aren't stored: they're spread evenly, clamped at
  the ends of an open spline (so it starts and ends at its first and last pole)
  and periodic for a closed one. Even knots cover the curves students draw and
  keep the file and the commands simple; custom knots can come with fit-point
  splines if they need them.
- Control points are points constraints can use: `PointRole::Pole` with
  `PointRef::index`, named `Spline1.pole1`, `.pole2`, ... (counting from 1 for
  students; the file stores the index from 0). An open spline's `.start` and
  `.end` are its first and last pole.
- Solving: each pole is a PlaneGCS point. No constraint uses the curve itself yet,
  so the curve isn't in PlaneGCS; step C adds PlaneGCS's `BSpline` for point on
  spline and tangency.
- `splinePoint()` and `splineSample()` evaluate the curve (de Boor, in homogeneous
  coordinates for weights). Closed shapes take spline edges; their area is
  integrated exactly (8-point Gauss-Legendre per knot span).
- `kernel::Spline2` becomes an OCCT `Geom_BSplineCurve` with the same knots. Mass
  properties now integrate to 1e-9 (volumes by Gauss-Kronrod): OCCT's quick
  default was off by up to 1e-3 on solids with spline faces.
- `sketch.add_spline poles=0,0/0,30/40,30/40,0 degree=3 closed=false`, with a new
  argument type for lists of points.
- The app shows splines, their control polygons and control points, and drags them.

**B. Drawing splines in the app** (`kh_sketch`, `kh_model`, the sketcher plugin; built):

- Two tools: **Spline** (clicks are control points) and **Fit spline** (the curve
  goes through the clicks). Click the last point again or press Esc to finish;
  click the first point to close the loop. Degree 3, or less with 2 or 3 points.
  Ends that snap onto sketch points get Coincident (every control point does, for
  the Spline tool).
- `splinePolesThrough()` interpolates with the same even knots: one control point
  per fit point, the curve passing through each at its control point's Greville
  point (open), or at evenly spaced places from the loop's start (closed). Both
  give a well-conditioned system (Schoenberg-Whitney), solved directly.
- Fit points aren't stored: they become control points when the spline is made.
  Keeping them (and editing a spline by its fit points) needs custom knots and a
  new file field, so it waits until students ask for it.
- Commands: `sketch.add_spline through=...`, `sketch.set_spline` (degree,
  closed), `sketch.set_weight`, `sketch.add_pole` and `sketch.remove_pole`. Adding
  or removing a control point renumbers the constraints on later ones, so they
  stay on the same points; removing one deletes the constraints on it.
- Degree, closed, weight and add/remove control point are in the Sketch panel
  (not Properties, which shows the step) while a spline or control point is picked.

**C. Spline constraints** (`kh_sketch`, `kh_sketch_planegcs`, `kh_model`, the sketcher plugin; built):

- Each spline is now also a PlaneGCS `BSpline` made of the same pole numbers, with
  fixed weights and knots (clamped or periodic, as Khan's).
- **Point on spline** (`point_on_spline`, the **On** button, and snapping while
  drawing): PlaneGCS's `PointOnBSpline`, with an extra unknown for where along the
  spline the point is, started at the nearest place (`nearestOnSpline()`).
- **Tangent at a join:** a spline's end joined (Coincident) to the end of a line,
  arc or spline. PlaneGCS's angle-via-point at the spline's own end, where it
  knows the spline's direction (from its last two poles), 0 or pi as now.
  `check()` asks for the join first, in plain words.
- **Smooth (G2)** (`smooth`, a new button): tangent, plus Khan's own PlaneGCS
  constraint `ConstraintSameCurvature`, in `kh_sketch_planegcs` (no change to the
  PlaneGCS copy): the difference of the two curvature vectors across the first
  curve's direction, with central-difference gradients. Curvature at a spline's
  end comes from its derivative control points (The NURBS Book, 3.3) in
  homogeneous coordinates, so weights count. The error is weighed as mm at 100 mm,
  without which PlaneGCS could stop early when a spline's bend has to turn around.
- **Dimensions on control points** already worked: they're points. Fit points
  aren't kept (see B), so they get no dimensions.
- Property tests: end derivatives against finite differences for degrees 1 to 5,
  weighted or not; every join's answer measured (directions parallel to 1e-6,
  curvature vectors equal to 1e-6, 1/radius for an arc, 0 for a line), with the
  degrees of freedom each takes away.

**D. Import a spline from a file of points, with Reload from file** (`kh_sketch`,
`kh_model`, `kh_io`, `kh_ui`, the sketcher plugin; built):

- `readPoints()` in `kh_sketch` reads the text (no file access): one point per
  line, split by commas, spaces, tabs or semicolons; a heading line skipped, `#`
  comments, a z left out; up to 500 points (the interpolation solve is dense).
  A bad line is named with its number and text.
- `SplineShape::source` remembers the file (its absolute path, UTF-8), whether
  the curve goes through the points, and their unit; saved as the spline's
  optional `source` key.
- `sketch.import_spline` and `sketch.reload_spline` read the file when the
  command is made, so undo and redo hold exactly those points and never read it
  again. Reload keeps the degree and weights and removes constraints on control
  points that are gone.
- `ui::UiRegistry::askOpenFile()` asks for the file, answered by tests through
  `AppUi::answerOpenFile`. The app takes the file's numbers to be in the design's
  unit; `kh_cli` takes `unit=` (mm by default).
- The path is kept as it was, not relative to the design: a design moved to
  another computer needs its file put in the same place, or importing again.
  Relative paths can come with assemblies, which will need the same thing.

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
