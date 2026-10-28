# Copying and grouping steps

> **Draft.** Parts 1 to 3 and the Move step are built (Oct 27 to 28, 2026). They
> were pulled forward from later phases after Phase 0; linked copies wait for
> Phase 2's patterns, and groups that move together for Phase 4's assemblies (see
> the [roadmap](roadmap.md)).

Students want to reuse what they've made (another hole like this one, the same
bracket in a new design) and to keep big feature trees tidy. This page covers
six asks, built in this order:

| Part | What the student gets | Status |
| --- | --- | --- |
| 1. Duplicate, Copy and Paste | New steps with the same numbers, in this design or another one | Built |
| 2. Turn steps off together | Turn off (suppress) several picked steps at once, and back on | Built |
| 3. Folders in the feature tree | Put steps in a named folder that opens and closes | Built |
| 4. Move, then linked copies | A Move step (built), then copies that follow their original | Move built; linked copies in Phase 2 |
| 5. Groups that move together | Bodies grouped into a part you can place as one | Phase 4 (assemblies) |

Copy between designs is part 1: the clipboard works across designs.

## 1. Duplicate, Copy and Paste

**What it does.** Copying makes *new* steps, with new ids, the same sizes and
positions, and the next free names. Copying a step also copies every step it
uses (its inputs, their inputs, and so on), so a copied Cut keeps its tool. The
copies are independent: changing one never changes the other.

**How it's built.**

- `kh_model`: `copySteps(doc, ids)` finds the steps and everything they use, in
  design order, and gives them new ids with `freshIds()`, which also points each
  copy's inputs at the other copies (`Feature::remapInputs`). `AddFeaturesCommand`
  adds a list of steps at the end of the design as **one** undo step: it renames
  taken names, adds the custom materials the steps use, and checks every input.
  If any step can't be added, nothing is.
- `feature.duplicate` is a registry command (so `kh_cli run`, Python and the AI
  assistant get it too), and kh.modeling adds Edit > Duplicate step (Ctrl+D).
- `kh_io`: `stepsToText()` writes the copied steps as JSON in the same form a
  `.khan` file uses, plus the custom materials they use:
  `{"format": "khan-steps", "version": 1, "steps": [...], "materials": [...]}`.
  `stepsFromText()` reads it back with new ids and plain-word errors
  (`paste.not_steps`, `paste.too_new`, `paste.damaged`, `paste.unknown_step`).
  Every input must be an earlier copied step: pasted text can never point into
  the design it's pasted into.
- The desktop app puts that text on the system clipboard twice: as
  `application/x-khan-steps`, and as plain text, so steps can be pasted into a
  message and back. Edit > Copy and Edit > Paste are the shell's own items, next
  to Undo and Redo.

**Choices.**

- *Paste goes at the end.* A step can only use steps above it, so the end is
  always safe. Pasting after the picked step can come with folders.
- *Materials match by key, then by name.* A custom material's key is a UUID, so
  pasting twice doesn't add it twice. If the design already has its own material
  with the same name, the steps use that one, so there are never two "Blue PETG"s.
- *Copies sit on top of the originals.* The student moves a copy with a Move step
  (part 4).

## 2. Turn steps off together

Steps could already be off (`suppressed` is saved in `.khan` files and shown as
`off` in the feature tree); now the student can change it.

- `kh_model`: `SuppressFeaturesCommand(ids, off)` turns several steps off or on
  as **one** undo step. Steps already that way are left alone, and undo only
  changes the ones it changed. If none would change, it says so
  (`feature.already_off`, `feature.already_on`).
- `feature.suppress` takes `steps` (a new argument type, `FeatureList`: one or
  more steps, `steps=Box1,Hole` on the command line) and `off` (default true).
