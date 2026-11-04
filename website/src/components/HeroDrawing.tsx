// The home page's drawing: the keychain tag from the first tutorial, first as a
// sketch with its sizes (60 mm by 30 mm, a 5 mm hole), then as the solid part
// it becomes. The lines draw themselves once when the page opens.

const MM = 4; // sketch scale: pixels per mm

// The solid tag, seen at an angle (isometric), 60 x 30 x 4 mm.
const S = 4; // part scale: pixels per mm
function iso(x: number, y: number, z: number) {
  const s = S;
  const ox = 304;
  const oy = 226;
  return [ox + (x - y) * Math.cos(Math.PI / 6) * s, oy + (x + y) * Math.sin(Math.PI / 6) * s - z * s];
}
const pts = (list: number[][]) => list.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(" ");

export function HeroDrawing() {
  const sx = 40;
  const sy = 72; // the sketch's top left corner
  const w = 60 * MM;
  const h = 30 * MM;
  const hole = { x: sx + 9 * MM, y: sy + 15 * MM, r: 2.5 * MM };

  const top = [iso(0, 0, 4), iso(60, 0, 4), iso(60, 30, 4), iso(0, 30, 4)];
  const front = [iso(0, 30, 4), iso(60, 30, 4), iso(60, 30, 0), iso(0, 30, 0)];
  const side = [iso(60, 0, 4), iso(60, 30, 4), iso(60, 30, 0), iso(60, 0, 0)];
  const [hx, hy] = iso(9, 15, 4);

  return (
    <svg
      className="hero-drawing"
      viewBox="0 0 560 440"
      role="img"
      aria-label="A keychain tag sketched as a 60 by 30 millimeter rectangle with a 5 millimeter hole, then shown as the solid part it makes"
    >
      {/* The sketch */}
      <g className="sketch">
        <rect className="draw" pathLength={1} x={sx} y={sy} width={w} height={h} rx="0" />
        <circle className="draw draw-late" pathLength={1} cx={hole.x} cy={hole.y} r={hole.r} />
        <circle className="point" cx={sx} cy={sy} r="3" />
        <circle className="point" cx={sx + w} cy={sy} r="3" />
        <circle className="point" cx={sx + w} cy={sy + h} r="3" />
        <circle className="point" cx={sx} cy={sy + h} r="3" />
      </g>
      {/* Its dimensions */}
      <g className="dims">
        <line x1={sx} y1={sy - 8} x2={sx} y2={sy - 34} />
        <line x1={sx + w} y1={sy - 8} x2={sx + w} y2={sy - 34} />
        <line className="dim-line" x1={sx} y1={sy - 26} x2={sx + w} y2={sy - 26} />
        <text x={sx + w / 2} y={sy - 33} textAnchor="middle">
          60 mm
        </text>
        <line x1={sx + w + 8} y1={sy} x2={sx + w + 34} y2={sy} />
        <line x1={sx + w + 8} y1={sy + h} x2={sx + w + 34} y2={sy + h} />
        <line className="dim-line" x1={sx + w + 26} y1={sy} x2={sx + w + 26} y2={sy + h} />
        <text x={sx + w + 34} y={sy + h / 2 + 5}>
          30 mm
        </text>
        <line x1={hole.x + hole.r * 0.7} y1={hole.y + hole.r * 0.7} x2={hole.x + 44} y2={hole.y + 44} />
        <text x={hole.x + 48} y={hole.y + 52}>
          ⌀ 5 mm
        </text>
      </g>
      {/* The part it makes */}
      <g className="part">
        <polygon className="face-top" points={pts(top)} />
        <polygon className="face-front" points={pts(front)} />
        <polygon className="face-side" points={pts(side)} />
        <ellipse className="face-hole" cx={hx} cy={hy} rx={2.5 * S * 1.22} ry={2.5 * S * 0.7} />
        <text className="part-label" x={iso(0, 30, 0)[0] - 14} y={iso(0, 30, 0)[1] + 30} textAnchor="end">
          Extruded 4 mm
        </text>
      </g>
    </svg>
  );
}
