"""Renders STL files to a PNG picture for Khan's guides.

A small software renderer (NumPy z-buffer), so the pictures look the same on
every computer and need no GPU or window:

  * an isometric-style view, flat shading from two lights,
  * dark lines on sharp edges (like a CAD view),
  * bodies drawn solid, or as see-through "ghosts" (to show a tool body that a
    cut removes).

Usage (normally called by make_images.py):

    python3 render.py out.png part.stl[:color[:ghost|outline]] [more.stl...]
        [--view iso|front|top|right] [--size 640x420]

Colors: blue, orange, green, gray, red, purple, or #rrggbb.
"""

import argparse
import struct
import sys

import numpy as np

COLORS = {
    "blue": (0.36, 0.56, 0.86),
    "orange": (0.93, 0.56, 0.24),
    "green": (0.38, 0.70, 0.45),
    "gray": (0.70, 0.71, 0.73),
    "red": (0.86, 0.33, 0.33),
    "purple": (0.60, 0.46, 0.82),
}
EDGE = np.array([0.16, 0.18, 0.22])
BACKGROUND = np.array([1.0, 1.0, 1.0])
SUPERSAMPLE = 3


def parse_color(text):
    if text.startswith("#") and len(text) == 7:
        return tuple(int(text[i : i + 2], 16) / 255 for i in (1, 3, 5))
    return COLORS[text]


def load_stl(path):
    """Returns triangles as an (n, 3, 3) array, from binary or ASCII STL."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) >= 84:
        count = struct.unpack_from("<I", data, 80)[0]
        if 84 + count * 50 == len(data):
            rec = np.frombuffer(data, dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]),
                                count=count, offset=84)
            return rec["v"].reshape(-1, 3, 3).astype(np.float64)
    verts = [list(map(float, line.split()[1:4])) for line in data.decode("ascii", "replace").splitlines()
             if line.strip().startswith("vertex")]
    return np.array(verts, dtype=np.float64).reshape(-1, 3, 3)


def view_matrix(view):
    """Rotation that maps model coordinates (z up) to screen (x right, y up, z toward viewer)."""
    if view == "front":   # looking along +y
        return np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=float)
    if view == "top":     # looking down -z
        return np.eye(3)
    if view == "right":   # looking along -x
        return np.array([[0, 1, 0], [0, 0, 1], [1, 0, 0]], dtype=float)
    az, el = np.radians(-50.0), np.radians(28.0)  # iso-style: from the front right, a little above
    rz = np.array([[np.cos(az), -np.sin(az), 0], [np.sin(az), np.cos(az), 0], [0, 0, 1]])
    tilt = np.array([[1, 0, 0], [0, np.sin(el), np.cos(el)], [0, -np.cos(el), np.sin(el)]])
    return tilt @ rz


def feature_edges(tris, rot, angle_deg=25.0):
    """Edges where two triangles meet at a sharp angle, edges with only one triangle,
    and outline (silhouette) edges, where one triangle faces the viewer and the other doesn't."""
    normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    keep = lengths > 1e-12
    normals[keep] /= lengths[keep, None]
    edges = {}
    for t, tri in enumerate(tris):
        if not keep[t]:
            continue
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            ka, kb = tuple(np.round(a, 5)), tuple(np.round(b, 5))
            key = (ka, kb) if ka < kb else (kb, ka)
            edges.setdefault(key, []).append(t)
    limit = np.cos(np.radians(angle_deg))
    facing = (normals @ rot.T)[:, 2]
    out = []
    for (a, b), ts in edges.items():
        sharp = len(ts) == 1 or any(np.dot(normals[ts[0]], normals[o]) < limit for o in ts[1:])
        outline = any((facing[ts[0]] > 0) != (facing[o] > 0) for o in ts[1:])
        if sharp or outline:
            out.append((np.array(a), np.array(b)))
    return out


