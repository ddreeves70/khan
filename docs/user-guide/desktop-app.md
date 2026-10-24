# The desktop app

`kh_app` is Khan's desktop app. You build a part by adding steps from the menus,
see every step in the feature tree, and change any step's numbers in Properties.
Everything you do can be undone.

The 3D view arrives in the next version. Until then the middle of the window
shows the grid from above, and lists your finished bodies and their volumes. You
can export a part to look at it in another program.

![The Khan window with the desk organizer open](images/app-main.png)

| # | Part | What it's for |
| --- | --- | --- |
| 1 | **Menus** | File (new, open, save), Edit (undo, redo, delete), Create (shapes), Combine (union, cut, intersect), View (the grid), Window (panels, layouts) |
| 2 | **Toolbar** | The most-used menu items, one click away |
| 3 | **Feature tree** | Every step of the design, in the order Khan builds them, with how each one built |
| 4 | **Viewer** | The 3D view (next version). For now: the grid seen from above, and the finished bodies with their volumes |
| 5 | **Properties** | The selected step's name and numbers. Change them here |
| 6 | **Status bar** | Messages, and what went wrong when something does |
| 7 | **Grid status** | The grid's spacing and style, and whether snapping is on |

## Starting it

After you build Khan, the app is in your build folder:

| System | Path |
| --- | --- |
| Windows | `build\win\apps\kh_app\kh_app.exe` |
| macOS | `build/dev/apps/kh_app/kh_app.app` |
| Linux | `build/dev/apps/kh_app/kh_app` |

It starts with an empty design called Untitled. To open a design straight away,
give its file: `kh_app tag.khan`.

## Make a part

The same keychain tag as [Tutorial 1](../tutorials/01-keychain-tag.md), with the
mouse:

1. **Create > Box.** Khan adds `Box1`, a 20 mm cube, and selects it.
2. In **Properties**, set the size to x 60, y 30, z 4, and change the name to
   `Plate`. Each change rebuilds the design; the volume in the middle updates.
3. **Create > Cylinder.** In Properties, set radius 2.5, height 6, base
   x 9, y 15, z -1, and name it `Hole`.
4. In the feature tree, click **Plate**, then hold Ctrl (⌘ on a Mac) and click
   **Hole**. The order matters: first the body to keep, then the cutting tool.
5. **Combine > Cut.** Khan adds `Cut1`: the plate with the hole cut out.
6. **File > Save**, and pick a name.

## The feature tree

Each row is one step: its name, its kind, and its status.

| Status | Meaning |
| --- | --- |
| `ok` | It built |
| `failed` | It couldn't be built. Rest the mouse on it, or look in Properties, to see why |
| `blocked` | It uses a step that failed |
| `off` | It's turned off |

- **Pick** a step by clicking it. Ctrl-click (⌘-click) picks more than one; Khan
  remembers the order you picked them in.
- **Rename** a step by double-clicking its name, or pressing F2.
- **Delete** a step with the Delete key, or right-click > Delete step. A step that
  another step uses can't be deleted until that step is.

Picking two steps turns on **Combine**: union, cut or intersect. For a cut, pick
the body to keep first.

![Plate and Hole picked: Union, Cut and Intersect are on](images/app-combine.png)

## Properties

Properties shows the picked step's name and numbers. Lengths are in mm.

![Properties for a cylinder](images/app-properties.png)

- Type a number and press Enter, or click elsewhere: the step changes and the
  design rebuilds. Edit > Undo puts it back.
- Holding an arrow key or rolling the mouse wheel changes a number in small
  steps. Quick changes to the same number undo as one step.
- **Base** and **Corner** are positions: x goes right, y goes back, z goes up
  (see [Shapes](shapes.md)).
- **Axis** is the direction a cylinder points: 0, 0, 1 is straight up.

When a step doesn't build, Properties says why in red, and how to fix it:

![A cylinder with no axis direction: failed, with the reason](images/app-failed.png)

## The grid

The grid is a pattern of dots or lines on the ground (the x-y plane) that helps
you judge sizes and line things up. It starts **on**, every **10 mm**, drawn as
**dots**, with **snapping off**. Change it in the **View** menu:

| Menu item | Keys | What it does |
| --- | --- | --- |
| View > Show grid | Ctrl+G | Shows or hides the grid |
| View > Snap to grid | Ctrl+Shift+G | Points you pick with the mouse jump to the nearest grid crossing |
| View > Grid style > Dots or Lines | | Dots are light and keep your part easy to see; lines look like graph paper |
| View > Grid spacing | | 1, 2, 5, 10, 20 or 50 mm, or **Other...** for any size from 0.01 mm to 1000 mm |

| Dots, 10 mm (the start) | Lines, 5 mm |
| --- | --- |
| ![The grid as dots, 10 mm apart](images/app-grid-dots.png) | ![The grid as lines, 5 mm apart](images/app-grid-lines.png) |

The red line is the x axis and the green line the y axis; they cross at the
origin, 0, 0. The status bar (7 in the picture at the top) always shows the
grid's settings, like "Grid 10 mm, dots, snap off". Khan remembers your grid
settings for next time.

- **When the grid is very fine,** Khan draws every 2nd, 5th or 10th line so it
  doesn't turn into a grey blur. Snapping still uses the spacing you set.
- **Snapping** matters once you place things with the mouse: in the 3D view and
  in sketches, both coming soon. Numbers you type in Properties are never
  snapped.

## Undo and redo

**Edit > Undo** (Ctrl+Z) takes back the last change, whatever made it: a menu
item, a renamed step, or a number typed in Properties. The menu item says what it
will undo, like "Undo Add Box1". **Edit > Redo** (Ctrl+Y or Ctrl+Shift+Z) puts it
back.

Opening a design starts a fresh undo history.

## Saving

| Menu item | Keys | What it does |
| --- | --- | --- |
| File > New | Ctrl+N | Starts an empty design |
| File > Open... | Ctrl+O | Opens a `.khan` design |
| File > Save | Ctrl+S | Saves to the design's file (asks for a name the first time) |
| File > Save as... | Ctrl+Shift+S | Saves under a new name |

A `*` after the name in the title bar means there are unsaved changes. Khan asks
before closing or replacing a design with unsaved changes.

Khan has one design open at a time for now. To export STL or STEP, use
[`kh_cli open --export`](../cli/open.md) until the File menu gets Export.

## Panels and layouts

Every panel can be moved:

- **Drag** a panel's tab to dock it on another side, or onto another panel to
  share its space as a tab.
- **Drag it out** of the window to make it a window of its own, for example on a
  second monitor.
- **Window** lists every panel: tick one to show it again after closing it.

Khan remembers where your panels were and puts them back next time. If a panel
was on a monitor that's no longer plugged in, it comes back onto your main screen.

**Window > Layouts** keeps named layouts:

- **Save layout as...** saves the current arrangement under a name.
- Pick a saved name to switch to it.
- **Reset layout** puts every panel back where it started.

## Plugins in the app

The feature tree, Properties, and the Create and Combine menus all come from
plugins that ship with Khan. Plugins you add (see [Plugins](plugins.md)) can add
menu items and panels too. Put plugin folders in a `plugins` folder next to
`kh_app`, or in Khan's folder in your user data:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\Khan\Khan\plugins` |
| macOS | `~/Library/Application Support/Khan/Khan/plugins` |
| Linux | `~/.local/share/Khan/Khan/plugins` |
