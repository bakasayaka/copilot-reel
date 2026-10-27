"""Windows font faces for the VI: product faces from tokens.typography, document faces, icon outlines.

Segoe UI Variable ships as one variable font; reportlab needs static faces, so each named
optical size (Small, Text, Display) is instanced once with fontTools into a temp cache.
Segoe Fluent Icons glyphs are read as outlines, so the PDF and SVG carry them as vectors.
"""
from __future__ import annotations

import os
import tempfile
from functools import lru_cache
from pathlib import Path

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTCollection, TTFont as FontFile
from fontTools.varLib import instancer
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

from tokens import ICON, TYPE, cjk_ranges

FONTS = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts'
CACHE = Path(tempfile.gettempdir()) / 'copilot-reel-vi' / 'fonts'
REPO = Path(__file__).resolve().parents[3]
# Windows family -> file. Static families map weights; the variable family maps its file. The
# bundled Japanese face is read from the repository, as the application reads it beside itself.
FILES = {'Segoe UI Variable': 'SegUIVar.ttf', 'Segoe UI': {400: 'segoeui.ttf', 600: 'seguisb.ttf', 700: 'segoeuib.ttf'},
         'Segoe Fluent Icons': 'SegoeIcons.ttf', 'Segoe MDL2 Assets': 'segmdl2.ttf',
         'Microsoft YaHei UI': {400: 'msyh.ttc', 700: 'msyhbd.ttc'}, 'Consolas': {400: 'consola.ttf', 700: 'consolab.ttf'},
         TYPE['japanese']['family']: {400: REPO / TYPE['japanese']['files'][0], 600: REPO / TYPE['japanese']['files'][1]},
         TYPE['chinese']['family']: {400: REPO / TYPE['chinese']['files'][0], 600: REPO / TYPE['chinese']['files'][1]}}

_CSS = {}   # registered key -> (css family list, weight)


def _variable(family, weight):
    """'Segoe UI Variable Text' -> a static instance at that optical size and weight."""
    base, _, optical = family.rpartition(' ')
    source = FONTS / FILES[base]
    if not source.exists():
        return None
    stem = f'{base.replace(" ", "")}{optical}-{weight}'
    out = CACHE / f'{stem}.ttf'
    if not out.exists():
        font = FontFile(source)
        sizes = {}
        for inst in font['fvar'].instances:
            last = font['name'].getDebugName(inst.subfamilyNameID).split()[-1]
            sizes[last if last in ('Small', 'Text', 'Display') else 'Text'] = inst.coordinates['opsz']
        CACHE.mkdir(parents=True, exist_ok=True)
        for table in ('GPOS', 'GDEF'):   # reportlab ignores GPOS; its variation data trips the instancer
            if table in font:
                del font[table]
        instancer.instantiateVariableFont(font, {'wght': weight, 'opsz': sizes[optical]}, inplace=True)
        # Every instance needs its own face name: reportlab aliases dynamic fonts that share one.
        names = font['name']
        names.names = [r for r in names.names if r.nameID not in (1, 2, 4, 6, 16, 17)]
        for record_id, value in ((1, f'{base} {optical} {weight}'), (2, 'Regular'), (4, f'{base} {optical} {weight}'), (6, stem)):
            names.setName(value, record_id, 3, 1, 0x409)
        font.save(out)
    return out, 0


def _static(family, weight):
    entry = FILES.get(family)
    if not isinstance(entry, dict):
        return None
    name = entry[min(entry, key=lambda w: (abs(w - weight), -w))]
    path = FONTS / name
    if not path.exists():
        return None
    if path.suffix == '.ttc':
        for index, face in enumerate(TTCollection(path).fonts):
            if face['name'].getDebugName(1) == family:
                return path, index
        return None
    return path, 0


@lru_cache(None)
def face(family, weight=400):
    """Registered reportlab font name for a Windows family at a weight; falls back per tokens."""
    candidates = [family]
    if family == TYPE['preferred'] or family.startswith('Segoe UI Variable'):
        candidates.append(TYPE['fallback'])
    for name in candidates:
        found = _variable(name, weight) if name.startswith('Segoe UI Variable') else _static(name, weight)
        if found:
            reg = f'{name}-{weight}'.replace(' ', '')
            pdfmetrics.registerFont(TTFont(reg, str(found[0]), subfontIndex=found[1]))
            _CSS[reg] = (f"'{name}', 'Segoe UI', 'Microsoft YaHei UI', sans-serif", weight)
            return reg
    raise FileNotFoundError(f'no font file for {family} {weight}')


