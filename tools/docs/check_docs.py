"""Checks Khan's guides against the real kh_cli, so they can't go stale.

Run by ctest as docs.guides. It:

1. Runs every example in docs/cli, docs/tutorials and docs/user-guide that is
   marked with <!-- cli-example --> (or <!-- cli-example exit=N --> when the last
   command should fail) and checks it prints exactly what the page shows.
   In the expected output, "..." matches any text (and the spaces around it): a
   whole line of "..." matches any number of lines.
2. Checks every kh_cli command (from its usage text) has a page in docs/cli, and
   every command `kh_cli help` lists has a section and a tested example in
   docs/cli/commands.md.
3. Checks every relative link and picture in the guides points at a file that exists.
4. Checks docs/dependencies.md lists every package in vcpkg.json and every
   FetchContent download in the CMake files.

Examples run in a fresh folder per page, so a page's examples can build on each
other (a tutorial saves a file, the next step opens it). Paths in commands that
start with build/dev/ are mapped to the real build folder, and paths starting
with tests/ or docs/ to the repo.

    python3 check_docs.py --kh-cli <path> --build-dir <dir> --repo <dir> [--update]

--update writes what kh_cli printed into examples that don't match, when the
example has no "..." in it. Use it after an intended change to kh_cli's output,
then read every change in `git diff` before committing: the guides are only as
right as the outputs you approve.
"""

import argparse
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

GUIDES = ["docs/cli", "docs/tutorials", "docs/user-guide", "docs/plugin-guide"]
EXAMPLE = re.compile(r"<!--\s*cli-example(?:\s+exit=(\d+))?\s*-->\s*\n\s*```console\n(.*?)```", re.S)
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)")

failures = []


def fail(where, message):
    failures.append(f"{where}: {message}")


def matches(expected, actual):
    """Line-by-line match; '...' inside a line matches any text, a line of '...' any lines."""
    if not expected:
        return not actual
    head = expected[0]
    if head.strip() == "...":
        return any(matches(expected[1:], actual[i:]) for i in range(len(actual) + 1))
    if not actual:
        return False
    # "..." also takes the spaces around it, so a column that shifts when a value
    # gets wider (5.3 ms vs 24.2 ms) still matches.
    parts = head.split("...")
    parts = [p.rstrip(" ") if i < len(parts) - 1 else p for i, p in enumerate(parts)]
    parts = [p.lstrip(" ") if i > 0 else p for i, p in enumerate(parts)]
    pattern = "^" + r"\s*.*?\s*".join(re.escape(part) for part in parts) + "$"
    return re.match(pattern, actual[0]) is not None and matches(expected[1:], actual[1:])


def map_arg(arg, args):
    """A path in the repository (build/dev/..., tests/..., docs/...), on its own or as a
    command setting's value (file=docs/...), made into the real path."""
    if "=" in arg and not arg.startswith("-"):
        key, value = arg.split("=", 1)
        mapped = map_arg(value, args)
        return arg if mapped == value else key + "=" + mapped
    for prefix, base in (("build/dev/", args.build_dir), ("tests/", args.repo / "tests"),
                         ("docs/", args.repo / "docs")):
        if arg.startswith(prefix):
            rest = arg[len(prefix):]
            return str(Path(base) / rest) if prefix == "build/dev/" else str(base / rest)
    return arg


def normalize(text, args, workdir):
    for real, shown in ((str(args.build_dir), "build/dev"), (str(args.repo), "."), (str(workdir), ".")):
        text = text.replace(real + os.sep, shown + "/").replace(real, shown)
    return [line.rstrip() for line in text.replace("\r\n", "\n").split("\n")]


def updated_block(blocks, outputs):
    """The text of an example block with each command's output replaced."""
    lines = []
    for (command, _), output in zip(blocks, outputs):
        lines.append("$ " + command)
        lines.extend(output)
    return "\n".join(lines) + "\n"


