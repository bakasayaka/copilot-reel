"""The Soft Orbit reel as vector scenes: a line-by-line mirror of src/renderer.cpp.

Layer order (back to front, renderer Initialize): shadow, backdrop (frosted), plate fill, rim,
idle ring, back chevron, page (seat, items, language chevrons), handoff
clone, center label. Every size comes from tokens.json via tokens.py.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field, replace

import art
import fonts
from scene import Scene, arc, round_rect_path, transform_path
from tokens import (CANVAS, PLATE_R, ORBIT_R, CENTER_R, RING, TILE, TILE_R, TILE_STROKE,
                    SEAT, SEAT_R, GLYPH_EM, BADGE, SWITCH, CHEVRON, DISABLED, LABEL, MONOGRAM, SHADOW_BLUR, SHADOW_DY,
                    G, APP_VERSION, glyph_code, slot_angle, theme as make_theme, acrylic_recipe)

C = CANVAS / 2


@dataclass(frozen=True)
class Item:
    key: str              # brand key ('claude'), Shell key ('notepad'), 'cmd:<name>', 'language', 'size', 'generic'
    name: str
    enabled: bool = True

    @property
    def command(self):
        return self.key.startswith('cmd:')


def app(key, name, enabled=True):
    return Item(key, name, enabled)


def cmd(name, label, enabled=True):
    return Item('cmd:' + name, label, enabled)


LANGUAGE = Item('language', 'Language')
SIZE = Item('size', 'Reel size')
# The Reel size tile's monogram at the default step (geometry.sizeDefault).
SIZE_LABEL = MONOGRAM['sizeStep']['states'][G['sizeDefault']]
AUTHOR = 'baka_sayaka'    # src/radial_menu.h kAuthor

# Representative content (names as the runtime displays them; built-in commands from src/radial_menu.h).
APPS = [app('chatgpt', 'ChatGPT'), app('claude', 'Claude'), app('gemini', 'Gemini'), app('perplexity', 'Perplexity'),
        app('vscode', 'Visual Studio Code'), app('notepad', 'Notepad'), app('settings', 'Windows Settings')]
PLUS = cmd('add', 'Apps and settings')
MAIN = APPS + [PLUS]
# The + hub in its settled order. Tray icon shows the reel's color mark (the default color icon); the
# preferred agent is set on an app's actions page, never here.
HUB = [LANGUAGE, cmd('appearance', 'Appearance'), SIZE, app('copilot-reel', 'Tray icon'), cmd('manage', 'Manage apps'),
       cmd('add', 'Add app'), cmd('about', 'About')]
MANAGE = APPS + [cmd('add', 'Add app')]
# About at its defaults: the reel's own mark named by the version and the author, GitHub, Auto-check on,
# Auto-install off and the update tile before a check.
ABOUT = [app('copilot-reel', f'{APP_VERSION} · {AUTHOR}'), app('github', 'GitHub'), cmd('auto-check:on', 'Auto-check'),
         cmd('auto-install:off', 'Auto-install'), cmd('check-update', 'Check for updates')]
PRESETS = [app('chatgpt', 'ChatGPT / Codex'), app('claude', 'Claude Desktop'), app('claude', 'Claude Science'),
           app('antigravity', 'Antigravity'), app('zcode', 'ZCode'), cmd('add', 'Custom app'), cmd('next', 'Next')]
# App actions: the app itself first (it carries its preferred badge), then Edit, Set as preferred and,
# while more than one app remains, Remove.
ENTRY = [APPS[0], cmd('edit', 'Edit app'), cmd('preferred', 'Set as preferred'), cmd('remove', 'Remove from reel')]
EIGHT = APPS + [app('dsh', 'DeepSeek Harness (DSH)')]
COMMAND_GLYPHS = {'add': 'Add', 'manage': 'Manage', 'preferred': 'Preferred', 'edit': 'Edit', 'remove': 'Remove',
                  'previous': 'Previous', 'next': 'Next', 'about': 'About', 'check-update': 'CheckUpdate'}


def command_glyph(name, dark):
    """src/artwork.cpp CommandGlyph: Appearance follows the theme, About's switches carry their state."""
    if name == 'appearance':
        return 'Moon' if dark else 'Sun'
    if name.startswith('auto-') and name.endswith((':on', ':off')):
        return 'SwitchOn' if name.endswith(':on') else 'SwitchOff'
    return COMMAND_GLYPHS[name]


