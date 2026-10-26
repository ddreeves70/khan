# Files

## Khan designs (.khan)

A `.khan` file is a whole design: every step, its numbers and its name. Open it
again and you can keep changing it. Save with `--save` and open with `--open`
or [`kh_cli open`](../cli/open.md).

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli open tag.khan
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
```

A few things about `.khan` files:

- **They're small.** They store the recipe, not the shape: the tag is about
  1.5 kB.
- **They're readable.** A `.khan` file is a zip of text files. Rename it to
  `.zip` and open it to see each step. The [file format](../file-format.md) page
  describes it.
- **They work well with Git.** Saving the same design twice gives exactly the same
  file, so version control only shows real changes.
- **Older Khan can't open newer files.** A file saved by a newer Khan says
  "Update Khan to open it" rather than opening wrongly.

## Exporting

To use a part somewhere else, **export** its shape. Exported files hold only the
shape, not the steps, so keep the `.khan` file too. In the desktop app, use
[File > Export...](desktop-app.md#exporting) (Ctrl+E). With `kh_cli`, use
`kh_cli open --export`, as shown below.


| Format | Ending | Use it for |
| --- | --- | --- |
| STL | `.stl` | 3D printing: open it in your slicer (Cura, PrusaSlicer, Bambu Studio, ...) |
| STEP | `.step` or `.stp` | Other CAD programs (Fusion, SolidWorks, FreeCAD, Onshape), and machine shops |

<!-- cli-example -->
```console
$ kh_cli open tag.khan --export tag.stl
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.stl
```

Export writes the design's one finished body. If the design has more than one,
join them with a [union](combining.md#union), or pick one step with `--step`:

<!-- cli-example -->
```console
$ kh_cli open tag.khan --export plate.step --step Plate
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote plate.step
```

## Importing

Opening STEP, STL and 3MF files from other programs is planned (see the
[user's guide overview](README.md#where-khan-is-today)).
