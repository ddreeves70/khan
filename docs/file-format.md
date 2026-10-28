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

## Same design, same bytes

Saving a design that hasn't changed gives exactly the same file, so Git shows no
change and a diff of two versions shows only the steps that changed. To make that
work:

- JSON keys are sorted and indented with two spaces, with a newline at the end.
- Every zip entry is dated 1980-01-01 00:00, written directly (not through the
  computer's time zone).
- The file has no "created" or "modified" dates and no hashes.

## Opening a file

Khan reads `manifest.json`, then `materials.json` if it's there, then
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
| `file.damaged` | Anything else: bad JSON, a missing step file, a missing number, an input that doesn't exist, a material without its id, name, density or color, a folder whose steps aren't next to each other. |
| `file.cant_write` | (Saving) The folder is missing, read-only or full. |

Saving writes `<name>.saving` first and then renames it over the old file, so a
failed save never damages the last good copy.

## Changing the format

- Adding a step type, or an optional key that older Khan can ignore, keeps schema 1.
  Materials were added this way (Oct 2026): an older Khan opens a design with
  materials, shows every body as plain geometry, and drops the materials if it
  saves the file again. Folders were added the same way: an older Khan shows the
  steps without folders, and drops the folders if it saves the file again.
- Anything an older Khan would read wrongly bumps `schema` (in
  `src/core/include/kh/core/Version.h`) and adds a migration from the old schema.
- `tests/files/` holds files saved with each schema. They are checked in and never
  regenerated; every version of Khan must still open them. Add one when the schema
  changes.
- Format changes need a line-by-line human review (see CLAUDE.md).

## Not in schema 1 yet

From the design document's file format plan, these come later:

- `parameters.json` (named parameters, Phase 1) and `components.json` (assemblies,
  Phase 3).
- A `cache/` folder with built geometry for faster opening.
- Keeping keys this Khan doesn't understand when it saves a file from a newer
  minor version. Today they are dropped.
