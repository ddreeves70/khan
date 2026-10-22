# SolidWorks-class scope

> **Draft.** This design changes as Khan is built. Questions and ideas are welcome in [GitHub Discussions](https://github.com/ddreeves70/khan/discussions).

Yes, the architecture can stretch to SolidWorks-class, but the project changes character on the way. It grows from a solo build into a much larger team effort. The education-first plan stays as the on-ramp. What changes now is a handful of cheap data-model and architecture decisions in Phases 0-2, so professional features can land later without a rewrite.

SolidWorks users are paying for depth and trust more than for features: configurations, very large assemblies, standard-part libraries, drawings that pass a customer's inspection, data management, and about thirty years of files and add-ins. Each of those is a multi-year workstream.

## Capability gap beyond Fusion parity

These are the areas professional SolidWorks users depend on that the current plan does not cover, ordered roughly by how often they decide a purchase.

| Capability | Why users depend on it | What it means for us | Stage |
| --- | --- | --- | --- |
| Configurations and design tables | One file holds a whole product family (sizes, variants) driven by a spreadsheet | Per-configuration parameter overrides and suppression in the core data model | 4 |
| Large assemblies | 10,000+ components with fast open, lightweight modes, large design review | Lightweight loading, instancing, multi-threaded rebuild, assembly-level caching | 4 |
| Rich mates | Gear, cam, path, width, limit and slot mates; mechanical motion | Full 3D assembly constraint solver, not just joints | 4 |
| Standard-part library | Fasteners, bearings, pins to ISO, ANSI, DIN, JIS in one drag | Parametric library format plus a community catalog | 3-4 |
| External references and top-down design | Parts edited in the context of an assembly; skeleton and layout sketches | Cross-file reference graph with out-of-date tracking | 4 |
| Weldments and structure system | Frames from standard profiles with cut lists | Profile library, trim and extend features, cut-list BOM | 4 |
| Advanced sheet metal | Lofted bends, forming tools, gauge tables, flat patterns for fabrication | Sheet-metal feature family with manufacturing exports | 3-4 |
| Advanced surfacing | Boundary, fill and freeform surfaces with G2/G3 continuity, zebra and curvature checks | Kernel-level surface quality; likely the hardest kernel gap | 5 |
| Mold tools | Draft analysis, parting lines, core and cavity split | Analysis tools plus split and shut-off features | 5 |
| Routing | Piping, tubing and electrical harnesses with routed BOMs | Route sketch engine, fitting libraries, harness flattening | 5 |
| Production drawings and MBD | ASME/ISO drawings that pass inspection; semantic GD&T in the 3D model | Standards-exact drafting engine; PMI data model exported to STEP AP242 | 4 |
| Simulation suite | Linear static through nonlinear, fatigue, thermal, frequency; CFD | More study types on open solvers; CFD via OpenFOAM out of process | 4-5 |
| Data management (PDM) | Vault, check-in/out, revisions, lifecycle, ECOs, where-used, ERP links | A PDM server with workflow engine and connectors | 4 |
| API and add-in ecosystem | Decades of macros and partner add-ins automate shops | Full automation API, macro recorder, stable plugin ABI, partner program | 3-5 |
| Multi-CAD interop | Opening SolidWorks, Inventor, Creo, NX files and keeping links | Native-format readers are licensed or reverse-engineered; STEP AP242 first | 4-5 |
| Ecosystem and trust | Certifications, resellers, training, support contracts | Training material, certification, support SLAs, long-term-support releases | 4-5 |

## Architecture changes

The layered design holds: the feature graph, kernel abstraction layer, command stack and open file format all scale up. Eight things change.

1. **Configurations in the data model.** A `Configuration` holds parameter overrides, feature suppression, appearance and custom properties. The rebuild key becomes (feature, configuration). Design tables bind a spreadsheet to configurations.
2. **Separate part and assembly documents with a cross-file reference graph.** Every document, feature and entity gets a global id (UUID). A workspace-level graph tracks external references, in-context features and skeleton models, and marks dependents out of date when a source file changes.
3. **Large-assembly engine.** Components load in three levels: graphics only (cached mesh), lightweight (B-rep on demand), and resolved (full feature graph). Rebuilds run in parallel across components; display uses instancing and level of detail.
4. **A real 3D assembly solver.** Replace the joint-only solver with a general 3D constraint solver for mates, limits, mechanical mates and motion. This is a major component; SolveSpace's solver is a useful starting point, but plan to build one that scales to thousands of mates.
5. **Kernel strategy for surfacing and robustness.** Keep the KAL and OCCT, and close the gap in the open: paid developers working on OCCT robustness and surfacing upstream, plus a long-term open kernel research track. A proprietary kernel is ruled out because it could not ship in a free product.
6. **Semantic PMI.** GD&T, datums and surface finish become model data, not drawing annotations, and export to STEP AP242 for model-based definition and CMM inspection.
7. **PDM as a service.** A vault server (self-hostable) with check-in/out, revisions, lifecycle states, change orders, where-used and ERP connectors, built on the versioning hub from Phase 7.
8. **API as a product.** Every command and query is generated from one registry into Python, C++ and a stable C ABI, with a macro recorder, so the add-in ecosystem can grow without breaking on each release.

Class model impact: add `Configuration`, `ConfigOverride`, `Workspace`, `ExternalRef`, `LoadState`, `MateSolver`, and a `Pmi` family; `GeometryRef` gains a document id.

## Stages

Stages 1-3 are the [roadmap](roadmap.md) (Phases 0-7). Stage 4 adds configurations, large assemblies, the 3D mate solver, weldments, production drawings with PMI, and PDM. Stage 5 adds advanced surfacing, mold tools, routing, CFD and multi-CAD import. A stage starts only when the previous one is in daily use by its target users.

## Decide now: cheap changes to Phases 0-2

None of these add features. Each costs days now and would cost months to retrofit.

- [ ] Use UUIDs for document, feature, entity and parameter ids from the first commit, and include the document id in `GeometryRef`.
- [ ] Make the rebuild cache key (feature id, input hash, configuration id), with a single default configuration in v0.1.
- [ ] Split part and assembly documents in the file format now, even if v0.1 only writes parts.
- [ ] Keep `IKernel` free of OCCT types and include surface-continuity queries (G1/G2 checks) so a second kernel can be added.
- [ ] Build the command registry so every command has a name, typed arguments and a script binding; generate the Python API from it.
- [ ] Store dimensions and annotations as model data with a hook for semantic PMI, not as drawing-only objects.
- [ ] Design the viewer around instancing and per-component meshes from the start.
- [ ] Keep the core license permissive (Apache-2.0) with a DCO sign-off, and register the project name as a trademark so only the official build can use it.

## Risks in the professional market

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Kernel quality ceiling (surfacing, fillets on complex parts) | Professionals hit failures SolidWorks handles | Paid, sustained OCCT robustness and surfacing work; long-term open kernel research |
| Legacy data lock-in | Shops cannot leave thousands of SolidWorks files | STEP AP242 with PMI first; licensed native readers; side-by-side workflows |
| Trust and longevity | Companies will not bet production on a project that might stall | Foundation or company backing, LTS releases, support contracts, public roadmap |
| Scope explosion | Ten workstreams, none finished | Stage gates: a stage starts only when the previous one is used daily by its target users |
| Incumbent response | Price cuts or free tiers for education and startups | Win on openness, data ownership, offline use and extensibility, which incumbents are slow to match |
| Patents and trade dress | Legal exposure from copying UI or algorithms too closely | Original UI design; patent review before implementing distinctive features |
