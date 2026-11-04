#!/usr/bin/env python3
"""Makes SketchBillet's app icons from packaging/icons/sketchbillet.svg.

Writes, next to the SVG:
  sketchbillet.png   256 x 256 pixels, for Linux (the AppImage and its menu entry)
  sketchbillet.ico   16 to 256 pixels, for Windows (the .exe, the installer, the Start menu)
  sketchbillet.icns  16 to 1024 pixels, for macOS (the app in Finder and the Dock)

Run it after changing the SVG, and commit what it makes:
  python3 tools/packaging/make_icons.py
Needs Python's playwright (to draw the SVG) and Pillow.
"""
import io
import pathlib

from PIL import Image
from playwright.sync_api import sync_playwright

ICONS = pathlib.Path(__file__).resolve().parents[2] / "packaging" / "icons"


def render(svg: str, size: int) -> Image.Image:
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": size, "height": size})
        page.set_content(
            f'<html><body style="margin:0;background:transparent">'
            f'<div style="width:{size}px;height:{size}px">{svg.replace("<svg ", f"<svg width={size} height={size} ")}</div>'
            f"</body></html>"
        )
        png = page.screenshot(omit_background=True, clip={"x": 0, "y": 0, "width": size, "height": size})
        browser.close()
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main() -> None:
    svg = (ICONS / "sketchbillet.svg").read_text()
    big = render(svg, 1024)
    big.resize((256, 256), Image.LANCZOS).save(ICONS / "sketchbillet.png")
    # Small sizes are drawn at their own size, so the lines stay sharp.
    small = {s: render(svg, s) for s in (16, 24, 32, 48, 64, 128, 256)}
    small[256].save(ICONS / "sketchbillet.ico", sizes=[(s, s) for s in small], append_images=list(small.values()))
    big.save(ICONS / "sketchbillet.icns")
    print(f"wrote sketchbillet.png, .ico and .icns in {ICONS}")


if __name__ == "__main__":
    main()
