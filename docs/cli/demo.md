# sketchbillet_cli demo

Makes a sample part, a keychain tag with a hole, so you can check that SketchBillet and
its geometry kernel work.

```text
sketchbillet_cli demo <out.sb|out.stl|out.step>
```

| Argument | Meaning |
| --- | --- |
| `out.sb` | Save the tag as a SketchBillet design: three steps you can open and change |
| `out.stl` | Save only the shape, as a mesh for 3D printing |
| `out.step` | Save only the shape, as a STEP file for other CAD programs |

![The demo keychain tag: a 60 x 30 x 4 mm plate with a 5 mm hole](images/demo-tag.png)

## Examples

Save it as a design, then look at its steps with [`sketchbillet_cli open`](open.md):

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

Save only the shape, for a slicer or another CAD program:

<!-- cli-example -->
```console
$ sketchbillet_cli demo tag.stl
volume: 7121.46 mm^3
wrote tag.stl
$ sketchbillet_cli demo tag.step
volume: 7121.46 mm^3
wrote tag.step
```

The plate is 60 × 30 × 4 mm = 7200 mm^3. The hole takes away
π × 2.5² × 4 ≈ 78.54 mm^3, which leaves 7121.46 mm^3.
