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

## Same design, same bytes

Saving a design that hasn't changed gives exactly the same file, so Git shows no
change and a diff of two versions shows only the steps that changed. To make that
work:

- JSON keys are sorted and indented with two spaces, with a newline at the end.
- Every zip entry is dated 1980-01-01 00:00, written directly (not through the
  computer's time zone).
- The file has no "created" or "modified" dates and no hashes.

## Opening a file

Khan reads `manifest.json`, then `timeline.json`, then each step. Each step is
added through the normal add-step command, so the same checks run as when a student
builds the design. The opened design has no undo history and no unsaved changes.

If something is wrong, Khan opens nothing and explains why:

| Code | When |
| --- | --- |
| `file.cant_read` | The file isn't there, or Khan isn't allowed to open it. |
| `file.not_khan` | Not a zip, no `manifest.json`, or `format` isn't `"khan"`. |
| `file.too_new` | `schema` is higher than this Khan reads. The message says to update Khan. |
| `file.unknown_step` | A step's `type` is one this Khan doesn't know (maybe from a plugin). |
| `file.damaged` | Anything else: bad JSON, a missing step file, a missing number, an input that doesn't exist. |
| `file.cant_write` | (Saving) The folder is missing, read-only or full. |

Saving writes `<name>.saving` first and then renames it over the old file, so a
failed save never damages the last good copy.

## Changing the format

- Adding a step type, or an optional key that older Khan can ignore, keeps schema 1.
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
