# The desktop app

`kh_app` is Khan's desktop app. You build a part by adding steps from the menus,
see it in 3D as it grows, see every step in the feature tree, and change any
step's numbers in Properties. Everything you do can be undone.

![The Khan window with the desk organizer open](images/app-main.png)

| # | Part | What it's for |
| --- | --- | --- |
| 1 | **Menus** | File (new, open, save), Edit (undo, redo, copy, paste, duplicate, turn off and on, folders, delete), Create (shapes), Combine (union, cut, intersect), Modify (move), Sketch (new, edit, draw), View (the grid), Window (panels, layouts) |
| 2 | **Toolbar** | The most-used menu items, one click away, each with a picture and its name. While you edit a sketch it shows the sketch tools instead, plus the constraint buttons that fit what you picked. If they don't all fit across the window, the rest go on a second row |
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

- **Turn off** steps with Edit > Turn off, or right-click > Turn off. A step
  that's off isn't built, but it isn't deleted: its numbers are kept, and Edit >
  Turn on (or right-click > Turn on) brings it back. Every picked step changes
  at once, as one Undo. A step that uses a step that's off shows `blocked`.
- **Put steps in a folder** with Edit > New folder. See [Folders](#folders).
- **Duplicate** a step with Edit > Duplicate step (Ctrl+D). See
  [Copying steps](#copying-steps).

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

The status bar (7 in the picture at the top) always shows the
grid's settings, like "Grid 10 mm, lines, snap off". Khan remembers your grid
settings for next time, and they're also in the [View options](#view-options)
panel, where Grid spacing takes any size.

- **How far the grid reaches:** 50 lines each way from the origin (500 mm with
  10 mm spacing), but at least 100 mm and never more than 100 lines, so a fine
  grid stays quick to draw.
- **Snapping** matters once you place things with the mouse, in sketches (coming
  soon). Numbers you type in Properties are never snapped.
- **The grid is a plugin,** `kh.grid`, built into Khan. That's why it isn't in
  `kh_cli`: there's nothing to draw it on there.

## The axes

The short coloured lines at the origin (0, 0, 0) are the **axes**, each 25 mm
long and labeled at its end: red **X**, green **Y** and blue **Z**. They show
which way is which, so you can tell a box's width (x) from its depth (y) and
height (z). Both start **on**:

| Menu item | What it does |
| --- | --- |
| View > Show axes | Shows or hides the three axes (and their letters) |
| View > Show axis labels | Shows or hides just the letters X, Y and Z |

Khan remembers both for next time, and both are also in the
[View options](#view-options) panel. The letters always face you and stay the
same size as you zoom. The axes are a plugin too, `kh.axes`, built into Khan.

## View options

Everything the viewer can show, the grid and the axes so far, is also in one
place: **View > View options...** opens the **View options** panel. It lists every
option, grouped, and changing one there is the same as using the View menu: the
menu's check marks follow, and Khan remembers it.

![The View options panel](images/app-view-options.png)

- The panel starts closed. Close it again with its **x**, or open and close it from
  the **Window** menu. Like any panel, you can dock it anywhere.
- **Grid spacing** takes any size from 0.01 mm to 1000 mm, not just the menu's
  sizes. Type it and press Enter.

### One pane only

Usually a view option changes **every pane** of every viewer. Sometimes you want
one pane to be different: a fine 2 mm grid in the 3D pane only, say, or no axis
letters in the top view.

- **Right-click in a pane** (click without dragging; a drag pans) for its **This
  pane only** menu: the same options, with that pane's check marks. Changing one
  there changes that pane alone.
- Or, in the **View options** panel, pick the pane in **Show options for**. A value
  the pane has of its own is in **bold**, with a round-arrow button that puts it
  back to the all-panes value.
- A pane with its own options shows a yellow **Own view options** strip at its top.
  **Same as all panes** puts every option in that pane back.

![Four panes: the top right one has its own finer grid and no axis letters](images/app-pane-options.png)

Things to know:

- **A change for all panes wins.** Changing an option from the View menu, or with
  **All panes** in the panel, applies to every pane, including one that had its
  own value for that option.
- **New panes start like all the others.** Switching from 1 to 4 panes gives the
  new panes the all-panes options. Going back to fewer panes forgets the own
  options of the panes that close.
- Khan remembers each pane's own options for next time.
- The status bar's grid text shows the grid of the pane you clicked last.

## Folders

A folder holds steps that are next to each other in the feature tree, so a long
tree is easier to read. Folders only keep the tree tidy: they never change what's
built.

![The Tag parts folder is picked, so Plate and Hole are picked too; the Spare folder's steps are all off](images/app-folders.png)

- **Make one:** pick the steps (Ctrl-click or Shift-click), then Edit > New folder,
  or right-click > New folder. It's called `Folder1`, `Folder2`, ...
- **Rename it:** double-click its name, or press F2.
- **Open and close it** with the arrow next to its name.
- **Pick the folder** to pick all its steps. Then Edit > Turn off turns them all off.
- **Right-click a folder** for Rename folder, Turn off (or Turn on), and Remove
  folder. Removing a folder keeps its steps.

The **Status** of a folder is a problem inside it (`failed` or `blocked`), or
`off` if all its steps are off.

Example: in the keychain tag, pick `Plate` and `Hole`, then Edit > New folder, and
rename it `Tag parts`. `Tag` stays outside, under the folder.

Tips:

- Only steps with no other steps between them can share a folder, and a step can
  be in only one folder.
- New steps go at the end of the tree, outside any folder.
- Deleting all of a folder's steps removes the folder too. Undo brings both back.

## Copying steps

Copying makes new steps with the same sizes and positions as the ones you picked.
Copying a step also copies every step it uses, so a copied cut still has its
hole: copying `Tag` (which cuts `Hole` out of `Plate`) gives `Plate1`, `Hole1`
and `Tag1`. The copies are separate: changing one never changes the other.

- **Edit > Duplicate step** (Ctrl+D) copies the picked step into the same design,
  at the end of the feature tree.
- **Edit > Copy** (Ctrl+C) copies the picked steps. **Edit > Paste** (Ctrl+V) adds
  them to the end of the design that's open, which can be a different design, or
  the same one. Paste works as often as you like, and every paste is one Undo.
- A copied name that's taken gets the next free number: `Plate` becomes `Plate1`.
- Materials of your own come along. If the design you paste into already has a
  material with the same name, the steps use that one.

Example: make a 20 mm box, pick it, press Ctrl+C, then File > New and Ctrl+V. The
new design has `Box1`, 20 mm on each side.

Tips:

- The copies sit exactly on top of the originals, so the viewer looks the same.
  Pick a copy and use Modify > Move to put it somewhere else (see
  [Moving bodies](moving.md)).
- What you copy is plain text, so you can paste it into a message or a file and
  paste it back into Khan later.

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
| File > Export... | Ctrl+E | Writes the part as STL (for 3D printing) or STEP (for other CAD programs) |

A `*` after the name in the title bar means there are unsaved changes. Khan asks
before closing or replacing a design with unsaved changes. Khan has one design
open at a time for now.

## Exporting

**File > Export...** writes your part to a file another program can use: an
`.stl` file for your 3D printer's slicer, or a `.step` file for other CAD
programs. Pick the type in the box under the file name, or just end the name with
`.stl` or `.step`. The status bar says what was written, like "Exported Tag to
tag.stl".

Export writes one body:

- If you picked one step in the feature tree, that step's shape is exported
  (even one a later step used up, like the Hole of a Cut).
- Otherwise, the design's one finished body.
- If the design has two or more finished bodies and none is picked, Khan says so:
  pick the one you want in the feature tree, or join them with
  [Combine > Union](combining.md#union).

An exported file holds only the shape, so keep your `.khan` file too: it's the
one you can change later. More about the formats is on the
[Files](files.md#exporting) page.

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
