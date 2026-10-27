# View options

> **Agreed Oct 27, 2026:** one shared service for view options instead of each
> plugin managing its own; different values per pane are possible, but a change
> applies to every pane unless you choose "this pane only"; every option,
> including custom "Other..." values, can be set per pane; a new pane starts with
> the all-panes values.
> Builds on [Shared concepts between plugins](plugin-contracts.md).

## The problem

Each thing the viewer can show is set up by hand in its own plugin. `kh.grid`
and `kh.axes` each keep their own settings, build their own View menu check
items, and listen for their own changes. Every new view option (background,
edges, see-through, shadows, ...) would repeat that, and a student has no single
place to see what the view can do.

## The idea: options are declared, drawing stays in the plugins

Two jobs, split:

1. **Configuration** is shared. A `kh.viewoptions` plugin offers a service,
   `kh.viewoptions/1`. Plugins **declare** their options as plain data, and the
   service stores them, checks them, remembers them, tells everyone when they
   change, and builds the menus and the panel for them.
2. **Drawing and behaviour** stay in each plugin. `kh.grid` still snaps and
   draws its lattice; `kh.axes` still draws its lines and letters. They read
   their options from the service.

```cpp
// In kh.axes, while registering (it lists kh.viewoptions in "requires").
auto* options = ctx.service<kh::viewoptions::IViewOptions>();
options->add({.id = "kh.axes.labels", .text = "Show axis labels", .group = "Axes",
              .kind = OptionKind::Flag, .initial = true});

// In kh.grid.
options->add({.id = "kh.grid.spacing", .text = "Grid spacing", .group = "Grid",
              .kind = OptionKind::Length, .initial = 10.0, .min = 0.01, .max = 1000,
              .presets = {1, 2, 5, 10, 20, 50}});
options->add({.id = "kh.grid.style", .text = "Grid style", .group = "Grid",
              .kind = OptionKind::Choice, .initial = "lines",
              .choices = {{"lines", "Lines"}, {"dots", "Dots"}}});
```

Kinds to start with: **Flag** (on/off), **Choice** (one of a list), **Length**
(mm, with limits and presets) and **Number**. A Color kind comes when the first
option needs it.

This is how VS Code does it: extensions declare their settings as data, and one
Settings screen shows them all. It's also the "plugin-defined extension point"
pattern from the shared-concepts page: `kh.viewoptions` doesn't know what a grid
is, and new options never change it.

## Panes: one value for all, or one pane only

The viewer can be split into 1, 2 or 4 panes, and you can open more viewers.
Every option has:

- **An all-panes value** (the usual case), and
- **Optional per-pane values** that override it in one pane.

A pane uses its own value if it has one, otherwise the all-panes value.

**By default, a change applies to every pane.** Changing an option from the View
menu or the panel's "All panes" view sets the all-panes value **and clears that
option's per-pane values**, so it really does apply everywhere. For example,
turning on axis labels turns them on in every pane, including one that had them
off on its own.

**"This pane only"** is a separate, deliberate choice:

- **Right-click in a pane** gives a "This pane only" menu with the same options,
  with check marks showing *this pane's* values. Changing one sets a value for
  that pane alone.
- In the **View options panel**, a "Show options for" list at the top switches
  between **All panes** and each pane by name (for example "Viewer 1, Top"). Values
  set for that pane alone are marked, with a button to put each one back to the
  all-panes value.
- A pane that has any values of its own shows a small marker in its corner. Its
  tooltip lists them, and **Use the same as all panes** clears them all.

Per-pane values are remembered with the window layout (they belong to panes,
which come and go with layouts), and saved in named layouts too.

## How drawing follows per-pane values

Overlays are drawn by every pane today. To let a pane differ, an overlay can be
**for one pane**: a plugin sets an overlay with a pane's key, and that pane draws
it *instead of* the all-panes overlay with the same name. Other panes keep the
all-panes one.

So `kh.grid`, when the grid spacing differs in the Top pane, sets an all-panes
lattice and a Top-pane lattice. When the per-pane value is cleared, it removes the
Top-pane overlay. The viewer still knows nothing about grids.

Things that act on a pane, like snapping, ask with the pane:
`grid->snap(point, pane)` snaps to *that pane's* spacing. That's a new contract
version, `kh.grid/2`, offered next to `kh.grid/1` for a release.

## What the service offers

| Part | What it does |
| --- | --- |
| `add(option)` | Declares an option, while registering. Its id starts with the plugin's id |
| `value(id, pane)` | The value in a pane (its own, else the all-panes value); no pane: the all-panes value |
| `set(id, value)` | Sets the all-panes value and clears per-pane values (what the menu and panel do) |
| `set(id, value, pane)` | Sets one pane's value ("this pane only") |
| `reset(id, pane)` / `resetPane(pane)` | Puts one option, or all of a pane's options, back to the all-panes value |
| `kh.viewoptions.changed` event | Says which option changed, and for which pane (none: all panes) |
| Values are checked | A Length outside its limits, or an unknown Choice, is refused with a plain message, like the grid does today |

The service builds:

- **View menu items** for every option, grouped: check marks for Flags,
  submenus for Choices, and presets plus **Other...** for Lengths. This replaces
  the menu items `kh.grid` and `kh.axes` make by hand.
- **The pane's right-click "This pane only" menu**, with the same items.
- **A dockable "View options" panel**, like Properties: every option grouped,
  the "Show options for" list, and the per-pane markers and reset buttons.

`kh.viewoptions` is in the `foundation` tier, so `kh.grid` (foundation) and
`kh.axes` can require it. It adds its panel only in the desktop app; in `kh_cli`
it still stores options, so scripts can set them.

## What moves

| Today | After |
| --- | --- |
| `kh.grid` keeps visible, spacing, snap and style in its own settings | Four `kh.grid.*` options |
| `kh.grid` builds Show grid, Snap to grid, Grid style, Grid spacing by hand | Built by the service from the options |
| `kh.axes` keeps two settings and builds two menu items | Two `kh.axes.*` options |
| Every pane draws the same overlays | An overlay can be for one pane |
| `kh.grid/1`: `snap(point)` | `kh.grid/2` adds `snap(point, pane)`; `/1` stays for a release |

The grid's status-bar text stays in `kh.grid` and shows the active pane's values.

## Building it

1. `kh.viewoptions` with all-panes values: the service, options, events, the View
   menu built from options and the panel. `kh.grid` and `kh.axes` move onto it.
   For the student, nothing changes except the new panel.
2. Per-pane values: pane keys from the viewer, overlays for one pane, the
   right-click menu, panel scope, markers, saved with layouts, and `kh.grid/2`.

Each step updates the plugin guide (a new "Add view options" section replacing
the hand-made check-mark example for view settings) and the user guide.

## Decisions

- **"Other..." values** (like a 12.5 mm grid) can be set per pane too: every
  option works the same way.
- **A new pane** (switching from 1 to 4 panes, or a new viewer) starts with the
  all-panes values; a pane's own values are only ever set on purpose.