def hub(at_capacity=False):
    """The hub; at capacity its Add app tile stays in place, disabled."""
    return [replace(it, enabled=not at_capacity) if it.key == 'cmd:add' else it for it in HUB]


def item_center(index, count):
    a = math.radians(slot_angle(index, count))
    return C + ORBIT_R * math.cos(a), C + ORBIT_R * math.sin(a)


@dataclass
class State:
    """A rendered moment of the reel. Opacities/scales let the motion pages draw frames."""
    items: list
    selected: int = -1
    preferred: int = -1
    language: bool = False        # PageModel.languageControl (the hub)
    submenu: bool = False
    lang: str = 'EN'
    label: str | None = None      # override the center text (None = the selected item's name)
    root_alpha: float = 1.0
    root_scale: float = 1.0
    page_alpha: float = 1.0
    page_scale: float = 1.0
    seat_alpha: float | None = None
    seat_scale: float = 1.0
    seat_angle: float | None = None   # degrees; travel frames place the seat between slots
    label_alpha: float = 1.0
    center_alpha: float = 1.0     # ring / back chevron opacity multiplier
    plate_alpha: float = 1.0      # commit: plate, shadow and rim fade together
    item_alpha: dict = field(default_factory=dict)
    item_scale: dict = field(default_factory=dict)
    hidden: frozenset = frozenset()
    handoff: tuple | None = None  # (index, progress 0..1 position, scale, alpha)
    back_alpha: float | None = None
    shadow: bool = True


def glyph(s: Scene, code, cx, cy, em, color, alpha=1.0, knob=False):
    """A command glyph centered on its ink. `knob`: the on switch, ToggleFilled with its knob cut
    out (geometry.switch), so the tile or the seat beneath shows through it as in src/artwork.cpp."""
    outline = fonts.icon_outline(code)
    if outline:
        path = transform_path(outline[0], cx, cy, em)
        if knob:
            path = path + _knob_hole(path, cx + SWITCH['knobX'] * em, cy, SWITCH['knobRadius'] * em)
        s.path(path, fill=color, alpha=alpha)


def _winding(cmds, px, py):
    """Nonzero winding number of a point for a path's control polygon (exact enough well inside the ink)."""
    w, start, prev = 0, None, None

    def edge(a, b):
        nonlocal w
        (x0, y0), (x1, y1) = a, b
        side = (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)
        if y0 <= py < y1 and side > 0:
            w += 1
        elif y1 <= py < y0 and side < 0:
            w -= 1
    for cmd, *v in cmds:
        pts = list(zip(v[0::2], v[1::2]))
        if cmd == 'M':
            if prev is not None and prev != start:
                edge(prev, start)
            start = prev = pts[0]
        elif cmd in ('L', 'C'):
            for p in pts:
                edge(prev, p)
                prev = p
        elif cmd == 'Z':
            if prev != start:
                edge(prev, start)
            prev = start
    if prev is not None and prev != start:
        edge(prev, start)
    return w


def _knob_hole(path, kx, ky, r):
    """A circle wound against the ink around (kx, ky), so the nonzero fill leaves it empty."""
    inside = _winding(path, kx, ky)
    if inside == 0:
        return []
    circle = arc(kx, ky, r, 0, 360) + [('Z',)]
    if (_winding(circle, kx, ky) > 0) == (inside > 0):
        circle = arc(kx, ky, r, 360, 0) + [('Z',)]
    return circle


def label_text(text):
    return fonts.trim(text, LABEL['width'], 'label', 'cjk', LABEL['size'])[0]


def centered_text(s: Scene, text, cx, cy, size, color, face, alpha=1.0):
    """renderer DrawCentered: baseline = center + capHeight * size / 2 (Latin face metrics)."""
    s.text(cx, cy + fonts.cap_ratio() * size / 2, text, size, color, face=face, cjk='cjk', align='center', alpha=alpha)


def draw_tile(s, th, x, y, fill=True, alpha=1.0, scale=1.0):
    t = TILE * scale
    if fill:
        s.rect(x - t / 2, y - t / 2, t, t, fill=th.tile, r=TILE_R * scale, alpha=alpha)
    s.rect(x - t / 2, y - t / 2, t, t, stroke=th.tileStroke, sw=TILE_STROKE, r=TILE_R * scale, alpha=alpha, inset=True)


