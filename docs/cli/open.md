# sketchbillet_cli open

Opens a saved design, rebuilds it, and prints every step with its volume, and
each finished body's mass and material. It can
also export the finished shape for 3D printing or another CAD program, or list a
sketch as the solver left it.

```text
sketchbillet_cli open <file.sb> [--export <out.stl|out.step> [--step <name>]]
sketchbillet_cli open <file.sb> --sketch <name>
```

| Option | Meaning |
| --- | --- |
| `<file.sb>` | The design to open |
| `--export <out.stl>` | Write the finished shape as an STL mesh, for slicers and 3D printing |
| `--export <out.step>` | Write the finished shape as a STEP file, for other CAD programs (`.stp` works too) |
| `--step <name>` | Export the shape of this step instead, even if later steps use it |
| `--sketch <name>` | Also list this sketch: each line, circle, arc, spline and point where the solver put it (in the design's unit, angles in degrees; a spline lists its degree and control points), each constraint (a dimension with a formula shows it after its number, like `40.00 mm = wall * 10`), then its closed shapes (with their areas) and any ends that aren't joined |

Each step shows one of these:

| Word | Meaning |
| --- | --- |
| `ok` | The step built. Its volume is shown; a finished body also shows its mass and [material](../user-guide/materials.md). A [sketch](../user-guide/sketches.md) shows what's in it and how free it still is |
| `failed` | The step couldn't be built. The line under it says why |
| `blocked` | The step uses another step that failed or is turned off |
| `off` | The step is turned off (suppressed) |

A step number that comes from a formula is listed under its step, like
`    distance = wall * 4  (12 mm)`. A design with
[parameters](../user-guide/parameters.md) lists them after its steps, each with its formula and, if that isn't just a number, what it works out
to. A parameter SketchBillet can't work out shows the problem under it.

## Examples

First make a design to open (see [`sketchbillet_cli demo`](demo.md)):

<!-- cli-example -->
```console
$ sketchbillet_cli demo tag.sb
wrote tag.sb
$ sketchbillet_cli open tag.sb
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
```

Export it for 3D printing:

<!-- cli-example -->
```console
$ sketchbillet_cli open tag.sb --export tag.stl
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.stl
```

Export just the hole's cylinder:

<!-- cli-example -->
```console
$ sketchbillet_cli open tag.sb --export hole.step --step Hole
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote hole.step
```

## More than one body

`--export` writes one body. A design with two separate bodies needs `--step`, or
a [`feature.add_boolean`](commands.md#featureadd_boolean) to join them:

<!-- cli-example exit=1 -->
```console
$ sketchbillet_cli run --save two.sb feature.add_box size=10,10,10
two: 1 step
  Box1         ok  volume 1000.00 mm^3  mass 1.24 g (PLA)
wrote two.sb
$ sketchbillet_cli run --open two.sb --save two.sb feature.add_box corner=20,0,0 size=5,5,5
two: 2 steps
  Box1         ok  volume 1000.00 mm^3  mass 1.24 g (PLA)
  Box2         ok  volume 125.00 mm^3  mass 0.15 g (PLA)
wrote two.sb
$ sketchbillet_cli open two.sb --export two.stl
two: 2 steps
  Box1         ok  volume 1000.00 mm^3  mass 1.24 g (PLA)
  Box2         ok  volume 125.00 mm^3  mass 0.15 g (PLA)
error: this design has 2 finished bodies, and export writes one. Combine them with feature.add_boolean, or pick one with --step <name>.
```

## A sketch, solved

A sketch is a flat drawing that the solver makes exact (see
[Sketches](../user-guide/sketches.md)). `--sketch` lists it as solved:

<!-- cli-example -->
```console
$ sketchbillet_cli run --save disc.sb feature.add_sketch
disc: 1 step
  Sketch1      ok  empty
wrote disc.sb
$ sketchbillet_cli run --open disc.sb --save disc.sb sketch.add_circle sketch=Sketch1 center=1,2 radius=3
disc: 1 step
  Sketch1      ok  1 circle, 0 constraints, free to move 3 ways
wrote disc.sb
$ sketchbillet_cli run --open disc.sb --save disc.sb sketch.constrain sketch=Sketch1 kind=diameter on=Circle1 value=10
disc: 1 step
  Sketch1      ok  1 circle, 1 constraint, free to move 2 ways
wrote disc.sb
$ sketchbillet_cli open disc.sb --sketch Sketch1
disc: 1 step
  Sketch1      ok  1 circle, 1 constraint, free to move 2 ways
Sketch1 (XY plane): free to move 2 ways
  Circle1        circle  center 1.00, 2.00  radius 5.00
  Diameter1      diameter            Circle1  10.00 mm
closed shapes: 1
  shape 1  Circle1  area 78.54 mm^2
```

Last come the sketch's **closed shapes**, which [extrude](../user-guide/extrude.md) turns into solids:
each shape's lines, arcs and circles in order around it, its area, and any holes
in it. Ends of lines and arcs that nothing else meets are listed as
`open ends`; a shape with an open end isn't closed yet. A `warning` says when
three or more pieces meet at one point, or when shapes cross, since SketchBillet can't
tell which closed shape those belong to.

A sketch whose constraints conflict is listed as drawn, with `(conflicts)` after
the constraints that fight.

## Files that can't be opened

<!-- cli-example exit=1 -->
```console
$ sketchbillet_cli open missing.sb
error [file.cant_read]: SketchBillet couldn't open missing.sb. Check that the file is still there and that you're allowed to open it. missing.sb
```

The [file format page](../file-format.md) lists every reason a file can be refused.
