# kh_cli help

Lists the modeling commands that [`kh_cli run`](run.md) can run, or explains one
command and its settings.

```text
kh_cli help [--plugins <folder>]... [<command>]
```

| Option | Meaning |
| --- | --- |
| `<command>` | Explain this command, e.g. `feature.add_box` |
| `--plugins <folder>` | Also load the plugins in this folder, so their commands are listed too. Can be given more than once |

## List every command

<!-- cli-example -->
```console
$ kh_cli help
Commands for kh_cli run:
  document.set_units       Set units
  feature.add_boolean      Combine bodies
  feature.add_box          Add box
  feature.add_cylinder     Add cylinder
  feature.add_extrude      Extrude
  feature.add_move         Move body
  feature.add_sketch       Add sketch
  feature.delete           Delete step
  feature.duplicate        Duplicate step
  feature.rename           Rename step
  feature.set_material     Set material
  feature.suppress         Turn steps off or on
  folder.add               New folder
  folder.delete            Remove folder
  folder.rename            Rename folder
  material.define          Define material
  material.delete          Delete material
  sketch.add_arc           Add arc
  sketch.add_circle        Add circle
  sketch.add_line          Add line
  sketch.add_point         Add point
  sketch.add_pole          Add control point
  sketch.add_rectangle     Add rectangle
  sketch.add_spline        Add spline
  sketch.constrain         Add constraint
  sketch.delete            Delete from sketch
  sketch.import_spline     Import spline
  sketch.reload_spline     Reload spline from file
  sketch.remove_pole       Remove control point
  sketch.set_spline        Change spline
  sketch.set_value         Change dimension
  sketch.set_weight        Change control point weight
Run "kh_cli help <command>" to see a command's settings.
```

## Explain one command

<!-- cli-example -->
```console
$ kh_cli help feature.add_cylinder
feature.add_cylinder: Add cylinder (from kh.modeling)
  Adds a cylinder from its base center, axis, radius and height.
Settings:
  radius     number               required  Radius in mm.
  height     number               required  Height in mm.
  base       x,y,z                optional  Center of the base in mm. Default 0, 0, 0.
  axis       x,y,z                optional  Direction of the axis. Default 0, 0, 1 (up).
  name       text                 optional  Step name. Default Cylinder1, Cylinder2, ...
```

Each setting shows the kind of value it takes:

| Kind | Type it like |
| --- | --- |
| number | `25` or `2.5` |
| whole number | `3` |
| true or false | `true`, `false`, `yes` or `no` |
| text | `Lid` (put quotes around text with spaces: `"name=Big lid"`) |
| x,y,z | `10,20,30`, no spaces |
| step name | the name of a step in the design, like `Box1` |
| `a|b|c` | one of the words listed |

## Commands from plugins

Plugins can add commands. Load them with `--plugins`:

<!-- cli-example -->
```console
$ kh_cli help --plugins build/dev/khan-plugins example.hello.add_cube
example.hello.add_cube: Add cube (from example.hello)
  Adds a cube at the origin.
Settings:
  size       number               optional  Length of each side, in mm. Default 20 mm.
```

## Errors

<!-- cli-example exit=1 -->
```console
$ kh_cli help feature.add_sphere
error: there is no command called feature.add_sphere. Run "kh_cli help" to see them.
```
