# kh_cli materials

Lists the materials a body can be made of: Khan's own, and with a design, the
ones it added. Use the names with
[`feature.set_material`](commands.md#featureset_material).

```text
kh_cli materials [<file.khan>]
```

| Option | Meaning |
| --- | --- |
| `<file.khan>` | Also list the materials this design added, after Khan's own |

| Column | Meaning |
| --- | --- |
| `density` | Mass of one cubic centimeter, in grams (g/cm³). Mass = volume × density |
| `stiffness` | Young's modulus, in GPa: how hard it is to stretch or bend. Steel is about 200, PLA about 3.5 |
| `poisson` | Poisson's ratio: how much thinner it gets when stretched, 0 to 0.5 |
| `yield` | Yield strength, in MPa: the stress where it starts to bend for good |
| `price` | Rough price per kilogram, in US dollars |

`-` means unknown. Strength numbers are typical values, good for comparing
materials; for anything that must not break, use the supplier's data sheet.

## Examples

<!-- cli-example -->
```console
$ kh_cli materials
material                density stiffness  poisson    yield   price
                         g/cm^3       GPa               MPa  per kg
PLA                        1.24      3.50     0.36       50   20.00
PETG                       1.27      2.10     0.38       50   22.00
ABS                        1.05      2.20     0.35       40   20.00
TPU (flexible)             1.21      0.03     0.48        9   35.00
Nylon (PA12)               1.01      1.70     0.39       48   60.00
Resin (standard)           1.18      2.50     0.35       45   50.00
Acrylic (PMMA)             1.18      3.20     0.37       70    8.00
Polycarbonate              1.20      2.40     0.37       62   12.00
Aluminum 6061              2.70     69.00     0.33      276    6.00
Steel (mild, 1018)         7.87    205.00     0.29      370    2.00
Stainless steel 304        8.00    193.00     0.29      215    5.00
Brass (C360)               8.50     97.00     0.31      140    9.00
Copper (C110)              8.94    117.00     0.34       69   11.00
Oak (red)                  0.70     12.50     0.35       45    3.00
Plywood (birch)            0.68      9.00     0.30       30    2.50
MDF                        0.75      4.00     0.25       18    1.00
```

A design's own materials come after Khan's:

<!-- cli-example -->
```console
$ kh_cli demo tag.khan
wrote tag.khan
$ kh_cli run --open tag.khan --save tag.khan material.define name="Glow PLA" density=1.3 color=#a8f0a0
Demo tag: 3 steps
  Plate        ok  volume 7200.00 mm^3
  Hole         ok  volume 117.81 mm^3
  Tag          ok  volume 7121.46 mm^3  mass 8.83 g (PLA)
wrote tag.khan
$ kh_cli materials tag.khan
material                density stiffness  poisson    yield   price
                         g/cm^3       GPa               MPa  per kg
...
Demo tag's own:
Glow PLA                   1.30         -        -        -       -
```

## Errors

<!-- cli-example exit=1 -->
```console
$ kh_cli materials missing.khan
error [file.cant_read]: Khan couldn't open missing.khan. Check that the file is still there and that you're allowed to open it. missing.khan
```
