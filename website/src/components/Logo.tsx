// The mark: a billet (a block of stock) seen at an angle, with a sketch line
// and a dimension on its front face. Drawn in the text's color.
export function Mark({ size = 28 }: { size?: number }) {
  return (
    <svg className="mark" width={size} height={size} viewBox="0 0 32 32" aria-hidden="true">
      <path d="M4 11 L16 5 L28 11 L28 23 L16 29 L4 23 Z" fill="none" stroke="currentColor" strokeWidth="2" strokeLinejoin="round" />
      <path d="M4 11 L16 17 L28 11 M16 17 L16 29" fill="none" stroke="currentColor" strokeWidth="2" strokeLinejoin="round" />
      <path d="M7 19.5 L13 22.5" stroke="var(--safety)" strokeWidth="2" strokeLinecap="round" />
    </svg>
  );
}

export function Wordmark() {
  return (
    <a className="wordmark" href="/" aria-label="SketchBillet, home">
      <Mark />
      <span>
        Sketch<span className="wordmark-billet">Billet</span>
      </span>
    </a>
  );
}
