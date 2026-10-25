# The desktop app

`kh_app` is Khan's desktop app. You build a part by adding steps from the menus,
see it in 3D as it grows, see every step in the feature tree, and change any
step's numbers in Properties. Everything you do can be undone.

![The Khan window with the desk organizer open](images/app-main.png)

| # | Part | What it's for |
| --- | --- | --- |
| 1 | **Menus** | File (new, open, save), Edit (undo, redo, delete), Create (shapes), Combine (union, cut, intersect), View (the grid), Window (panels, layouts) |
| 2 | **Toolbar** | The most-used menu items, one click away |
| 3 | **Feature tree** | Every step of the design, in the order Khan builds them, with how each one built |
| 4 | **Viewer** | Your part in 3D, on the grid. Turn it with the mouse or the view cube, and click a body to pick it |
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
   `Plate`. Each change rebuilds the design, and the viewer shows the new plate.
3. **Create > Cylinder.** In Properties, set radius 2.5, height 6, base
   x 9, y 15, z -1, and name it `Hole`.
4. In the feature tree, click **Plate**, then hold Ctrl (⌘ on a Mac) and click
   **Hole**. The order matters: first the body to keep, then the cutting tool.
5. **Combine > Cut.** Khan adds `Cut1`: the plate with the hole cut out.
6. **File > Save**, and pick a name.

## The 3D viewer

The viewer in the middle of the window shows your finished bodies, sitting on the
grid. Steps used up by a later step (like the Hole that a Cut cut out) aren't
drawn, unless you pick them.

**Moving around** with the mouse:

| To | Do this |
| --- | --- |
| Orbit (turn around the part) | Drag with the left button |
| Pan (slide sideways) | Drag with the right or middle button, or Shift and the left button |
| Zoom | Roll the wheel; it zooms towards the mouse |
| Look from a side | Click a face of the **view cube** (top right). Click one of its edges or corners to look from in between |
| See everything | **Fit all** on the viewer's toolbar |

Z always stays pointing up while you orbit, so the floor stays the floor.

**Picking.** Click a body to pick its step: it turns orange, and the feature tree
and Properties show the same step. Ctrl-click (⌘-click) or Shift-click picks more
than one, or un-picks one. Clicking empty space un-picks everything. Picking works
both ways: rows you pick in the feature tree turn orange in the viewer too. A
picked step that a later step used up is drawn in orange so you can see where it
is, like the cutting tool of a Cut.

**Standard views.** The toolbar's **Front**, **Top**, **Right** and **Iso** buttons
turn the camera to look from that side and zoom to fit (**More views** has Back,
Left and Bottom). Front looks from -y towards +y, Top looks down, and Iso looks
from the front, right and above at once.

**Panes.** **Panes** on the toolbar splits the viewer into one pane, two side by
side, two stacked, or four. Each pane has its own camera. Four panes are set out
like a drawing: Top above Front, Right beside Front, and Iso in the corner. The
pane you clicked last has a blue frame, and the view buttons turn that pane;
**Fit all** fits every pane.

![Four panes: Top, Iso, Front and Right](images/app-viewer-panes.png)

**More viewers.** **Window > New viewer** opens another viewer next to the first,
with its own panes and cameras, for example to keep a close-up and the whole part
on screen at once. Drag its tab to move it, even onto another monitor. Extra
viewers close with their tab's x, and aren't reopened next time.

**No 3D graphics?** The viewer needs OpenGL 2.1 or newer, which almost every
computer has. If it can't start, the viewer says why and how to fix it (usually:
update the graphics driver), and shows the grid from above with a list of your
finished bodies instead:

![The viewer with 3D graphics turned off](images/app-viewer-flat.png)

If the 3D view crashes or draws garbage on your computer, you can turn it off:
set the environment variable `KH_NO_3D` to `1` before starting Khan (on Windows:
`set KH_NO_3D=1`, then `kh_app`).

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
- **Material** picks what the step's body is made of. Under it, Properties shows
  the body's volume, mass, weight and material cost. See [Materials](materials.md).

When a step doesn't build, Properties says why in red, and how to fix it:

![A cylinder with no axis direction: failed, with the reason](images/app-failed.png)

## The grid

The grid is a pattern of dots or lines on the ground (the x-y plane) that helps
you judge sizes and line things up. It starts **on**, every **10 mm**, drawn as
**lines**, with **snapping off**. Change it in the **View** menu:

| Menu item | Keys | What it does |
| --- | --- | --- |
| View > Show grid | Ctrl+G | Shows or hides the grid |
| View > Snap to grid | Ctrl+Shift+G | Points you pick with the mouse jump to the nearest grid crossing |
| View > Grid style > Lines or Dots | | Lines look like graph paper; dots are lighter and keep your part easy to see |
| View > Grid spacing | | 1, 2, 5, 10, 20 or 50 mm, or **Other...** for any size from 0.01 mm to 1000 mm |

| Lines, 10 mm (the start) | Dots, 5 mm |
| --- | --- |
| ![The grid as lines, 10 mm apart](images/app-grid-lines.png) | ![The grid as dots, 5 mm apart](images/app-grid-dots.png) |

The short coloured lines at the origin (0, 0, 0) are the axes: red is x, green
is y and blue is z. The status bar (7 in the picture at the top) always shows the
grid's settings, like "Grid 10 mm, lines, snap off". Khan remembers your grid
settings for next time.

- **How far the grid reaches:** 50 lines each way from the origin (500 mm with
  10 mm spacing), but at least 100 mm and never more than 100 lines, so a fine
  grid stays quick to draw.
- **Snapping** matters once you place things with the mouse, in sketches (coming
  soon). Numbers you type in Properties are never snapped.
- **The grid is a plugin,** `kh.grid`, built into Khan. That's why it isn't in
  `kh_cli`: there's nothing to draw it on there.

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

The viewer, the feature tree, Properties, and the Create and Combine menus all
come from plugins that ship with Khan. Plugins you add (see [Plugins](plugins.md)) can add
menu items and panels too. Put plugin folders in a `plugins` folder next to
`kh_app`, or in Khan's folder in your user data:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\Khan\Khan\plugins` |
| macOS | `~/Library/Application Support/Khan/Khan/plugins` |
| Linux | `~/.local/share/Khan/Khan/plugins` |
