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
| [`feature.duplicate`](#featureduplicate) | Copies a step, and the steps it uses |
| [`feature.set_material`](#featureset_material) | Picks what a body is made of |
| [`feature.suppress`](#featuresuppress) | Turns steps off, or back on |
| [`material.define`](#materialdefine) | Adds a material of your own, or changes one |
| [`material.delete`](#materialdelete) | Removes a material you added |

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
  Box1         ok  volume 24000.00 mm^3  mass 29.76 g (PLA)
wrote box.khan
```

![A 40 x 30 x 20 mm box](images/cmd-box.png)

Moved away from the origin, and named:

<!-- cli-example -->
```console
$ kh_cli run feature.add_box corner=10,0,5 size=20,20,2 name=Shelf
Untitled: 1 step
  Shelf        ok  volume 800.00 mm^3  mass 0.99 g (PLA)
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
  Cylinder1    ok  volume 7853.98 mm^3  mass 9.74 g (PLA)
```

![A cylinder standing up, and one lying along x](images/cmd-cylinders.png)

Lying on its side, pointing along x:

<!-- cli-example -->
```console
$ kh_cli run feature.add_cylinder radius=5 height=40 axis=1,0,0 name=Rod
Untitled: 1 step
  Rod          ok  volume 3141.59 mm^3  mass 3.90 g (PLA)
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
  Box1         ok  volume 9000.00 mm^3  mass 11.16 g (PLA)
wrote parts.khan
$ kh_cli run --open parts.khan --save parts.khan feature.add_cylinder base=30,15,0 radius=10 height=20
parts: 2 steps
  Box1         ok  volume 9000.00 mm^3  mass 11.16 g (PLA)
  Cylinder1    ok  volume 6283.19 mm^3  mass 7.79 g (PLA)
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
  Union1       ok  volume 13712.39 mm^3  mass 17.00 g (PLA)
$ kh_cli run --open parts.khan feature.add_boolean operation=cut a=Box1 b=Cylinder1
parts: 3 steps
  Box1         ok  volume 9000.00 mm^3
  Cylinder1    ok  volume 6283.19 mm^3
  Cut1         ok  volume 7429.20 mm^3  mass 9.21 g (PLA)
$ kh_cli run --open parts.khan feature.add_boolean operation=intersect a=Box1 b=Cylinder1
parts: 3 steps
  Box1         ok  volume 9000.00 mm^3
  Cylinder1    ok  volume 6283.19 mm^3
  Intersect1   ok  volume 1570.80 mm^3  mass 1.95 g (PLA)
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
  Box1         ok  volume 7200.00 mm^3  mass 8.93 g (PLA)
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan feature.rename feature=Box1 name=Plate
tag: 1 step
  Plate        ok  volume 7200.00 mm^3  mass 8.93 g (PLA)
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
  Box1         ok  volume 1000.00 mm^3  mass 1.24 g (PLA)
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

## feature.duplicate

Copies a step, and every step it uses, as new steps at the end of the design.
The copies get new names (the next free number) and have the same sizes and
positions, so they sit exactly on top of the originals until you change them.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `feature` | step name | yes | The step to copy |

In the [demo tag](demo.md), `Tag` cuts `Hole` out of `Plate`, so duplicating
`Tag` copies all three. The copied `Tag1` cuts `Hole1` out of `Plate1`; changing
the copies never changes the originals.

<!-- cli-example -->
```console
$ kh_cli demo demo.khan
wrote demo.khan
$ kh_cli run --open demo.khan --save demo.khan feature.duplicate feature=Tag
Demo tag: 6 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
  Plate1       ok  volume 7200.00 mm^3
  Hole1        ok  volume 117.81 mm^3
  Tag1         ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote demo.khan
```

## feature.set_material

Picks what a step's body is made of. The material sets the body's mass, weight,
material cost and its color in the viewer. A body with no material picked is
**PLA**. A union, cut or intersect with no material of its own takes the material
of its first body, `a` (see [Materials](../user-guide/materials.md)).

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `feature` | step name | yes | The step |
| `material` | text | yes | A material's name, like `PLA` or `"Aluminum 6061"` (upper or lower case). [`kh_cli materials`](materials.md) lists them. Leave it empty (`material=`) to go back to the material the step gets by default |

The demo tag in aluminum instead of PLA: `Tag` cuts the hole out of `Plate`, so it
takes `Plate`'s material.

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan feature.set_material feature=Plate material="Aluminum 6061"
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 19.23 g (Aluminum 6061)
wrote tag.khan
```

A material Khan doesn't know is refused:

<!-- cli-example exit=1 -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan feature.set_material feature=Plate material=gold
error [material.unknown]: There is no material called gold. See the list with kh_cli materials, or add your own with material.define.
```

## feature.suppress

Turns steps off, so they aren't built, or back on. Nothing is deleted: a step
that's off keeps all its numbers. A step that uses a step that's off is
`blocked` until that step is on again. Turning several steps off or on is one
undo step.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `steps` | step names | yes | The steps, separated by commas, like `Box1,Hole` |
| `off` | true or false | no | `true` (the default) turns them off; `false` turns them back on |

In the [demo tag](demo.md), turning off `Hole` blocks `Tag`, which cuts it out of
`Plate`. Because a step is blocked, `kh_cli` ends with exit code 1, but the design
is still saved:

<!-- cli-example exit=1 -->
```console
$ kh_cli demo demo.khan
wrote demo.khan
$ kh_cli run --open demo.khan --save demo.khan feature.suppress steps=Hole
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         off
  Tag          blocked
    error [feature.blocked]: This step uses Hole, which is turned off. Turn Hole back on, or change this step.
wrote demo.khan
```

Turn it back on with `off=false`:

<!-- cli-example -->
```console
$ kh_cli run --open demo.khan --save demo.khan feature.suppress steps=Hole off=false
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote demo.khan
```

Steps that are already that way can't be changed again:

<!-- cli-example exit=1 -->
```console
$ kh_cli run --open demo.khan feature.suppress steps=Plate,Hole off=false
error [feature.already_on]: Those steps are already turned on.
```

## material.define

Adds a material of your own to the design, for a filament or stock Khan doesn't
list. It's saved in the design. Defining a material with the same name again
changes it, and every step that uses it follows. Khan's own materials can't be
changed: give yours another name, like `My PLA`.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `name` | text | yes | The material's name |
| `density` | number | yes | Density in g/cm³, from 0.01 to 25. Water is 1; the maker's data sheet has it |
| `color` | text | no | Its color in the viewer, as `#` and six hex digits, like `#3366cc`. Default `#9eb2cc` |
| `youngs_modulus` | number | no | Stiffness (Young's modulus) in GPa. Default 0, meaning unknown |
| `poissons_ratio` | number | no | Poisson's ratio, 0 to 0.5. Default 0, meaning unknown |
| `yield_strength` | number | no | Yield strength in MPa. Default 0, meaning unknown |
| `cost_per_kg` | number | no | Price per kilogram. Default 0, meaning unknown |

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan material.define name="Blue PETG" density=1.27 color=#3366cc cost_per_kg=25
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan feature.set_material feature=Plate material="Blue PETG"
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 9.04 g (Blue PETG)
wrote tag.khan
$ kh_cli materials tag.khan
material                density stiffness  poisson    yield   price
                         g/cm^3       GPa               MPa  per kg
...
Demo tag's own:
Blue PETG                  1.27         -        -        -   25.00
```

Numbers that can't be right are refused:

<!-- cli-example exit=1 -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan material.define name=Fluff density=0
error [material.density]: A material's density must be between 0.01 and 25 g/cm³. Water is 1 g/cm³, PLA 1.24, steel 7.87.
```

## material.delete

Removes a material you added. A material a step uses can't be removed: pick
another material for that step first. Khan's own materials can't be removed.

| Setting | Kind | Needed | Meaning |
| --- | --- | --- | --- |
| `name` | text | yes | The material's name |

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan material.define name=Cork density=0.24
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan material.delete name=Cork
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.khan
```