def draw_seat(s, th, x, y, alpha=1.0, scale=1.0):
    w = th.seatStrokeWidth
    size, r = SEAT * scale, SEAT_R * scale
    s.rect(x - size / 2, y - size / 2, size, size, fill=th.selection, r=r, alpha=alpha)
    s.rect(x - size / 2, y - size / 2, size, size, stroke=th.seatStroke, sw=w, r=r, alpha=alpha, inset=True)


def draw_art(s, th, it: Item, x, y, *, selected=False, preferred=False, lang='EN', alpha=1.0, show_badge=True):
    """renderer DrawItemArt: monogram, command glyph, raster artwork, plated hairline, badge."""
    fg = th.selectionText if (selected and th.hc) else th.foreground
    if it.key == 'language':
        centered_text(s, lang, x, y, MONOGRAM['size'], fg, 'monogram', alpha)
    elif it.key == 'size':
        centered_text(s, SIZE_LABEL, x, y, MONOGRAM['sizeStep']['size'], fg, 'monogram', alpha)
    elif it.command:
        g = command_glyph(it.key[4:], th.dark)
        glyph(s, glyph_code(g), x, y, GLYPH_EM, fg, alpha, knob=g == 'SwitchOn')
    else:
        placement = art.placed(it.key, th.dark) if it.key != 'generic' else None
        if placement is None:
            glyph(s, glyph_code('GenericApp'), x, y, GLYPH_EM, fg, alpha)
        else:
            img, ox, oy, w, h, plated = placement
            if plated:
                with s.clip(round_rect_path(x - TILE / 2, y - TILE / 2, TILE, TILE, TILE_R)):
                    s.image(img, x + ox, y + oy, w, h, alpha)
                draw_tile(s, th, x, y, fill=False, alpha=alpha)
            else:
                s.image(img, x + ox, y + oy, w, h, alpha)
    if preferred and show_badge:
        bx, by = x + BADGE['offset'], y - BADGE['offset']
        s.circle(bx, by, BADGE['ringRadius'], fill=th.selection if selected else th.plate, alpha=alpha)
        s.circle(bx, by, BADGE['radius'], fill=th.accent, alpha=alpha)
        e = 2 * BADGE['radius']   # the check figure is defined in em = badge diameter
        pts = [(-.25, 0), (-.0833, .1667), (.25, -.1667)]
        s.path([('M', bx + pts[0][0] * e, by + pts[0][1] * e)] + [('L', bx + px * e, by + py * e) for px, py in pts[1:]],
               stroke=th.textOnAccent, sw=BADGE['checkStroke'], alpha=alpha)


def is_plated(it: Item, th):
    if it.command or it.key in ('language', 'size', 'generic'):
        return False
    return art.source_path(it.key, th.dark) is not None and art.treatment(it.key, th.dark).plated


def draw_item(s, th, it, x, y, *, seated=False, selected=False, preferred=False, lang='EN', alpha=1.0, scale=1.0):
    alpha *= 1.0 if it.enabled else DISABLED
    with s.group(x * (1 - scale), y * (1 - scale), scale):
        if not is_plated(it, th) and not seated:
            draw_tile(s, th, x, y, alpha=alpha)
        draw_art(s, th, it, x, y, selected=selected, preferred=preferred, lang=lang, alpha=alpha)


