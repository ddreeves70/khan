# Geometry regression corpus

Each `.json` file here is one reference part. CTest runs every case through
`sketchbillet_cli regress`, on every platform in CI. A case builds a shape from simple steps
and checks the result against recorded values, so any change in kernel behavior
(an OCCT upgrade, a new feature, an AI-written refactor) shows up as a failing case.

## Running

```bash
sketchbillet_cli regress tests/corpus              # all cases
sketchbillet_cli regress tests/corpus/keychain-tag.json
ctest --preset <preset> -R corpus        # through CTest
```

Each case is built twice and must give identical results (kernel operations must be
deterministic).

## Case format

```json
{
  "name": "keychain-tag",
  "description": "What the case covers and why it matters.",
  "steps": [
    { "id": "plate", "op": "box", "corner": [0, 0, 0], "size": [60, 30, 4] },
    { "id": "hole", "op": "cylinder", "base": [9, 15, -1], "axis": [0, 0, 1], "radius": 2.5, "height": 6 },
    { "id": "tag", "op": "cut", "a": "plate", "b": "hole" }
  ],
  "result": "tag",
  "expect": {
    "valid": true,
    "volume": 7121.460184,
    "area": 4343.561945,
    "bounds": [[0, 0, 0], [60, 30, 4]],
    "solids": 1, "faces": 7, "edges": 15, "vertices": 10,
    "tolerance": { "relative": 1e-6, "length": 1e-5 }
  }
}
```

- **Steps:** `box` (`corner`, `size`), `cylinder` (`base`, `axis`, `radius`, `height`),
  `union`, `cut`, `intersect` (`a`, `b` are earlier step ids), `move` (`body`, an
  earlier step id; `by`, `turn` in degrees, `axis`, `about`), `face` (`plane`:
  `origin`, `x`, `y`, default the ground; `outer` and optional `holes`: lists of edges,
  each `{"line": [[x, y], [x, y]]}`, `{"arc": {"center": [x, y], "radius": r, "from":
  degrees, "to": degrees}}`, `{"circle": {"center": [x, y], "radius": r}}` or
  `{"spline": {"poles": [[x, y], ...], "degree": 3, "weights": [...], "closed": false}}`
  (degree, weights and closed are optional; knots are spread evenly), with
  `"reversed": true` for an edge the loop runs backwards), `extrude` (`face`, an
  earlier step id; `by`, the direction and distance), and `fillet` (`radius`) and
  `chamfer` (`distance`) with `body`, an earlier step id, and `edges`, a list of
  points: each the middle of one edge (within 1e-6 mm), so a case doesn't depend on
  the kernel's edge order. Lengths are in mm.
- **`result`:** which step to measure; defaults to the last step.
- **`expect`:** only the keys present are checked. Use `"error": "<code>"` for cases that
  must fail with a specific diagnostic.
- **`tolerance`:** optional. `relative` applies to volume and area, `length` (mm) to bounds.
  Topology counts and `valid` must match exactly.

## Adding a case

1. Write the file with `name`, `description` and `steps` (no `expect`).
2. Record the results: `sketchbillet_cli regress --update tests/corpus/<name>.json`.
3. **Check the recorded values by hand** (volume against a formula, face counts against
   a sketch). The corpus is only useful if what it records is right.
4. Re-run CMake configure so CTest picks up the new file, then commit.

When a kernel change alters results on purpose, run `--update` on the affected cases,
review the diff in each file, and explain the change in the commit message.

**Curved and free-form parts need values worked out without SketchBillet.** Recording
SketchBillet's own output and calling it checked is how a wrong number gets into the
corpus: two spline cases kept OCCT's slightly wrong surface areas until Oct 31,
2026, when the spline cases' values were worked out separately (the B-splines
evaluated with SciPy, areas by Green's theorem, lengths by adaptive integration
per knot span, extremes refined from samples) and compared. Say in the case's
`description` how its numbers were checked.