- Plugin API 11: `ActionSpec::fromSelectionList` fills a `FeatureList` argument
  with every selected step and enables the item when one or more are selected.
  kh.modeling uses it for **Edit > Turn off** and **Edit > Turn on**. The feature
  tree's right-click menu has the same item, for the picked steps (or just the
  row clicked, if it isn't picked).
- A step that uses a step that's off is `blocked`, with a message naming the step
  to turn back on, as before.

## 3. Folders in the feature tree

A folder is a named, persistent object (`FolderId`) holding steps that are next to
each other in the tree. It's for tidiness only: it never changes what gets built.

- `kh_model`: `Folder {id, name, steps}`. `AddFolderCommand`, `RenameFolderCommand`
  and `DeleteFolderCommand` (which keeps the steps) are the only ways to change
  folders. `Document::folders()` lists them in tree order, each with only the
  steps still in the design; a folder whose steps were all deleted isn't listed
  (and comes back when undo brings a step back). So deleting a step never has to
  touch folders, and undo stays simple.
- `folder.add` (steps, name), `folder.rename` (folder, name) and `folder.delete`
  (folder) are registry commands. A new argument type, `FolderRef`, names a folder
  (by name on the command line). Plugin API 12.
- File: an optional `folders.json` (see [the file format](../file-format.md#foldersjson)),
  read after the steps through `folder.add`, so the same checks run. Designs
  without folders save byte for byte as before, and the schema stays 1: an older
  Khan opens the steps and drops the folders.
- Feature tree: a folder row (kind "Folder") with its steps under it. Picking the
  row picks its steps, so Turn off works on a folder; a folder shows `off` when
  all its steps are. Right-click a folder for Rename folder, Turn off or on, and
  Remove folder. Whether a folder is open is remembered while Khan runs, not
  saved.

**Choices.**

- *Steps next to each other only.* A folder is a stretch of the tree, so it
  always shows steps in build order. Moving steps up and down (reordering) comes
  later; when it does, it has to keep folders together.
- *One level.* Folders can't hold folders yet. `folders.json` can grow a `parent`
  key later without breaking older files.
- *Not copied.* Copy and Paste copy steps, not folders.

## 4. Move, then linked copies

**Move (built).** A Move step takes a body, turns it about a line, then slides it.
The body keeps its size and shape, and the step it moves is used up, like a
Boolean's inputs.

- `kh_kernel`: `IKernel::transform(shape, Motion)`, with `Motion {offset, center,
  axis, angle}` (mm and radians). It's added at the end of `IKernel`, so the
  plugin API doesn't change. The OCCT adapter copies the geometry
  (`BRepBuilderAPI_Transform` with copy on), so moved shapes have no hidden
  placement and mesh and export like any other. Corpus cases: `move-slide`,
  `move-turn`, `move-turn-then-slide` (the turn comes first) and
  `invalid-move-axis`.
- `kh_model`: `MoveFeature` (`FeatureType::Move`), `feature.add_move` (`body`,
  `by`, `turn` in degrees, `axis`, `about`). A moved body is made of what the
  body it moves is made of: `materialOf()` now follows any step's first input,
  not just a Boolean's A.
- File: a new step type, `"Move"`, with `inputs.body` and `params.offset`,
  `center`, `axis` and `angle` (radians). New step types keep schema 1; an older
  Khan says the step is a kind it doesn't know.
- App: **Modify > Move** (Ctrl+M) adds a Move of the picked step; Properties
  shows Move (mm), Turn (degrees), Axis and Center (mm).

**Linked copies (Phase 2).** A linked copy is a step whose shape is "that step's
body, moved", which keeps the original as a body too and follows it when it
changes. It's the same idea as Phase 2's linear and circular patterns, so it'll be
built with them: a Move whose input isn't used up, and patterns as many of them.

## 5. Groups that move together (Phase 4)

Moving several bodies as one is really a *part* placed in an *assembly*
(Phase 4: assemblies and drawings). It needs its own design page first: what's
grouped (bodies, not steps), how it's placed, and how it's saved.