class Canvas:
    def __init__(self, width, height):
        self.w, self.h = width * SUPERSAMPLE, height * SUPERSAMPLE
        self.color = np.tile(BACKGROUND, (self.h, self.w, 1))
        self.depth = np.full((self.h, self.w), np.inf)

    def triangles(self, pts, shades, color, alpha=1.0, write_depth=True):
        """pts: (n, 3, 3) screen-space points (x, y in pixels, z = distance; smaller is nearer)."""
        color = np.array(color)
        for tri, shade in zip(pts, shades):
            x, y, z = tri[:, 0], tri[:, 1], tri[:, 2]
            x0, x1 = max(int(np.floor(x.min())), 0), min(int(np.ceil(x.max())), self.w - 1)
            y0, y1 = max(int(np.floor(y.min())), 0), min(int(np.ceil(y.max())), self.h - 1)
            if x0 > x1 or y0 > y1:
                continue
            area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0])
            if abs(area) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((x[1] - gx) * (y[2] - gy) - (x[2] - gx) * (y[1] - gy)) / area
            w1 = ((x[2] - gx) * (y[0] - gy) - (x[0] - gx) * (y[2] - gy)) / area
            w2 = 1 - w0 - w1
            inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
            if not inside.any():
                continue
            zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
            region = self.depth[y0 : y1 + 1, x0 : x1 + 1]
            nearer = inside & (zz < region - 1e-7)
            if not nearer.any():
                continue
            target = self.color[y0 : y1 + 1, x0 : x1 + 1]
            lit = np.clip(color * shade, 0, 1)
            target[nearer] = lit if alpha >= 1 else target[nearer] * (1 - alpha) + lit * alpha
            if write_depth:
                region[nearer] = zz[nearer]

    def lines(self, segments, color, width, alpha=1.0, bias=0.0):
        """segments: list of (p, q) screen points. Hidden parts are skipped using the depth buffer."""
        color = np.array(color)
        r = width / 2
        for p, q in segments:
            length = np.hypot(*(q[:2] - p[:2]))
            steps = max(int(length * 1.5), 1)
            t = np.linspace(0, 1, steps + 1)
            pts = p[None, :] + (q - p)[None, :] * t[:, None]
            for px, py, pz in pts:
                ix0, ix1 = int(px - r), int(px + r)
                iy0, iy1 = int(py - r), int(py + r)
                for iy in range(max(iy0, 0), min(iy1, self.h - 1) + 1):
                    for ix in range(max(ix0, 0), min(ix1, self.w - 1) + 1):
                        if pz <= self.depth[iy, ix] + bias:
                            c = self.color[iy, ix]
                            self.color[iy, ix] = c * (1 - alpha) + color * alpha

    def image(self):
        s = SUPERSAMPLE
        img = self.color.reshape(self.h // s, s, self.w // s, s, 3).mean(axis=(1, 3))
        return np.flipud(img)  # row 0 is the top of the picture


def render(bodies, out, view="iso", size=(640, 420), labels=()):
    """bodies: list of (triangles, color, ghost). labels: list of (point xyz in mm, text, color)."""
    rot = view_matrix(view)
    all_pts = np.concatenate([b[0].reshape(-1, 3) for b in bodies]) @ rot.T
    lo, hi = all_pts.min(axis=0), all_pts.max(axis=0)
    width, height = size
    margin = 0.08
    scale = min(width * (1 - 2 * margin) / max(hi[0] - lo[0], 1e-9),
                height * (1 - 2 * margin) / max(hi[1] - lo[1], 1e-9)) * SUPERSAMPLE
    center = (lo + hi) / 2
    depth_span = max(hi[2] - lo[2], 1e-9)

    def to_screen(p):
        s = (p @ rot.T - center) * scale
        s[..., 0] += width * SUPERSAMPLE / 2
        s[..., 1] += height * SUPERSAMPLE / 2
        s[..., 2] = -(p @ rot.T)[..., 2] / depth_span  # nearer = smaller
        return s

    lights = [(np.array([-0.4, 0.5, 0.75]), 0.62), (np.array([0.6, -0.2, 0.5]), 0.22)]
    lights = [(d / np.linalg.norm(d), k) for d, k in lights]

    def shades(tris):
        n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]) @ rot.T
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        n[n[:, 2] < 0] *= -1  # light both sides the same
        s = np.full(len(tris), 0.30)
        for d, k in lights:
            s += k * np.clip(n @ d, 0, None)
        return s

    canvas = Canvas(width, height)
    solid = [b for b in bodies if not b[2]]
    ghosts = [b for b in bodies if b[2]]  # b[2]: True or "ghost" = see-through, "outline" = edges only
    for tris, color, _ in solid:
        canvas.triangles(to_screen(tris), shades(tris), color)
    for tris, color, _ in solid:
        segs = [(to_screen(a), to_screen(b)) for a, b in feature_edges(tris, rot)]
        canvas.lines(segs, EDGE, width=1.6 * SUPERSAMPLE / 2, bias=2e-3)
    # Ghosts: drawn on their own layer (so parts inside a solid still show), then
    # laid over the picture see-through, with their edges on top.
    for tris, color, style in ghosts:
        if style != "outline":
            layer = Canvas(width, height)
            layer.color[:] = np.nan  # nan = not covered
            layer.triangles(to_screen(tris), shades(tris), color)
            covered = ~np.isnan(layer.color[..., 0])
            # Strong where the ghost is in front of the solids, faint where it is behind or inside them.
            strength = np.where(layer.depth < canvas.depth, 0.45, 0.22)[covered][:, None]
            canvas.color[covered] = canvas.color[covered] * (1 - strength) + layer.color[covered] * strength
        segs = [(to_screen(a), to_screen(b)) for a, b in feature_edges(tris, rot)]
        canvas.lines(segs, np.array(color) * 0.55, width=1.4 * SUPERSAMPLE / 2, alpha=0.9, bias=1e9)

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.image as mpimg
    import matplotlib.pyplot as plt

    image = np.clip(canvas.image(), 0, 1)
    if not labels:
        mpimg.imsave(out, image)
        return
    fig = plt.figure(figsize=(width / 100, height / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.imshow(image, interpolation="none")
    ax.set_axis_off()
    for point, text, color in labels:
        sx, sy = to_screen(np.array(point, dtype=float))[:2] / SUPERSAMPLE
        ax.text(sx, height - sy, text, color=parse_color(color) if isinstance(color, str) else color,
                fontsize=12, fontweight="bold", ha="center", va="center",
                bbox=dict(boxstyle="round,pad=0.2", fc="white", ec="none", alpha=0.85))
    fig.savefig(out, dpi=100)
    plt.close(fig)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("bodies", nargs="+", help="file.stl[:color[:ghost]]")
    ap.add_argument("--view", default="iso", choices=["iso", "front", "top", "right"])
    ap.add_argument("--size", default="640x420")
    args = ap.parse_args(argv)
    bodies = []
    for spec in args.bodies:
        parts = spec.split(":")
        color = parse_color(parts[1]) if len(parts) > 1 else COLORS["blue"]
        bodies.append((load_stl(parts[0]), color, parts[2] if len(parts) > 2 else False))
    w, h = (int(v) for v in args.size.split("x"))
    render(bodies, args.out, args.view, (w, h))


if __name__ == "__main__":
    main(sys.argv[1:])
