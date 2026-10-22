# Geometry regression corpus

Each `.json` file here is one reference part. CTest runs every case through
`kh_cli regress`, on every platform in CI. A case builds a shape from simple steps
and checks the result against recorded values, so any change in kernel behavior
(an OCCT upgrade, a new feature, an AI-written refactor) shows up as a failing case.

## Running

```bash
kh_cli regress tests/corpus              # all cases
kh_cli regress tests/corpus/keychain-tag.json
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
  and `union`, `cut`, `intersect` (`a`, `b` are earlier step ids). Lengths are in mm.
- **`result`:** which step to measure; defaults to the last step.
- **`expect`:** only the keys present are checked. Use `"error": "<code>"` for cases that
  must fail with a specific diagnostic.
- **`tolerance`:** optional. `relative` applies to volume and area, `length` (mm) to bounds.
  Topology counts and `valid` must match exactly.

## Adding a case

1. Write the file with `name`, `description` and `steps` (no `expect`).
2. Record the results: `kh_cli regress --update tests/corpus/<name>.json`.
3. **Check the recorded values by hand** (volume against a formula, face counts against
   a sketch). The corpus is only useful if what it records is right.
4. Re-run CMake configure so CTest picks up the new file, then commit.

When a kernel change alters results on purpose, run `--update` on the affected cases,
review the diff in each file, and explain the change in the commit message.
