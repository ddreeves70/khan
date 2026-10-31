# Parameters

A parameter is a number with a name, like `wall = 3 mm`. Give the important
sizes of a part names, and other values can be worked out from them, like
`height = wall * 4`. Change `wall`, and everything worked out from it changes
too. That's what makes a design *parametric*: you change one number, not ten.

Parameters are saved in the design. Soon sketch dimensions and step sizes will
use them too (that's next in Phase 2), and a **Parameters** panel in the desktop
app will list them. Today you add them with
[`kh_cli`](../cli/commands.md#parameterset).

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
