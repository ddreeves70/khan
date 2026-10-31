# Parameters

A parameter is a number with a name, like `wall = 3 mm`. Give the important
sizes of a part names, and other values can be worked out from them, like
`height = wall * 4`. Change `wall`, and everything worked out from it changes
too. That's what makes a design *parametric*: you change one number, not ten.

Parameters are saved in the design, and [sketch dimensions](#using-parameters-in-a-sketch)
and [steps' numbers](#using-parameters-in-steps), like how far an extrude goes,
can use them. Soon a **Parameters** panel in the desktop app will list them.
Today you add them with [`kh_cli`](../cli/commands.md#parameterset).

## An example

A box for a phone stand has walls 3 mm thick and is 4 walls tall, plus an inch
for the phone to sit in:

<!-- cli-example -->
```console
$ kh_cli run --save stand.khan parameter.set name=wall value="3 mm"
stand: 0 steps
Parameters:
  wall = 3 mm
wrote stand.khan
$ kh_cli run --open stand.khan --save stand.khan parameter.set name=height value="wall * 4 + 1 in"
stand: 0 steps
Parameters:
  wall   = 3 mm
  height = wall * 4 + 1 in  (37.4 mm)
wrote stand.khan
```

Khan worked out `height`: 4 × 3 mm + 25.4 mm = 37.4 mm. Make the walls thinner,
and `height` follows:

<!-- cli-example -->
```console
$ kh_cli run --open stand.khan --save stand.khan parameter.set name=wall value="2 mm"
stand: 0 steps
Parameters:
  wall   = 2 mm
  height = wall * 4 + 1 in  (33.4 mm)
wrote stand.khan
```

## What a formula can have

| You can write | Like | Means |
| --- | --- | --- |
| A number with a unit | `3 mm`, `2.5 cm`, `1 in`, `0.5 ft`, `1 m` | A length |
| An angle | `30 deg`, `30°`, `0.5 rad` | An angle |
| A number with no unit | `4`, `0.5` | A plain number, like a count or a ratio |
| Another parameter | `wall` | Its value |
| `+` `-` `*` `/` | `wall * 2 + 1 mm` | Add, take away, times, divide |
| `^` | `wall ^ 2` | To the power of (here, an area in mm^2) |
| Brackets | `(wall + 1 mm) * 2` | Work this out first |
| `pi` | `pi * 10 mm` | 3.14159... |
| `sqrt(x)` | `sqrt(16)` | Square root |
| `abs(x)` | `abs(gap)` | The value without its minus sign |
| `min(a, b, ...)`, `max(a, b, ...)` | `max(wall, 2 mm)` | The smallest or largest |
| `sin`, `cos`, `tan` | `sin(30 deg)` | Of an angle |
| `asin`, `acos`, `atan` | `atan(0.5)` | The angle with that sine, cosine or tangent |

Khan works out `*` and `/` before `+` and `-`, as in maths: `1 + 2 * 3` is 7.
Use brackets to change that: `(1 + 2) * 3` is 9.

## Units are checked

Khan keeps track of what each value measures, so mistakes show up straight away
instead of making a part the wrong size:

- Lengths add to lengths, whatever their units: `1 in + 1 mm` is 26.4 mm. A
  length and a plain number don't add: `wall + 2` is refused. Write `wall + 2 mm`.
- Times and divide change the units: `wall * wall` is an area (mm^2), and
  `wall / 2 mm` is a plain number.
- `sin`, `cos` and `tan` need an angle: write `sin(30 deg)`, not `sin(30)`.

## Names

A name has letters, digits and `_`, and starts with a letter: `wall`,
`hole_2`, `LidGap`. Upper and lower case count, so `Wall` and `wall` are
different, and Khan won't let you have both. Words formulas already use (units
like `mm` and `in`, functions like `sin`, and `pi`) can't be names.

[`parameter.rename`](../cli/commands.md#parameterrename) changes a parameter's
name and every formula that uses it, so nothing breaks.

## Using parameters in a sketch

A dimension in a [sketch](sketches.md) can have a formula instead of a number:
give `sketch.constrain` or `sketch.set_value` a `formula` instead of a `value`.
Here a plate is 10 walls wide:

<!-- cli-example -->
```console
$ kh_cli run --open stand.khan --save stand.khan feature.add_sketch
stand: 1 step
  Sketch1      ok  empty
Parameters:
  wall   = 2 mm
  height = wall * 4 + 1 in  (33.4 mm)
wrote stand.khan
$ kh_cli run --open stand.khan --save stand.khan sketch.add_rectangle sketch=Sketch1 from=0,0 to=20,30
stand: 1 step
  Sketch1      ok  4 lines, 8 constraints, free to move 4 ways
Parameters:
  wall   = 2 mm
  height = wall * 4 + 1 in  (33.4 mm)
wrote stand.khan
$ kh_cli run --open stand.khan --save stand.khan sketch.constrain sketch=Sketch1 kind=length on=Line1 formula="wall * 10"
stand: 1 step
  Sketch1      ok  4 lines, 9 constraints, free to move 3 ways
Parameters:
  wall   = 2 mm
  height = wall * 4 + 1 in  (33.4 mm)
wrote stand.khan
$ kh_cli run --open stand.khan --save stand.khan sketch.constrain sketch=Sketch1 kind=length on=Line2 formula=height
stand: 1 step
  Sketch1      ok  4 lines, 10 constraints, free to move 2 ways
Parameters:
  wall   = 2 mm
  height = wall * 4 + 1 in  (33.4 mm)
wrote stand.khan
```

The plate is 20 mm wide (10 × 2 mm) and 33.4 mm tall. Change `wall`, and both
dimensions follow, because `height` uses `wall` too:

<!-- cli-example -->
```console
$ kh_cli run --open stand.khan --save stand.khan parameter.set name=wall value="3 mm"
stand: 1 step
  Sketch1      ok  4 lines, 10 constraints, free to move 2 ways
Parameters:
  wall   = 3 mm
  height = wall * 4 + 1 in  (37.4 mm)
wrote stand.khan
$ kh_cli open stand.khan --sketch Sketch1
...
  Length1        length              Line1  30.00 mm = wall * 10
  Length2        length              Line2  37.40 mm = height
...
```

A length dimension needs a length and an angle dimension an angle, so a plain
number like `12` is refused: write `12 mm`. Khan also refuses a parameter
change that would make a length 0 mm or less. In the desktop app, a dimension
with a formula says so when you point at its number; typing a new number there
replaces the formula.

## Using parameters in steps

A step's numbers, like an extrude's distance, a box's size or a cylinder's
radius, can have formulas too: use
[`feature.set_value`](../cli/commands.md#featureset_value) with a `formula`.
Here the plate from the sketch above is extruded one wall thick:

<!-- cli-example -->
```console
$ kh_cli run --open stand.khan --save stand.khan feature.add_extrude sketch=Sketch1 distance=5
stand: 2 steps
  Sketch1      ok  4 lines, 10 constraints, free to move 2 ways
  Extrude1     ok  volume 5610.00 mm^3  mass 6.96 g (PLA)
Parameters:
  wall   = 3 mm
  height = wall * 4 + 1 in  (37.4 mm)
wrote stand.khan
$ kh_cli run --open stand.khan --save stand.khan feature.set_value feature=Extrude1 setting=distance formula=wall
stand: 2 steps
  Sketch1      ok  4 lines, 10 constraints, free to move 2 ways
  Extrude1     ok  volume 3366.00 mm^3  mass 4.17 g (PLA)
    distance = wall  (3 mm)
Parameters:
  wall   = 3 mm
  height = wall * 4 + 1 in  (37.4 mm)
wrote stand.khan
```

Now one number, `wall`, sets the plate's width, height and thickness:

<!-- cli-example -->
```console
$ kh_cli run --open stand.khan --save stand.khan parameter.set name=wall value="2 mm"
stand: 2 steps
  Sketch1      ok  4 lines, 10 constraints, free to move 2 ways
  Extrude1     ok  volume 1336.00 mm^3  mass 1.66 g (PLA)
    distance = wall  (2 mm)
Parameters:
  wall   = 2 mm
  height = wall * 4 + 1 in  (33.4 mm)
wrote stand.khan
```

That's 20 mm × 33.4 mm × 2 mm = 1336 mm^3. In the desktop app's Properties
panel, a number with a formula says so when you point at it; typing a new number
there replaces the formula, and the other numbers keep theirs.

## Tips

- Name the sizes you'd want to change later: wall thickness, the gap for a lid,
  a screw's size. Work the rest out from those.
- Add a note to say what a parameter is for: `note="Gap so the lid slides on"`.
- A parameter can't use itself, or use one that uses it back: Khan couldn't
  work either out. If you see that message, change one of them to a number.
- Khan won't let you make a change that breaks another parameter. It says which
  one would break, and why, so you can fix that one first.
- A parameter that another one uses can't be deleted until nothing uses it.
- Every change to a parameter can be undone.
