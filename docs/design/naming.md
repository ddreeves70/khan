# Naming faces and edges

> **Draft.** This design changes as SketchBillet is built. Questions and ideas are welcome in [GitHub Discussions](https://github.com/ddreeves70/sketchbillet/discussions).

A fillet rounds *these* edges; a sketch sits on *this* face. When an earlier step
changes, the later step must still find the same edge or face, or say plainly
that it can't. That's **topological naming**, the problem the
[architecture](architecture.md#parametric-modeling-engine) says to design up
front. This page is how SketchBillet does it (Phase 2, built Nov 2026), in
`sb_kernel` (history) and `sb_model` (names and resolving).

## The rules

1. **Faces are named from history.** Each step names the faces of its result
   from where they came from, using what the kernel says it did (OCCT's own
   `Modified` / `Generated` records), never the kernel's numbering.
2. **Edges and vertices are named from their faces.** An edge is the edge
   between two named faces; a vertex is the corner of the faces around it. Only
   faces need history.
3. **Repeats are numbered by position.** Two faces with the same name (one face
   split in two by a cut) get `#1`, `#2`, ... in order of where their middles are
   (along x, then y, then z), never by kernel order.
4. **References keep a name and a signature.** A step that uses an edge saves
   its name and what it looked like when picked (kind of curve, middle,
   direction, length, radius).
5. **Never guess.** Finding a reference again is *Exact* (same name), *Matched*
   (name gone, one clear candidate from the same place), *Ambiguous* or
   *Missing*. The last two stop the step with a plain message: pick again.

## Names

A face's name is `<step>/<role>` or `<step>/<role>:<source>`, where `<step>` and
sketch-entity ids are shortened to their first 8 characters.

| Made by | Faces |
| --- | --- |
| Box | `left`, `right`, `front`, `back`, `bottom`, `top` (by which way each faces) |
| Cylinder | `side`, `bottom`, `top` (top is where the axis points) |
| Extrude | `side:<curve>` for each side, from the sketch line, arc, circle or spline it was swept from; `start:<curve>` and `end:<curve>` for each shape's ends, after the outline curve whose id comes first |
| Boolean, Move | Each face keeps the name it had on its body. Faces merged into one get their names joined by `+` |
| Fillet, Chamfer | Faces it kept keep their names; each round or bevel is `round:<edge>` or `bevel:<edge>`, after the edge it replaced; a corner where several meet is `corner:<vertex>` |
| Anything else | `<step>/new` |

An edge is `E[<face>|<face>]` (the two names sorted; one name for a seam, where a
round face meets itself), and a vertex `V[<face>|<face>|...]`. So a fillet on the
top front edge of an extruded rectangle keeps the edge
`E[<extrude>/end:<line>|<extrude>/side:<Line1>]`: "between the extrude's end and
the side made from Line1". Changing the rectangle's size, the extrude's distance
or which way it goes changes none of these names.

Role words never contain `+`, `|`, `[`, `]` or `#`, which the names use as
separators.

## Finding a reference again

`resolve()` in `sb/model/Naming.h`:

1. **Exact:** an element has the name.
2. **Same place:** elements of the same kind of curve or surface whose name is the
   same before any `#k` (an edge: between faces with the same names). One of them
   → *Matched*. Several → the one most like the signature (middle, direction,
   size, radius, with distances relative to the element's own size), if it's
   close and the next is at least twice as unlike → *Matched*; else *Ambiguous*.
3. **Partly related:** edges that kept one of their two faces (vertices, two of
   their faces). *Matched* only if one clearly looks like the signature; otherwise
   *Missing*.
4. Nothing → *Missing*.

A *Matched* reference builds, with a note ("found an edge by where it is ...
check it's the right one"). [`feature.set_edges`](../cli/commands.md#featureset_edges)
picks edges again.

## People see numbers and words, not names

Names are for matching. People see:

- **Numbers** in [`sketchbillet_cli open --edges`](../cli/open.md#a-bodys-edges-and-faces),
  in listing order (bottom to top, front to back, left to right), which doesn't
  depend on the kernel's order or version. `edges=9,12` on the command line is
  turned into references when the command is made.
- **Words**, from `describeElement()`: "between end of Extrude1 and side from
  Line1 of Sketch1 (Extrude1)".
- **Picking** in the 3D view (next).

## Tested by

- `tests/kernel/test_elements.cpp`: elements and every operation's history,
  against geometry worked out by hand.
- `tests/model/test_naming.cpp`: names are unique, survive size changes and
  which way an extrude goes, carry through cuts and moves; references are
  Exact, Matched, Ambiguous or Missing as the rules say.
- `tests/model/test_fillet.cpp`: fillets and chamfers keep their edges through
  changes, fail plainly when an edge is gone, save and open, and copy with their
  body.
- The corpus cases `fillet-*` and `chamfer-*`, with volumes and areas worked out
  by hand.

## Not yet

- **Picking faces and edges in the 3D view,** and sketching on a face (the next
  pieces of Phase 2).
- **Fuzzing:** random edits on corpus models, checking every reference stays
  Exact or Matched, or is reported (the [design model](design-model.md#threading-errors-and-testing-hooks)
  asks for it).
- **Offering the same repick** to every step that used an edge that went away.
- **Sketch edits that replace a curve** (deleting a line and drawing a new one)
  give its side face a new name, so references to the old one are *Missing*,
  even where a person would call it the same face.
