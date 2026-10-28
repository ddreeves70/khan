# Sketches

A **sketch** is a flat drawing (lines, circles, arcs and points) on one of the
three main planes. **Constraints** say how the pieces relate (this line is
horizontal, these two ends meet) and **dimensions** give sizes (this line is
40 mm long). Khan's solver moves the drawing until every constraint and dimension
is true. Soon, extrude will turn a sketch's closed shapes into solid parts.

You don't have to draw exactly: draw roughly, then add constraints and
dimensions, and the solver makes it exact.

![A plate drawn roughly, then solved into an exact 40 × 20 mm rectangle with a hole](images/sketch-plate.png)

Drawing in the desktop app is coming next. Today you build sketches with
`kh_cli`; the desktop app shows them in the feature tree, and Properties shows how
free a sketch still is.

## Example

Make a sketch, draw a line, and pin it down: horizontal, starting where it is,
40 mm long.

<!-- cli-example -->
```console
$ kh_cli run --save line.khan feature.add_sketch
line: 1 step
  Sketch1      ok  empty
wrote line.khan
$ kh_cli run --open line.khan --save line.khan sketch.add_line sketch=Sketch1 from=0,0 to=38,3
line: 1 step
  Sketch1      ok  1 line, 0 constraints, free to move 4 ways
wrote line.khan
$ kh_cli run --open line.khan --save line.khan sketch.constrain sketch=Sketch1 kind=horizontal on=Line1
line: 1 step
  Sketch1      ok  1 line, 1 constraint, free to move 3 ways
wrote line.khan
$ kh_cli run --open line.khan --save line.khan sketch.constrain sketch=Sketch1 kind=fix on=Line1.start
line: 1 step
  Sketch1      ok  1 line, 2 constraints, free to move 1 way
wrote line.khan
$ kh_cli run --open line.khan --save line.khan sketch.constrain sketch=Sketch1 kind=length on=Line1 value=40
line: 1 step
  Sketch1      ok  1 line, 3 constraints, fully constrained
wrote line.khan
```

See where the solver put the line with [`kh_cli open --sketch`](../cli/open.md):

<!-- cli-example -->
```console
$ kh_cli open line.khan --sketch Sketch1
line: 1 step
  Sketch1      ok  1 line, 3 constraints, fully constrained
Sketch1 (XY plane): fully constrained
  Line1          line    0.00, 0.00 to 40.00, 0.00
  Horizontal1    horizontal          Line1
  Fix1           fix                 Line1.start
  Length1        length              Line1  40.00 mm
```

The line was drawn from 0, 0 to 38, 3. Now it runs from 0, 0 to 40, 0.

## Planes

| Plane | It's | Sketch x and y are |
| --- | --- | --- |
| `xy` (the default) | The ground, seen from the top | x and y |
| `xz` | Standing up, facing the front | x and z (up) |
| `yz` | Standing up, facing the right | y and z (up) |

## What you can draw

| Command | Draws | Its points |
| --- | --- | --- |
| `sketch.add_line` | A straight line, `from` one end `to` the other | `.start`, `.end` |
| `sketch.add_circle` | A circle, from its `center` and `radius` | `.center` |
| `sketch.add_arc` | Part of a circle, counterclockwise from `start` to `end` (degrees: 0 points right, 90 up) | `.start`, `.end`, `.center` |
| `sketch.add_point` | A single point | the point itself (`Point1`) |

Each one gets a name: `Line1`, `Line2`, `Circle1`, ... Add `construction=true`
for helper geometry (a center line, say) that isn't part of the shape.

## Constraints and dimensions

Add them with `sketch.constrain`: `kind` says what, and `on` says what to: lines,
circles and arcs by name (`Line1`), and points as `Line1.start`, `Line1.end` or
`Circle1.center`.

| `kind` | `on` | Keeps true |
| --- | --- | --- |
| `fix` | a point | The point stays where it is |
| `coincident` | two points | They're the same point (corners meet) |
| `horizontal`, `vertical` | a line | It's flat, or straight up and down |
| `parallel`, `perpendicular` | two lines | They never meet, or they meet square |
| `equal` | two lines, or two circles or arcs | Same length, or same radius |
| `point_on_line`, `point_on_circle` | a point and a line, or a circle or arc | The point is on it |
| `midpoint` | a point and a line | The point is in the middle of the line |
| `tangent` | a line and a circle or arc, or two circles or arcs | They touch without crossing |

Dimensions need a `value`:

| `kind` | `on` | `value` |
| --- | --- | --- |
| `length` | a line | mm |
| `distance` | two points | mm |
| `horizontal_distance`, `vertical_distance` | two points | mm from the first to the second (negative: left or down) |
| `radius`, `diameter` | a circle or arc | mm |
| `angle` | two lines | degrees, counterclockwise from the first line to the second |

Change a dimension with `sketch.set_value`, and delete anything with
`sketch.delete` (deleting a line also deletes the constraints on it).

## Free to move: degrees of freedom

Khan counts how many ways the drawing can still move or change size. A free line
can move 4 ways (each end, across and up). Each constraint takes some away: a
horizontal line can move 3 ways; fix its start and it can only stretch (1 way);
give it a length and it's **fully constrained** (0). Aim for 0, so the part comes
out the same every time.

## When constraints fight

If constraints can't all be true at once, the sketch fails and Khan names them:

<!-- cli-example exit=1 -->
```console
$ kh_cli run --open line.khan sketch.constrain sketch=Sketch1 kind=horizontal_distance on=Line1.start,Line1.end value=50
line: 1 step
  Sketch1      failed
    error [sketch.conflicting]: These constraints can't all be true at once: Length1, HorizontalDistance1. Delete one of them, or change a dimension.
```

A line can't be 40 mm long and reach 50 mm across. Delete one, or change a value.
If a constraint only repeats what others already say, the sketch still works, and
Khan says which one you could delete.

## Tips

- Draw close to the shape you want. The solver moves things as little as it can,
  so a rough drawing near the answer solves the way you expect.
- Fix one point first (often a corner at 0, 0), so the sketch can't drift around.
- A line running smoothly into an arc needs both: `coincident` where they meet,
  and `tangent`.
