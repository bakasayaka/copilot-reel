"""Mockup: the preferred badge and the update dot on a selected tile (light, fallback accent), current vs one
cutout rule. Geometry in DIP from tokens.json: seat 48 r14 stroke 1.5 inset 0.75; badge disc r6 / ring r7.5 at
(+17,-17); dot disc r4 / ring r5.5. Drawn at 10 px per DIP, 4x supersampled."""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageChops
OUT = sys.argv[1]
K = 10; SS = 4; P = K * SS
PLATE = (249, 249, 249); ACC = (0, 95, 184)
SEAT = tuple(round(p + (a - p) * 0.10) for p, a in zip(PLATE, ACC))
WHITE = (255, 255, 255); INK = (27, 27, 27)
HALF = 36  # DIP shown around the tile center

def canvas():
    return Image.new('RGB', (2 * HALF * P, 2 * HALF * P), PLATE)
def xy(x, y): return ((x + HALF) * P, (y + HALF) * P)
def circle(d, cx, cy, r, fill):
    (x0, y0), (x1, y1) = xy(cx - r, cy - r), xy(cx + r, cy + r); d.ellipse((x0, y0, x1, y1), fill=fill)
def seat_shapes(d):
    (x0, y0), (x1, y1) = xy(-24, -24), xy(24, 24)
    d.rounded_rectangle((x0, y0, x1, y1), radius=14 * P, fill=ACC)
    (x0, y0), (x1, y1) = xy(-22.5, -22.5), xy(22.5, 22.5)
    d.rounded_rectangle((x0, y0, x1, y1), radius=12.5 * P, fill=SEAT)
def seat_mask():
    m = Image.new('L', (2 * HALF * P,) * 2, 0); md = ImageDraw.Draw(m)
    (x0, y0), (x1, y1) = xy(-24, -24), xy(24, 24); md.rounded_rectangle((x0, y0, x1, y1), radius=14 * P, fill=255)
    return m
def glyph(d):
    circle(d, 0, 0, 9, INK); circle(d, 0, 0, 7.5, SEAT)  # stand-in mark
def check(d, cx, cy):
    pts = [xy(cx - 3, cy + 0.2), xy(cx - 1, cy + 2.2), xy(cx + 3.2, cy - 2.2)]
    d.line(pts, fill=WHITE, width=int(1.5 * P), joint='curve')

def render(kind, proposal):
    im = canvas(); d = ImageDraw.Draw(im)
    seat_shapes(d); glyph(d)
    disc, ring = (6, 7.5) if kind == 'badge' else (4, 5.5)
    cx, cy = 17, -17
    if not proposal:
        circle(d, cx, cy, ring, SEAT)          # renderer.cpp:624: ring in `selection` when selected
    else:
        # One cutout rule: the ring erases to what lies beneath the seat: seat fill inside, plate outside.
        ringm = Image.new('L', im.size, 0); circle(ImageDraw.Draw(ringm), cx, cy, ring, 255)
        inside = ImageChops.multiply(ringm, seat_mask())
        im.paste(SEAT, mask=inside)
        outside = ImageChops.subtract(ringm, seat_mask()); im.paste(PLATE, mask=outside)
        d = ImageDraw.Draw(im)
    circle(d, cx, cy, disc, ACC)
    if kind == 'badge': check(d, cx, cy)
    return im.resize((2 * HALF * K,) * 2, Image.LANCZOS)

font = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-SemiBold.otf', 22)
small = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-Regular.otf', 17)
cells = [('Preferred badge, current', render('badge', False)), ('Preferred badge, one cutout rule', render('badge', True)),
         ('Update dot, current', render('dot', False)), ('Update dot, one cutout rule', render('dot', True))]
w = cells[0][1].width
sheet = Image.new('RGB', (4 * w + 5 * 20, w + 120), (255, 255, 255)); sd = ImageDraw.Draw(sheet)
sd.text((20, 14), 'Selected tile, light theme, fallback accent: 10 px per DIP', font=font, fill=INK)
for i, (title, im) in enumerate(cells):
    x = 20 + i * (w + 20); sheet.paste(im, (x, 60)); sd.text((x, 64 + w), title, font=small, fill=INK)
sd.text((20, 92 + w), 'Current: the ring is painted in the seat fill, so it spills past the seat edge onto the plate as a tinted crescent.', font=small, fill=(93, 93, 93))
sheet.save(OUT, optimize=True); print(OUT, sheet.size)
