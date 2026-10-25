"""Mockup: the app icon at taskbar/tray sizes, current Lanczos downsample vs a hand-tuned small master.
Proposal: below 32 px drop the inner arc, keep the outer orbit and the dot, snap the stroke to whole
pixels and keep a 1 px keyline inset so the plate does not touch the cell edge."""
import math, sys
from PIL import Image, ImageDraw, ImageFont
ROOT, OUT = sys.argv[1], sys.argv[2]
BLUE = '#005FB8'
def master(size, small):
    s = 16; W = size * s
    im = Image.new('RGBA', (W, W), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    if not small:
        ico = Image.open(f'{ROOT}/packaging/assets/CopilotReel.ico'); ico.size = (size, size) if (size, size) in ico.info['sizes'] else (32, 32)
        return ico.copy().convert('RGBA').resize((size, size), Image.LANCZOS)
    inset = 1 * s if size <= 24 else 2 * s
    d.rounded_rectangle((inset, inset, W - inset - 1, W - inset - 1), radius=int(W * 0.22), fill=BLUE)
    stroke = (1.5 if size <= 20 else 2.0) * s
    r = W * 0.30
    c = W / 2
    d.arc((c - r, c - r, c + r, c + r), start=-25, end=275, fill='white', width=int(stroke))
    a = math.radians(-50); dr = W * 0.085
    x, y = c + r * math.cos(a), c + r * math.sin(a)
    d.ellipse((x - dr, y - dr, x + dr, y + dr), fill='white')
    return im.resize((size, size), Image.LANCZOS)
font = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-SemiBold.otf', 20)
small = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-Regular.otf', 16)
sizes = [16, 20, 24, 32]
Z = 6
rows = [('Current: one 256 px master, Lanczos', False), ('Proposal: a small master for 16-32 px (outer orbit and dot only)', True)]
cols = []
W = 40 + 2 * (sum(s * Z + 24 for s in sizes) + 40)
H = 60 + len(rows) * (32 * Z + 70)
sheet = Image.new('RGB', (W, H), (255, 255, 255))
dr = ImageDraw.Draw(sheet)
for bi, (bg, fg, name) in enumerate([((243, 243, 243), (27, 27, 27), 'light taskbar'), ((32, 32, 32), (255, 255, 255), 'dark taskbar')]):
    x0 = 20 + bi * (W // 2)
    dr.rectangle((x0 - 10, 0, x0 - 10 + W // 2, H), fill=bg)
    dr.text((x0, 16), f'{name}, true pixels shown {Z}x', font=font, fill=fg)
    y = 60
    for title, sm in rows:
        dr.text((x0, y), title, font=small, fill=fg); y += 26
        x = x0
        for s in sizes:
            ic = master(s, sm)
            cell = Image.new('RGBA', (s, s), bg + (255,)); cell.alpha_composite(ic)
            sheet.paste(cell.convert('RGB').resize((s * Z, s * Z), Image.NEAREST), (x, y))
            dr.text((x, y + s * Z + 4), f'{s} px', font=small, fill=fg)
            x += s * Z + 24
        y += 32 * Z + 44
sheet.save(OUT, optimize=True); print(OUT, sheet.size)
