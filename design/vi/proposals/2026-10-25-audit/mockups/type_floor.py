"""Mockup: center-name pixel size per Reel size step at 100% display scaling, current vs a 12 px floor.
Renders each name at its true pixel size with grayscale antialiasing, then enlarges 3x (nearest) so the
pixels stay visible. Inter / Reel Sans JP / WenQuanYi stand in for Segoe UI Variable / Reel Sans JP / YaHei UI."""
import sys
from PIL import Image, ImageDraw, ImageFont
ROOT = sys.argv[1]; OUT = sys.argv[2]
LAT = '/usr/share/fonts/opentype/inter/Inter-SemiBold.otf'
JA = f'{ROOT}/assets/fonts/ReelSansJP-SemiBold.ttf'
ZH = '/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc'
NAMES = [('Windows Settings', LAT), ('设置', ZH), ('リールの大きさ', JA)]
STEPS = [0.7, 0.85, 1.0, 1.15]
PLATE = (44, 44, 44); FG = (255, 255, 255); SEC = (207, 207, 207); BG = (24, 24, 24)
Z = 3

def cell(text, font_path, px, width_px):
    w, h = int(width_px) + 8, 26
    im = Image.new('RGB', (w, h), PLATE)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(font_path, size=max(1, round(px)))
    trimmed = False
    while d.textlength(text, font=f) > width_px and len(text) > 1:
        text = text.rstrip('\u2026')[:-1] + '\u2026'; trimmed = True
    tw = d.textlength(text, font=f)
    d.text(((w - tw) / 2, h / 2), text, font=f, fill=FG, anchor='lm')
    return im.resize((w * Z, h * Z), Image.NEAREST), trimmed

label = ImageFont.truetype(LAT, 22); small = ImageFont.truetype(LAT, 18)
rows = []
for title, floor, line_dip in (('Current: 14 DIP x 0.8 x step, 132 DIP line', None, 132), ('Proposal: never below 11 px, 160 DIP line (clear of the side seats)', 11.0, 160)):
    for step in STEPS:
        eff = round(96 * 0.8 * step); px = 14 * eff / 96
        if floor: px = max(px, floor)
        line = line_dip * eff / 96
        cells = [cell(t, fp, px, line) for t, fp in NAMES]
        rows.append((title, step, px, line, cells))
W = max(330 + sum(im.width + 24 for im, _ in r[4]) for r in rows) + 10
H = 220 + len(rows) * 100
sheet = Image.new('RGB', (W, H), BG)
d = ImageDraw.Draw(sheet)
d.text((24, 20), 'Center name at 100% display scaling (96 DPI), rendered at true pixel size, shown 3x', font=label, fill=FG)
d.text((24, 52), 'Stand-in faces: Inter for Segoe UI Variable, WenQuanYi Zen Hei for YaHei UI, and the bundled Reel Sans JP.', font=small, fill=SEC)
y = 100; last = None
for title, step, px, line, cells in rows:
    if title != last:
        d.text((24, y), title, font=label, fill=(96, 205, 255)); y += 34; last = title
    d.text((24, y + 22), f'{round(step*100)}%  {px:4.1f} px  line {line:3.0f} px', font=small, fill=SEC)
    x = 330
    for im, trimmed in cells:
        sheet.paste(im, (x, y));
        if trimmed: d.text((x, y + im.height - 2), 'trimmed with an ellipsis', font=small, fill=(255, 153, 164))
        x += im.width + 24
    y += 100
sheet = sheet.crop((0, 0, W, y + 10))
sheet.save(OUT, optimize=True)
print(OUT, sheet.size)
