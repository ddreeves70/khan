"""Makes the pictures in Khan's guides from real designs.

Each picture in docs/pictures.json is built by running kh_cli commands in a
fresh folder, exporting the steps to show as STL, and rendering them with
render.py. Run it after changing a picture's recipe or anything that changes how
parts are built, then commit the PNG files:

    python3 tools/docs/make_images.py --kh-cli build/dev/apps/kh_cli/kh_cli [name...]

docs/pictures.json holds {"scripts": {name: [commands]}, "pictures": [recipe, ...]}.
A recipe (a "script" runs that shared list of commands before its own "commands"):

    {
      "out": "docs/tutorials/images/tag-hole.png",   picture to write
      "size": "560x360",                              optional, pixels
      "view": "iso",                                   optional: iso, front, top, right
      "commands": ["run --save tag.khan feature.add_box size=60,30,4"],
      "show": [{"file": "tag.khan", "step": "Plate", "color": "blue"},
               {"file": "tag.khan", "step": "Hole", "color": "orange", "ghost": true}],
      "labels": [{"at": [0, 0, 0], "text": "origin", "color": "#1f2328"}]   optional
    }

Needs Python 3 with NumPy and Matplotlib.
"""

import argparse
import json
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import render  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kh-cli", required=True)
    ap.add_argument("--repo", default=str(HERE.parent.parent))
    ap.add_argument("names", nargs="*", help="only pictures whose file name contains one of these")
    args = ap.parse_args()
    repo = Path(args.repo).resolve()
    kh_cli = str(Path(args.kh_cli).resolve())
    data = json.loads((repo / "docs/pictures.json").read_text(encoding="utf-8"))
    scripts = data.get("scripts", {})

    for recipe in data["pictures"]:
        out = repo / recipe["out"]
        if args.names and not any(n in out.name for n in args.names):
            continue
        with tempfile.TemporaryDirectory(prefix="kh_pictures_") as tmp:
            commands = scripts.get(recipe.get("script"), []) + recipe.get("commands", [])
            for command in commands:
                argv = [kh_cli] + [str(repo / a) if a.startswith(("tests/", "docs/")) else a
                                   for a in shlex.split(command)]
                proc = subprocess.run(argv, cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if proc.returncode != 0:
                    sys.exit(f"{out.name}: '{command}' failed:\n{proc.stdout.decode()}")
            bodies = []
            for i, show in enumerate(recipe["show"]):
                stl = Path(tmp) / f"body{i}.stl"
                argv = [kh_cli, "open", show["file"], "--export", str(stl)]
                if "step" in show:
                    argv += ["--step", show["step"]]
                proc = subprocess.run(argv, cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if proc.returncode != 0:
                    sys.exit(f"{out.name}: export failed:\n{proc.stdout.decode()}")
                ghost = show.get("ghost", False)  # true (see-through), "outline" (edges only) or false
                bodies.append((render.load_stl(stl), render.parse_color(show.get("color", "blue")), ghost))
            w, h = (int(v) for v in recipe.get("size", "560x360").split("x"))
            out.parent.mkdir(parents=True, exist_ok=True)
            labels = [(lab["at"], lab["text"], lab.get("color", "#1f2328")) for lab in recipe.get("labels", [])]
            render.render(bodies, str(out), recipe.get("view", "iso"), (w, h), labels)
            print(f"wrote {out.relative_to(repo)}")


if __name__ == "__main__":
    main()
