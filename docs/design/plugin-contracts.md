# Shared concepts between plugins

> **Agreed Oct 25, 2026** (warning list, configurable slow-start limit, built-in
> plugins treated like any other, tier names and plugin-defined tiers). Part of the
> [UI and plugin architecture](architecture.md#ui-and-plugin-architecture).

Some ideas are used by several plugins at once: the grid (drawn by viewers, used
to snap points by the sketcher, set from a menu), snapping, units, measure. If
every such idea needed a change to Khan's base libraries (`kh_core` to
`kh_plugin`, `kh_ui`), the base would grow with every feature, and every new
idea would wait for a base change.

This page describes how a **plugin can add a shared concept, and other plugins
can use it, with no change to the base.**

## The rule

**The base only gains new *kinds* of extension, never concepts.** A change that
teaches `kh_plugin` what a grid is has the wrong shape. A change that lets
plugins save settings has the right shape, and every later concept reuses it.

Today the grid breaks this rule: `Grid` lives in `kh_plugin`, and the shell owns
its View menu, status text and saved settings. The grid is the worked example
below, and it moves out first.

## Contracts

A shared concept is a **plugin that publishes a contract**: a small header with
an interface, its event types and a version, and no implementation. Other
plugins code against the contract, never against the plugin's code.

```cpp
// plugins/grid/include/kh/grid/Grid.h: the contract, shipped with kh.grid
namespace kh::grid {

/// Sent when the grid's settings change.
struct GridChanged {
    static constexpr const char* kType = "kh.grid.changed";  // starts with the plugin id
    double spacing = 10;  // mm
    bool visible = true;
    bool snap = false;
};

/// The grid, as other plugins see it.
class IGrid {
public:
    static constexpr const char* kServiceName = "kh.grid/1";  // version in the name
    virtual ~IGrid() = default;
    virtual double spacing() const = 0;                         // mm
    virtual kernel::Vec3 snap(kernel::Vec3 point) const = 0;    // unchanged when snapping is off
};

}  // namespace kh::grid
```

```cpp
// In kh.grid: offer the service during registration.
ctx.provide<kh::grid::IGrid>(grid_);

// In kh.sketcher, which lists kh.grid in "uses": look it up when started.
ctx.onStart([&ctx] {
    if (auto* grid = ctx.service<kh::grid::IGrid>()) { /* snap with it */ }
});
ctx.listen<kh::grid::GridChanged>([](const auto& e) { /* redraw */ });
```

- **Contracts are versioned in the service name.** A breaking change publishes
  `kh.grid/2` next to `kh.grid/1` for at least one release, so users have time
  to move. Adding a function at the end of an interface is not allowed (it
  changes the layout); add a new versioned interface instead.
- **Contracts are abstract classes plus plain structs.** No STL containers with
  allocation across the boundary in outside contracts, no exceptions, no Qt in
  model-layer contracts. Built-in and outside C++ plugins are built with the same
  compiler per release (see Binary compatibility), so this is about keeping
  contracts stable, not about mixing compilers.
- **Names start with the plugin id:** services `kh.grid/1`, events
  `kh.grid.changed`. Khan's own base events keep their current names.
- **Contracts ship in the SDK** as header-only CMake targets (`kh::grid_api`), so
  an outside plugin can use `kh.grid` without linking to it.

## What the base provides, once

| Mechanism | What it lets a plugin do | Today |
| --- | --- | --- |
| **Plugin services** | Offer an interface by versioned name (`ctx.provide`); others find it (`ctx.service<T>()`). The host records which plugin provided each service | Only the app can provide services |
| **Plugin events** | Define and publish its own event types | Works already (events are keyed by a string name); add the naming rule |
| **Two-phase startup** | Register first (offer services, events, commands, extension points), then start (look up and connect to others) | One phase: the register function |
| **`requires` and `uses`** | Hard dependencies, and optional ones the plugin works without | `requires` only |
| **Settings store** | Remembered settings per plugin (`ctx.settings()`), saved by the app (QSettings) and by `kh_cli` (none, or a file) | The shell saves the grid's settings itself |
| **Design data** | Plugin-owned data saved in the `.khan` file (`plugins/<id>.json`), changed only through that plugin's commands, so undo works | Not yet; built when a concept first needs it |
| **UI hooks** | Check-mark menu items bound to a setting, status-bar items | Plain menu items and panels only |
| **View overlays** | Things for a viewer to draw, as plain data: points and lines in mm, with a color | Not yet |
| **Plugin-defined extension points** | A contract that itself accepts contributions from other plugins | Comes with plugin services |

The last row is what stops the base from growing. Snapping is the example: a
`kh.snap` plugin offers a service that other plugins *add snap sources to*
(grid crossings from `kh.grid`, line ends and midpoints from the sketcher). The
sketcher asks `kh.snap` for the best point. A new kind of snap, even from an
outside plugin, needs no change to the base or the sketcher.

## Startup and dependencies

**Two phases.**