def draw_reel(s: Scene, th, st: State, backdrop=None):
    """Draw at reel-local coordinates (canvas 0..CANVAS, center C). `backdrop` is an RGB image
    covering the canvas square for frosted plates (tonal illustration), or (image, (x, y, w, h))
    covering part of it."""
    n = len(st.items)
    with s.group(C * (1 - st.root_scale), C * (1 - st.root_scale), st.root_scale, st.root_alpha):
        # Plate: shadow, fill (or backdrop), rim.
        with s.group(alpha=st.plate_alpha):
            if th.shadowOpacity > 0 and st.shadow:
                img, left, top, size = art.shadow(PLATE_R, SHADOW_BLUR, SHADOW_DY, th.shadowOpacity, PLATE_R - 1)
                s.image(img, C + left, C + top, size, size)
            if th.frosted and backdrop is not None:
                img, box = backdrop if isinstance(backdrop, tuple) else (backdrop, (0, 0, CANVAS, CANVAS))
                with s.clip_circle(C, C, PLATE_R):
                    s.image(img, *box)
            else:
                s.circle(C, C, PLATE_R, fill=th.plate)
            rim = th.plateStrokeWidth
            s.circle(C, C, PLATE_R - rim / 2, stroke=th.plateStroke, sw=rim)

        # Center state (renderer UpdateCenter).
        seat_index = -1 if st.selected < 0 or not st.items[st.selected].enabled else st.selected
        text = st.label if st.label is not None else ('' if st.selected < 0 else st.items[st.selected].name)
        named = bool(text) and st.label_alpha > 0
        ca = st.center_alpha
        if not named:
            s.circle(C, C, CENTER_R, stroke=th.ringIdle, sw=RING, alpha=ca)
        back = st.back_alpha if st.back_alpha is not None else (1.0 if st.submenu and not named else 0.0)
        if back > 0:
            glyph(s, glyph_code('BackChevron'), C, C, CHEVRON['backEm'], th.secondary, back * ca)

        # Page: seat, items, chevrons.
        with s.group(C * (1 - st.page_scale), C * (1 - st.page_scale), st.page_scale, st.page_alpha):
            seat_alpha = st.seat_alpha if st.seat_alpha is not None else (1.0 if seat_index >= 0 else 0.0)
            if seat_alpha > 0 and (seat_index >= 0 or st.seat_angle is not None):
                if st.seat_angle is not None:
                    a = math.radians(st.seat_angle)
                    sx, sy = C + ORBIT_R * math.cos(a), C + ORBIT_R * math.sin(a)
                else:
                    sx, sy = item_center(seat_index, n)
                sa = st.item_alpha.get(seat_index, 1.0) * seat_alpha
                ss = st.item_scale.get(seat_index, 1.0) * st.seat_scale
                with s.group(sx * (1 - ss), sy * (1 - ss), ss):
                    draw_seat(s, th, sx, sy, sa)
            for i, it in enumerate(st.items):
                if i in st.hidden:
                    continue
                x, y = item_center(i, n)
                draw_item(s, th, it, x, y, seated=i == seat_index, selected=i == seat_index, preferred=i == st.preferred,
                          lang=st.lang, alpha=st.item_alpha.get(i, 1.0), scale=st.item_scale.get(i, 1.0))
            if st.language and n:
                active = st.selected == 0
                col = th.foreground if active else th.secondary
                y = C - ORBIT_R
                glyph(s, glyph_code('ChevronLeftSmall'), C - CHEVRON['offset'], y, CHEVRON['em'], col)
                glyph(s, glyph_code('ChevronRightSmall'), C + CHEVRON['offset'], y, CHEVRON['em'], col)

        # Handoff clone travelling to the center.
        if st.handoff:
            index, progress, scale, alpha = st.handoff
            x0, y0 = item_center(index, n)
            x, y = x0 + (C - x0) * progress, y0 + (C - y0) * progress
            it = st.items[index]
            with s.group(x * (1 - scale), y * (1 - scale), scale, alpha):
                if not is_plated(it, th):
                    draw_tile(s, th, x, y)
                draw_art(s, th, it, x, y, lang=st.lang)

        if named:
            centered_text(s, label_text(text), C, C, LABEL['size'], th.foreground, 'label', st.label_alpha)


def reel(s: Scene, cx, cy, th, st: State, scale=1.0, backdrop=None):
    """Place a reel with its plate center at (cx, cy) in page units."""
    with s.group(cx - C * scale, cy - C * scale, scale):
        draw_reel(s, th, st, backdrop)


def frosted_backdrop(th, wall, wall_box, cx, cy, scale):
    """Acrylic crop under a reel whose plate center sits at (cx, cy) over a wallpaper drawn in
    `wall_box` (x, y, w, h page units). Returns an RGB image covering the reel canvas square."""
    x, y, w, h = wall_box
    k = art.WALL_PX
    acr = art.acrylic(wall, acrylic_recipe('dark' if th.dark else 'light'), k, scale)
    left, top = (cx - C * scale - x) * k, (cy - C * scale - y) * k
    size = CANVAS * scale * k
    return acr.crop((round(left), round(top), round(left + size), round(top + size)))


def theme(key='light', material='solid'):
    return make_theme(key, material)
