"""Self-test for check_docs.py's output matcher. Run by ctest as docs.checker."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_docs import matches, updated_block  # noqa: E402

PAGE = ["PASS box-basic                         ... ms", "PASS keychain-tag                      ... ms", "",
        "2 passed, 0 failed"]

CASES = [
    # (expected, actual, should match, why)
    (PAGE, ["PASS box-basic                         1.4 ms", "PASS keychain-tag                      9.9 ms", "",
            "2 passed, 0 failed"], True, "short timings"),
    # Bug: a two-digit time shifts the column one space left (seen on Windows).
    (PAGE, ["PASS box-basic                         5.3 ms", "PASS keychain-tag                     24.2 ms", "",
            "2 passed, 0 failed"], True, "a wider timing"),
    (PAGE, ["PASS box-basic                         5.3 ms", "FAIL keychain-tag                     24.2 ms", "",
            "1 passed, 1 failed"], False, "a real failure"),
    (["a", "...", "z"], ["a", "b", "c", "z"], True, "a line of ... matches several lines"),
    (["a", "...", "z"], ["a", "z"], True, "a line of ... matches no lines"),
    (["a", "z"], ["a", "b", "z"], False, "an extra line"),
    (["error (...)"], ["error (C:\\build\\x.dll)"], True, "... inside brackets"),
    (["exact"], ["exact "], False, "no trailing text without ..."),
]

failed = 0
for expected, actual, want, why in CASES:
    if matches(expected, actual) != want:
        print(f"FAIL: {why}")
        failed += 1
print(f"{len(CASES) - failed} of {len(CASES)} matcher cases pass")

# --update rebuilds a block from its commands and their new output.
block = updated_block([("kh_cli a", ["old"]), ("kh_cli b", [])], [["new 1", "new 2"], ["done"]])
if block != "$ kh_cli a\nnew 1\nnew 2\n$ kh_cli b\ndone\n":
    print("FAIL: updated_block")
    failed += 1
sys.exit(1 if failed else 0)
