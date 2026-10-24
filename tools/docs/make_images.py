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

An "app" recipe takes a screenshot of the desktop app instead (pass --kh-app):

    {"out": "docs/user-guide/images/app-main.png",
     "commands": [...], "app": {"open": "o.khan", "select": ["Cup"], "settings": {"grid/style": "lines"}},
     "crop": [x, y, width, height], "callouts": [{"at": [x, y], "n": 1}]}   both optional

Needs Python 3 with NumPy and Matplotlib.
"""

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import render  # noqa: E402


def mark_up(png, crop, callouts):
    """Crops a screenshot ([x, y, width, height] in pixels) and draws numbered
    callouts ({"at": [x, y], "n": 1}, in the cropped picture's pixels)."""
    if not crop and not callouts:
        return
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.image as mpimg
    import matplotlib.pyplot as plt

    img = mpimg.imread(str(png))
    if crop:
        x, y, w, h = crop
        img = img[y : y + h, x : x + w]
    h, w = img.shape[:2]
    fig = plt.figure(figsize=(w / 100, h / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.imshow(img, interpolation="none")
    ax.set_axis_off()
    for c in callouts:
        cx, cy = c["at"]
        ax.add_patch(plt.Circle((cx, cy), 13, color="#d1453b", zorder=3))
        ax.text(cx, cy, str(c["n"]), color="white", fontsize=11, fontweight="bold", ha="center", va="center",
                zorder=4)
    fig.savefig(str(png), dpi=100)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kh-cli", required=True)
    ap.add_argument("--kh-app", help="kh_app, for screenshots of the desktop app (recipes with \"app\")")
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
                if proc.returncode != 0 and recipe.get("check", True):  # "check": false for pictures of failures
                    sys.exit(f"{out.name}: '{command}' failed:\n{proc.stdout.decode()}")
            if "app" in recipe:
                # A screenshot of the desktop app with a design open.
                if not args.kh_app:
                    print(f"skipped {out.relative_to(repo)}: needs --kh-app")
                    continue
                app = recipe["app"]
                w, h = (int(v) for v in recipe.get("size", "1280x800").split("x"))
                if "settings" in app:
                    # Start the app with these settings, e.g. {"grid/style": "lines"}.
                    sections = {}
                    for key, value in app["settings"].items():
                        group, name = key.split("/", 1)
                        sections.setdefault(group, []).append(f"{name}={value}")
                    ini = "".join(f"[{g}]\n" + "\n".join(lines) + "\n" for g, lines in sections.items())
                    (Path(tmp) / "settings.ini").write_text(ini)
                argv = [str(Path(args.kh_app).resolve()), "--settings", str(Path(tmp) / "settings.ini"),
                        "--screenshot", str(out)]
                if "open" in app:
                    argv += ["--open", app["open"]]
                if "select" in app:
                    argv += ["--select", ",".join(app["select"])]
                env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
                out.parent.mkdir(parents=True, exist_ok=True)
                proc = subprocess.run(argv, cwd=tmp, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if proc.returncode != 0 or not out.exists():
                    sys.exit(f"{out.name}: kh_app failed:\n{proc.stdout.decode()}")
                mark_up(out, recipe.get("crop"), recipe.get("callouts", []))
                print(f"wrote {out.relative_to(repo)}")
                continue
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
