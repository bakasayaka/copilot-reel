"""Read design/vi/tokens.json, the hand-edited source of every VI value.

Nothing here writes tokens.json. Geometry, palettes, motion, typography and brand
treatments come from the file; this module only resolves them into drawable values.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VI = ROOT / 'design' / 'vi'
TOKENS_PATH = VI / 'tokens.json'
T = json.loads(TOKENS_PATH.read_text(encoding='utf-8'))

G, PAL, MOTION, TYPE = T['geometry'], T['palettes'], T['motion'], T['typography']
ICON, ELEV, MATERIAL, OSD = T['iconography'], T['elevation'], T['systemAppearance'], T['osd']
VERSION, APP_VERSION = T['schemaVersion'], T['appVersion']


def tok(path):
    """Value at a dotted tokens.json path, e.g. tok('geometry.seat.size')."""
    value = T
    for part in path.split('.'):
        value = value[int(part)] if isinstance(value, list) else value[part]
    return value


# Geometry (DIP)
CANVAS = G['canvas']
PLATE_R, PLATE_STROKE, PLATE_INSET = G['plate']['radius'], G['plate']['stroke'], G['plate']['strokeInset']
ORBIT_R = G['orbit']['radius']
CENTER_R = G['centerZone']['radius']
RING = G['centerZone']['ringStroke']
TILE, TILE_R, TILE_STROKE, TILE_INSET = G['tile']['size'], G['tile']['radius'], G['tile']['stroke'], G['tile']['strokeInset']
SEAT, SEAT_R, SEAT_STROKE, SEAT_INSET = G['seat']['size'], G['seat']['radius'], G['seat']['stroke'], G['seat']['strokeInset']
GLYPH_EM = G['glyph']['em']
KEYLINE = G['artwork']['keyline']
BADGE = G['badge']
CHEVRON = G['chevrons']
DISABLED = G['disabledOpacity']
LABEL = TYPE['applicationLabel']
MONOGRAM = TYPE['iconMonogram']
SHADOW_BLUR, SHADOW_DY = ELEV['shadowBlur'], ELEV['shadowOffsetY']
MAX_APPS = OSD['maxApps']


def slot_angle(index, count):
    """Degrees on screen (y down) for a slot: first slot and direction from geometry.orbit."""
    hour = int(G['orbit']['firstSlot'].split()[0]) % 12
    sign = 1 if G['orbit']['direction'] == 'clockwise' else -1
    return hour * 30 - 90 + sign * index * 360 / count


# Colors are (r, g, b, a) floats.
def rgb(value):
    value = value.lstrip('#')
    return tuple(int(value[i:i + 2], 16) / 255 for i in (0, 2, 4))


def color(value):
    if isinstance(value, dict):
        return (*rgb(value['hex']), float(value['alpha']))
    return (*rgb(value), 1.0)


def over(top, base):
    """Composite `top` over an opaque `base`."""
    a = top[3]
    return tuple(top[i] * a + base[i] * (1 - a) for i in range(3)) + (1.0,)


def mix(a, b, t):
    """palettes.mix: a + (b - a) * t per channel, opaque."""
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3)) + (1.0,)


def hexof(c):
    return '#' + ''.join(f'{round(max(0, min(1, v)) * 255):02X}' for v in c[:3])


def luminance(c):
    def lin(v):
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in c[:3])
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


ROLES = ['plate', 'foreground', 'secondary', 'outline', 'tile', 'tileStroke', 'plateStroke', 'ringIdle',
         'accent', 'accentText', 'textOnAccent', 'selection', 'selectionText']


@dataclass(frozen=True)
class Theme:
    key: str                 # light | dark | contrast
    material: str            # solid | frosted
    plate: tuple
    foreground: tuple
    secondary: tuple
    outline: tuple
    tile: tuple
    tileStroke: tuple
    plateStroke: tuple
    ringIdle: tuple
    accent: tuple
    accentText: tuple
    textOnAccent: tuple
    selection: tuple
    selectionText: tuple
    seatStroke: tuple
    shadowOpacity: float
    plateStrokeWidth: float
    seatStrokeWidth: float

    @property
    def dark(self): return self.key == 'dark' or (self.key == 'contrast' and luminance(self.plate) < .5)

    @property
    def hc(self): return self.key == 'contrast'

    @property
    def frosted(self): return self.material == 'frosted'

    @property
    def label(self):
        return {'light': 'Light', 'dark': 'Dark', 'contrast': 'High contrast'}[self.key] + (' frosted' if self.frosted else '')


def _system(role, sample):
    """High-contrast role strings start with the system color they map to."""
    name = re.match(r'COLOR_[A-Z]+', role).group(0)
    key = {'COLOR_WINDOW': 'window', 'COLOR_WINDOWTEXT': 'windowText',
           'COLOR_HIGHLIGHT': 'highlight', 'COLOR_HIGHLIGHTTEXT': 'highlightText'}[name]
    return color(sample[key])


def theme(key='light', material='solid'):
    if key == 'contrast':
        hc = PAL['highContrast']
        sample = hc['previewSample']
        values = {role: _system(hc[role], sample) for role in ROLES}
        stroke = re.search(r'(COLOR_[A-Z]+) stroke', hc['selection'])
        values['seatStroke'] = _system(stroke.group(1), sample) if stroke else values['textOnAccent']
        return Theme(key, 'solid', shadowOpacity=hc['shadowOpacity'],
                     plateStrokeWidth=G['plate']['strokeHighContrast'],
                     seatStrokeWidth=G['seat']['strokeHighContrast'], **values)
    p = PAL[key]
    frosted = material == 'frosted'
    plate = color(p['plate'])
    accent = color(p['accent']['fallback'])
    tile = color(p['tile'])
    if frosted:
        tile = tile[:3] + (p['frostedTileAlpha'],)
    return Theme(key, material, plate=plate, foreground=color(p['foreground']), secondary=color(p['secondary']),
                 outline=color(p['outline']), tile=tile, tileStroke=color(p['tileStroke']),
                 plateStroke=color(p['plateStroke']), ringIdle=color(p['ringIdle']), accent=accent,
                 accentText=color(p['accentText']['fallback']), textOnAccent=color(p['textOnAccent']),
                 selection=mix(plate, accent, p['seatTint']),
                 selectionText=color(p[p['selectionText']]) if p['selectionText'] in p else color(p['foreground']),
                 seatStroke=accent, shadowOpacity=p['shadowOpacity'],
                 plateStrokeWidth=PLATE_STROKE, seatStrokeWidth=SEAT_STROKE)


def acrylic_recipe(key):
    f = MATERIAL['frosted'][key]
    return {'tint': color(f['tint']), 'tintOpacity': f['tintOpacity'], 'luminosityOpacity': f['luminosityOpacity'],
            'fallback': color(f['fallback']), 'noiseSize': MATERIAL['frosted']['noise']['size'],
            'noiseOpacity': MATERIAL['frosted']['noise']['opacity']}


# Motion
CURVES = {name: tuple(v) for name, v in MOTION['curves'].items()}
EVENTS = MOTION['events']


def bezier(curve, x):
    """y for x on a CSS-style cubic bezier (P0 = 0,0; P3 = 1,1)."""
    x1, y1, x2, y2 = CURVES[curve] if isinstance(curve, str) else curve
    if x <= 0: return 0.0
    if x >= 1: return 1.0
    lo, hi = 0.0, 1.0
    for _ in range(60):
        t = (lo + hi) / 2
        bx = 3 * (1 - t) ** 2 * t * x1 + 3 * (1 - t) * t * t * x2 + t ** 3
        lo, hi = (t, hi) if bx < x else (lo, t)
    t = (lo + hi) / 2
    return 3 * (1 - t) ** 2 * t * y1 + 3 * (1 - t) * t * t * y2 + t ** 3


# Typography
def cjk_ranges():
    """Han, CJK punctuation and kana: every range the product's CJK fallback maps away from Segoe UI."""
    return [tuple(int(v, 16) for v in r.split('-')) for r in TYPE['cjk']['ranges'] + TYPE['japanese']['ranges']]


# Iconography
def glyph_code(name):
    return int(ICON['commandGlyphs'][name], 16)


def brand_tokens():
    return {b['key']: b for b in ICON.get('brands', [])}
