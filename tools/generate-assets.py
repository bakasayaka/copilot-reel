"""Rasterize the Soft Orbit brand geometry for the Windows package and ICO assets.

The reel itself draws native vector geometry; these are build-time assets. Requires Pillow.

Two masters, both from iconography.orbitMark (design/vi/tokens.json):
- the large master, drawn at 8x and resampled, for 48 px and up;
- the small master for 16 to 40 px, the rule src/orbit_mark.h's MeasureOrbitMark uses for the hub's
  Tray icon tile, whose mark has the same proportion to its plate as this icon's:
  the mark's outer edge on a whole or half pixel, a whole-pixel stroke of at least 1/14 of the mark,
  the inner ring only from a 2 px stroke (innerRingFromStrokePx) and a dot of at least minimumDotPx,
  drawn exactly on the pixel grid (16x supersampled, box-filtered), so 16 to 24 px show the outer
  orbit and the dot as crisp single pixels instead of blurred rings.

Writes packaging/assets: CopilotReel.ico (16, 20, 24, 32, 40, 48, 64, 128, 256), the package logos as
MRT resources (Square44x44Logo targetsize-16...256 with altform-unplated and altform-lightunplated,
Square44x44Logo, Square150x150Logo and StoreLogo at scale-100...400), which tools/package.ps1 indexes
into resources.pri; and assets/brands/copilot-reel.png, the plated mark About's first tile shows.

    uv run --python 3.14 --with pillow python tools/generate-assets.py
"""
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'packaging/assets'
MARK = json.loads((ROOT / 'design/vi/tokens.json').read_text(encoding='utf-8'))['iconography']['orbitMark']
CANVAS = MARK['canvas']
# The small master's sizes; from 48 px the large master's own detail holds.
SMALL_UP_TO = 40
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
TARGET_SIZES = [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256]
SCALES = [100, 125, 150, 200, 400]
LOGOS = {'Square44x44Logo': 44, 'Square150x150Logo': 150, 'StoreLogo': 50}


def draw_mark(draw, center, outer, stroke, inner, inner_ring, dot, scale):
    """The orbit mark around `center` (pixels), each ring's stroke inside its edge, at `scale`."""
    def ring(edge, start, end):
        box = [(center - edge) * scale, (center - edge) * scale, (center + edge) * scale, (center + edge) * scale]
        draw.arc(box, start=start, end=end, fill='white', width=round(stroke * scale))
    ring(outer, MARK['outerFrom'], MARK['outerTo'])
    if inner_ring:
        ring(inner, MARK['innerFrom'], MARK['innerTo'])
    angle = math.radians(MARK['dotAngle'])
    x, y = center + outer * math.cos(angle), center + outer * math.sin(angle)
    draw.ellipse([(x - dot) * scale, (y - dot) * scale, (x + dot) * scale, (y + dot) * scale], fill='white')


def large_master():
    """The 256-unit canvas at 8x, drawn as before the small master existed, so its pixels stay the same."""
    scale = 8
    image = Image.new('RGBA', (CANVAS * scale, CANVAS * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    def box(x, y, w, h):
        return tuple(int(v * scale) for v in (x, y, x + w, y + h))

    def ring(radius):
        return box(c - radius, c - radius, 2 * radius, 2 * radius)
    c = MARK['center']
    draw.rounded_rectangle(box(0, 0, CANVAS, CANVAS), radius=MARK['plateRadius'] * scale, fill=MARK['plate'])
    draw.arc(ring(MARK['outerRadius']), start=MARK['outerFrom'], end=MARK['outerTo'], fill='white', width=MARK['stroke'] * scale)
    draw.arc(ring(MARK['innerRadius']), start=MARK['innerFrom'], end=MARK['innerTo'], fill='white', width=MARK['stroke'] * scale)
    angle, dot = math.radians(MARK['dotAngle']), MARK['dotRadius']
    x, y = c + MARK['outerRadius'] * math.cos(angle), c + MARK['outerRadius'] * math.sin(angle)
    draw.ellipse(box(x - dot, y - dot, 2 * dot, 2 * dot), fill='white')
    return image


def measure(extent_px, minimum_stroke_px):
    """MeasureOrbitMark in src/orbit_mark.h."""
    edge = MARK['outerRadius']
    outer = max(2.0, round(extent_px)) / 2
    stroke = max(1.0, math.floor(max(extent_px * MARK['stroke'] / MARK['extent'], minimum_stroke_px) + .5))
    inner = outer - math.floor(outer * (edge - MARK['innerRadius']) / edge + .5)
    dot = max(outer * MARK['dotRadius'] / edge, MARK['minimumDotPx'])
    return outer, stroke, inner, stroke >= MARK['innerRingFromStrokePx'], dot


def small_master(size):
    """The plated mark at `size` px, its rings on whole pixels."""
    scale = 16
    image = Image.new('RGBA', (size * scale, size * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([0, 0, size * scale, size * scale], radius=MARK['plateRadius'] * size / CANVAS * scale, fill=MARK['plate'])
    extent = size * MARK['extent'] / CANVAS
    outer, stroke, inner, inner_ring, dot = measure(extent, extent / 14)
    # As src/orbit_mark.h asks of its callers: the center sits `outer` pixels from a pixel boundary,
    # so every ring edge lands on the grid.
    center = math.floor(size / 2 - outer + .5) + outer
    draw_mark(draw, center, outer, stroke, inner, inner_ring, dot, scale)
    return image.reduce(scale)


def icon(size, large):
    return small_master(size) if size <= SMALL_UP_TO else large.resize((size, size), Image.Resampling.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    large = large_master()
    # The MRT names replace the plain logos; resources.pri picks a file per scale or target size.
    for old in list(OUT.glob('*.png')):
        old.unlink()
    frames = [icon(size, large) for size in ICO_SIZES]
    frames[-1].save(OUT / 'CopilotReel.ico', sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])
    for size in TARGET_SIZES:
        image = icon(size, large)
        # The plated mark reads on light and dark taskbars alike, so all three variants are the same pixels.
        for suffix in ('', '_altform-unplated', '_altform-lightunplated'):
            image.save(OUT / f'Square44x44Logo.targetsize-{size}{suffix}.png')
    for name, base in LOGOS.items():
        for scale in SCALES:
            icon(round(base * scale / 100), large).save(OUT / f'{name}.scale-{scale}.png')
    large.resize((256, 256), Image.Resampling.LANCZOS).save(ROOT / 'assets/brands/copilot-reel.png')
    count = len(list(OUT.glob('*.png')))
    print(f'Generated CopilotReel.ico ({len(ICO_SIZES)} frames, {SMALL_UP_TO} px and below from the small master), '
          f'{count} package PNGs and the copilot-reel brand PNG.')


if __name__ == '__main__':
    main()
