# The .khan file format (schema 1)

A `.khan` file is a design. It is an ordinary zip file holding JSON text files, so
you can rename it to `.zip`, open it and read every step. The code that reads and
writes it is `kh_io` (`src/io/KhanFile.cpp`).

The file holds the **recipe** (the steps and their numbers), not the finished
geometry. Khan rebuilds the shape every time it opens a file, the same way it does
when you change a number.

## What's inside

```text
tag.khan
├── manifest.json                    what this file is and which Khan wrote it
├── timeline.json                    the steps, in order
├── materials.json                   the design's own materials (only if it has any)
├── folders.json                     the feature tree's folders (only if it has any)
├── parameters.json                  named values like wall = 3 mm (only if it has any)
└── features/
    ├── 926bde46-....json            one file per step
    ├── f153a811-....json
    └── acb900d7-....json
```

### manifest.json

```json
{
  "app": "0.0.1",
  "document": {
    "id": "26c5d391-a653-4100-ae82-ff04872a50fd",
    "kind": "part",
    "name": "Demo tag"
  },
  "files": [
    "timeline.json",
    "features/926bde46-9c5b-403a-bc25-c01e5cf11ce3.json"
  ],
  "format": "khan",
  "schema": 1,
  "units": { "angle": "rad", "length": "mm" }
}
```

| Key | Meaning |
| --- | --- |
| `format` | Always `"khan"`. A file without it is not a Khan design. |
| `schema` | The format version, a whole number. This document describes schema 1. |
| `app` | The Khan version that saved the file. For information only. |
| `units` | Units of every number in the file: lengths in millimeters, angles in radians. |
| `document.id` | The design's UUID. It never changes, even when the file is renamed. |
| `document.kind` | `"part"` (or `"assembly"`, from Phase 3). |
| `document.name` | The design's name. |
| `document.lengthUnit` | Only when the student picked another unit: the unit the design *shows* lengths in, `"cm"`, `"m"`, `"in"` or `"ft"`. The numbers in the file are still in mm (see `units`). A unit Khan doesn't know opens as mm. |
| `files` | Every other entry in the zip. For information only; readers go by `timeline.json`. |

### timeline.json

```json
{
  "features": [
    { "id": "926bde46-9c5b-403a-bc25-c01e5cf11ce3", "suppressed": false },
    { "id": "f153a811-ba41-4895-ab27-886a6ca3871e", "suppressed": false }
  ]
}
```

The steps in the order they are built. `suppressed: true` means the step is turned
off. Each `id` names a file `features/<id>.json`.

### features/&lt;id&gt;.json

Every step has `id`, `type` and `name`. Steps that use other steps list them under
`inputs` by id. The numbers go under `params`.

| `type` | `inputs` | `params` |
| --- | --- | --- |
| `Box` | none | `corner` [x, y, z] mm, `size` [x, y, z] mm |
| `Cylinder` | none | `base` [x, y, z] mm, `axis` [x, y, z] direction, `radius` mm, `height` mm |
| `Boolean` | `a`, `b`: step ids | `operation`: `"union"`, `"cut"` or `"intersect"` |
| `Sketch` | none | `plane`: `"xy"`, `"xz"` or `"yz"`; `entities` and `constraints` (below) |
| `Move` | `body`: step id | `offset` [x, y, z] mm, `center` [x, y, z] mm, `axis` [x, y, z] direction, `angle` radians. Turns by `angle` about the line through `center` along `axis`, then slides by `offset` |
| `Extrude` | `sketch`: a Sketch step's id | `distance` mm (the whole thickness), `side`: `"one"` (out of the sketch's front, the side its plane is seen from), `"flip"` (the other way) or `"both"` (half each way). Every closed shape of the sketch is swept; holes go through |

```json
{
  "id": "acb900d7-6be6-44e9-aace-a242499cd817",
  "inputs": {
    "a": "926bde46-9c5b-403a-bc25-c01e5cf11ce3",
    "b": "f153a811-ba41-4895-ab27-886a6ca3871e"
  },
  "name": "Tag",
  "params": { "operation": "cut" },
  "type": "Boolean"
}
```

A step's inputs must come before it in the timeline.

A `Sketch` step keeps what was drawn, not the solved positions: Khan solves it
again every time it opens or rebuilds the design.