1. **Register:** each plugin offers what it has: services, events, commands,
   actions, panels, extension points. It shouldn't call other plugins yet.
2. **Start:** once every plugin has registered, each one's `onStart` callbacks
   run, in load order. Every service exists by now, so a plugin can look up and
   connect to any other.

Because services are looked up during Start, **load order rarely matters**:
`requires` mostly means "must be installed and working", not "must load first".

**Two kinds of dependency** in `plugin.json`:

| Key | Meaning | If it's missing or fails |
| --- | --- | --- |
| `requires` | "I can't work without it." | This plugin doesn't load, with a plain message (as today) |
| `uses` | "I'll use it if it's there." | This plugin loads anyway; `ctx.service<T>()` returns null and the plugin does without |

Most links should be `uses`, which keeps the graph loose and stops one failure
spreading.

**Load order.** Plugins load in dependency order (both kinds); plugins that are
ready at the same time load in id order, so the order is the same on every
computer.

**Loops are allowed, with a warning.** If plugins depend on each other in a loop
(A requires B, B requires A), Khan still loads them: two-phase startup means none
of them needs another during registration. Inside a loop they load in id order.
Khan warns, because a loop is almost always a mistake and the plugins should
probably be restructured (see "Breaking a loop" below), but it doesn't force it.
This replaces today's behavior, where every plugin in a loop fails.

If a plugin in a loop fails to register, every plugin that `requires` it (in the
loop or outside) doesn't load, as with any failed dependency; plugins that only
`use` it carry on.

## Warnings

During loading Khan watches for things that work but are probably not what the
author meant, and reports them as **warnings**: `Severity::Warning` diagnostics
on the plugin. Warnings never stop a plugin loading. They show in
`kh_cli plugins`, in the Plugin manager (a yellow mark on the plugin), and in the
app's log. Students aren't interrupted with pop-ups; plugin developers see them
where they look.

| Code | When | Message (plain words, says how to fix) |
| --- | --- | --- |
| `plugin.dependency_loop` | Plugins depend on each other in a loop | "Grid and Snap need each other (Grid → Snap → Grid). Khan loaded them anyway, but this is usually a mistake: move what both need into a third plugin, or have one use events instead." Names the whole loop, once per loop |
| `plugin.undeclared_dependency` | A plugin uses a service or listens to an event from a plugin it doesn't list in `requires` or `uses` | "Sketcher uses kh.grid/1 from Grid but doesn't list kh.grid in its plugin.json. Add it to \"uses\" so Khan loads them in the right order." |
| `plugin.early_lookup` | A plugin looks up another plugin's service during registration instead of in `onStart` | "Sketcher asked for kh.grid/1 while registering. That only works because of the load order; look it up in onStart instead." |
| `plugin.service_taken` | Two plugins provide the same service name | The first (in load order) keeps it; the second gets "Grid Pro tried to provide kh.grid/1, which Grid already provides. Khan kept Grid's." |
| `plugin.event_name` | A plugin publishes an event whose type doesn't start with its id | "Sam's cube sent the event cube.made. Event names must start with the plugin id, like sam.cube.made, so they don't clash." |
| `plugin.old_contract` | A plugin uses a contract version that has a newer one, or that the provider marks as going away | "Sketcher uses kh.grid/1; Grid also offers kh.grid/2. kh.grid/1 will be removed in a later version." |
| `plugin.uses_missing` | A `uses` plugin isn't installed | Info, not a warning: "Sketcher can use Grid, which isn't installed, so snapping to the grid is off." |
| `plugin.slow_start` | Register or start takes longer than the slow-start limit (500 ms unless changed, see below) | "Sam's cube took 1.2 s to start, which slows Khan's start. Do slow work later, when it's first needed." |
| `plugin.tier_order` | A plugin requires or uses a plugin in the same or a higher tier (see Tiers) | "Grid (Foundation) uses Viewer (UI). A plugin should only use plugins in lower tiers; move the shared part down or use events." |
| `plugin.tier_unknown`, `plugin.tier_conflict` | A plugin names a tier nobody defined, or defines a tier that clashes with another definition | "Sam's mill names the tier sam.machining, which no plugin defines, so its tier isn't checked." |

`kh_cli plugins --strict` turns warnings into failures, for plugin authors' CI.