def run_examples(page, args):
    text = page.read_text(encoding="utf-8")
    replacements = []  # (start, end, new text) for --update
    with tempfile.TemporaryDirectory(prefix="kh_docs_") as tmp:
        workdir = Path(tmp).resolve()
        for m in EXAMPLE.finditer(text):
            want_exit = int(m.group(1) or 0)
            line_no = text[: m.start()].count("\n") + 1
            blocks = []  # (command, expected lines)
            for line in m.group(2).rstrip("\n").split("\n"):
                if line.startswith("$ "):
                    blocks.append((line[2:], []))
                elif blocks:
                    blocks[-1][1].append(line.rstrip())
                else:
                    fail(f"{page}:{line_no}", "example output before any '$ ' command")
            outputs = []
            stale = False
            for i, (command, expected) in enumerate(blocks):
                argv = shlex.split(command)
                if not argv or argv[0] != "kh_cli":
                    fail(f"{page}:{line_no}", f"examples may only run kh_cli: {command}")
                    continue
                argv = [args.kh_cli] + [map_arg(a, args) for a in argv[1:]]
                proc = subprocess.run(argv, cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                actual = normalize(proc.stdout.decode("utf-8", "replace"), args, workdir)
                while actual and actual[-1] == "":
                    actual.pop()
                while expected and expected[-1] == "":
                    expected.pop()
                outputs.append(actual)
                code = want_exit if i == len(blocks) - 1 else 0
                if proc.returncode != code:
                    fail(f"{page}:{line_no}", f"'{command}' exited with {proc.returncode}, the page expects {code}")
                if not matches(expected, actual):
                    if args.update and not any("..." in e for e in expected):
                        stale = True
                        continue
                    fail(f"{page}:{line_no}", f"'{command}' printed something else.\n--- page ---\n" +
                         "\n".join(expected) + "\n--- kh_cli ---\n" + "\n".join(actual))
            if stale:
                replacements.append((m.start(2), m.end(2), updated_block(blocks, outputs)))
    if replacements:
        for start, end, new in reversed(replacements):
            text = text[:start] + new + text[end:]
        page.write_text(text, encoding="utf-8")
        print(f"updated {len(replacements)} example(s) in {page}")


def check_coverage(args):
    usage = subprocess.run([args.kh_cli], stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.decode()
    for name in re.findall(r"kh_cli (\S+)", usage):
        page = args.repo / "docs/cli" / ("version.md" if name == "--version" else f"{name}.md")
        if not page.exists():
            fail("docs/cli", f"kh_cli {name} has no page ({page.name})")

    listing = subprocess.run([args.kh_cli, "help"], stdout=subprocess.PIPE).stdout.decode()
    commands = re.findall(r"^  (\S+\.\S+)\s", listing, re.M)
    page = args.repo / "docs/cli/commands.md"
    text = page.read_text(encoding="utf-8") if page.exists() else ""
    examples = "\n".join(m.group(2) for m in EXAMPLE.finditer(text))
    for name in commands:
        if not re.search(rf"^#+ `?{re.escape(name)}`?\s*$", text, re.M):
            fail("docs/cli/commands.md", f"no section for {name}")
        if not re.search(rf"\$ kh_cli run .*\b{re.escape(name)}\b", examples):
            fail("docs/cli/commands.md", f"no tested example that runs {name}")


def check_api_version(args):
    """The plugin guide states the plugin API version; it must match Api.h."""
    api = (args.repo / "src/plugin/include/kh/plugin/Api.h").read_text(encoding="utf-8")
    m = re.search(r"kPluginApiVersion = (\d+);", api)
    if not m:
        fail("Api.h", "can't find kPluginApiVersion")
        return
    for page in sorted((args.repo / "docs/plugin-guide").rglob("*.md")):
        for said in re.findall(r"This Khan uses \*\*(\d+)\*\*", page.read_text(encoding="utf-8")):
            if said != m.group(1):
                fail(str(page), f"says the plugin API version is {said}, but Api.h says {m.group(1)}")


def check_dependencies(args):
    """docs/dependencies.md must name every vcpkg package and FetchContent download."""
    import json
    page = args.repo / "docs/dependencies.md"
    if not page.exists():
        fail("docs", "docs/dependencies.md is missing")
        return
    listed = page.read_text(encoding="utf-8")
    names = []
    manifest = args.repo / "vcpkg.json"
    if manifest.exists():
        for dep in json.loads(manifest.read_text(encoding="utf-8")).get("dependencies", []):
            names.append(dep if isinstance(dep, str) else dep["name"])
    for cmake in [args.repo / "CMakeLists.txt", *sorted(args.repo.glob("cmake/*.cmake")),
                  *sorted(args.repo.glob("src/*/CMakeLists.txt")), *sorted(args.repo.glob("apps/*/CMakeLists.txt"))]:
        names += re.findall(r"FetchContent_Declare\(\s*(\S+)", cmake.read_text(encoding="utf-8"))
    for name in names:
        if f"`{name}`" not in listed:
            fail("docs/dependencies.md", f"doesn't list `{name}` (from vcpkg.json or FetchContent). Add a row for it.")


def check_links(page):
    text = page.read_text(encoding="utf-8")
    # Ignore links inside code blocks.
    text = re.sub(r"```.*?```", "", text, flags=re.S)
    for target in LINK.findall(text):
        if re.match(r"^[a-z]+:", target) or target.startswith("#"):
            continue
        path = (page.parent / target.split("#")[0]).resolve()
        if not path.exists():
            fail(str(page), f"broken link or picture: {target}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kh-cli", required=True, type=lambda p: str(Path(p).resolve()))
    ap.add_argument("--build-dir", required=True, type=lambda p: Path(p).resolve())
    ap.add_argument("--repo", required=True, type=lambda p: Path(p).resolve())
    ap.add_argument("--update", action="store_true", help="write kh_cli's output into stale examples")
    args = ap.parse_args()

    pages = sorted(p for g in GUIDES for p in (args.repo / g).rglob("*.md"))
    if not pages:
        fail("docs", "no guide pages found")
    for page in pages:
        run_examples(page, args)
        check_links(page)
    check_coverage(args)
    check_api_version(args)
    check_dependencies(args)

    count = sum(len(EXAMPLE.findall(p.read_text(encoding="utf-8"))) for p in pages)
    if failures:
        print("\n\n".join(failures))
        print(f"\n{len(failures)} problem(s) in the guides")
        return 1
    print(f"{len(pages)} pages, {count} examples: all match kh_cli")
    return 0


if __name__ == "__main__":
    sys.exit(main())