# Named faces. Product faces come from tokens; document faces use the same Windows family.
FACES = {
    'label': (TYPE['preferred'], TYPE['applicationLabel']['weight']),
    'monogram': (TYPE['preferred'], TYPE['iconMonogram']['weight']),
    'cjk': (TYPE['chinese']['family'], TYPE['weight']),     # Reel Sans SC, chosen 2026-10-27 (P5)
    'cjk-regular': (TYPE['chinese']['family'], 400),
    'yahei-bold': (TYPE['cjk']['family'], 700),             # the system face, for page 16's comparison
    'yahei': (TYPE['cjk']['family'], 400),
    'ja': (TYPE['japanese']['family'], TYPE['applicationLabel']['weight']),
    'ja-regular': (TYPE['japanese']['family'], 400),
    'display': ('Segoe UI Variable Display', 600),
    'text': ('Segoe UI Variable Text', 400),
    'text-sb': ('Segoe UI Variable Text', 600),
    'small': ('Segoe UI Variable Small', 400),
    'small-sb': ('Segoe UI Variable Small', 600),
    'mono': ('Consolas', 400),
}


def key(name):
    """Registered reportlab key for a FACES name (or an already registered key)."""
    if name in FACES:
        return face(*FACES[name])
    return name


def css(reg):
    return _CSS.get(reg, ("'Segoe UI', sans-serif", 400))


@lru_cache(None)
def metrics(reg):
    """(capHeight, ascent, descent) per em for a registered face, from OS/2 and hhea."""
    f = pdfmetrics.getFont(reg).face
    return f.capHeight / 1000, f.ascent / 1000, -f.descent / 1000


def cap_ratio():
    """The renderer centers labels on the Latin face's cap height (DWRITE_FONT_METRICS)."""
    return metrics(key('label'))[0]


def is_cjk(ch):
    code = ord(ch)
    return any(a <= code <= b for a, b in cjk_ranges())


def runs(text, latin, cjk):
    """Split text into (face, chunk) runs: CJK ranges use the CJK face, the rest the Latin face."""
    out = []
    for ch in text:
        k = cjk if is_cjk(ch) else latin
        if out and out[-1][0] == k:
            out[-1][1] += ch
        else:
            out.append([k, ch])
    return [tuple(r) for r in out]


def width(text, latin, cjk, size, tracking=0.0):
    latin, cjk = key(latin), key(cjk)
    return sum(pdfmetrics.stringWidth(chunk, k, size) for k, chunk in runs(text, latin, cjk)) \
        + tracking * max(0, len(text) - 1)


def trim(text, max_width, latin='label', cjk='cjk', size=14.0):
    """Character-ellipsis trimming (DWRITE_TRIMMING_GRANULARITY_CHARACTER with an ellipsis sign)."""
    if width(text, latin, cjk, size) <= max_width:
        return text, False
    for n in range(len(text) - 1, 0, -1):
        candidate = text[:n].rstrip() + '\u2026'
        if width(candidate, latin, cjk, size) <= max_width:
            return candidate, True
    return '\u2026', True


def wrap(text, max_width, face='text', size=14.0, cjk='cjk-regular'):
    lines = []
    for paragraph in text.split('\n'):
        line = ''
        for word in paragraph.split(' '):
            candidate = (line + ' ' + word).strip()
            if line and width(candidate, face, cjk, size) > max_width:
                lines.append(line)
                line = word
            else:
                line = candidate
        lines.append(line)
    return lines


# Segoe Fluent Icons ------------------------------------------------------------------------

class _Outline(BasePen):
    def __init__(self, glyphs):
        super().__init__(glyphs)
        self.cmds = []

    def _moveTo(self, p): self.cmds.append(('M', *p))
    def _lineTo(self, p): self.cmds.append(('L', *p))
    def _curveToOne(self, a, b, c): self.cmds.append(('C', *a, *b, *c))
    def _closePath(self): self.cmds.append(('Z',))


@lru_cache(None)
def _icon_font():
    for family in (ICON['commandFont'], ICON['commandFontFallback']):
        path = FONTS / FILES[family]
        if path.exists():
            return FontFile(path), family
    return None, None


def icon_family():
    return _icon_font()[1]


@lru_cache(None)
def icon_outline(code):
    """Glyph outline in em units (y down) centered on its ink bounds, plus the ink size in em."""
    font, _ = _icon_font()
    if font is None:
        return None
    name = font.getBestCmap().get(code)
    if name is None:
        return None
    glyphs = font.getGlyphSet()
    pen = _Outline(glyphs)
    glyphs[name].draw(pen)
    upm = font['head'].unitsPerEm
    g = font['glyf'][name]
    cx, cy = (g.xMin + g.xMax) / 2, (g.yMin + g.yMax) / 2
    out = []
    for cmd, *v in pen.cmds:
        pts = [((v[i] - cx) / upm, -(v[i + 1] - cy) / upm) for i in range(0, len(v), 2)]
        out.append((cmd, *[c for p in pts for c in p]))
    ink = ((g.xMax - g.xMin) / upm, (g.yMax - g.yMin) / upm)
    return tuple(out), ink
