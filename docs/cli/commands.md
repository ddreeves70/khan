# Modeling commands

These are the commands [`kh_cli run`](run.md) runs. Each one adds or changes one
step of a design. The desktop app's buttons, Python scripts and the AI assistant
will run the same commands, by the same names.

All lengths are in millimeters (mm). Positions and sizes are written `x,y,z`:
**x** goes right, **y** goes back, **z** goes up.

| Command | What it does |
| --- | --- |
| [`feature.add_box`](#featureadd_box) | Adds a box |
| [`feature.add_cylinder`](#featureadd_cylinder) | Adds a cylinder |
| [`feature.add_boolean`](#featureadd_boolean) | Joins two bodies, cuts one from another, or keeps their overlap |
| [`feature.rename`](#featurerename) | Renames a step |
| [`feature.delete`](#featuredelete) | Deletes a step |

Plugins can add more commands. [`kh_cli help`](help.md) lists every command
Khan has right now.

## feature.add_box

Adds a box with its edges along x, y and z.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `size` | x,y,z | yes | Width (x), depth (y) and height (z), in mm. Each must be more than 0 |
| `corner` | x,y,z | no | Where the corner nearest the origin goes, in mm. Default `0,0,0` |
| `name` | text | no | The step's name. Default `Box1`, `Box2`, ... |

<!-- cli-example -->
```console
$ kh_cli run --save box.khan feature.add_box size=40,30,20
box: 1 step
  Box1         ok  volume 24000.00 mm^3
wrote box.khan
```

![A 40 x 30 x 20 mm box](images/cmd-box.png)

Moved away from the origin, and named:

<!-- cli-example -->
```console
$ kh_cli run feature.add_box corner=10,0,5 size=20,20,2 name=Shelf
Untitled: 1 step
  Shelf        ok  volume 800.00 mm^3
```

## feature.add_cylinder

Adds a round cylinder.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `radius` | number | yes | Radius in mm (half the diameter). More than 0 |
| `height` | number | yes | Height in mm, along the axis. More than 0 |
| `base` | x,y,z | no | Center of the bottom circle, in mm. Default `0,0,0` |
| `axis` | x,y,z | no | Which way the cylinder points. Default `0,0,1` (straight up) |
| `name` | text | no | The step's name. Default `Cylinder1`, `Cylinder2`, ... |

<!-- cli-example -->
```console
$ kh_cli run feature.add_cylinder radius=10 height=25
Untitled: 1 step
  Cylinder1    ok  volume 7853.98 mm^3
```

![A cylinder standing up, and one lying along x](images/cmd-cylinders.png)

Lying on its side, pointing along x:

<!-- cli-example -->
```console
$ kh_cli run feature.add_cylinder radius=5 height=40 axis=1,0,0 name=Rod
Untitled: 1 step
  Rod          ok  volume 3141.59 mm^3
```

## feature.add_boolean

Combines two bodies into one.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `operation` | `union`, `cut` or `intersect` | yes | How to combine them (pictures below) |
| `a` | step name | yes | The first body. For `cut`, the body that keeps its material |
| `b` | step name | yes | The second body. For `cut`, the tool that is taken away |
| `name` | text | no | The step's name. Default `Union1`, `Cut1` or `Intersect1` |

A box and a cylinder that overlap:

<!-- cli-example -->
```console
$ kh_cli run --save parts.khan feature.add_box size=30,30,10
parts: 1 step
  Box1         ok  volume 9000.00 mm^3
wrote parts.khan
$ kh_cli run --open parts.khan --save parts.khan feature.add_cylinder base=30,15,0 radius=10 height=20
parts: 2 steps
  Box1         ok  volume 9000.00 mm^3
  Cylinder1    ok  volume 6283.19 mm^3
wrote parts.khan
```

| `union`: both together | `cut`: a minus b | `intersect`: only the overlap |
| --- | --- | --- |
| ![Union](images/cmd-union.png) | ![Cut](images/cmd-cut.png) | ![Intersect](images/cmd-intersect.png) |

<!-- cli-example -->
```console
$ kh_cli run --open parts.khan feature.add_boolean operation=union a=Box1 b=Cylinder1
parts: 3 steps
  Box1         ok  volume 9000.00 mm^3
  Cylinder1    ok  volume 6283.19 mm^3
  Union1       ok  volume 13712.39 mm^3
$ kh_cli run --open parts.khan feature.add_boolean operation=cut a=Box1 b=Cylinder1
parts: 3 steps
  Box1         ok  volume 9000.00 mm^3
  Cylinder1    ok  volume 6283.19 mm^3
  Cut1         ok  volume 7429.20 mm^3
$ kh_cli run --open parts.khan feature.add_boolean operation=intersect a=Box1 b=Cylinder1
parts: 3 steps
  Box1         ok  volume 9000.00 mm^3
  Cylinder1    ok  volume 6283.19 mm^3
  Intersect1   ok  volume 1570.80 mm^3
```

The Boolean step uses up `a` and `b`: they stay in the design (you can still
change them), but only the combined body is a finished body.

## feature.rename

Gives a step a new name. Steps that use it keep working, because they point at
the step itself, not its name.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `feature` | step name | yes | The step to rename |
| `name` | text | yes | Its new name. No other step may have it |

<!-- cli-example -->
```console
$ kh_cli run --save tag.khan feature.add_box size=60,30,4
tag: 1 step
  Box1         ok  volume 7200.00 mm^3
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan feature.rename feature=Box1 name=Plate
tag: 1 step
  Plate        ok  volume 7200.00 mm^3
wrote tag.khan
```

Two steps can't have the same name:

<!-- cli-example exit=1 -->
```console
$ kh_cli demo demo.khan
wrote demo.khan
$ kh_cli run --open demo.khan feature.rename feature=Hole name=Plate
error [feature.name_taken]: Another step is already called Plate. Pick a different name.
```

## feature.delete

Deletes a step. A step that other steps use can't be deleted; delete those first.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `feature` | step name | yes | The step to delete |

<!-- cli-example -->
```console
$ kh_cli run --save scrap.khan feature.add_box size=10,10,10
scrap: 1 step
  Box1         ok  volume 1000.00 mm^3
wrote scrap.khan
$ kh_cli run --open scrap.khan --save scrap.khan feature.delete feature=Box1
scrap: 0 steps
wrote scrap.khan
```

A step that another step uses can't be deleted. In the [demo tag](demo.md), `Tag`
cuts `Hole` out of `Plate`:

<!-- cli-example exit=1 -->
```console
$ kh_cli demo demo.khan
wrote demo.khan
$ kh_cli run --open demo.khan feature.delete feature=Plate
error [feature.in_use]: Plate is used by Tag. Delete or change Tag first.
```
