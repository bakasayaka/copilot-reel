"""Rasterize the monochrome tray mark exactly as src/orbit_mark.h + tray_menu.cpp MarkCoverage do,
with 16x16 supersampling (D2D's software AA is analytic; this is a close approximation).
Also renders a P3-style variant (outer ring + dot only, snapped stroke) for comparison.
"""
import math, sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
SS = 16

def in_arc(px, py, cx, cy, r_in, r_out, a_from, a_to):
    dx, dy = px - cx, py - cy
    r = math.hypot(dx, dy)
    if r < r_in or r > r_out:
        return False
    a = math.degrees(math.atan2(dy, dx))
    # normalise relative to a_from in [0,360)
    rel = (a - a_from) % 360
    return rel <= (a_to - a_from)

def coverage_code(size):
    """Geometry from orbit_mark.h with MarkCoverage's margin and minimum stroke."""
    margin = size / 16
    ext = size - 2 * margin
    scale = ext / 184
    stroke = max(12 * scale, size / 16)
    cx = cy = margin + (128 - 36) * scale
    rings = []
    for radius, f, t in ((92, -30, 280), (56, 150, 440)):
        r_out = radius * scale
        rings.append((r_out - stroke, r_out, f, t))
    dang = math.radians(-50)
    dx, dy = cx + 92 * scale * math.cos(dang), cy + 92 * scale * math.sin(dang)
    dr = 16 * scale
    return rasterize(size, cx, cy, rings, (dx, dy, dr)), dict(size=size, margin=margin, scale=scale, stroke=stroke,
        outer=(rings[0][0], rings[0][1]), inner=(rings[1][0], rings[1][1]), dot=(dx, dy, dr), center=cx)

def coverage_p3(size):
    """P3 proposal: outer orbit + dot only, stroke snapped (1.5 px at 16-20, 2 px at 24-32), 1 px keyline."""
    stroke = 1.5 if size <= 20 else 2.0
    margin = 1.0
    cx = cy = size / 2
    r_out = size / 2 - margin
    rings = [(r_out - stroke, r_out, -30, 280)]
    dang = math.radians(-50)
    dr = max(1.5, size * 16 / 256 * 1.15)
    dx, dy = cx + r_out * math.cos(dang), cy + r_out * math.sin(dang)
    return rasterize(size, cx, cy, rings, (dx, dy, dr)), None

def rasterize(size, cx, cy, rings, dot):
    cov = [[0.0] * size for _ in range(size)]
    dx, dy, dr = dot
    for y in range(size):
        for x in range(size):
            hit = 0
            for sy in range(SS):
                for sx in range(SS):
                    px = x + (sx + .5) / SS
                    py = y + (sy + .5) / SS
                    ok = math.hypot(px - dx, py - dy) <= dr
                    if not ok:
                        for r_in, r_out, f, t in rings:
                            if in_arc(px, py, cx, cy, r_in, r_out, f, t):
                                ok = True
                                break
                    hit += ok
            cov[y][x] = hit / (SS * SS)
    return cov

def tile(cov, fg, bg, zoom):
    size = len(cov)
    im = Image.new('RGB', (size, size), bg)
    for y in range(size):
        for x in range(size):
            a = cov[y][x]
            im.putpixel((x, y), tuple(round(fg[i] * a + bg[i] * (1 - a)) for i in range(3)))
    return im, im.resize((size * zoom, size * zoom), Image.Resampling.NEAREST)

def main():
    sizes = [16, 20, 24, 32]
    LIGHT_BG, DARK_BG = (243, 243, 243), (32, 32, 32)   # Win11 taskbar approx (Mica light/dark)
    zoom = 6
    font = ImageFont.load_default()
    rows = []
    stats = []
    for name, fn in (('code (both rings, unsnapped)', coverage_code), ('P3 (outer + dot, snapped)', coverage_p3)):
        covs = []
        for s in sizes:
            cov, info = fn(s)
            covs.append(cov)
            if info:
                # fraction of covered pixels that are "solid" (>= 0.9) vs partial (0.1-0.9)
                flat = [c for row in cov for c in row]
                solid = sum(1 for c in flat if c >= .9)
                part = sum(1 for c in flat if .1 <= c < .9)
                info['solid'] = solid; info['partial'] = part
                stats.append(info)
        rows.append((name, covs))
    W = 40 + sum(s * zoom + 30 for s in sizes)
    H = 30 + len(rows) * 2 * (32 * zoom + 40)
    sheet = Image.new('RGB', (W * 1, H), (255, 255, 255))
    d = ImageDraw.Draw(sheet)
    y = 10
    for name, covs in rows:
        for bgname, bg, fg in (('light taskbar', LIGHT_BG, (0, 0, 0)), ('dark taskbar', DARK_BG, (255, 255, 255))):
            d.text((10, y), f'{name} - {bgname} (6x)', fill=(0, 0, 0), font=font)
            x = 10
            band = Image.new('RGB', (W - 20, 32 * zoom + 20), bg)
            bx = 10
            for s, cov in zip(sizes, covs):
                small, big = tile(cov, fg, bg, zoom)
                band.paste(big, (bx, 10))
                small.save(f'{OUT}/mark-{"code" if "code" in name else "p3"}-{s}-{"light" if bg == LIGHT_BG else "dark"}.png')
                bx += s * zoom + 30
            sheet.paste(band, (10, y + 14))
            y += 32 * zoom + 40
    sheet.save(f'{OUT}/tray-mark-sheet.png')
    # 1:1 strip at true size on both backgrounds
    strip = Image.new('RGB', (sum(s + 8 for s in sizes) + 8, 2 * 40), LIGHT_BG)
    sd = ImageDraw.Draw(strip)
    sd.rectangle((0, 40, strip.width, 80), fill=DARK_BG)
    x = 8
    for s, cov in zip(sizes, rows[0][1]):
        a, _ = tile(cov, (0, 0, 0), LIGHT_BG, 1); strip.paste(a, (x, 4))
        b, _ = tile(cov, (255, 255, 255), DARK_BG, 1); strip.paste(b, (x, 44))
        x += s + 8
    strip.save(f'{OUT}/tray-mark-1x.png')
    for st in stats:
        print({k: (round(v, 3) if isinstance(v, float) else (tuple(round(t, 3) for t in v) if isinstance(v, tuple) else v)) for k, v in st.items()})

main()
