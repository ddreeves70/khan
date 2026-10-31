# Moving bodies

A **Move** step slides a body to a new place, turns it, or both. The body keeps
its size and shape. Like a Boolean, a Move uses up the body it moves: the moved
body is the one that's finished, and the original isn't drawn or exported any more.

![The keychain tag turned 30° and moved 40 mm along y; Properties shows the Move step's numbers](images/app-move.png)

## Example

Slide the [demo tag](../cli/demo.md) 40 mm along y:

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan feature.add_move body=Tag by=0,40,0
Demo tag: 4 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3
  Move1        ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.khan
```

The volume stays 7121.46 mm³: moving never changes a body's size.

## Turning

A Move can also turn the body, before sliding it. Three numbers say how:

| Setting | In Properties | Meaning |
| --- | --- | --- |
| `turn` | Turn | How far to turn, in degrees. Positive turns counterclockwise, looking down the axis (from its tip back toward the center) |
| `axis` | Axis | The direction of the line it turns about. `0,0,1` (the default) is straight up, so the body spins flat on the table |
| `about` | Center (mm) | A point on that line. Pick the middle of the body to turn it in place |

Turn the tag 30° about its middle (it's 60 × 30 mm, so its middle is at 30, 15):

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan feature.add_move body=Tag turn=30 about=30,15,0
Demo tag: 4 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3
  Move1        ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.khan
```

## Dragging a body

The quickest way to move a body is to drag it. Click a body in the 3D viewer to
pick it: three arrows and a small square appear in its middle.

![The demo tag picked, with its red, green and blue arrows and the purple square](images/app-drag-handles.png)

| Drag | And the body |
| --- | --- |
| The **red** arrow | slides along x (left and right) |
| The **green** arrow | slides along y (back and forth) |
| The **blue** arrow | slides along z (up and down) |
| The **purple** square | slides flat along the ground, in x and y at once |

The arrow under the mouse turns yellow, so you can see which one you'll grab.
Arrows are drawn over the body, so you can always grab them, even from inside.
The body moves as you drag, in steps of 1 mm (0.1 in in a design shown in
inches or feet), and the whole drag is one step of Edit > Undo. Press **Escape**
before you let go to put it back.

What a drag changes depends on the body:

- A **box** or **cylinder** gets a new corner or base, as if you'd typed it in
  Properties. A number that came from a [formula](parameters.md) is typed over.
- A **Move** step slides further.
- Any other body (an extrude, a union, a cut) gets a new **Move** step at the end
  of the design, which is then picked; dragging again moves that same step.

A body that a later step uses can't be dragged on its own (dragging the Hole out
of the Tag would leave the Tag behind): drag the finished body instead. You can
do the same from `kh_cli` with [`feature.slide`](../cli/commands.md#featureslide).
The arrows are a plugin too, `kh.movehandles`, built into Khan; a plugin of your
own can put [handles](../plugin-guide/extension-points.md#handles-to-drag-desktop-app-only)
in the viewer the same way.

## Moving by numbers in the desktop app

1. Pick a step in the feature tree.
2. **Modify > Move** (Ctrl+M), or the **Move** button. A `Move1` step is added and
   picked; it doesn't move anything yet.
3. In Properties, type how far in **Move (mm)**, and how far to **Turn**.

Change the numbers whenever you like: the design rebuilds, and Edit > Undo takes
a change back.

## Tips

- A Move of a step that's turned off is `blocked`, like any step that uses it.
- The moved body is made of what the original is made of, unless you pick a
  material for the Move step.
- To keep the original and move a copy, duplicate it first (Edit > Duplicate
  step), then move the copy.
- Turning about an axis of `0,0,0` doesn't work: there's no line to turn about.
  Give the axis a direction, like `0,0,1`.
