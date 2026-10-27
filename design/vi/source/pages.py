"""The VI v0.9 pages. Every product value is read from tokens.json (via tokens.py)."""
from __future__ import annotations

import math
from functools import lru_cache

from PIL import Image

import art
import fonts
import reel as R
from layout import (Page, W, ML, MR, CW, TOP, BOTTOM, INK, INK2, INK3, RULE, PANEL, PANEL_DARK, PAPER, RED, DO, DONT,
                    para, heading, caption, mono, panel, tag, dim_h, dim_v, leader, check_mark)
from tokens import (T, G, PAL, MOTION, TYPE, ICON, ELEV, MATERIAL, OSD, CANVAS, PLATE_R, PLATE_STROKE, PLATE_INSET, ORBIT_R,
                    CENTER_R, RING, TILE, TILE_R, TILE_STROKE, TILE_INSET, SEAT, SEAT_R,
                    SEAT_STROKE, SEAT_INSET, GLYPH_EM, KEYLINE, BADGE, CHEVRON, DISABLED, LABEL, MONOGRAM, SHADOW_BLUR,
                    SHADOW_DY, MAX_APPS, ROOT, color, over, contrast, slot_angle, glyph_code)


def f(v):
    """Token number for prose: 40.0 -> '40', 0.75 -> '0.75'."""
    return f'{v:g}' if isinstance(v, (int, float)) else str(v)


LIGHT, DARK, HC = R.theme('light'), R.theme('dark'), R.theme('contrast')
LIGHT_F, DARK_F = R.theme('light', 'frosted'), R.theme('dark', 'frosted')


def main_state(**kw):
    return R.State(R.MAIN, preferred=0, **kw)


def hub_state(**kw):
    return R.State(R.HUB, language=True, submenu=True, **kw)


def seat_overshoot():
    """Peak overshoot of the seat's spring, exp(-pi z / sqrt(1 - z^2)) for its damping ratio z, as a percentage."""
    z = MOTION['events']['selection']['travel']['dampingRatio']
    return f'{math.exp(-math.pi * z / math.sqrt(1 - z * z)) * 100:.1f}%'


# 01 Principles ------------------------------------------------------------------------------

