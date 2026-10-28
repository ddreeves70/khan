# Copying and grouping steps

> **Draft.** Parts 1 and 2 (copy, paste and duplicate; turning steps off) are
> built. The other parts are the plan, in the order they'll be built.

Students want to reuse what they've made (another hole like this one, the same
bracket in a new design) and to keep big feature trees tidy. This page covers
six asks, built in this order:

| Part | What the student gets | Status |
| --- | --- | --- |
| 1. Duplicate, Copy and Paste | New steps with the same numbers, in this design or another one | Built |
| 2. Turn steps off together | Turn off (suppress) several picked steps at once, and back on | Built |
| 3. Folders in the feature tree | Put steps in a named folder that opens and closes | Next |
| 4. Move, then linked copies | A Move step, then copies that follow their original | Planned |
| 5. Groups that move together | Bodies grouped into a part you can place as one | Needs its own design |

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
- *Copies sit on top of the originals.* There's no Move step yet (part 4), so the
  student moves a copy by changing its Position. A copied Cut has to be moved by
  moving its inputs, which is one reason for part 4.

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

A folder is a named, persistent object (`FolderId`) holding a run of steps next
to each other in the tree. It's for tidiness only: it doesn't change what gets
built. Folders are saved in the `.khan` file, changed only by commands (add,
rename, move steps in and out, delete folder but keep its steps), and turning a
folder off turns its steps off (part 2). Because this changes the file format,
it gets its own reviewed pull request.

## 4. Move, then linked copies

A **Move** step takes a body and moves or turns it (a distance in mm, an angle in
radians inside the code). With it, a copy can be moved without touching its
inputs. A **linked copy** is a step whose shape is "that step's body, moved": it
follows the original whenever the original changes. Patterns (a row or ring of
copies) come after, on the same idea.

## 5. Groups that move together

Moving several bodies as one is really a *part* placed in an *assembly*
(Phase 4: assemblies and drawings). It needs its own design page first: what's
grouped (bodies, not steps), how it's placed, and how it's saved.