**The slow-start limit is an application setting,** not a constant: 500 ms by
default, changed with the `plugins/slowStartMs` setting in the desktop app's
settings, or `--slow-start-ms <n>` for `kh_cli`. 0 turns the warning off. (A
slow school laptop might want 1500 ms; a plugin author's CI might want 200 ms.)

## Breaking a loop

When a loop warning appears, one of three patterns removes it:

| Pattern | How it breaks the loop | Example |
| --- | --- | --- |
| **Move the shared part down** | Put what both need into a third, lower plugin (or contract) that both depend on | Viewer and sketcher both need snapping: `kh.snap`, which both use and which uses neither |
| **Turn the arrow around with an extension point** | The lower plugin offers a slot; the higher one plugs into it. The lower one never knows who plugged in | `kh.snap` doesn't depend on the sketcher; the sketcher adds snap sources to `kh.snap` |
| **Talk back with events** | The lower plugin announces changes; anyone interested listens. Events create no dependency | The grid publishes `kh.grid.changed`; viewers listen; the grid doesn't know viewers exist |

## Tiers

**Built-in plugins get no special treatment.** They use only the public plugin
API, follow the same rules and get the same warnings as any other plugin. The
only differences are packaging: they ship with each release, and ids starting
with `kh.` are kept for them.

A plugin can say which **tier** it belongs to, in `plugin.json`:

```json
{ "id": "kh.grid", "tier": "foundation", "uses": ["kh.snap"] }
```

Tiers are ordered, lowest first, and a plugin should only `require` or `use`
plugins in lower tiers. Khan defines three:

| Tier | For | Khan's own plugins |
| --- | --- | --- |
| `foundation` | Shared concepts other plugins build on | `kh.modeling`, `kh.grid`, `kh.snap`, units |
| `tools` | Things the student does | sketcher, measure, later CAM |
| `ui` | Panels and views that show the design | `kh.viewer`, `kh.featuretree`, `kh.properties`, later the timeline |

A dependency into the same or a higher tier gets the `plugin.tier_order`
warning; a plugin with no tier isn't checked. Tiers never change the load order,
which comes from dependencies alone. They only point out dependencies that run
the wrong way.

**A plugin can add a tier,** placed between existing ones. For example, a
machining suite that wants its own layer between tools and the views:

```json
{
  "id": "sam.mill",
  "tiers": [{ "name": "sam.machining", "after": "tools", "before": "ui" }],
  "tier": "sam.machining"
}
```

- A new tier's name starts with the plugin id that defines it (`sam.machining`),
  so two plugins can't define the same name by accident. Khan's three keep their
  short names.
- `after` and `before` name existing tiers; at least one is needed. A plugin can
  only add tiers, never move or remove existing ones.
- Two tiers placed in the same gap (both between `tools` and `ui`) are treated as
  side by side: neither is above the other, and a dependency between them gets no
  tier warning.
- A tier whose placement contradicts the others (say `after: ui, before: tools`),
  or a name already defined, gets `plugin.tier_conflict`; Khan ignores that
  definition and carries on.
- A plugin naming a tier nobody defines gets `plugin.tier_unknown`, and its tier
  isn't checked.

**Khan's own CI** runs `kh_cli plugins --strict` on the built-in plugins, so for
them a loop or a tier warning fails the build. That's how Khan holds itself to
the rule; it's the same check every plugin gets, made strict.

## Worked example: the grid

| | Today | After |
| --- | --- | --- |
| Settings and snapping | `kh::plugin::Grid` in `kh_plugin`, provided by the app | `kh.grid` plugin; `IGrid` contract (`kh.grid/1`) |
| Remembering settings | The shell writes QSettings keys | `kh.grid` uses the settings store |
| View menu items | Built by the shell | Check-mark actions from `kh.grid`, bound to its settings |
| Status bar text | Built by the shell | A status-bar item from `kh.grid` |
| Drawing it | `kh.viewer` reads `GridChanged` and tells each pane | `kh.grid` adds a view overlay; the viewer draws overlays and knows nothing about grids |
| Snapping | `Grid::snap()` | `kh.grid` adds grid crossings as a snap source to `kh.snap` (Phase 1) |

After the move, the shell, `kh_plugin` and `kh.viewer` contain no grid code.

## What stays in the base

Concepts that every design and every plugin depends on stay in the core
libraries, because they are the model itself, not features on top of it:
documents, features, commands and undo, the selection, the kernel, and
**materials** (a body's mass, cost and later its strength depend on them, and
they're saved with each step). The test for "core or plugin?" is: could Khan
still be a working CAD program without it? If yes, it's a plugin.

## Building it

1. **Mechanisms** (plugin API version 4): plugin services, two-phase startup
   (`onStart`), `uses`, loops allowed with the warning, the other warnings,
   `--strict`, the configurable slow-start limit, the settings store, check-mark actions and status-bar items. The
   plugin guide gets a "Share something with other plugins" page with a worked
   example built and loaded by a test.
2. **The grid moves out:** view overlays, the `kh.grid` plugin, tiers in
   `plugin.json` (with plugin-defined tiers), and `kh_cli plugins --strict` on
   the built-in plugins in CI.
   The user's guide is unchanged: the grid looks and works the same.
3. **Later, when first needed:** design data, `kh.snap` (with the sketcher in
   Phase 1), plugin-defined extension points beyond snapping.

Other applications built this way: VS Code (an extension returns an API from
`activate()` for other extensions to use), Eclipse and OSGi (a service registry
with versioned bundles). FreeCAD is the counter-example: its workbenches import
each other's Python code directly, which tangles them.
