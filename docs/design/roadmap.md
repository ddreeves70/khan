# Roadmap

> **Draft.** This design changes as Khan is built. Questions and ideas are welcome in [GitHub Discussions](https://github.com/ddreeves70/khan/discussions).

Khan is built in phases, each ending with a build you can try and a written exit check. The first release, v0.1, is for classrooms: sketching, part modeling and export for 3D printing. Phases are listed in order without dates, because the pace depends on volunteer time. Full Fusion-class capability is a multi-year, multi-contributor effort, so the plan is built to welcome help after v0.1.

## Decisions

| Question | Decision | What it changes |
| --- | --- | --- |
| Team | Solo volunteer to start | Reuse mature libraries aggressively; ship small releases; recruit contributors after v0.1 |
| Core language | C++20 | OCCT, Qt 6, PlaneGCS, and Manifold all link natively; Python bindings via pybind11 |
| Platform | Desktop first | Windows, macOS, Linux. Web viewer deferred; Chromebooks reached later via Linux apps or a web build |
| Codebase | Clean architecture, borrow components | Own the document model, feature graph, and UI; reuse kernel, solver, and math libraries behind interfaces |
| First users | Education, then makers/CNC, then small shops | v0.1 scope is sketch + part + export + teaching material; assemblies and drawings next; CAM after |
| Plugins | Everything is a plugin, Khan's own features included | The app is a small shell; built-in plugins prove the API. Outside C++ plugins are built per release; most authors use Python |
| Docking | Qt Advanced Docking System | Every panel and viewer docks, tabs, floats and moves to another monitor; named layouts are saved per user |

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

License check: LGPL components must stay dynamically linked and replaceable, which fits an Apache-2.0 core.

## Phases

Each phase ends with a build you can try and a written exit check.

### Phase 0: Foundations

- Repo, CMake + vcpkg, CI building on Windows, macOS, Linux; Apache-2.0 license; contributor guide.
- Kernel Abstraction Layer skeleton: create a box, Boolean two shapes, tessellate, export STEP and STL.
- Document model + command framework with undo/redo; save and load the JSON container.
- Plugin shell: plugin host and manifests in a Qt-free `kh_plugin` library, event bus, shared selection, and Qt ADS docking with saved layouts. Box, cylinder and Booleans move into the `kh.modeling` plugin.
- Viewer as the first UI plugin (`kh.viewer`): 1, 2 or 4 panes with their own cameras, more than one viewer, orbit/pan/zoom, view cube, selection shared across views. A short spike first checks that Qt ADS builds in CI on all three systems.
- Plugin developer guide v0, in `docs/plugin-guide/` and published with the API reference: a Hello plugin tutorial, the manifest format and the extension points built so far. From here on, every pull request that changes the plugin API updates the guide (a CLAUDE.md rule).
- **Exit:** create a box, undo, save, reopen, export STL on all three OSes, with the box tool and the viewer loaded as plugins, and the guide's Hello plugin adding a working command.

### Phase 1: Sketcher

- PlaneGCS wrapped behind a solver interface; lines, arcs, circles, points, construction geometry.
- Core constraints and driving dimensions; degrees-of-freedom coloring; conflict highlighting.
- Auto-constraints while drawing, snapping, and closed-profile detection.
- **Exit:** a student can draw and fully constrain a bracket profile in under 5 minutes in a hallway test.

### Phase 2: Parametric core

- Parameter table with expressions and units.
- Feature graph with incremental recompute; timeline bar with rollback, edit, suppress.
- Features: extrude (new/join/cut), revolve, fillet, chamfer, hole, shell, mirror, linear and circular pattern, construction planes.
- Topological naming v1 (history-based names plus geometric fallback); ambiguous references flagged, never guessed.
- Geometry regression corpus started: every bug becomes a test model.
- **Exit:** 20 reference classroom parts model cleanly, and changing an upstream sketch dimension breaks none of them.

### Phase 3: v0.1 Education release

- Measure tool, materials/appearances, mass properties.
- Export STL and 3MF for 3D printing, STEP for interop; print-ready checks (watertight, minimum wall).
- Installers for all three OSes, auto-update check, crash reporter (opt-in).
- 5 starter lessons with sample files; in-app getting-started tour.
- **Gate:** one teacher runs a lesson with a class and the students finish it.

### Phase 4: v0.2 Assemblies and drawings

- Multi-component documents, joints (rigid, revolute, slider), interference check, exploded view.
- Drawings: base/projected/section views, dimensions, BOM, PDF and DXF export.
- More features: sweep, loft, draft, rib, thread.
- **Gate:** students complete a multi-part project from model to printed drawing.

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

## Education wedge

Schools need software that installs easily, runs on modest hardware, and comes with lessons. Those needs drive v0.1 scope more than features do.

- **Install and IT:** silent installer (MSI) for school-managed Windows machines, no account or internet required, per-machine install.
- **Hardware:** usable on integrated graphics and 8 GB RAM laptops; test on a low-end machine every release.
- **Learnability:** in-app tooltips with short animations, a guided first model, clear error messages that say how to fix the model.
- **Teaching material:** lessons mapped to common design and technology curricula, with sample and solution files, under a Creative Commons license.
- **3D printing path:** one-click export to the school's slicer; that is the outcome most classes want.
- **Chromebooks:** a known gap for desktop-first; plan for the web build or Linux app support later and say so on the website.
- **Partners:** teachers review each phase and help define the 20 reference parts in Phase 2.