- Each entity has `id`, `name` (like `"Line1"`), `type` and its numbers (mm,
  angles in radians), and `"construction": true` if it's helper geometry:
  `point` (`at` [x, y]), `line` (`start`, `end`), `circle` (`center`, `radius`),
  `arc` (`center`, `radius`, `start_angle`, `end_angle`, counterclockwise),
  `spline` (`poles`: a list of [x, y] control points; `degree`, 1 to 5;
  `weights`, one per pole, left out when they're all 1; `"closed": true` for a
  closed loop, left out when false; `source` only for one imported from a file
  of points: `file`, the file's path when it was imported, `through`, true if the
  curve goes through the points (false: they're its poles), and `unit`, the
  unit of the file's numbers, `"mm"`, `"cm"`, `"m"`, `"in"` or `"ft"`). A spline's knots aren't saved: they're spread
  evenly, clamped at the ends of an open spline and periodic for a closed one (see
  `kh::sketch::SplineShape`).
- Each constraint has `id`, `name` (like `"Length1"`), `kind` (`"horizontal"`,
  `"point_on_line"`, ... as `sketch.constrain` spells them), `points` (each an
  `entity` id and a `role`: `"start"`, `"end"`, `"center"`, `"pole"` with an
  `index` from 0 for a spline's control point, or none for a point),
  `entities` (ids), `value` for dimensions (mm, or radians for `angle`), and
  `at` [x, y] for `fix`.

```json
{
  "id": "38e7e201-4f01-4d9a-b39e-be083cdc1d15",
  "name": "Sketch1",
  "params": {
    "constraints": [
      { "entities": ["bee5fd67-8e65-4251-954d-2d8741b05c01"], "id": "2e306830-39b8-49c1-8656-45f9405a1fc9",
        "kind": "length", "name": "Length1", "value": 40.0 }
    ],
    "entities": [
      { "end": [38.0, 2.0], "id": "bee5fd67-8e65-4251-954d-2d8741b05c01", "name": "Line1",
        "start": [0.0, 0.0], "type": "line" }
    ],
    "plane": "xy"
  },
  "type": "Sketch"
}
```

A step can also have `"material"`: the key of the material picked for its body,
either a built-in material's key (like `"pla"` or `"aluminum_6061"`, listed in
`src/model/Material.cpp`) or the `id` of one of the design's own materials. A step
with no material picked has no `"material"` key. A key Khan doesn't know is kept
as it is, and the body counts as PLA.

### materials.json

Only a design that added materials of its own has this file:

```json
{
  "materials": [
    {
      "color": "#3366cc",
      "cost_per_kg": 25.0,
      "density": 1.27,
      "id": "5b0f3c1e-8d2a-4f6b-9c3d-2e1a0b9c8d7f",
      "name": "Blue PETG",
      "poissons_ratio": 0.0,
      "youngs_modulus": 0.0,
      "yield_strength": 0.0
    }
  ]
}
```

| Key | Meaning |
| --- | --- |
| `id` | The material's UUID. Steps refer to it by this, so renaming it later won't break them. |
| `name` | Its name. No two of the design's materials share a name, and none uses a built-in material's name. |
| `density` | g/cm³, from 0.01 to 25. |
| `color` | `#rrggbb`. |
| `youngs_modulus`, `poissons_ratio`, `yield_strength`, `cost_per_kg` | GPa, a ratio, MPa, and price per kg. 0 means unknown. |

Materials are added before the steps, through the same command a student uses
(`material.define`), so the same checks run.

### folders.json

Only a design with folders in its feature tree has this file:

```json
{
  "folders": [
    {
      "id": "3c1f7e2a-9b4d-4e8a-a1f2-6d5c4b3a2918",
      "name": "Tag parts",
      "steps": [
        "926bde46-9c5b-403a-bc25-c01e5cf11ce3",
        "f153a811-ba41-4895-ab27-886a6ca3871e"
      ]
    }
  ]
}
```

| Key | Meaning |
| --- | --- |
| `id` | The folder's UUID. |
| `name` | Its name. No two folders share a name. |
| `steps` | Its steps' ids, in design order. They are next to each other in `timeline.json`, and no step is in two folders. |

Folders only arrange the feature tree; they never change what's built. They are
added after the steps, through the same command a student uses (`folder.add`),
so the same checks run.

### parameters.json

Only a design with [parameters](user-guide/parameters.md) has this file:

```json
{
  "parameters": [
    {
      "expression": "3 mm",
      "id": "98104fd6-668c-4e5e-b457-bcd28e6e0511",
      "name": "wall",
      "note": "How thick the walls are"
    },
    {
      "expression": "wall * 4",
      "id": "d46c5462-e9c0-4a29-add0-0b294fabc63c",
      "name": "height"
    }
  ]
}
```

| Key | Meaning |
| --- | --- |
| `id` | The parameter's UUID. It stays the same when the parameter is renamed. |
| `name` | Its name: letters, digits and `_`, starting with a letter. No two names differ only in upper and lower case. |
| `expression` | Its formula, exactly as typed, with its units: `3 mm`, `1.5 in`, `wall * 4`. Not converted to mm, so it reads back the way the student wrote it. |
| `note` | What it's for (optional; left out when empty). |

Parameters are listed in the order they were added, not the order they're
worked out in. A formula is stored even if it has a problem (it uses a name
that isn't there, say), so the design still opens and the student can fix it.
Names must be ones a parameter can have, or the file counts as damaged.

## Same design, same bytes

Saving a design that hasn't changed gives exactly the same file, so Git shows no
change and a diff of two versions shows only the steps that changed. To make that
work:

- JSON keys are sorted and indented with two spaces, with a newline at the end.
- Every zip entry is dated 1980-01-01 00:00, written directly (not through the
  computer's time zone).
- The file has no "created" or "modified" dates and no hashes.

## Opening a file

Khan reads `manifest.json`, then `parameters.json` and `materials.json` if they're there, then
`timeline.json`, then each step, then `folders.json` if it's there. Each step is
added through the normal add-step command, so the same checks run as when a student
builds the design. The opened design has no undo history and no unsaved changes.

If something is wrong, Khan opens nothing and explains why:

| Code | When |
| --- | --- |
| `file.cant_read` | The file isn't there, or Khan isn't allowed to open it. |
| `file.not_khan` | Not a zip, no `manifest.json`, or `format` isn't `"khan"`. |
| `file.too_new` | `schema` is higher than this Khan reads. The message says to update Khan. |
| `file.unknown_step` | A step's `type` is one this Khan doesn't know (maybe from a plugin). |
| `file.damaged` | Anything else: bad JSON, a missing step file, a missing number, an input that doesn't exist, a material without its id, name, density or color, a folder whose steps aren't next to each other, a parameter without its id, name or expression, or with a name it can't have. |
| `file.cant_write` | (Saving) The folder is missing, read-only or full. |

Saving writes `<name>.saving` first and then renames it over the old file, so a
failed save never damages the last good copy.

## Changing the format

- Adding a step type, or an optional key that older Khan can ignore, keeps schema 1.
  Materials were added this way (Oct 2026): an older Khan opens a design with
  materials, shows every body as plain geometry, and drops the materials if it
  saves the file again. Folders were added the same way: an older Khan shows the
  steps without folders, and drops the folders if it saves the file again.
  Splines, and the constraints only splines use (`point_on_spline`, `smooth`,
  Oct 2026), were added in schema 1 too, but an older Khan can't skip them: it
  won't open a design that has them, and says the file is damaged. The same goes
  for `symmetric`. A spline's
  `source` is an optional key an older Khan skips (and drops if it saves).
  Parameters were added like materials (Oct 2026): an older Khan opens the
  design without them, and drops them if it saves the file again.
- Anything an older Khan would read wrongly bumps `schema` (in
  `src/core/include/kh/core/Version.h`) and adds a migration from the old schema.
- `tests/files/` holds files saved with each schema. They are checked in and never
  regenerated; every version of Khan must still open them. Add one when the schema
  changes.
- Format changes need a line-by-line human review (see CLAUDE.md).

## Not in schema 1 yet

From the design document's file format plan, these come later:

- `components.json` (assemblies, Phase 3).
- Sketch dimensions and step values that use parameters (Phase 2, next).
- A `cache/` folder with built geometry for faster opening.
- Keeping keys this Khan doesn't understand when it saves a file from a newer
  minor version. Today they are dropped.
