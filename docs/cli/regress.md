# kh_cli regress

Runs geometry regression cases: small reference parts with recorded results.
Khan's tests run them on every change, so a change in how shapes are built (a new
kernel version, a refactor) shows up as a failing case. This command is mostly for
people working on Khan itself.

```text
kh_cli regress [--update] <case.json|folder>...
```

| Option | Meaning |
| --- | --- |
| `<case.json>` | A case to run. Each case builds a shape and checks its volume, area, size and counts of faces, edges and corners |
| `<folder>` | Run every `.json` case in the folder |
| `--update` | Write the actual results into each case as the new expected values. Only for intended changes: check the new numbers by hand |

Each case prints `PASS` or `FAIL` and how long it took. The exit code is 1 if any
case failed.

## Examples

<!-- cli-example -->
```console
$ kh_cli regress tests/corpus/box-basic.json tests/corpus/keychain-tag.json
PASS box-basic                         ... ms
PASS keychain-tag                      ... ms

2 passed, 0 failed
```

All cases:

<!-- cli-example -->
```console
$ kh_cli regress tests/corpus
...
... passed, 0 failed
```

The case format is described in
[tests/corpus/README.md](../../tests/corpus/README.md).
