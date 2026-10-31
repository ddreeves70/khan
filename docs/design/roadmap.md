# Roadmap

> **Draft.** This design changes as Khan is built. Questions and ideas are welcome in [GitHub Discussions](https://github.com/ddreeves70/khan/discussions).

Khan is built in phases, each ending with a build you can try and a written exit check. Capability comes first: the plan takes the fastest path to a fully functional modeler, and the classroom material comes after it. The first release, v0.1, is a working modeler: sketching, parametric part modeling, import and export. Phases are listed in order without dates, because the pace depends on volunteer time. Full Fusion-class capability is a multi-year, multi-contributor effort, so the plan is built to welcome help after v0.1.

## Decisions

| Question | Decision | What it changes |
| --- | --- | --- |
| Team | Solo volunteer to start | Reuse mature libraries aggressively; ship small releases; recruit contributors after v0.1 |
| Core language | C++20 | OCCT, Qt 6, PlaneGCS, and Manifold all link natively; Python bindings via pybind11 |
| Platform | Desktop first | Windows, macOS, Linux. Web viewer deferred; Chromebooks reached later via Linux apps or a web build |
| Codebase | Clean architecture, borrow components | Own the document model, feature graph, and UI; reuse kernel, solver, and math libraries behind interfaces |
| First users | Anyone who designs parts: makers, students, CNC users, small shops | v0.1 scope is sketch + parametric part + import and export; assemblies and drawings next; CAM and the Education pack after |
| Plugins | Everything is a plugin, Khan's own features included | The app is a small shell; built-in plugins prove the API. Outside C++ plugins are built per release; most authors use Python |
| Docking | Qt Advanced Docking System | Every panel and viewer docks, tabs, floats and moves to another monitor; named layouts are saved per user |
| Priority | Capability first (Oct 23, 2026) | Every phase takes the shortest path to a fully functional modeler. Teacher interviews, lessons and classroom features move to an Education pack after v0.2 |
| Guides | User's guide, CLI reference and tutorials grow with the code (Oct 23, 2026) | Every pull request that changes something a user sees updates them; their examples run as tests |
| Strength simulation | CalculiX runs as a separate program, never built in (Oct 24, 2026) | Khan stays Apache-2.0; Khan writes CalculiX's input file and reads its results. See [dependencies](../dependencies.md) |

**One deliberate shortcut:** use OCCT's built-in 3D viewer (AIS/V3d) for the first releases instead of writing the WebGPU renderer from the design. It saves months for a solo developer. Keep rendering behind an interface so it can be replaced later.

## Stack: reuse vs build

Build only what makes this product different: the document model, feature graph, naming, and UX. Everything else is borrowed behind an interface.

| Area | Use | License | Build in-house? |
| --- | --- | --- | --- |
| B-rep kernel | OCCT | LGPL 2.1 + exception | No, wrap in the KAL |
| Sketch solver | PlaneGCS, extracted from FreeCAD | LGPL 2.1+ | No at first; wrap so it can be replaced |
| Mesh Booleans | Manifold | Apache-2.0 | No |
| 2D offsets (later CAM) | Clipper2 | Boost | No |
| Linear algebra | Eigen | MPL 2.0 | No |
| UI | Qt 6 Widgets (dynamically linked) | LGPL 3 | Shell and panels are ours |
| Docking and layouts | Qt Advanced Docking System | LGPL 2.1 | No; wrapped in DockLayout |
| 3D viewer (early releases) | OCCT AIS/V3d inside Qt | LGPL 2.1 + exception | Later, replaced by own renderer |
| Scripting | Python embedded via pybind11 | BSD | API design is ours |
| File format | Zip container + JSON (nlohmann/json, libzip) | MIT / BSD | Schema is ours |
| Build and deps | CMake + vcpkg, GitHub Actions on 3 OSes | n/a | n/a |
| Tests | Catch2 + geometry regression corpus | BSL-1.0 | Corpus is ours |
| Document model, feature graph, naming, timeline, commands/undo | n/a | Apache-2.0 (project license) | Yes, the core of the product |

License check: LGPL components must stay dynamically linked and replaceable, which fits an Apache-2.0 core. The running list of every dependency, with versions, status and plans, is [docs/dependencies.md](../dependencies.md).

## Phases

Each phase ends with a build you can try and a written exit check.

### Phase 0: Foundations

- Repo, CMake + vcpkg, CI building on Windows, macOS, Linux; Apache-2.0 license; contributor guide.
- Kernel Abstraction Layer skeleton: create a box, Boolean two shapes, tessellate, export STEP and STL.
- Document model + command framework with undo/redo; save and load the JSON container.
- Plugin shell: plugin host and manifests in a Qt-free `kh_plugin` library, event bus, shared selection, and Qt ADS docking with saved layouts. Box, cylinder and Booleans move into the `kh.modeling` plugin.
- Viewer as the first UI plugin (`kh.viewer`): 1, 2 or 4 panes with their own cameras, more than one viewer, orbit/pan/zoom, view cube, selection shared across views. A short spike first checks that Qt ADS builds in CI on all three systems.
- Plugin developer guide v0, in `docs/plugin-guide/` and published with the API reference: a Hello plugin tutorial, the manifest format and the extension points built so far. From here on, every pull request that changes the plugin API updates the guide (a CLAUDE.md rule).
- **Exit:** create a box, undo, save, reopen, export STL on all three OSes, with the box tool and the viewer loaded as plugins, and the guide's Hello plugin adding a working command. The checklist, with the tests that cover it and the steps to check by hand: [Phase 0 exit check](phase0-exit.md). **Done Oct 27, 2026:** checked on Windows, macOS and Linux.

**Pulled forward after Phase 0 (Oct 27 to 28, 2026):** a few small pieces of later
phases, asked for once Phase 0 was done (see [Copying and grouping steps](copy-and-group.md)):

- Turning steps off, several at once, as one undo (from Phase 2's timeline "suppress").
- Folders in the feature tree (from the timeline "groups" in the architecture).
- Duplicate, Copy and Paste steps, between designs too (not on the roadmap before).
- A Move step that slides and turns a body (not on the roadmap before; Phase 2's
  mirror and patterns build on it).
- Dragging a picked body with arrow handles in the 3D view (asked for Oct 31,
  2026; built then, with `feature.slide`).

Phase 1 started Oct 28, 2026.

### Phase 1: Sketcher

- PlaneGCS wrapped behind a solver interface; lines, arcs, circles, points, construction geometry. (Solver built Oct 28, 2026: `kh_sketch`, `ISketchSolver`, PlaneGCS as a shared library. Sketch steps with commands, saved in files and solved on rebuild: built Oct 28, 2026. Drawing sketches with the mouse in the app: built Oct 28, 2026. Picking and dragging sketch geometry, colored by what can still move: built Oct 28, 2026. Constraints and dimensions from the toolbar, typed in the Sketch panel, labeled on the drawing, with conflicts in red: built Oct 29, 2026. See [The sketcher](sketcher.md).)
- Core constraints and driving dimensions; degrees-of-freedom coloring; conflict highlighting.
- Auto-constraints while drawing, snapping, and closed-profile detection. (Built Oct 29, 2026: On, Horizontal, Vertical and Tangent added while drawing, with a hint and an off switch; closed shapes with holes, areas and open ends, in `kh_sketch`. See [The sketcher](sketcher.md).)
- **Full B-spline support,** after lines, arcs and circles work (early Phase 2 if Phase 1 runs long). (Started Oct 30, 2026: splines in sketches, solved, saved, made by `sketch.add_spline` and extruded; their control points dragged in the app. Oct 31: drawn by control points or through points, with degree, weights and control points changed in the Sketch panel; points on splines, and tangent and smooth (G2) joins; imported from a file of points, with Reload from file. See [The sketcher](sketcher.md), step 7.) Spline constraints are where a solver most easily fails, so they come once the basic sketcher is solid:
  - Drawing by control points and by fit points (the curve passes through the points clicked).
  - Editing: drag points, add and remove points, change degree and weights, with the rest of the sketch staying solved.
  - Constraints: end on a line or arc, tangent (G1) and curvature-continuous (G2) where curves meet, point on spline, dimensions on control and fit points.
  - Degrees of freedom and conflicts reported for splines like any other geometry, so a spline can be fully constrained.
  - Closed profiles with spline edges extrude and revolve like any other profile (OCCT already handles B-splines in 3D).
  - Import a spline from a file of points, with Reload from file.
  - PlaneGCS already represents B-splines and has a few constraints; we add the rest in our extracted copy, behind `ISolver`, and offer the changes back to FreeCAD (LGPL requires publishing them anyway, and upstream keeps them maintained).
  - **Spline exit:** every item above works in the app, each spline constraint has a property test (really tangent, really through its fit points), and tricky splines are in the regression corpus. (Met Oct 31, 2026: 9 spline cases, weighted, degree 1 and 5, closed, joined tangentially, cut as a pocket, standing up and turned, each checked against values worked out without Khan. That found and fixed wrong surface areas on spline walls. Revolve comes with Phase 2.)
- **Exit:** a new user can draw and fully constrain a bracket profile in under 5 minutes in a hallway test. (Bracket tools built Oct 31, 2026: Rectangle, rounding corners, Trim and Symmetric. Next: the hallway test itself, with a new user and a stopwatch. See [The sketcher](sketcher.md), step 8.)

### Phase 2: Parametric core

Phase 2 started Oct 31, 2026, with the parameter table.

- Parameter table with expressions and units. (Started Oct 31, 2026: named parameters with formulas, units checked, worked out in the order they use each other, with loops and breaking changes refused; `parameter.set`, `parameter.rename` and `parameter.delete`; saved in `parameters.json`. Sketch dimensions that use them, following every change: built Oct 31, 2026. Steps' numbers (an extrude's distance, a box's size, a move's angle) that use them, with `feature.set_value`: built Oct 31, 2026. Next: a Parameters panel in the app, with formulas typed into its number boxes. See the [user's guide](../user-guide/parameters.md).)
- Feature graph with incremental recompute; timeline bar with rollback, edit, suppress.
- Features: extrude (new/join/cut), revolve, fillet, chamfer, hole, shell, mirror, linear and circular pattern, construction planes. (Extrude, new body only, one side, flipped or both: built Oct 29, 2026, as the sketcher's step 6; join and cut go through Combine for now. See [The sketcher](sketcher.md).)
- Linked copies: a copy that follows its original when it changes (a Move that keeps its input), built with the patterns.
- Topological naming v1 (history-based names plus geometric fallback); ambiguous references flagged, never guessed.
- Geometry regression corpus started: every bug becomes a test model.
- **Exit:** 20 reference parts (brackets, enclosures, knobs, mounts) model cleanly, and changing an upstream sketch dimension breaks none of them.

### Phase 3: v0.1 Functional modeler release

- Measure tool, materials/appearances, mass properties. (Materials with density, color, strength numbers and price, per body, and mass, weight and cost: built Oct 24, 2026.)
- Export STL and 3MF for 3D printing, STEP for interop; print-ready checks (watertight, minimum wall).
- Installers for all three OSes, auto-update check, crash reporter (opt-in).
- STEP, STL and 3MF import; sample parts; the user's guide, CLI reference and tutorials cover every feature so far.
- **Gate:** the 20 reference parts are modeled end to end in the desktop app on all three systems, and someone outside the project models a part of their own using only the user's guide.

### Phase 4: v0.2 Assemblies and drawings

- Multi-component documents, joints (rigid, revolute, slider), interference check, exploded view.
- Groups of bodies that move together, as parts placed in an assembly (needs its own design page first).
- Drawings: base/projected/section views, dimensions, BOM, PDF and DXF export.
- More features: sweep, loft, draft, rib, thread.
- **Gate:** a multi-part project goes from model to printed drawing.

### Phase 5: API and community (alongside Phase 4)

- Python API and console, script-defined custom features, plugin manifest.
- Public roadmap, good-first-issue labels, architecture docs.
- **Exit:** at least 3 regular outside contributors.

### Phase 6: v0.3 Makers and hobby CNC

- Mesh workspace (import, repair, Booleans via Manifold), direct editing (push/pull).
- CAM 2.5D: setups, facing, contour, pocket, adaptive clearing, drilling, Grbl and LinuxCNC posts, stock simulation.
- **Gate:** a community member cuts a part on a hobby CNC from a toolpath made in the app.

### Phase 7: v0.4+ Small engineering shops

- STEP assemblies with structure, sheet metal, GD&T on drawings, static FEA via CalculiX.
- Version history with visual diff, optional self-hosted hub.
- 3-axis CAM, more post-processors.

### Beyond Phase 7: professional and SolidWorks-class

Configurations, large assemblies, PDM, advanced surfacing and the rest are planned as Stages 4 and 5 in [SolidWorks-class scope](solidworks-scope.md). Its "Decide now" checklist adds a few small tasks to Phases 0-2.

## Guides built as we go

Four guides live in the repo beside the code. Every pull request that changes something a user sees updates them in the same change, and their examples run as tests.

| Guide | Folder | For | Kept current by |
| --- | --- | --- | --- |
| User's guide | `docs/user-guide/` | Anyone using Khan; illustrated with pictures rendered from real designs (app screenshots once the app exists) | A script that regenerates every picture from its design |
| CLI reference | `docs/cli/` | Scripts, CI, power users; every `kh_cli` command and modeling command with examples | Each example runs as a test (`docs.guides`) |
| Tutorials | `docs/tutorials/` | New users; step-by-step projects that build a printable part | Each tutorial's commands run as a test |
| Plugin developer guide | `docs/plugin-guide/` | Plugin authors | The Hello plugin is built and loaded by a test |

## Education pack (after v0.2)

Moved after v0.2 on Oct 23, 2026, so capability comes first. Schools still need software that installs easily, runs on modest hardware, and comes with lessons; these needs shape the Education pack, and the hardware and install items still apply to every release.

- **Install and IT:** silent installer (MSI) for school-managed Windows machines, no account or internet required, per-machine install.
- **Hardware:** usable on integrated graphics and 8 GB RAM laptops; test on a low-end machine every release.
- **Learnability:** in-app tooltips with short animations, a guided first model, clear error messages that say how to fix the model.
- **Teaching material:** lessons mapped to common design and technology curricula, with sample and solution files, under a Creative Commons license.
- **3D printing path:** one-click export to the school's slicer; that is the outcome most classes want.
- **Chromebooks:** a known gap for desktop-first; plan for the web build or Linux app support later and say so on the website.
- **Partners:** when the Education pack starts, find one or two teachers to review the lessons and the classroom flow.
