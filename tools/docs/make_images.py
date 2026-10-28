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
     "commands": [...], "app": {"open": "o.khan", "select": ["Cup"], "settings": {"grid/style": "lines"},
                                "trigger": ["kh.viewer.layout.fourpanes"], "flat": false},
     "crop": [x, y, width, height], "callouts": [{"at": [x, y], "n": 1}]}   both optional

A "sketch" recipe draws a sketch twice, side by side: as it was drawn (from the
.khan file) and as the solver left it (from `kh_cli open --sketch`):

    {"out": "docs/user-guide/images/sketch-rectangle.png", "commands": [...],
     "sketch": {"file": "r.khan", "name": "Sketch1"}, "size": "720x300"}

"trigger" clicks menu items and buttons by object name before the picture is
taken. The 3D viewer needs OpenGL: on Linux the app runs on a virtual screen
(xvfb-run, with Mesa's software OpenGL); elsewhere it opens on your screen for a
moment. "flat": true takes the picture without 3D graphics, as the viewer looks
on a computer that can't start them.

Needs Python 3 with NumPy and Matplotlib.
"""

import argparse
import json
import os
import shlex
import shutil
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


def read_drawn(khan, name):
    """The entities of a sketch as drawn: [(name, kind, numbers, construction)], from the .khan zip."""
    import zipfile

    with zipfile.ZipFile(khan) as z:
        for entry in z.namelist():
            if not entry.startswith("features/"):
                continue
            step = json.loads(z.read(entry))
            if step.get("type") == "Sketch" and step.get("name") == name:
                out = []
                for e in step["params"]["entities"]:
                    t = e["type"]
                    if t == "point":
                        nums = e["at"]
                    elif t == "line":
                        nums = e["start"] + e["end"]
                    elif t == "circle":
                        nums = e["center"] + [e["radius"]]
                    else:
                        import math

                        nums = e["center"] + [e["radius"], math.degrees(e["start_angle"]),
                                              math.degrees(e["end_angle"])]
                    out.append((e["name"], t, nums, e.get("construction", False)))
                return out
    sys.exit(f"{khan}: no sketch called {name}")


def read_solved(text):
    """Parses `kh_cli open --sketch` output: (state line, [(name, kind, numbers, construction)])."""
    import re

    state = ""
    entities = []
    for line in text.splitlines():
        if " plane): " in line:
            state = line.split(" plane): ", 1)[1]
            continue
        m = re.match(r"\s+(\S+)\s+(point|line|circle|arc)\s+(.*)$", line)
        if not m:
            continue
        nums = [float(x) for x in re.findall(r"-?\d+\.\d+", m.group(3))]
        entities.append((m.group(1), m.group(2), nums, "(construction)" in line))
    return state, entities


def draw_sketch(out, panels, size):
    """Draws sketches side by side: panels is [(title, entities)]."""
    import math

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Arc, Circle

    w, h = size
    fig, axes = plt.subplots(1, len(panels), figsize=(w / 100, h / 100), dpi=100)
    xs, ys = [], []
    for _, entities in panels:
        for _, kind, n, _ in entities:
            if kind == "point":
                xs += [n[0]]; ys += [n[1]]
            elif kind == "line":
                xs += [n[0], n[2]]; ys += [n[1], n[3]]
            else:
                xs += [n[0] - n[2], n[0] + n[2]]; ys += [n[1] - n[2], n[1] + n[2]]
    pad = 0.15 * max(max(xs) - min(xs), max(ys) - min(ys), 1)
    for ax, (title, entities) in zip(axes, panels):
        ax.set_aspect("equal")
        ax.set_xlim(min(xs) - pad, max(xs) + pad)
        ax.set_ylim(min(ys) - pad, max(ys) + pad)
        ax.grid(True, color="#d8dee4", linewidth=0.6)
        ax.set_axisbelow(True)
        ax.tick_params(labelsize=7, colors="#57606a")
        ax.set_title(title, fontsize=10, color="#1f2328")
        for name, kind, n, construction in entities:
            color = "#8c959f" if construction else "#0969da"
            style = (0, (4, 3)) if construction else "solid"
            if kind == "point":
                ax.plot([n[0]], [n[1]], "o", color=color, markersize=4)
                at = (n[0], n[1])
            elif kind == "line":
                ax.plot([n[0], n[2]], [n[1], n[3]], color=color, linewidth=2, linestyle=style)
                ax.plot([n[0], n[2]], [n[1], n[3]], "o", color="#1f2328", markersize=3)
                at = ((n[0] + n[2]) / 2, (n[1] + n[3]) / 2)
            elif kind == "circle":
                ax.add_patch(Circle((n[0], n[1]), n[2], fill=False, color=color, linewidth=2, linestyle=style))
                at = (n[0], n[1] + n[2])
            else:
                ax.add_patch(Arc((n[0], n[1]), 2 * n[2], 2 * n[2], theta1=n[3], theta2=n[4], color=color,
                                 linewidth=2, linestyle=style))
                mid = math.radians((n[3] + n[4]) / 2)
                at = (n[0] + n[2] * math.cos(mid), n[1] + n[2] * math.sin(mid))
            ax.annotate(name, at, textcoords="offset points", xytext=(4, 4), fontsize=7, color="#57606a")
    fig.tight_layout()
    fig.savefig(str(out), dpi=100)
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
                if "trigger" in app:
                    argv += ["--trigger", ",".join(app["trigger"])]
                env = dict(os.environ)
                if app.get("flat"):
                    env.update(QT_QPA_PLATFORM="offscreen", KH_NO_3D="1")
                elif sys.platform.startswith("linux"):
                    xvfb = shutil.which("xvfb-run")
                    if not xvfb:
                        sys.exit(f"{out.name}: pictures of the 3D view need xvfb-run (apt install xvfb)")
                    argv = [xvfb, "-a", "-s", "-screen 0 1600x1200x24"] + argv
                    env.update(QT_QPA_PLATFORM="xcb", LIBGL_ALWAYS_SOFTWARE="1")
                out.parent.mkdir(parents=True, exist_ok=True)
                proc = subprocess.run(argv, cwd=tmp, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if proc.returncode != 0 or not out.exists():
                    sys.exit(f"{out.name}: kh_app failed:\n{proc.stdout.decode()}")
                mark_up(out, recipe.get("crop"), recipe.get("callouts", []))
                print(f"wrote {out.relative_to(repo)}")
                continue
            if "sketch" in recipe:
                sk = recipe["sketch"]
                proc = subprocess.run([kh_cli, "open", sk["file"], "--sketch", sk["name"]], cwd=tmp,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                state, solved = read_solved(proc.stdout.decode())
                drawn = read_drawn(Path(tmp) / sk["file"], sk["name"])
                w, h = (int(v) for v in recipe.get("size", "720x300").split("x"))
                out.parent.mkdir(parents=True, exist_ok=True)
                draw_sketch(out, [("As drawn", drawn), (f"Solved: {state}", solved)], (w, h))
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