def p01():
    p = Page(1, 'Principles', 'A quiet orbit on the Windows surface.',
             'Copilot Reel opens one neutral plate at the pointer. Apps sit on a single orbit; the center stays silent until a name helps.')
    s = p.s
    y0, h = TOP, 404
    for i, (th, st, cap) in enumerate([
            (LIGHT, main_state(), 'Light  ·  Main, idle'),
            (DARK, main_state(selected=1), 'Dark  ·  Main, Claude selected')]):
        x = ML + i * (CW / 2 + 12)
        w = CW / 2 - 12
        panel(s, x, y0, w, h, dark=th.dark)
        R.reel(s, x + w / 2, y0 + h / 2 - 2, th, st, 1.0)
        caption(s, x + 20, y0 + h - 18, cap, color='#CFCFCF' if th.dark else INK2)
    principles = [
        ('Windows-neutral surface', f'The system flyout surface, {PAL["light"]["plate"]} or {PAL["dark"]["plate"]}: one soft shadow, '
         f'a {f(PLATE_STROKE)} DIP rim, frosted when transparency is on.'),
        ('One orbit', f'Every item sits on the {f(ORBIT_R)} DIP orbit, slot 0 at {G["orbit"]["firstSlot"]}, {G["orbit"]["direction"]}, '
         'an equal step apart. Slots never reflow; icons never rotate.'),
        ('A quiet center', 'Idle Main shows a faint ring and submenus a back chevron. Hover shows exactly one name, never a title, caption or hint.'),
        ('Selection is a seat', f'A {f(SEAT)} DIP seat tinted with the system accent replaces the tile fill. Command glyphs and EN/ZH/JA stay foreground.'),
        ('Marks stay true', f'Vendor marks keep their own color and proportion. Plated marks fill the {f(TILE)} DIP tile; open marks sit on a {f(KEYLINE)} DIP keyline.'),
        ('Brief motion', f'{MOTION["events"]["open"]["fadeMs"]} to {MOTION["events"]["drill"]["enterMs"]} ms, scaled from the plate center. '
         f'Easing never overshoots; only the seat\'s spring does, by {seat_overshoot()}. Reduced motion and high contrast settle at once.'),
    ]
    cw = (CW - 2 * 40) / 3
    for i, (title, body) in enumerate(principles):
        x = ML + (i % 3) * (cw + 40)
        y = y0 + h + 52 + (i // 3) * 92
        s.text(x, y, f'{i + 1}', 13, RED, face='text-sb')
        heading(s, x + 18, y, title)
        para(s, x + 18, y + 24, body, cw - 18, 13.5, 20)
    return p


# 02 Anatomy ---------------------------------------------------------------------------------

def p02():
    p = Page(2, 'Anatomy', 'Every dimension, in DIP.',
             'One canvas, one plate, one orbit. The selection seat is concentric with the tile; the center holds one line of text.')
    s = p.s
    k = 1.0
    gutter = 108
    c0x, c0y = ML + gutter, TOP + 30
    cx, cy = c0x + CANVAS * k / 2, c0y + CANVAS * k / 2
    n = len(R.MAIN)
    R.reel(s, cx, cy, LIGHT, main_state(selected=1), k)
    # Canvas
    s.rect(c0x, c0y, CANVAS * k, CANVAS * k, stroke=RED, sw=0.7, dash=[3, 3])
    dim_h(s, c0x, c0x + CANVAS * k, c0y - 12, f'canvas {f(CANVAS)} × {f(CANVAS)}')
    # Plate, orbit and center zone: leaders into the left gutter, through the gaps between slots.
    s.circle(cx, cy, ORBIT_R * k, stroke=RED, sw=0.7, dash=[3, 3])
    s.circle(cx, cy, CENTER_R * k, stroke=RED, sw=0.7, dash=[3, 3])
    s.circle(cx, cy, 1.8, fill=RED)
    for radius, angle, text in ((PLATE_R, 180 - 360 / n / 2, f'plate r {f(PLATE_R)}'),
                                (ORBIT_R, 180 + 360 / n / 2, f'orbit r {f(ORBIT_R)}'),
                                (CENTER_R, 135, f'center zone r {f(CENTER_R)}')):
        a = math.radians(angle)
        px, py = cx + radius * k * math.cos(a), cy + radius * k * math.sin(a)
        leader(s, px, py, ML + gutter - 14, py, text, align='right')
    # Label box
    lw, lh = LABEL['width'] * k, LABEL['height'] * k
    s.rect(cx - lw / 2, cy - lh / 2, lw, lh, stroke=RED, sw=0.7, dash=[2, 2])
    caption(s, cx, cy + lh / 2 + 14, f'label {f(LABEL["width"])} × {f(LABEL["height"])}', RED, 'center', 11, 'small-sb')
    # Slot step, inside the plate between slot 0 and slot 1
    from scene import arc
    a0, a1 = slot_angle(0, n), slot_angle(1, n)
    rr = (PLATE_R - 16) * k
    s.path(arc(cx, cy, rr, a0, a1), stroke=RED, sw=0.9)
    for a in (a0, a1):
        ar = math.radians(a)
        s.line(cx + (ORBIT_R + 24) * k * math.cos(ar), cy + (ORBIT_R + 24) * k * math.sin(ar),
               cx + (PLATE_R - 8) * k * math.cos(ar), cy + (PLATE_R - 8) * k * math.sin(ar), RED, 0.6, dash=[2, 2])
    am = math.radians(a0 + (a1 - a0) * 0.62)
    caption(s, cx + (rr + 9) * math.cos(am), cy + (rr + 9) * math.sin(am), f'{f(360 / n)}°', RED, 'left', 11, 'small-sb')
    y = c0y + CANVAS * k + 22
    for i, line in enumerate([
            f'Slot 0 at {G["orbit"]["firstSlot"]}, {G["orbit"]["direction"]}; step 360/n ({f(360 / n)}° for n = {n}).',
            f'Shadow: blur {f(SHADOW_BLUR)}, offset y {f(SHADOW_DY)}, inside the {f(ELEV["canvasMargin"])} DIP canvas margin.',
            f'Rim: {f(PLATE_STROKE)} DIP, inset {f(PLATE_INSET)} (high contrast {f(G["plate"]["strokeHighContrast"])}).',
            'Pixel grid: centers, tiles and seats snap to device pixels; hairlines ≥ 1 px.']):
        caption(s, c0x, y + i * 15, line, RED, size=11, face='small-sb')

    # Center states strip
    y += 62
    states = [('Idle Main', f'ring {f(RING)}, ringIdle', main_state()),
              ('Idle submenu', f'back chevron em {f(CHEVRON["backEm"])}', hub_state()),
              ('Named', 'one name, ring hidden', main_state(selected=1)),
              ('Disabled', f'tile and art at {f(DISABLED)}', R.State(R.hub(True), language=True, submenu=True))]
    cw = (CANVAS * k + gutter + 12) / 4
    box_w, box_h, kk = cw - 14, 76, 0.78
    for i, (title, note, st) in enumerate(states):
        x = ML + i * cw
        with s.clip_rect(x, y, box_w, box_h, 10):
            s.rect(x, y, box_w, box_h, fill=PANEL)
            if title == 'Disabled':
                ix, iy = R.item_center(next(j for j, it in enumerate(st.items) if not it.enabled), len(st.items))
                R.reel(s, x + box_w / 2 - (ix - R.C) * 1.6, y + box_h / 2 - (iy - R.C) * 1.6, LIGHT, st, 1.6)
            else:
                R.reel(s, x + box_w / 2, y + box_h / 2, LIGHT, st, kk)
        heading(s, x, y + box_h + 18, title, 12.5)
        caption(s, x, y + box_h + 33, note, INK2, size=11)

    # Detail cards
    x0 = ML + gutter + CANVAS * k + 40
    cw2 = (W - MR - x0 - 24) / 2
    ch = (BOTTOM - TOP - 24) / 2
    cards = [(x0, TOP), (x0 + cw2 + 24, TOP), (x0, TOP + ch + 24), (x0 + cw2 + 24, TOP + ch + 24)]
    _card_seat(s, *cards[0], cw2, ch)
    _card_artwork(s, *cards[1], cw2, ch)
    _card_badge(s, *cards[2], cw2, ch)
    _card_chevrons(s, *cards[3], cw2, ch)
    return p


def _card(s, x, y, w, h, title, note):
    s.rect(x, y, w, h, stroke=RULE, sw=1, r=12)
    heading(s, x + 18, y + 28, title, 13.5)
    caption(s, x + 18, y + h - 16, note, INK3, size=11)


def _card_seat(s, x, y, w, h):
    _card(s, x, y, w, h, 'Tile and seat', f'High contrast: seat stroke {f(G["seat"]["strokeHighContrast"])}. Vector fallback glyph stroke '
          f'{f(G["glyph"]["fallbackStroke"])} ({f(G["glyph"]["fallbackStrokeHighContrast"])}).')
    k = 2.9
    cx, cy = x + 112, y + h / 2 + 6
    with s.group(cx - R.C * k, cy - R.C * k, k):
        R.draw_seat(s, LIGHT, R.C, R.C)
        R.draw_art(s, LIGHT, R.cmd('add', 'Add'), R.C, R.C)
    t, st = TILE * k, SEAT * k
    s.rect(cx - t / 2, cy - t / 2, t, t, stroke=RED, sw=0.6, r=TILE_R * k, dash=[2, 2])
    dim_h(s, cx - st / 2, cx + st / 2, cy - st / 2 - 10, f'seat {f(SEAT)}  r {f(SEAT_R)}')
    dim_h(s, cx - t / 2, cx + t / 2, cy + st / 2 + 16, f'tile {f(TILE)}  r {f(TILE_R)}', above=False)
    leader(s, cx + st / 2 - 1, cy + 24, cx + st / 2 + 26, cy + 52,
           f'stroke {f(SEAT_STROKE)}, inset {f(SEAT_INSET)}\naccent; fill = selection', align='left')
    ink = fonts.icon_outline(glyph_code('Add'))[1][0] * GLYPH_EM * k
    s.rect(cx - ink / 2, cy - ink / 2, ink, ink, stroke=RED, sw=0.5, dash=[1.5, 1.5])
    # Neutral tile, upper right
    kk = k * .62
    tx, ty = x + w - 76, y + 112
    with s.group(tx - R.C * kk, ty - R.C * kk, kk):
        R.draw_tile(s, LIGHT, R.C, R.C)
        R.draw_art(s, LIGHT, R.cmd('manage', 'Manage'), R.C, R.C)
    caption(s, tx, ty - TILE * kk / 2 - 10, f'glyph em {f(GLYPH_EM)}, ink-centered', RED, 'center', 11, 'small-sb')
    caption(s, tx, ty + TILE * kk / 2 + 18, 'neutral tile', INK2, 'center', 11)
    caption(s, tx, ty + TILE * kk / 2 + 32, f'hairline {f(TILE_STROKE)}, inset {f(TILE_INSET)}', RED, 'center', 11, 'small-sb')


def _card_artwork(s, x, y, w, h):
    _card(s, x, y, w, h, 'Artwork', 'Open marks sit on the keyline; plated marks fill the tile.')
    k = 2.9
    cy = y + h / 2 + 4
    for i, (key, name) in enumerate([('chatgpt', 'ChatGPT'), ('claude', 'Claude')]):
        cx = x + w * (0.28 + 0.44 * i)
        with s.group(cx - R.C * k, cy - R.C * k, k):
            R.draw_item(s, LIGHT, R.app(key, name), R.C, R.C)
        tr = art.treatment(key)
        if tr.plated:
            t = TILE * k
            dim_h(s, cx - t / 2, cx + t / 2, cy - t / 2 - 10, f'cover {f(TILE)}, clip r {f(TILE_R)}')
            caption(s, cx, cy + t / 2 + 20, 'plated', INK2, 'center', 11)
            caption(s, cx, cy + t / 2 + 34, 'hairline on top', RED, 'center', 11, 'small-sb')
        else:
            kl = KEYLINE * tr.optical * k
            s.rect(cx - kl / 2, cy - kl / 2, kl, kl, stroke=RED, sw=0.6, dash=[2, 2])
            dim_h(s, cx - kl / 2, cx + kl / 2, cy - TILE * k / 2 - 10, f'keyline {f(KEYLINE)} × {f(tr.optical)}')
            caption(s, cx, cy + TILE * k / 2 + 20, 'glyph', INK2, 'center', 11)
            caption(s, cx, cy + TILE * k / 2 + 34, 'long ink side on the keyline', RED, 'center', 11, 'small-sb')


def _card_badge(s, x, y, w, h):
    _card(s, x, y, w, h, 'Preferred badge', 'Accent disc, plate-colored cutout ring, path check.')
    k = 2.9
    cx, cy = x + 112, y + h / 2 + 14
    with s.group(cx - R.C * k, cy - R.C * k, k):
        R.draw_item(s, LIGHT, R.app('chatgpt', 'ChatGPT'), R.C, R.C, preferred=True)
    bx, by = cx + BADGE['offset'] * k, cy - BADGE['offset'] * k
    t = TILE * k / 2
    # Offsets measured from the item center, dimensioned outside the tile.
    s.line(cx, cy - 5, cx, cy + 5, RED, 0.7)
    s.line(cx - 5, cy, cx + 5, cy, RED, 0.7)
    dim_h(s, cx, bx, cy + t + 14, f'+{f(BADGE["offset"])}', above=False, ext=cy)
    dim_v(s, cx - t - 14, by, cy, f'−{f(BADGE["offset"])}', left=True)
    s.line(cx - t - 14, by, bx, by, RED, 0.5, dash=[2, 2])
    s.circle(bx, by, BADGE['ringRadius'] * k, stroke=RED, sw=0.5, dash=[1.5, 1.5])
    leader(s, bx + BADGE['ringRadius'] * k * .72, by + BADGE['ringRadius'] * k * .72, bx + 60, by + 46,
           f'disc r {f(BADGE["radius"])}, accent\nring r {f(BADGE["ringRadius"])}: {f(BADGE["ring"])} DIP\nplate-colored cutout\n'
           f'check {f(BADGE["checkStroke"])}, round caps,\ntextOnAccent', align='left')


def _card_chevrons(s, x, y, w, h):
    _card(s, x, y, w, h, 'Language tile', 'EN / ZH / JA monogram; chevrons and A / D switch in place.')
    k = 1.9
    cx, cy = x + w / 2, y + h / 2 + 2
    with s.group(cx - R.C * k, cy - (R.C - ORBIT_R) * k, k):
        th = LIGHT
        R.draw_tile(s, th, R.C, R.C - ORBIT_R)
        R.draw_art(s, th, R.LANGUAGE, R.C, R.C - ORBIT_R)
        for dx, g in ((-CHEVRON['offset'], 'ChevronLeftSmall'), (CHEVRON['offset'], 'ChevronRightSmall')):
            R.glyph(s, glyph_code(g), R.C + dx, R.C - ORBIT_R, CHEVRON['em'], th.secondary)
    hit = CHEVRON['hit'] * k
    for dx in (-CHEVRON['offset'], CHEVRON['offset']):
        s.rect(cx + dx * k - hit / 2, cy - hit / 2, hit, hit, stroke=RED, sw=0.6, dash=[2, 2])
    dim_h(s, cx, cx + CHEVRON['offset'] * k, cy - hit / 2 - 12, f'+{f(CHEVRON["offset"])}', ext=cy - 4)
    dim_h(s, cx - CHEVRON['offset'] * k, cx, cy - hit / 2 - 12, f'−{f(CHEVRON["offset"])}', ext=cy - 4)
    ink = fonts.icon_outline(glyph_code('ChevronRightSmall'))[1]
    for i, line in enumerate([f'centers (±{f(CHEVRON["offset"])}, {f(CHEVRON["orbitY"])}) from the plate center',
                              f'hit {f(CHEVRON["hit"])} × {f(CHEVRON["hit"])}  ·  em {f(CHEVRON["em"])}, ink {ink[0] * CHEVRON["em"]:.1f} × {ink[1] * CHEVRON["em"]:.1f}',
                              f'{" / ".join(MONOGRAM["states"])} {f(MONOGRAM["size"])} / {MONOGRAM["weight"]}, centered on cap height']):
        caption(s, cx, cy + hit / 2 + 18 + i * 15, line, RED, 'center', 11, 'small-sb')


# 03 Color -----------------------------------------------------------------------------------

# Windows default Blue accent palette as read from the capture machine (HKCU ... Explorer\Accent\AccentPalette),
# shown only as a sample of the ramp the runtime reads through UISettings.GetColorValue.
SAMPLE_RAMP = [('AccentLight3', '#99EBFF'), ('AccentLight2', '#4CC2FF'), ('AccentLight1', '#0091F8'), ('Accent', '#0078D4'),
               ('AccentDark1', '#0067C0'), ('AccentDark2', '#003E92'), ('AccentDark3', '#001A68')]
ROLE_ROWS = [('plate', 'fill'), ('foreground', 'text'), ('secondary', 'text'), ('tile', 'fill'), ('tileStroke', 'stroke'),
             ('plateStroke', 'stroke'), ('ringIdle', 'stroke'), ('accent', 'fill'), ('accentText', 'text'),
             ('textOnAccent', 'text'), ('selection', 'fill'), ('selectionText', 'text'), ('outline', 'stroke'),
             ('shadowOpacity', 'shadow')]


def _role_value(key, role):
    p = PAL[key]
    v = p[role]
    if role == 'shadowOpacity':
        return f'{round(v * 100)}%'
    if isinstance(v, dict) and 'ramp' in v:
        return f'{v["ramp"]}  ·  {v["fallback"]}'
    if isinstance(v, dict):
        return f'{v["hex"]}  ·  {round(v["alpha"] * 100)}%'
    if role == 'selection':
        return f'Mix(plate, accent, {f(p["seatTint"])})'
    if role == 'tile' and p['frostedTileAlpha'] != 1:
        return f'{v}  ·  frosted {round(p["frostedTileAlpha"] * 100)}%'
    if role == 'outline':
        return f'{v}  ·  editor only'
    return v


def _hc_value(role):
    v = PAL['highContrast'][role]
    if role == 'shadowOpacity':
        return 'none'
    v = v.split(' (')[0].replace(' at geometry.plate.strokeHighContrast', f', {f(G["plate"]["strokeHighContrast"])} DIP')
    v = v.replace(' seat fill with a geometry.seat.strokeHighContrast COLOR_HIGHLIGHTTEXT stroke',
                  f', HIGHLIGHTTEXT {f(G["seat"]["strokeHighContrast"])} stroke')
    v = v.replace(' with a 1 DIP COLOR_WINDOWTEXT stroke', f', WINDOWTEXT {f(TILE_STROKE)} stroke')
    return v.replace('COLOR_', '')


def _swatch(s, x, y, th, role, kind, size=24):
    plate = th.plate
    if kind == 'shadow':
        s.rect(x, y, size, size, fill=plate, r=6)
        s.rect(x + 3, y + 7, size - 6, size - 8, fill='#000000', r=4, alpha=th.shadowOpacity)
        s.rect(x + 3, y + 3, size - 6, size - 8, fill=plate, stroke=th.plateStroke, sw=1, r=4)
        return
    value = getattr(th, role) if role != 'outline' else th.outline
    if kind == 'stroke':
        s.rect(x, y, size, size, fill=plate, r=6)
        s.rect(x, y, size, size, stroke=value, sw=3, r=6, inset=True)
    else:
        s.rect(x, y, size, size, fill=plate, r=6)
        s.rect(x, y, size, size, fill=value, r=6)
    s.rect(x, y, size, size, stroke='#000000', sw=0.6, r=6, alpha=0.12, inset=True)


def p03():
    p = Page(3, 'Color', 'Roles, not colors.',
             'Neutral surfaces from tokens, the accent from the Windows ramp, high contrast from live system colors.')
    s = p.s
    col_role, col_w = 128, 262
    y0 = TOP + 20
    heads = [('Light', LIGHT, 'light'), ('Dark', DARK, 'dark'), ('High contrast', HC, 'contrast')]
    for j, (name, th, key) in enumerate(heads):
        x = ML + col_role + j * col_w
        heading(s, x, y0, name, 14)
        if key == 'contrast':
            caption(s, x + 98, y0, 'COLOR_* system colors', INK3, size=11)
    s.line(ML, y0 + 12, ML + col_role + 3 * col_w - 20, y0 + 12, RULE)
    row_h = 33
    for i, (role, kind) in enumerate(ROLE_ROWS):
        y = y0 + 22 + i * row_h
        s.text(ML, y + 17, role, 12.5, INK, face='mono')
        for j, (name, th, key) in enumerate(heads):
            x = ML + col_role + j * col_w
            _swatch(s, x, y + 1, th, role, kind)
            text = _hc_value(role) if key == 'contrast' else _role_value(key, role)
            s.text(x + 34, y + 17, text, 12, INK2, face='small')
        s.line(ML, y + row_h - 3, ML + col_role + 3 * col_w - 20, y + row_h - 3, RULE, 0.6)

    # Accent ramp
    x0 = ML + col_role + 3 * col_w + 20
    wr = W - MR - x0
    heading(s, x0, y0, 'System accent ramp', 14)
    caption(s, x0, y0 + 20, 'Sample: Windows default blue (capture machine)', INK3, size=11)
    cw = (wr - 6 * 5) / 7
    ry = y0 + 34
    for i, (name, hexv) in enumerate(SAMPLE_RAMP):
        x = x0 + i * (cw + 5)
        s.rect(x, ry, cw, 40, fill=hexv, r=6)
        short = name.replace('Accent', '').replace('Light', 'L').replace('Dark', 'D') or 'Acc'
        s.text(x + cw / 2, ry + 56, short, 11, INK, face='small-sb', align='center')
        s.text(x + cw / 2, ry + 70, hexv[1:], 9.5, INK3, face='mono', align='center')
    names = [n for n, _ in SAMPLE_RAMP]
    for theme_key in ('dark', 'light'):
        for role, word in (('accent', 'fill'), ('accentText', 'text')):
            i = names.index(PAL[theme_key][role]['ramp'])
            x = x0 + i * (cw + 5) + cw / 2
            s.line(x, ry + 76, x, ry + 84, INK3, 0.8)
            s.text(x, ry + 96, theme_key, 10, INK, face='small-sb', align='center')
            s.text(x, ry + 108, word, 10, INK2, face='small', align='center')
    yy = ry + 132
    for line in [f'Fallbacks when the ramp is unavailable: {PAL["light"]["accent"]["fallback"]} / {PAL["light"]["accentText"]["fallback"]}',
                 f'(light), {PAL["dark"]["accent"]["fallback"]} / {PAL["dark"]["accentText"]["fallback"]} (dark). Scenes here use them.']:
        caption(s, x0, yy, line, INK2, size=11.5)
        yy += 16

    # Contrast
    yy += 18
    heading(s, x0, yy, 'Contrast (WCAG 2.2)', 14)
    s.text(x0 + wr - 92, yy, 'Light', 11, INK3, face='small-sb', align='right')
    s.text(x0 + wr - 42, yy, 'Dark', 11, INK3, face='small-sb', align='right')
    s.text(x0 + wr, yy, 'Need', 11, INK3, face='small-sb', align='right')
    pairs = [('Label, glyphs on plate', 'foreground', 'plate', 4.5),
             ('Glyphs on tile', 'foreground', 'tile', 4.5),
             ('Glyphs on seat', 'foreground', 'selection', 4.5),
             ('Chevrons on plate', 'secondary', 'plate', 3.0),
             ('Seat stroke on plate', 'accent', 'plate', 3.0),
             ('Badge check on disc', 'textOnAccent', 'accent', 3.0)]
    yy += 8
    for label, a, b, need in pairs:
        yy += 19
        s.text(x0, yy, label, 11.5, INK2, face='small')
        for j, th in enumerate((LIGHT, DARK)):
            bg = over(getattr(th, b), th.plate)
            ratio = contrast(over(getattr(th, a), bg), bg)
            xr = x0 + wr - 92 + j * 50
            s.text(xr, yy, f'{ratio:.2f}', 11.5, INK if ratio >= need else DONT, face='small-sb', align='right')
        s.text(x0 + wr, yy, f'{f(need)}:1', 11.5, INK3, face='small', align='right')
    yy += 19
    sample = PAL['highContrast']['previewSample']
    hc_text = contrast(color(sample['windowText']), color(sample['window']))
    hc_sel = contrast(color(sample['highlightText']), color(sample['highlight']))
    caption(s, x0, yy + 4, f'High Contrast Black sample: text {hc_text:.1f}:1, selection {hc_sel:.1f}:1.', INK3, size=11)

    # The roles in use: seat, glyph and badge per theme.
    yy += 30
    heading(s, x0, yy, 'In use', 14)
    k = 1.7
    cw3 = (wr - 2 * 10) / 3
    for j, th in enumerate((LIGHT, DARK, HC)):
        x = x0 + j * (cw3 + 10)
        bh = 92
        with s.clip_rect(x, yy + 12, cw3, bh, 10):
            ix, iy = R.item_center(1, len(R.HUB))
            R.reel(s, x + cw3 / 2 - (ix - R.C) * k, yy + 12 + bh / 2 - (iy - R.C) * k, th,
                   R.State(R.HUB, language=True, submenu=True, selected=1, label=''), k)
        caption(s, x, yy + bh + 28, th.label, INK2, size=11)

    # Rules
    y = y0 + 22 + len(ROLE_ROWS) * row_h + 26
    rules = [('Accent is a role', 'Fill and text accents come from UISettings ramp steps, never the raw Accent. Fallbacks apply only when the ramp is unavailable.'),
             ('The seat carries selection', 'Command glyphs and EN/ZH/JA stay foreground on the seat. High contrast paints the seat COLOR_HIGHLIGHT and selected glyphs selectionText.'),
             ('One mix rule', f'{PAL["mix"]}. Translucent roles composite over the plate; frosted tiles use frostedTileAlpha.')]
    cw = (col_role + 3 * col_w - 20 - 2 * 32) / 3
    for i, (title, body) in enumerate(rules):
        x = ML + i * (cw + 32)
        heading(s, x, y, title, 13.5)
        para(s, x, y + 21, body, cw, 12.5, 18)
    return p


# 04 Typography ------------------------------------------------------------------------------

def p04():
    ja = TYPE['japanese']
    p = Page(4, 'Typography', 'One name, centered on its cap height.',
             f'{TYPE["preferred"]} {TYPE["weight"]} for Latin, {TYPE["cjk"]["family"]} for Chinese, {ja["family"]} for Japanese. One line, never a second.')
    s = p.s
    k = 3.0
    lw, lh = LABEL['width'] * k, LABEL['height'] * k
    x0, y0 = ML, TOP + 8
    pw, ph = 678, 132
    cap = fonts.cap_ratio()
    specimens = [('Claude', 'cjk', f'{TYPE["preferred"]}  ·  {TYPE["weight"]}  ·  {f(LABEL["size"])} DIP'),
                 ('管理应用', 'cjk', f'{TYPE["cjk"]["family"]}  ·  Bold, the face DirectWrite picks for {TYPE["weight"]}'),
                 ('アプリの管理', 'ja', f'{ja["family"]}  ·  {TYPE["weight"]}, bundled  ·  page 16')]
    for i, (text, cjk, note) in enumerate(specimens):
        y = y0 + i * (ph + 12)
        panel(s, x0, y, pw, ph, fill=PAL['light']['plate'])
        bx = x0 + 24
        cx, cy = bx + lw / 2, y + ph / 2 + 10
        s.rect(bx, cy - lh / 2, lw, lh, stroke=RED, sw=0.7, dash=[3, 3])
        base = cy + cap * LABEL['size'] * k / 2
        top = base - cap * LABEL['size'] * k
        for yy, name in ((top, 'cap height'), (cy, 'plate center'), (base, 'baseline')):
            s.line(bx - 10, yy, bx + lw + 12, yy, RED, 0.6, dash=None if name == 'plate center' else [2, 2])
            s.text(bx + lw + 18, yy + 4, name, 10.5, RED, face='small-sb')
        s.text(cx, base, text, LABEL['size'] * k, LIGHT.foreground, face='label', cjk=cjk, align='center')
        caption(s, x0 + 18, y + 22, note, INK2, size=11.5, face='small-sb')
    caption(s, x0, y0 + 3 * (ph + 12) + 4, f'Box {f(LABEL["width"])} × {f(LABEL["height"])} on the plate center.  Baseline = center + capHeight × size / 2, '
            f'capHeight {cap:.2f} em from the Latin face, for every script.', RED, size=11, face='small-sb')

    # Truncation
    x1 = x0 + pw + 40
    heading(s, x1, y0 + 14, 'Truncation', 14)
    caption(s, x1, y0 + 34, f'One line in {f(LABEL["width"])} DIP; {LABEL["trimming"]} trimming. Widths measured with the product face.', INK2, size=12)
    kk = 2.0
    names = ['Visual Studio Code', 'Windows Settings', 'IntelliJ IDEA Ultimate', 'DeepSeek Harness (DSH)']
    for i, name in enumerate(names):
        y = y0 + 56 + i * 58
        bw, bh = LABEL['width'] * kk, 44
        s.rect(x1, y, bw + 24, bh, fill=PAL['light']['plate'], r=8)
        s.rect(x1 + 12, y + bh / 2 - LABEL['height'] * kk / 2, bw, LABEL['height'] * kk, stroke=RED, sw=0.6, dash=[2, 2])
        shown, cut = fonts.trim(name, LABEL['width'], 'label', 'cjk', LABEL['size'])
        R.centered_text(s, shown, x1 + 12 + bw / 2, y + bh / 2, LABEL['size'] * kk, LIGHT.foreground, 'label')
        wv = fonts.width(name, 'label', 'cjk', LABEL['size'])
        s.text(x1 + bw + 40, y + 18, f'{wv:.1f} DIP', 12, INK, face='small-sb')
        s.text(x1 + bw + 40, y + 34, 'trimmed with …' if cut else 'fits', 11.5, DONT if cut else DO, face='small-sb')

    # Tokens
    y = y0 + 56 + 4 * 58 + 22
    heading(s, x1, y, 'Type tokens', 14)
    rows = [('Label', f'{f(LABEL["size"])} DIP / {LABEL["weight"]}, {LABEL["lines"]} line, {f(LABEL["width"])} × {f(LABEL["height"])}, {LABEL["color"]}'),
            ('Monogram', f'{" / ".join(MONOGRAM["states"])}, {f(MONOGRAM["size"])} DIP / {MONOGRAM["weight"]}, cap-height centered, {MONOGRAM["color"]}'),
            ('Latin', f'{TYPE["preferred"]} ({TYPE["familyModel"]}); fallback {TYPE["fallback"]}'),
            ('Chinese', f'{TYPE["cjk"]["family"]}, {TYPE["cjk"]["locale"]}; then the system fallback'),
            ('Japanese', f'{TYPE["japanese"]["family"]} 400 / 600, then {TYPE["japanese"]["systemFamily"]}'),
            ('CJK ranges', ', '.join('U+' + r for r in TYPE['cjk']['ranges'][:3])),
            ('', ', '.join('U+' + r for r in TYPE['cjk']['ranges'][3:])),
            ('Kana ranges', ', '.join('U+' + r for r in TYPE['japanese']['ranges'])),
            ('Locales', f'{TYPE["locales"]["en"]} default, {TYPE["locales"]["zh"]}, {TYPE["locales"]["ja"]}; kana make a name {TYPE["locales"]["ja"]}'),
            ('Never drawn', ', '.join(OSD['neverDrawn'][:3])),
            ('', ', '.join(OSD['neverDrawn'][3:]))]
    for i, (a, b) in enumerate(rows):
        yy = y + 24 + i * 19
        s.text(x1, yy, a, 12, INK, face='small-sb')
        s.text(x1 + 104, yy, b, 12, INK2, face='small')
    # Monogram tiles
    yb = y0 + 3 * (ph + 12) + 26
    k2, bw2, bh2 = 1.6, 236, 112
    for j, th in enumerate((LIGHT, DARK)):
        bx = x0 + j * (bw2 + 14)
        panel(s, bx, yb, bw2, bh2, fill=th.plate)
        for i, code in enumerate(MONOGRAM['states']):
            cx = bx + bw2 / 2 + (i - 1) * 74
            with s.group(cx - R.C * k2, yb + bh2 / 2 - R.C * k2, k2):
                R.draw_tile(s, th, R.C, R.C)
                R.draw_art(s, th, R.LANGUAGE, R.C, R.C, lang=code)
    tx = x0 + 2 * (bw2 + 14) + 10
    heading(s, tx, yb + 28, 'The language is the icon', 13.5)
    para(s, tx, yb + 50, 'EN / ZH / JA names the interface language. Built-in labels are localized; user app names never are.',
         pw - (tx - x0), 12.5, 18)
    return p


# 05 Iconography: classes and command glyphs ---------------------------------------------------

def p05():
    p = Page(5, 'Iconography', 'Four kinds of mark, one tile.',
             f'Command glyphs from {ICON["commandFont"]}, open marks on a keyline, plated marks at full bleed, and one generic window.')
    s = p.s
    cw, chh = (CW - 3 * 24) / 4, 274
    y0 = TOP + 8
    k = 3.0
    classes = [
        ('Command', R.cmd('add', 'Add'), f'{ICON["commandFont"]} at em {f(GLYPH_EM)}, centered on the glyph\'s ink bounds, '
         f'in foreground. Vector fallback strokes {f(G["glyph"]["fallbackStroke"])} DIP (high contrast {f(G["glyph"]["fallbackStrokeHighContrast"])}), round caps and joins.'),
        ('Glyph', R.app('chatgpt', 'ChatGPT'), f'An open mark on the neutral tile. Its long ink side fits the {f(KEYLINE)} DIP keyline × opticalScale. '
         'Also Notepad, Settings and custom apps via their Shell icons.'),
        ('Plated', R.app('claude', 'Claude'), f'A mark with its own filled plate: cropped to the plate, scaled to cover {f(TILE)} DIP, '
         f'clipped to r {f(TILE_R)}, hairline on top. No neutral tile beneath.'),
        ('Generic', R.app('generic', 'App'), f'No artwork at all: {ICON["commandGlyphNotes"]["GenericApp"].split(";")[0]} on the neutral tile. '
         'Initials are never generated.'),
    ]
    for i, (name, it, body) in enumerate(classes):
        x = ML + i * (cw + 24)
        s.rect(x, y0, cw, chh, stroke=RULE, r=12)
        heading(s, x + 18, y0 + 28, name, 14)
        cx, cy = x + cw / 2, y0 + 122
        with s.group(cx - R.C * k, cy - R.C * k, k):
            R.draw_item(s, LIGHT, it, R.C, R.C)
        t = TILE * k
        if name == 'Command':
            em = GLYPH_EM * k
            s.rect(cx - em / 2, cy - em / 2, em, em, stroke=RED, sw=0.6, dash=[3, 3])
            ink = fonts.icon_outline(glyph_code('Add'))[1]
            s.rect(cx - ink[0] * em / 2, cy - ink[1] * em / 2, ink[0] * em, ink[1] * em, stroke=RED, sw=0.5, dash=[1.5, 1.5])
            caption(s, cx, cy - t / 2 - 12, f'em {f(GLYPH_EM)}  ·  ink {ink[0] * GLYPH_EM:.1f}', RED, 'center', 11, 'small-sb')
        elif name == 'Glyph':
            tr = art.treatment('chatgpt')
            kl = KEYLINE * tr.optical * k
            s.rect(cx - kl / 2, cy - kl / 2, kl, kl, stroke=RED, sw=0.6, dash=[3, 3])
            caption(s, cx, cy - t / 2 - 12, f'keyline {f(KEYLINE)} × {f(tr.optical)} = {KEYLINE * tr.optical:.1f}', RED, 'center', 11, 'small-sb')
        elif name == 'Plated':
            caption(s, cx, cy - t / 2 - 12, f'cover {f(TILE)}, clip r {f(TILE_R)}, hairline on top', RED, 'center', 11, 'small-sb')
        else:
            em = GLYPH_EM * k
            s.rect(cx - em / 2, cy - em / 2, em, em, stroke=RED, sw=0.6, dash=[3, 3])
            caption(s, cx, cy - t / 2 - 12, f'U+{ICON["commandGlyphs"]["GenericApp"]} at em {f(GLYPH_EM)}', RED, 'center', 11, 'small-sb')
        para(s, x + 18, y0 + 206, body, cw - 36, 12, 17)

    # Command glyph map: four rows of seven at a 72 pitch; the last row's tiles end above BOTTOM.
    y = y0 + chh + 38
    heading(s, ML, y, 'Command glyph map', 14)
    caption(s, ML + 160, y, f'{ICON["commandFont"]} code points from tokens iconography.commandGlyphs; fallback {ICON["commandFontFallback"]}, '
            'then vector paths. The badge check is always a path.', INK2, size=12)
    glyphs = list(ICON['commandGlyphs'].items())
    per_row = 7
    gw = CW / per_row
    kk = 1.4
    small = {'ChevronLeftSmall', 'ChevronRightSmall', 'BackChevron'}
    for i, (name, code) in enumerate(glyphs):
        x = ML + (i % per_row) * gw
        yy = y + 18 + (i // per_row) * 72
        cx, cy = x + 36, yy + 36
        em = CHEVRON['backEm'] if name == 'BackChevron' else CHEVRON['em'] if name in small else GLYPH_EM
        with s.group(cx - R.C * kk, cy - R.C * kk, kk):
            if name in small:
                s.rect(R.C - TILE / 2, R.C - TILE / 2, TILE, TILE, fill=LIGHT.plate, r=TILE_R)
            else:
                R.draw_tile(s, LIGHT, R.C, R.C)
            R.glyph(s, int(code, 16), R.C, R.C, em, LIGHT.secondary if name in small else LIGHT.foreground, knob=name == 'SwitchOn')
        s.text(x + 74, yy + 28, name, 12, INK, face='small-sb')
        s.text(x + 74, yy + 44, f'U+{code}', 11.5, INK2, face='mono')
        note = ICON['commandGlyphNotes'].get(name, '').split(' (')[0].split(' at ')[0].split(';')[0]
        # The Segoe Fluent name, from the notes that cover several glyphs at once (Tray, Switch, Update).
        note = {'ChevronLeftSmall': 'ChevronLeft', 'ChevronRightSmall': 'ChevronRight', 'GenericApp': 'window',
                'Previous': 'ChevronLeft', 'Next': 'ChevronRight', 'TrayPreview': 'View', 'TraySettings': 'Settings',
                'TrayDiagnostics': 'Diagnostic', 'TrayExit': 'PowerButton', 'SwitchOn': 'ToggleFilled',
                'SwitchOff': 'ToggleLeft', 'CheckUpdate': 'Refresh', 'CheckFailed': 'SyncError'}.get(name, note or name)
        s.text(x + 74, yy + 59, f'{note}  ·  em {f(em)}', 10.5, INK3, face='small')
    return p


# 06 Iconography: vendor marks ----------------------------------------------------------------

BRAND_NAMES = {'air': 'JetBrains Air', 'antigravity': 'Antigravity', 'chatgpt': 'ChatGPT', 'claude': 'Claude', 'clion': 'CLion',
               'dsh': 'DeepSeek Harness', 'gemini': 'Gemini', 'goland': 'GoLand', 'intellij-idea': 'IntelliJ IDEA',
               'jetbrains': 'JetBrains', 'junie': 'Junie', 'perplexity': 'Perplexity', 'phpstorm': 'PhpStorm',
               'pycharm': 'PyCharm', 'rider': 'Rider', 'rubymine': 'RubyMine', 'rustrover': 'RustRover', 'toolbox': 'Toolbox',
               'vscode': 'Visual Studio Code', 'webstorm': 'WebStorm', 'zcode': 'ZCode',
               'notepad': 'Notepad', 'settings': 'Windows Settings', 'generic': 'Custom app, no icon'}


def p06():
    p = Page(6, 'Iconography', 'Vendor marks, never redrawn.',
             'Each mark keeps its own color and proportion. The per-brand treatment is tokens iconography.brands, shown here at 1.25×.')
    s = p.s
    keys = []
    for b in ICON['brands']:
        k = 'chatgpt' if b['key'].startswith('chatgpt') else b['key']
        # App marks only: the reel's own mark and GitHub's belong to About, not to an app tile.
        if k not in keys and k in BRAND_NAMES:
            keys.append(k)
    keys += ['notepad', 'settings', 'generic']
    cols, gap = 6, 16
    cw = (CW - (cols - 1) * gap) / cols
    rows = math.ceil(len(keys) / cols)
    ch = 134
    k = 1.25
    sw = 62
    for i, key in enumerate(keys):
        x = ML + (i % cols) * (cw + gap)
        y = TOP + 10 + (i // cols) * (ch + 10)
        for j, th in enumerate((LIGHT, DARK)):
            bx = x + j * (sw + 6)
            s.rect(bx, y, sw, sw, fill=th.plate, r=10)
            with s.group(bx + sw / 2 - R.C * k, y + sw / 2 - R.C * k, k):
                R.draw_item(s, th, R.app(key, BRAND_NAMES[key]), R.C, R.C)
        s.text(x, y + sw + 20, BRAND_NAMES[key], 12.5, INK, face='text-sb')
        if key == 'generic':
            line1, line2 = 'GenericApp glyph', f'U+{ICON["commandGlyphs"]["GenericApp"]}, em {f(GLYPH_EM)}'
        else:
            tr = art.treatment(key)
            stem = 'chatgpt-light / -dark' if key == 'chatgpt' else tr.key
            line1 = stem + ('  ·  Shell icon' if key in art.SHELL else '')
            if tr.plated:
                line2 = 'plated  ·  cover, clip r10'
            else:
                line2 = f'glyph  ·  {KEYLINE * tr.optical:.1f} DIP ({f(tr.optical)})'
            if tr.origin == 'derived':
                line2 += '  ·  measured'
        s.text(x, y + sw + 36, line1, 11, INK2, face='mono')
        s.text(x, y + sw + 51, line2, 11, INK2, face='small')
        if key not in ('generic',):
            tr = art.treatment(key)
            c = tr.content
            if c != (0, 0, 1, 1):
                s.text(x, y + sw + 65, 'content ' + ', '.join(f'{v:.3f}'.rstrip('0').rstrip('.') for v in c), 10, INK3, face='mono')
    y = TOP + 10 + rows * (ch + 10) + 10
    caption(s, ML, y, 'Sources: the assets/brands PNGs the runtime decodes (SVG originals alongside); owner, source and hash in '
            'assets/brands/provenance.json. Content rects are normalized to the source canvas; "measured" = runtime alpha analysis.',
            INK2, size=12)
    return p


# 07 State matrix ----------------------------------------------------------------------------

SLICE = (-46, 140, 52)      # DIP: from the center leftwards, to past the 3 o'clock item, and half height
_acrylic_cache = {}


def _backdrop(th, x, y, scale, seed):
    """Acrylic crop for a reel whose canvas square starts at (x, y) page units."""
    key = (th.key, round(x), round(y), scale, seed)
    if key not in _acrylic_cache:
        size = CANVAS * scale
        wall = art.wallpaper(size, size, th.dark, seed, window=False)
        _acrylic_cache[key] = (wall, R.frosted_backdrop(th, wall, (x, y, size, size), x + size / 2, y + size / 2, scale))
    return _acrylic_cache[key]


def slice_cell(s, x, y, w, h, th, st, scale, seed=0):
    """A center-to-3-o'clock slice of the reel, clipped to a rounded cell."""
    cx, cy = x - SLICE[0] * scale, y + h / 2
    bg = {'light': PANEL, 'dark': PANEL_DARK, 'contrast': '#000000'}[th.key]
    with s.clip_rect(x, y, w, h, 8):
        s.rect(x, y, w, h, fill=bg)
        backdrop = None
        if th.frosted:
            wall, full = _backdrop(th, cx - R.C * scale, cy - R.C * scale, scale, seed)
            # Only the visible slice of the backdrop is embedded (reel-local DIP box).
            box = (R.C + SLICE[0], R.C - SLICE[2], w / scale, h / scale)
            px = full.size[0] / CANVAS
            backdrop = (full.crop(tuple(round(v * px) for v in (box[0], box[1], box[0] + box[2], box[1] + box[3]))), box)
            if st.plate_alpha < 1:   # the desktop only shows through a fading plate
                ox, oy = cx - R.C * scale, cy - R.C * scale
                wpx = wall.size[0] / (CANVAS * scale)
                crop = wall.crop((round((x - ox) * wpx), round((y - oy) * wpx), round((x - ox + w) * wpx), round((y - oy + h) * wpx)))
                s.image(crop, x, y, w, h)
        R.reel(s, cx, cy, th, st, scale, backdrop)
    if th.key == 'light' and not th.frosted:
        s.rect(x, y, w, h, stroke='#000000', sw=0.6, r=8, alpha=0.06, inset=True)


def state_models():
    """(name, note, State) with the item of interest at slot 1 of 4 (3 o'clock)."""
    gem, chat, claude = R.app('gemini', 'Gemini'), R.app('chatgpt', 'ChatGPT'), R.app('claude', 'Claude')
    four = [R.app('vscode', 'Visual Studio Code'), gem, R.app('notepad', 'Notepad'), R.PLUS]
    add_off = R.cmd('add', 'Add app', enabled=False)
    hub4 = [R.LANGUAGE, R.cmd('appearance', 'Appearance'), R.cmd('manage', 'Manage apps'), R.cmd('about', 'About')]
    commit_alpha = 1 - MOTION['events']['commit']['holdMs'] / MOTION['events']['commit']['fadeMs']
    return [
        ('Idle', 'neutral tiles, faint ring', R.State(four)),
        ('Selected', 'seat replaces the tile fill; one name', R.State(four, selected=1)),
        ('Disabled', f'{f(DISABLED)} opacity; named on hover; never seated', R.State([R.LANGUAGE, add_off, R.cmd('manage', 'Manage apps'),
                                                                               R.cmd('about', 'About')], selected=1, submenu=True)),
        ('Preferred', f'accent badge at (+{f(BADGE["offset"])}, −{f(BADGE["offset"])})', R.State([four[0], chat, four[2], R.PLUS], preferred=1)),
        ('Keyboard focus', 'the same seat; glyphs stay foreground', R.State(hub4, selected=1, submenu=True, language=True)),
        ('Commit', f't = {MOTION["events"]["commit"]["holdMs"]} ms: the choice holds, the rest fades',
         R.State([four[0], claude, four[2], R.PLUS], selected=1, plate_alpha=commit_alpha, label_alpha=commit_alpha,
                 item_alpha={0: commit_alpha, 2: commit_alpha, 3: commit_alpha}, item_scale={1: MOTION['events']['commit']['pressScale']})),
    ]


def p07():
    p = Page(7, 'States', 'Six states in every appearance.',
             'Each cell is the slice from the plate center to the item at 3 o\'clock. Selection is only ever the seat.')
    s = p.s
    models = state_models()
    variants = [(LIGHT, 'Light'), (DARK, 'Dark'), (LIGHT_F, 'Light frosted'), (DARK_F, 'Dark frosted'), (HC, 'High contrast')]
    lab = 104
    cw = (CW - lab) / len(models)
    gap = 10
    y0 = TOP + 6
    # The slice fills its column, but never so tall that the rows and the two caption lines pass BOTTOM.
    scale = min((cw - 12) / (SLICE[1] - SLICE[0]), ((BOTTOM - (y0 + 62) - 29) / len(variants) - gap) / (2 * SLICE[2]))
    ch = 2 * SLICE[2] * scale
    for j, (name, note, _) in enumerate(models):
        x = ML + lab + j * cw
        heading(s, x, y0 + 12, name, 13.5)
        for i, line in enumerate(fonts.wrap(note, cw - 18, 'small', 11)):
            caption(s, x, y0 + 30 + i * 14, line, INK2, size=11)
    y = y0 + 62
    for i, (th, vname) in enumerate(variants):
        yy = y + i * (ch + gap)
        heading(s, ML, yy + ch / 2 + 5, vname, 13)
        if th.frosted:
            tag(s, ML, yy + ch / 2 + 26, 'tonal')
        for j, (_, _, st) in enumerate(models):
            x = ML + lab + j * cw
            slice_cell(s, x, yy, cw - 12, ch, th, st, scale, seed=j)
    yy = y + len(variants) * (ch + gap) + 12
    w = tag(s, ML, yy, 'vector')
    caption(s, ML + w + 8, yy, 'Vector scenes from tokens.json; light and dark match the native captures (page 15). Frosted rows are tonal '
            'illustrations over a sample wallpaper.', INK2, size=11.5)
    caption(s, ML + w + 8, yy + 17, 'High contrast shows the High Contrast Black sample; the runtime paints with live GetSysColor values.',
            INK2, size=11.5)
    return p


# 08 Layouts ---------------------------------------------------------------------------------

def layout_models():
    apps = R.APPS
    more = [R.app('dsh', 'DeepSeek Harness (DSH)'), R.app('vscode', 'Visual Studio Code'), R.app('intellij-idea', 'IntelliJ IDEA'),
            R.app('pycharm', 'PyCharm'), R.app('junie', 'Junie')]
    n_hub = len(R.HUB)
    return [
        (2, 'Manage apps, one app', R.State([apps[0], R.cmd('add', 'Add app')], preferred=0, submenu=True)),
        (3, 'App actions, last app', R.State(R.ENTRY[:3], preferred=0, submenu=True)),
        (4, 'App actions', R.State(R.ENTRY, preferred=0, submenu=True)),
        (5, 'About', R.State(R.ABOUT, submenu=True)),
        (6, 'Main, five apps', R.State(apps[:5] + [R.PLUS], preferred=0)),
        (n_hub, 'The + hub', R.State(R.HUB, language=True, submenu=True)),
        (7, 'Presets, first page', R.State(R.PRESETS, submenu=True)),
        (8, 'Presets, middle page', R.State(more + [R.cmd('add', 'Custom app'), R.cmd('previous', 'Previous'), R.cmd('next', 'Next')], submenu=True)),
        (9, f'Main at capacity, {MAX_APPS} apps', R.State(R.EIGHT + [R.PLUS], preferred=0)),
        (n_hub, f'Hub at {MAX_APPS} apps: Add disabled', R.State(R.hub(True), language=True, submenu=True)),
    ]


def p08():
    p = Page(8, 'Layouts', 'One orbit for two to nine items.',
             f'Slot 0 at {G["orbit"]["firstSlot"]}, then {G["orbit"]["direction"]} in equal steps of 360/n. At capacity the Add slot stays, disabled.')
    s = p.s
    cols, k = 5, 0.6
    cw = CW / cols
    rh = 304
    for i, (n, name, st) in enumerate(layout_models()):
        x = ML + (i % cols) * cw
        y = TOP + 4 + (i // cols) * rh
        cx, cy = x + cw / 2, y + CANVAS * k / 2
        R.reel(s, cx, cy, LIGHT, st, k)
        for j in range(n):
            a = math.radians(slot_angle(j, n))
            rr = (ORBIT_R + 34) * k
            s.text(cx + rr * math.cos(a), cy + rr * math.sin(a) + 3.5, str(j), 9.5, RED, face='small-sb', align='center')
        heading(s, x + 22, y + CANVAS * k + 14, f'n = {n}  ·  {f(round(360 / n, 1))}°', 13.5)
        caption(s, x + 22, y + CANVAS * k + 31, name, INK2, size=12)
    return p


# 09 Navigation ------------------------------------------------------------------------------

def _arrow(s, x1, y1, x2, y2, text='', color=INK2, above=True, dash=None):
    s.line(x1, y1, x2, y2, color, 1.1, dash=dash)
    a = math.atan2(y2 - y1, x2 - x1)
    for d in (2.6, -2.6):
        s.line(x2, y2, x2 + 7 * math.cos(a + d), y2 + 7 * math.sin(a + d), color, 1.1, cap='round')
    if text:
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        s.text(mx, my - 7 if above else my + 15, text, 11.5, INK, face='small-sb', align='center')


def p09():
    p = Page(9, 'Navigation', 'Main, then one +.',
             'Main holds the apps and a single +. Everything else hangs off the + hub; the center or Esc returns one level.')
    s = p.s
    k = 0.42
    pr = PLATE_R * k + 6
    xs = [ML + 96, ML + 420, ML + 770, ML + 1076]
    top, mid, bot = 384, 500, 628
    nodes = {
        'main': (xs[0], mid, 'Main', 'apps and +', R.State(R.MAIN, preferred=0)),
        'hub': (xs[1], mid, '+ hub', 'language, appearance, reel size, tray icon, manage, add, about', R.State(R.HUB, language=True, submenu=True)),
        'manage': (xs[2], top, 'Manage apps', 'apps and Add app', R.State(R.MANAGE, preferred=0, submenu=True)),
        'about': (xs[2], bot, 'About', 'the version, the author on GitHub, two switches, updates', R.State(R.ABOUT, submenu=True)),
        'presets': (xs[3], top, 'Presets', 'five per page, Custom, paging', R.State(R.PRESETS, submenu=True)),
        'entry': (xs[3], bot, 'App actions', 'the app, edit, set as preferred, remove', R.State(R.ENTRY, preferred=0, submenu=True)),
    }
    for depth, x in enumerate(xs):
        caption(s, x, TOP + 14, f'depth {depth}', INK3, 'center', 11, 'small-sb')
    for key, (x, y, name, note, st) in nodes.items():
        R.reel(s, x, y, LIGHT, st, k)
        s.text(x, y + pr + 12, name, 13.5, INK, face='text-sb', align='center')
        caption(s, x, y + pr + 28, note, INK2, 'center', 11.5)

    def edge(a, b, text, above=True, t=0.5):
        x1, y1 = nodes[a][:2]
        x2, y2 = nodes[b][:2]
        ang = math.atan2(y2 - y1, x2 - x1)
        ax, ay = x1 + pr * math.cos(ang), y1 + pr * math.sin(ang)
        bx, by = x2 - pr * math.cos(ang), y2 - pr * math.sin(ang)
        _arrow(s, ax, ay, bx, by)
        if text:
            tx, ty = ax + (bx - ax) * t, ay + (by - ay) * t
            nx, ny = math.sin(ang), -math.cos(ang)          # normal pointing up for left-to-right lines
            if not above:
                nx, ny = -nx, -ny
            half = fonts.width(text, 'small-sb', 'cjk', 11.5) / 2
            off = 10 + abs(math.sin(ang)) * half            # clear the line at the label's ends
            s.text(tx + nx * off, ty + ny * off + 4, text, 11.5, INK, face='small-sb', align='center')
    edge('main', 'hub', '+  Apps and settings')
    edge('hub', 'manage', 'Manage apps', t=0.42)
    edge('hub', 'about', 'About', above=False, t=0.42)
    edge('manage', 'presets', 'Add app')
    edge('manage', 'entry', 'an app', t=0.3)
    # The hub's own Add app drills straight to Presets, above Manage.
    hx, hy = nodes['hub'][:2]
    px, py = nodes['presets'][:2]
    y_arc = top - pr - 36
    s.path([('M', hx, hy - pr), ('C', hx, y_arc, hx + 40, y_arc, hx + 160, y_arc), ('L', px - 160, y_arc),
            ('C', px - 40, y_arc, px, y_arc, px, py - pr - 2)], stroke=INK2, sw=1.1)
    _arrow(s, px, py - pr - 12, px, py - pr - 2)
    s.text((hx + px) / 2, y_arc - 8, 'Add app', 11.5, INK, face='small-sb', align='center')
    # Native editor
    ex, ey, ew, eh = W - MR - 112, mid - 36, 112, 72
    s.rect(ex, ey, ew, eh, fill=PAPER, stroke=INK3, sw=1, r=8)
    with s.clip_rect(ex, ey, ew, eh, 8):
        s.rect(ex, ey, ew, 16, fill=PANEL)
    caption(s, ex + ew / 2, ey + 42, 'Native editor', INK, 'center', 12, 'small-sb')
    caption(s, ex + ew / 2, ey + 58, 'name and target', INK2, 'center', 11)
    for node, text, sign in (('presets', 'Custom app', 1), ('entry', 'Edit app', -1)):
        nx, ny = nodes[node][:2]
        a = math.radians(32 * sign)
        sx_, sy_ = nx + pr * math.cos(a), ny + pr * math.sin(a)
        tx_, ty_ = ex + 26, ey - 4 if sign > 0 else ey + eh + 4
        _arrow(s, sx_, sy_, tx_, ty_)
        s.text((sx_ + tx_) / 2 + 10, (sy_ + ty_) / 2 + (-6 if sign > 0 else 14), text, 11.5, INK, face='small-sb')
    # In-place actions on the hub
    caption(s, hx, hy + pr + 50, 'In place, no navigation: EN / ZH / JA via chevrons or A / D;', INK2, 'center', 11.5)
    caption(s, hx, hy + pr + 65, 'Reel size and Tray icon change on each confirm.', INK2, 'center', 11.5)
    caption(s, hx, hy + pr + 80, 'Appearance opens Windows color settings.', INK2, 'center', 11.5)
    # Legend
    y = BOTTOM - 40
    s.line(ML, y - 22, W - MR, y - 22, RULE)
    items = [('Deeper', f'DrillIn: the tapped tile travels to the center ({MOTION["events"]["handoff"]["travelMs"]} ms) and becomes the back chevron.'),
             ('Back', 'Center or Esc: DrillOut, one level; Main closes. Saving a choice also returns.'),
             ('Paging', f'Previous / Next swap pages in place ({MOTION["events"]["swap"]["enterMs"]} ms).')]
    cw = CW / 3
    for i, (a, b) in enumerate(items):
        x = ML + i * cw
        heading(s, x, y, a, 13)
        para(s, x, y + 19, b, cw - 30, 12, 17)
    return p


# 10 Placement -------------------------------------------------------------------------------

def pointer(s, x, y, scale=1.0, dark=False):
    """The Windows arrow cursor, tip at (x, y)."""
    pts = [(0, 0), (0, 17), (4.2, 13.2), (7, 19.4), (9.6, 18.3), (6.9, 12.2), (12.4, 12.2)]
    cmds = [('M', x + pts[0][0] * scale, y + pts[0][1] * scale)] + [('L', x + px * scale, y + py * scale) for px, py in pts[1:]] + [('Z',)]
    s.path(cmds, fill='#FFFFFF' if not dark else '#000000', stroke='#000000' if not dark else '#FFFFFF', sw=1.0 * scale, join='miter')


def p10():
    p = Page(10, 'Placement', 'Centered on the pointer; clamped only at edges.',
             'The pointer is the plate center whenever the canvas fits. At an edge the reel moves inward just enough.')
    s = p.s
    k = 0.6
    cw = (CW - 3 * 24) / 4
    ph = 360
    y0 = TOP + 6
    half = CANVAS * k / 2
    scenarios = [
        ('Interior', 'Pointer = plate center. Choose a direction at once.', 'interior'),
        ('Screen edge', 'The 384 DIP footprint moves inward to the work area; the pointer may end up off the plate.', 'edge'),
        ('Corner', 'Both axes clamp minimally; the circle, order and angles stay.', 'corner'),
        ('Off the plate', 'A pointer left outside the plate selects by direction as soon as it moves; it never enters the center first.', 'armed'),
    ]
    for i, (title, note, kind) in enumerate(scenarios):
        x = ML + i * (cw + 24)
        panel(s, x, y0, cw, ph)
        # Work area: the panel; the taskbar sits below it in the corner case.
        wx0, wy0, wx1, wy1 = x, y0, x + cw, y0 + ph
        if kind == 'corner':
            wy1 = y0 + ph - 34
            s.rect(x, wy1, cw, ph - (wy1 - y0), fill='#E6E6E6', r=0)
            with s.clip_rect(x, y0, cw, ph, 12):
                s.rect(x, wy1, cw, ph - (wy1 - y0), fill='#E3E3E3')
            caption(s, x + 14, wy1 + 22, 'taskbar', INK3, size=11)
        if kind == 'interior':
            px, py = x + cw / 2, y0 + ph / 2
        elif kind in ('edge', 'armed'):
            px, py = wx1 - 14, y0 + ph / 2 - 30
        else:
            px, py = wx1 - 16, wy1 - 22
        cx = min(max(px, wx0 + half), wx1 - half)
        cy = min(max(py, wy0 + half), wy1 - half)
        # Off the plate, a move of the pointer selects the sector it points at from the plate center.
        n = len(R.MAIN)
        sector = int(((math.atan2(py - cy, px - cx) + math.pi / 2) % (2 * math.pi) + math.pi / n) // (2 * math.pi / n)) % n
        st = R.State(R.MAIN, preferred=0, selected=sector if kind == 'armed' else -1)
        with s.clip_rect(x, y0, cw, ph, 12):
            R.reel(s, cx, cy, LIGHT, st, k)
            if kind != 'interior':
                s.rect(cx - half, cy - half, 2 * half, 2 * half, stroke=RED, sw=0.7, dash=[3, 3])
            if kind == 'armed':
                mx, my = px - (px - cx) * 0.08, py - (py - cy) * 0.08
                s.path([('M', px, py), ('L', mx, my)], stroke=RED, sw=0.9, dash=[2, 3])
                s.circle(px, py, 2.5, fill=RED)
                pointer(s, mx, my, 0.9)
            else:
                pointer(s, px, py, 0.9)
        if kind in ('edge', 'corner'):
            caption(s, cx, cy - half - 8 if cy - half - 8 > y0 + 16 else cy + half + 16, f'canvas {f(CANVAS)} clamped to the work area',
                    RED, 'center', 10.5, 'small-sb')
        heading(s, x, y0 + ph + 26, title, 14)
        para(s, x, y0 + ph + 46, note, cw - 6, 12.5, 18)
    y = y0 + ph + 104
    rules = [('Zero offset', f'Interior: pointer offset {OSD and T["edgeLayout"]["pointerOffset"]}; no line from the pointer, no cursor warp.'),
             ('Minimal clamp', f'Clamp the whole canvas, shadow margin included ({f(ELEV["canvasMargin"])} DIP); keep order, angles, radius and icon size.'),
             ('No gate', f'On the plate or outside it (r {f(PLATE_R)}), selection goes by direction once the pointer moves 4 DIP; the opening position never selects, and an early release cancels.'),
             ('Small screens', 'If the canvas cannot fit, scale it uniformly; text and targets shrink with it.')]
    cw2 = (CW - 3 * 24) / 4
    for i, (a, b) in enumerate(rules):
        x = ML + i * (cw2 + 24)
        heading(s, x, y, a, 13)
        para(s, x, y + 19, b, cw2 - 6, 12, 17)
    return p


# 11 Motion: timing --------------------------------------------------------------------------

CURVE_COLORS = {'linear': '#8A8A8A', 'decelerate': '#0F6CBD', 'accelerate': '#C4314B', 'drill': '#8764B8', 'spring': '#038387'}


def _bezier_points(curve, n=40):
    from tokens import bezier
    return [(i / n, bezier(curve, i / n)) for i in range(n + 1)]


def _curve_plot(s, x, y, w, h, curve, title):
    s.rect(x, y, w, h, fill=PAPER, stroke=RULE, r=8)
    px, py, pw, ph = x + 14, y + 30, w - 28, h - 52
    s.line(px, py + ph, px + pw, py + ph, RULE, 1)
    s.line(px, py, px, py + ph, RULE, 1)
    s.line(px, py + ph, px + pw, py, RULE, 0.8, dash=[2, 2])
    col = CURVE_COLORS[curve]
    c = MOTION['curves'][curve]
    for (ax, ay), (bx, by) in (((0, 0), (c[0], c[1])), ((1, 1), (c[2], c[3]))):
        s.line(px + ax * pw, py + ph - ay * ph, px + bx * pw, py + ph - by * ph, col, 0.7, dash=[1.5, 1.5])
        s.circle(px + bx * pw, py + ph - by * ph, 2.2, fill=col)
    pts = _bezier_points(curve)
    s.path([('M', px, py + ph)] + [('L', px + u * pw, py + ph - v * ph) for u, v in pts[1:]], stroke=col, sw=1.8)
    heading(s, x + 14, y + 20, title, 12.5)
    mono(s, x + 14, y + h - 10, f'({", ".join(f(v) for v in c)})', INK2, size=10.5)


def motion_rows():
    E = MOTION['events']
    o, cl, cm, se, lb = E['open'], E['close'], E['commit'], E['selection'], E['centerLabel']
    dr, di, do_, ho, sw = E['drill'], E['drillIn'], E['drillOut'], E['handoff'], E['swap']
    hold = cm['holdMs']
    return [
        ('Open', [('opacity 0 → 1', 0, o['fadeMs'], o['fadeCurve']), (f'scale {f(o["scaleFrom"])} → 1', 0, o['scaleMs'], o['scaleCurve'])]),
        ('Close (cancel)', [('opacity → 0', 0, cl['fadeMs'], cl['fadeCurve']), (f'scale → {f(cl["scaleTo"])}', 0, cl['scaleMs'], cl['scaleCurve'])]),
        ('Commit', [('plate, others, label → 0', 0, cm['fadeMs'], cm['fadeCurve']),
                    (f'chosen: hold {hold}, then → 0', hold, cm['fadeMs'], cm['fadeCurve']),
                    (f'chosen: 1 → {f(cm["pressScale"])} → 1', 0, cm['pressMs'], 'press')]),
        ('Seat appears', [('seat opacity 0 → 1', 0, se['seatFadeMs'], 'linear'),
                          (f'seat scale {f(se["seatAppearScaleFrom"])} → 1', 0, se['seatAppearMs'], se['seatAppearCurve']),
                          ('tile fill → 0', 0, se['tileFillFadeMs'], 'linear')]),
        ('Seat travels', [(f'angle: spring ζ {f(se["travel"]["dampingRatio"])}, period {se["travel"]["periodMs"]} ms, overshoot {seat_overshoot()}', 0, 140, 'spring'),
                          ('tile fills swap', 0, se['tileFillFadeMs'], 'linear')]),
        ('Name', [('crossfade; ring hides', 0, lb['crossfadeMs'], lb['curve'])]),
        ('Drill in', [(f'out: scale → {f(di["exitScale"])}, → 0', 0, dr['exitMs'], 'decelerate'),
                      (f'in: scale {f(di["enterScaleFrom"])} → 1', 0, dr['enterMs'], dr['enterCurve']),
                      ('in: opacity 0 → 1', dr['enterDelayMs'], dr['enterFadeMs'], dr['enterFadeCurve'])]),
        ('Drill out', [(f'out: scale → {f(do_["exitScale"])}, → 0', 0, dr['exitMs'], 'decelerate'),
                       (f'in: scale {f(do_["enterScaleFrom"])} → 1', 0, dr['enterMs'], dr['enterCurve']),
                       ('in: opacity 0 → 1', dr['enterDelayMs'], dr['enterFadeMs'], dr['enterFadeCurve'])]),
        ('Handoff', [(f'tile → center, scale → {f(ho["scaleTo"])}', 0, ho['travelMs'], ho['curve']),
                     ('clone → 0 / back chevron in', ho['travelMs'] - ho['fadeMs'], ho['fadeMs'], 'linear'),
                     ('outgoing name clears', 0, ho['labelFadeMs'], 'linear')]),
        ('Swap', [('out → 0', 0, sw['exitMs'], sw['exitCurve']), (f'in: → 1, scale {f(sw["enterScaleFrom"])} → 1', 0, sw['enterMs'], sw['enterCurve'])]),
    ]


def _progress(curve, u):
    from tokens import bezier
    if curve == 'press':
        return 1 - abs(1 - 2 * bezier('decelerate', u))
    if curve == 'spring':
        z, wn = MOTION['events']['selection']['travel']['dampingRatio'], 4.4
        wd = wn * math.sqrt(1 - z * z)
        t = u * 1.0
        return 1 - math.exp(-z * wn * t) * (math.cos(wd * t) + z / math.sqrt(1 - z * z) * math.sin(wd * t))
    return bezier(curve, u)


def p11():
    p = Page(11, 'Motion', 'Brief, smooth, from the plate center.',
             'Full tier on Windows.UI.Composition: no app frame timers, easing without overshoot, every property from its current value.')
    s = p.s
    rows = motion_rows()
    x0, name_w = ML, 136
    cx0 = x0 + name_w
    cwid = 640
    span = 300
    ms = cwid / span
    lane, gap = 16, 12
    y = TOP + 26
    for t in range(0, span + 1, 50):
        xx = cx0 + t * ms
        s.text(xx, y - 6, f'{t} ms' if t == span else f'{t}', 10.5, INK3, face='small', align='center')
    total = sum(len(b) * lane + gap for _, b in rows)
    for t in range(0, span + 1, 50):
        s.line(cx0 + t * ms, y, cx0 + t * ms, y + total, RULE, 0.6)
    yy = y + 4
    for name, bars in rows:
        s.text(x0, yy + 11, name, 12, INK, face='small-sb')
        for i, (label, start, dur, curve) in enumerate(bars):
            by = yy + i * lane
            bx, bw = cx0 + start * ms, max(2.0, dur * ms)
            col = CURVE_COLORS.get(curve, CURVE_COLORS['decelerate'] if curve == 'press' else '#8A8A8A')
            s.rect(bx, by + 1, bw, lane - 3, fill=col, r=2, alpha=0.14)
            pts = [(u, _progress(curve, u)) for u in [j / 30 for j in range(31)]]
            s.path([('M', bx + pts[0][0] * bw, by + lane - 3 - pts[0][1] * (lane - 5))] +
                   [('L', bx + u * bw, by + lane - 3 - v * (lane - 5)) for u, v in pts[1:]], stroke=col, sw=1.1)
            if curve == 'spring':
                s.text(bx + bw + 6, by + 10.5, label + '  (observed ≈ 140 ms)', 10.5, INK2, face='small')
            else:
                s.text(bx + bw + 6, by + 10.5, f'{label}  ·  {dur} ms', 10.5, INK2, face='small')
        yy += len(bars) * lane + gap
    red = MOTION['events']['reducedMotion']
    caption(s, x0, yy + 14, f'Reduced motion and high contrast: the same code path with every duration {red["durationMs"]} ms; completion still fires. '
            f'Language EN / ZH / JA: only the monogram crossfades ({MOTION["events"]["language"]["monogramCrossfadeMs"]} ms).', INK2, size=11.5)

    # Curves
    rx = cx0 + cwid + 70
    rw = W - MR - rx
    pw = (rw - 12) / 2
    for i, (curve, title) in enumerate((('linear', 'Linear'), ('decelerate', 'Decelerate'), ('accelerate', 'Accelerate'), ('drill', 'Drill'))):
        _curve_plot(s, rx + (i % 2) * (pw + 12), TOP + 8 + (i // 2) * 170, pw, 158, curve, title)
    y2 = TOP + 8 + 2 * 170 + 18
    heading(s, rx, y2, 'Never', 13.5)
    for i, rule in enumerate(MOTION['notAllowed']):
        caption(s, rx, y2 + 20 + i * 16, '–  ' + rule[0].upper() + rule[1:], INK2, size=11.5)
    caption(s, rx, y2 + 20 + len(MOTION['notAllowed']) * 16 + 8, f'Origin: {MOTION["origin"]}.', INK2, size=11.5)
    return p


# 12 Motion: native stills ---------------------------------------------------------------------

# Native evidence: the reference stills of tools/capture-seam.py, named <variant>-<state>.png, and the motion
# strips, which only the 0.3.4 capture recorded (tools/capture-v034.py); the motion tokens they show are unchanged since.
STILLS_VERSION, MOTION_VERSION = '0.3.13', '0.3.4'
STILLS_DIR = ROOT / 'output' / 'runtime' / f'v{STILLS_VERSION}'
MOTION_DIR = ROOT / 'output' / 'runtime' / f'v{MOTION_VERSION}'


@lru_cache(None)
def _runtime(name):
    return Image.open(MOTION_DIR / name).convert('RGB')


@lru_cache(None)
def strip_frame(name, row, col):
    """One frame of a native motion strip: 8 columns of 240 px, rows of 264 px below a 40 px header."""
    img = _runtime(name)
    return img.crop((col * 240, 40 + row * 264, col * 240 + 240, 40 + row * 264 + 240))


MOTION_STILLS = [
    ('Open', 'motion-open-select.png', 'open requested at 300 ms', [(0, 6, '354'), (0, 7, '375'), (1, 0, '396'), (1, 1, '417'), (1, 2, '437'), (1, 5, '500')]),
    ('Close', 'motion-open-select.png', 'cancel at 51 ms', [(0, 0, '0'), (0, 1, '83'), (0, 2, '104'), (0, 3, '125'), (0, 4, '146'), (0, 5, '167')]),
    ('Seat', 'motion-open-select.png', 'appears on Claude, then travels to Gemini', [(1, 6, '785'), (1, 7, '805'), (2, 1, '847'), (2, 7, '1104'),
                                                                                  (3, 0, '1125'), (3, 2, '1166'), (3, 4, '1208'), (3, 6, '1250')]),
    ('Drill in', 'motion-drill.png', '+ clicked at 300 ms: handoff to the back chevron', [(1, 6, '361'), (1, 7, '382'), (2, 0, '403'), (2, 1, '424'),
                                                                                       (2, 2, '444'), (2, 4, '486'), (2, 7, '549')]),
    ('Drill out', 'motion-drill.png', 'Esc at 850 ms', [(3, 2, '889'), (3, 4, '930'), (3, 5, '951'), (3, 6, '972'), (4, 1, '1035')]),
    ('Commit', 'motion-commit.png', 'click at 300 ms', [(1, 7, '389'), (2, 0, '410'), (2, 1, '431'), (2, 2, '451'), (2, 3, '472')]),
]


def p12():
    p = Page(12, 'Motion', 'The same motion, captured.',
             f'Frames from Windows Graphics Capture of the native {MOTION_VERSION} window (dark, solid), whose timings '
             f'{STILLS_VERSION} keeps. Times are capture timestamps.')
    s = p.s
    fs = 92
    y = TOP + 6
    row_h = fs + 24
    rows = MOTION_STILLS
    i = 0
    while i < len(rows):
        pair = rows[i:i + 2] if rows[i][0] in ('Drill out',) else rows[i:i + 1]
        x = ML
        for n, (title, src, note, frames) in enumerate(pair):
            lab = 150 if n == 0 else 120
            heading(s, x, y + 18, title, 13.5)
            for j, line in enumerate(fonts.wrap(note, lab - 16, 'small', 11)):
                caption(s, x, y + 36 + j * 14, line, INK2, size=11)
            fx = x + lab
            for (r, c, t) in frames[:4] if n else frames:
                s.image(strip_frame(src, r, c), fx, y, fs, fs)
                caption(s, fx + fs / 2, y + fs + 15, f'{t} ms', INK2, 'center', 11)
                fx += fs + 8
            x = fx + 30
        y += row_h
        i += len(pair)
    w = tag(s, ML, y + 8, 'native')
    caption(s, ML + w + 8, y + 8, f'output/runtime/v{MOTION_VERSION}/motion-open-select.png, motion-drill.png, motion-commit.png '
            '(frame strips at about 48 fps).', INK2, size=11.5)
    caption(s, ML + w + 8, y + 25, f'No later build recorded motion strips, so these frames show {MOTION_VERSION}\'s five-item hub, '
            f'drawn before the {f(G["scale"])} reel scale.', INK2, size=11.5)
    return p


# 13 Material and elevation --------------------------------------------------------------------

def p13():
    fr = MATERIAL['frosted']
    p = Page(13, 'Material', 'Solid by default, frosted when Windows allows.',
             'Transparency off: the opaque neutral plate. Transparency on: luminosity acrylic over the host backdrop, with the same tiles.')
    s = p.s
    k = 0.66
    pw, ph = (CW - 3 * 20) / 4, 300
    y0 = TOP + 6
    cases = [(LIGHT, 'Light solid', 'vector'), (DARK, 'Dark solid', 'vector'), (LIGHT_F, 'Light frosted', 'tonal'), (DARK_F, 'Dark frosted', 'tonal')]
    for i, (th, name, kind) in enumerate(cases):
        x = ML + i * (pw + 20)
        with s.clip_rect(x, y0, pw, ph, 12):
            cx, cy = x + pw / 2, y0 + ph / 2 - 6
            backdrop = None
            if th.frosted:
                wall = art.wallpaper(pw, ph, th.dark, 2 + i)
                s.image(wall, x, y0, pw, ph)
                backdrop = R.frosted_backdrop(th, wall, (x, y0, pw, ph), cx, cy, k)
            else:
                s.rect(x, y0, pw, ph, fill=PANEL_DARK if th.dark else PANEL)
            R.reel(s, cx, cy, th, R.State(R.MAIN, preferred=0, selected=1), k, backdrop)
        heading(s, x, y0 + ph + 24, name, 13.5)
        tag(s, x + pw - fonts.width(kind.upper(), 'small-sb', 'cjk', 9.5, 0.8) - 14, y0 + ph + 22, kind)
        if th.frosted:
            f_ = fr['dark' if th.dark else 'light']
            lines = [f'tint {f_["tint"]}, tint opacity {f(f_["tintOpacity"])}', f'luminosity opacity {f(f_["luminosityOpacity"])}',
                     f'tile white at {f(PAL["dark" if th.dark else "light"]["frostedTileAlpha"])}; fallback {f_["fallback"]}']
        else:
            pal = PAL['dark' if th.dark else 'light']
            lines = [f'plate {pal["plate"]}, opaque', f'shadow {round(pal["shadowOpacity"] * 100)}%, blur {f(SHADOW_BLUR)}, y +{f(SHADOW_DY)}',
                     f'rim {_role_value("dark" if th.dark else "light", "plateStroke")}']
        for j, line in enumerate(lines):
            caption(s, x, y0 + ph + 44 + j * 16, line, INK2, size=11.5)

    # Recipe and layers
    y = y0 + ph + 118
    heading(s, ML, y, 'Acrylic recipe', 13.5)
    steps = ['Host backdrop', 'Luminosity blend', 'Color blend', 'Noise', 'Plate']
    notes = ['blurred by the shell;\nno extra blur', 'tint RGB at\nluminosity opacity', 'tint at\ntint opacity',
             f'{fr["noise"]["size"]} px tile, {round(fr["noise"]["opacity"] * 100)}%,\n{fr["noise"]["border"]}, made once', 'transparent fill;\nrim and tiles on top']
    bw, gapx = 150, 26
    for i, (a, b) in enumerate(zip(steps, notes)):
        x = ML + i * (bw + gapx)
        s.rect(x, y + 14, bw, 30, fill=PANEL, r=6)
        s.text(x + bw / 2, y + 34, a, 12, INK, face='small-sb', align='center')
        for j, line in enumerate(b.split('\n')):
            caption(s, x + bw / 2, y + 62 + j * 15, line, INK2, 'center', 11)
        if i:
            _arrow(s, x - gapx + 4, y + 29, x - 4, y + 29)
    x2 = ML + 5 * (bw + gapx) + 20
    heading(s, x2, y, 'Solid whenever', 13.5)
    for i, cond in enumerate(fr['fallbackToSolidWhen']):
        caption(s, x2, y + 24 + i * 16, '–  ' + cond[0].upper() + cond[1:], INK2, size=11.5)
    caption(s, x2, y + 24 + 4 * 16 + 6, 'Rebuilt only when dark, transparency, high contrast', INK3, size=11)
    caption(s, x2, y + 24 + 5 * 16 + 6, 'or capabilities change.', INK3, size=11)
    yl = y + 150
    heading(s, ML, yl, 'Layers, back to front', 13.5)
    layers = ['backdrop', 'shadow + plate + rim', 'ring', 'seat', 'page items', 'handoff', 'back + trash', 'labels ×2']
    lw = (CW - 7 * 8 - 170) / 8
    for i, name in enumerate(layers):
        x = ML + 170 + i * (lw + 8)
        s.rect(x, yl - 14, lw, 22, fill=PANEL, r=5)
        s.text(x + lw / 2, yl + 1, name, 11, INK2, face='small-sb', align='center')
    return p


# 14 Do / Don't --------------------------------------------------------------------------------

def _dont_scene(s, kind, x, y, w, h, good):
    """One crop of the reel: the correct design (good) or the named mistake, drawn for contrast only."""
    th = LIGHT
    k = 0.92
    with s.clip_rect(x, y, w, h, 10):
        s.rect(x, y, w, h, fill=PANEL)
        cx, cy = x + w / 2, y + h / 2
        if kind == 'plate':
            st = R.State(R.MAIN, preferred=0)
            kp = 0.54
            R.reel(s, cx, cy, th, st, kp)
            if not good:
                s.circle(cx, cy, (CENTER_R + 20) * kp, fill='#FFFFFF', stroke='#D1D1D1', sw=1)
                s.circle(cx, cy, (PLATE_R - 20) * kp, stroke='#D1D1D1', sw=1)
        elif kind == 'ring':
            st = R.State(R.MAIN, preferred=0, selected=1)
            R.reel(s, cx, cy + 6, th, st, k)
            if not good:
                s.circle(cx, cy + 6, CENTER_R * k, stroke=th.secondary, sw=1.2)
        elif kind == 'captions':
            st = R.State(R.HUB, language=True, submenu=True, selected=None if good else 1, label='' if good else None)
            R.reel(s, cx, cy + 20, th, R.State(R.HUB, language=True, submenu=True), k)
            if not good:
                with s.group(cx - R.C * k, cy + 20 - R.C * k, k):
                    s.circle(R.C, R.C, CENTER_R - 2, fill=th.plate)
                    R.centered_text(s, 'Settings', R.C, R.C - 9, 14, th.foreground, 'label')
                    s.text(R.C, R.C + 12, 'Release to choose', 11, th.secondary, face='small', align='center')
                    for i, it in enumerate(R.HUB):
                        ix, iy = R.item_center(i, len(R.HUB))
                        s.text(ix, iy + 32, it.name, 9.5, th.secondary, face='small', align='center')
        elif kind == 'sweep':
            st = R.State(R.MAIN, preferred=0)
            kp = 0.54
            R.reel(s, cx, cy, th, st, kp)
            if not good:
                from scene import arc
                s.path(arc(cx, cy, (PLATE_R - 1) * kp, -90, 110), stroke=th.accent, sw=2.5)
                s.line(x + w - 18, y + h - 16, cx + 16, cy + 12, th.accent, 1.8, cap='round')
                s.circle(x + w - 18, y + h - 16, 3.5, fill=th.accent)
        elif kind == 'marks':
            for i, key in enumerate(('claude', 'chatgpt')):
                tx = cx + (i - .5) * 92
                kk = 1.8
                with s.group(tx - R.C * kk, cy - R.C * kk, kk):
                    s.rect(R.C - 26, R.C - 26, 52, 52, fill=th.plate, r=13)
                    if good:
                        R.draw_item(s, th, R.app(key, key), R.C, R.C)
                    else:
                        R.draw_tile(s, th, R.C, R.C)
                        # Schematic stand-in: vendor artwork is never altered, not even for this example.
                        for a in range(0, 180, 30) if i == 0 else range(0, 360, 60):
                            ar = math.radians(a)
                            if i == 0:
                                s.line(R.C - 11 * math.cos(ar), R.C - 11 * math.sin(ar), R.C + 11 * math.cos(ar), R.C + 11 * math.sin(ar),
                                       th.accent, 2.4, cap='round')
                            else:
                                s.circle(R.C + 5 * math.cos(ar), R.C + 5 * math.sin(ar), 6.5, stroke=th.accent, sw=1.6)
        elif kind == 'accent':
            kk = 2.0
            with s.group(cx - R.C * kk, cy - R.C * kk, kk):
                s.rect(R.C - 34, R.C - 34, 68, 68, fill=th.plate, r=14)
                R.draw_seat(s, th, R.C, R.C)
                R.glyph(s, glyph_code('Manage'), R.C, R.C, GLYPH_EM, th.foreground if good else th.accent)
                if not good:
                    s.circle(R.C, R.C + 31, 2, fill=th.accent)


def p14():
    p = Page(14, 'Do and don\'t', 'Less, and only what the product draws.',
             'Six mistakes the native renderer cannot make, and the design it draws instead.')
    s = p.s
    pairs = [('plate', 'One plate', 'No plate inside the plate: no inner disc, panel or second rim.'),
             ('ring', 'The name owns the center', 'The ring hides while a name shows; never draw a label over the ring.'),
             ('captions', 'No captions, no helper copy', 'No titles, tile captions, hints or second lines. One name, on hover.'),
             ('sweep', 'No sweep, no connector', 'No accent perimeter stroke and no line from the pointer. The plate simply appears.'),
             ('marks', 'Marks stay true', 'Never recolor, outline or redraw a vendor mark (shown here schematically).'),
             ('accent', 'Glyphs stay foreground', 'The seat carries selection; no accent glyphs, no dot under the selection.')]
    cols = 3
    cw = (CW - (cols - 1) * 32) / cols
    sw_, sh = (cw - 12) / 2, 196
    for i, (kind, title, body) in enumerate(pairs):
        x = ML + (i % cols) * (cw + 32)
        y = TOP + 6 + (i // cols) * 306
        heading(s, x, y + 14, title, 14)
        for j, good in enumerate((True, False)):
            bx = x + j * (sw_ + 12)
            _dont_scene(s, kind, bx, y + 28, sw_, sh, good)
            word = 'Do' if good else 'Don\'t'
            pw_ = fonts.width(word, 'small-sb', 'cjk', 11.5) + 36
            s.rect(bx + 6, y + 28 + sh - 28, pw_, 22, fill=PAPER, r=11, alpha=0.94)
            check_mark(s, bx + 18, y + 28 + sh - 17, good, 14)
            s.text(bx + 30, y + 28 + sh - 13, word, 11.5, DO if good else DONT, face='small-sb')
        para(s, x, y + 28 + sh + 24, body, cw, 12.5, 18)
    return p


# 15 Runtime evidence -------------------------------------------------------------------------

def p15():
    p = Page(15, 'Evidence', f'Verified in the native {STILLS_VERSION} build.',
             'Windows Graphics Capture of the app window through the test seam, with the visual override; alpha background; 2x unless noted.')
    s = p.s
    folder = STILLS_DIR
    shots = [('light-solid-main.png', 'Light, Main'), ('light-solid-main-claude.png', 'Light, Claude selected'),
             ('light-solid-hub-language.png', 'Light, hub, Language'), ('dark-solid-hub-tray.png', 'Dark, hub, Tray icon'),
             ('dark-solid-about.png', 'Dark, About'), ('dark-solid-edge.png', 'Dark, corner, by direction'),
             ('contrast-main-claude.png', 'Contrast path*, Claude'), ('contrast-hub-appearance.png', 'Contrast path*, Appearance')]
    n = 8
    gap = 10
    fs = (CW - (n - 1) * gap) / n
    y = TOP + 4
    for i, (name, cap) in enumerate(shots):
        x = ML + i * (fs + gap)
        img = Image.open(folder / name).convert('RGBA')
        dark = 'dark' in name
        s.rect(x, y, fs, fs, fill=PANEL_DARK if dark else PANEL, r=10)
        s.image(img, x, y, fs, fs)
        caption(s, x, y + fs + 16, cap, INK2, size=11)
    tag(s, ML, y + fs + 40, 'native')
    caption(s, ML + 74, y + fs + 40, '* Contrast path: the high-contrast code path with this machine\'s current system colors (not a High Contrast theme).',
            INK2, size=11.5)
    # DPI comparison: the selected seat at 100 / 150 / 200 %
    y2 = y + fs + 74
    heading(s, ML, y2, 'One tile at 100, 150 and 200 %', 13.5)
    caption(s, ML, y2 + 18, 'Dark, hub, Appearance selected; each crop enlarged to the same size, nearest neighbour.', INK2, size=11.5)
    crops = [('dark-100-hub-appearance.png', 1, '100 %  ·  96 DPI'), ('dark-150-hub-appearance.png', 1.5, '150 %  ·  144 DPI'),
             ('dark-solid-hub-appearance.png', 2, '200 %  ·  192 DPI')]
    ix, iy = R.item_center(1, len(R.HUB))
    box = 64     # DIP around the seat
    zs = 196
    for i, (name, scale, label) in enumerate(crops):
        img = Image.open(folder / name).convert('RGB')
        # The still is the reel's canvas, centered, drawn at geometry.scale of its DIP values.
        k = scale * G['scale']
        l, t = img.width / 2 + (ix - R.C - box / 2) * k, img.height / 2 + (iy - R.C - box / 2) * k
        crop = img.crop((round(l), round(t), round(l + box * k), round(t + box * k)))
        crop = crop.resize((zs * 3, zs * 3), Image.NEAREST)
        x = ML + i * (zs + 16)
        s.image(crop, x, y2 + 30, zs, zs, jpeg=False)
        caption(s, x, y2 + 30 + zs + 16, label, INK2, size=11)
    # Verified / not verified
    x3 = ML + 3 * (zs + 16) + 40
    w3 = (W - MR - x3 - 32) / 2
    heading(s, x3, y2, 'Runtime-verified', 13.5)
    verified = ['Light and dark solid plates: Main, the seven-item hub, About, Manage apps with drag and trash; tiles, seat, '
                'badge, switches, chevrons, name and ring',
                'The high-contrast code path (seat in COLOR_HIGHLIGHT) with current system colors',
                'Rendering at 100, 150 and 200 % through the DPI override',
                f'Motion strips ({MOTION_VERSION}): open, cancel, seat appear and travel, drill in with handoff, drill out, commit',
                f'EN / ZH ({MOTION_VERSION}): idle Main pixel-identical; the hub differs only inside the monogram',
                'Placement: zero interior offset, minimal clamp at eight edge and corner positions, no cursor warp; off the '
                'plate, selection by direction']
    yy = y2 + 26
    for line in verified:
        yy = para(s, x3, yy, '–  ' + line, w3, 12, 17) + 8
    x4 = x3 + w3 + 32
    heading(s, x4, y2, 'Not verified yet', 13.5)
    pending = ['The live frosted backdrop over real content (window capture omits the DWM host backdrop)',
               'Real High Contrast themes (Aquatic, Desert, Dusk, Night sky)',
               f'Acrylic blend-mode mapping: tokens blendModeVerified is "{fr_status()}"',
               'Physical Copilot-key timing (tap versus hold) on hardware',
               'Reduced motion, 144 / 240 Hz smoothness and GPU cost']
    yy = y2 + 26
    for line in pending:
        yy = para(s, x4, yy, '–  ' + line, w3, 12, 17) + 8
    caption(s, ML, y2 + 30 + zs + 40, f'Evidence folder: output/runtime/v{STILLS_VERSION}, the reference set of tools/capture-seam.py '
            f'({len(list(folder.glob("*.png")))} stills).', INK3, size=11)
    caption(s, ML, y2 + 30 + zs + 55, f'Motion strips: output/runtime/v{MOTION_VERSION}. Validation log: docs/validation.md.', INK3, size=11)
    return p


def fr_status():
    return MATERIAL['frosted']['blendModeVerified']


# 16 Typography in three scripts --------------------------------------------------------------

def p16():
    ja = TYPE['japanese']
    p = Page(16, 'Typography', 'Three scripts, one voice.',
             "Apple's system pairing is the reference; the reel keeps Windows' own faces where they match it and bundles one where they do not.")
    s = p.s
    x0, y0 = ML, TOP + 14
    cols = [0, 130, 300, 490]     # script, Apple, Windows, Copilot Reel (specimen)
    wl = 790
    for c, title in zip(cols, ('Script', 'Apple reference', 'Windows system', 'Copilot Reel')):
        s.text(x0 + c, y0, title, 12, INK3, face='small-sb')
    s.line(x0, y0 + 10, x0 + wl, y0 + 10, RULE, 1)
    rows = [('Latin', 'SF Pro', 'Segoe UI Variable', f'{TYPE["preferred"]} {TYPE["weight"]}', 'Manage apps', 'cjk'),
            ('Chinese', 'PingFang SC', TYPE['cjk']['family'], f'{TYPE["cjk"]["family"]} Bold', '管理应用', 'cjk'),
            ('Japanese', 'Hiragino Sans', ja['systemFamily'], f'{ja["family"]} {TYPE["weight"]}', 'アプリの管理', 'ja')]
    rh = 88
    for i, (script, apple, system, reel, specimen, cjk) in enumerate(rows):
        y = y0 + 22 + i * rh
        if i == 2:
            s.rect(x0 - 10, y - 4, wl + 20, rh - 8, fill=PANEL, r=10)
        heading(s, x0, y + 26, script, 14)
        caption(s, x0 + cols[1], y + 26, apple, INK2, size=12.5)
        caption(s, x0 + cols[2], y + 26, system, INK2, size=12.5)
        caption(s, x0 + cols[3], y + 26, reel, INK, size=12.5, face='small-sb')
        s.text(x0 + cols[3], y + 64, specimen, 28, INK, face='label', cjk=cjk)
        s.line(x0, y + rh - 12, x0 + wl, y + rh - 12, RULE, 1)

    y = y0 + 22 + 3 * rh + 20
    heading(s, x0, y, 'Why Japanese is bundled', 14)
    y = para(s, x0, y + 22, 'SF Pro, PingFang SC and Hiragino Sans share a large face, low stroke contrast, flat terminals and open '
             'counters, so mixed-script names sit evenly. Hiragino drops the uroko at stroke shoulders, centres each glyph\'s '
             'weight and draws kana slightly large. Fontworks\' Rodin and UD faces share that build, but neither they nor Hiragino '
             'may ship inside another app without a licence, and Fontworks\' open faces are a pencil script or geometric brand faces. '
             'Yu Gothic UI is hinted but draws condensed kana. Noto Sans JP (Source Han Sans) is the open face closest to Hiragino.',
             wl, 12.5, 18)
    y += 18
    heading(s, x0, y, 'One fallback, chosen by locale', 14)
    rules = [('1', f'{ja["locale"]} text: Han, CJK punctuation and kana to {ja["family"]}, then {ja["systemFamily"]} for what the subset lacks.'),
             ('2', f'Other locales: kana to {ja["family"]}; Han and punctuation to {TYPE["cjk"]["family"]}, which keeps mainland forms.'),
             ('3', f'Everything else: the system fallback. A name that contains kana is laid out as {ja["locale"]} in every interface.')]
    for i, (n, rule) in enumerate(rules):
        yy = y + 22 + i * 19
        s.text(x0, yy, n, 12, RED, face='small-sb')
        s.text(x0 + 18, yy, rule, 12, INK2, face='small')

    x1 = x0 + wl + 50
    w1 = W - MR - x1
    heading(s, x1, y0 + 4, ja['family'], 14)
    facts = ['Noto Sans JP 2.004, static instances at 400 and 600',
             'Windows-31J, kana, CJK symbols and the interface strings: 7,617 characters, 2.4 MB a weight',
             "Segoe UI's line metrics, 1.079 / 0.251 em, so a Japanese line is as tall as an English one",
             'Renamed under the SIL Open Font License; OFL.txt ships beside it (tools/build-fonts.py)']
    yy = y0 + 26
    for line in facts:
        yy = para(s, x1, yy, '–  ' + line, w1, 12, 17) + 4
    img = Image.open(ROOT / 'design' / 'vi' / 'evidence' / 'typography-ja.png').convert('RGB')
    iy = yy + 10
    ih = min(w1 * img.height / img.width, BOTTOM - 30 - iy)
    iw = ih * img.width / img.height
    s.image(img, x1, iy, iw, ih, jpeg=False)
    w = tag(s, x1, iy + ih + 20, 'native')
    caption(s, x1 + w + 8, iy + ih + 20, f'Hub at 96, 144, 192 DPI: {ja["family"]} (left), {ja["systemFamily"]} (right)', INK2, size=11)
    return p


PAGES = [p01, p02, p03, p04, p05, p06, p07, p08, p09, p10, p11, p12, p13, p14, p15, p16]
