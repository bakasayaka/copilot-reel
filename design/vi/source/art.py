"""Artwork sources and raster helpers for the VI scenes.

Vendor marks come from assets/brands (the PNGs the runtime decodes; the SVG originals stay
beside them for provenance). Windows Settings and Notepad use their real Shell icons
(IShellItemImageFactory, as the runtime does), cached in the temp folder. Brand treatment is
read from tokens iconography.brands; anything else is classified with the runtime's alpha
analysis (src/artwork.cpp Analyze) and reported as 'derived'.
"""
from __future__ import annotations

import ctypes
import math
import tempfile
import uuid
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from tokens import ROOT, KEYLINE, TILE, brand_tokens

BRANDS = ROOT / 'assets' / 'brands'
CACHE = Path(tempfile.gettempdir()) / 'copilot-reel-vi' / 'shell'
SHELL = {'settings': 'shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel',
         'notepad': 'shell:AppsFolder\\Microsoft.WindowsNotepad_8wekyb3d8bbwe!App'}
ART_PX = 8          # raster density for artwork in the PDF: pixels per DIP (covers 2x previews at 4x zoom)


def asset_key(key, dark):
    """assets/brands file stem for a brand key in a theme (ChatGPT and GitHub ship light and dark marks)."""
    return key + ('-dark' if dark else '-light') if key in ('chatgpt', 'github') else key


def _shell_icon(name, px=256):
    class GUID(ctypes.Structure):
        _fields_ = [('a', ctypes.c_uint32), ('b', ctypes.c_uint16), ('c', ctypes.c_uint16), ('d', ctypes.c_ubyte * 8)]

    class SIZE(ctypes.Structure):
        _fields_ = [('cx', ctypes.c_long), ('cy', ctypes.c_long)]

    class BIH(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('w', ctypes.c_long), ('h', ctypes.c_long), ('planes', ctypes.c_ushort),
                    ('bits', ctypes.c_ushort), ('comp', ctypes.c_uint32), ('image', ctypes.c_uint32),
                    ('xppm', ctypes.c_long), ('yppm', ctypes.c_long), ('used', ctypes.c_uint32), ('important', ctypes.c_uint32)]
    iid = GUID()
    ctypes.memmove(ctypes.byref(iid), uuid.UUID('bcc18b79-ba16-442f-80c4-8a59c30c463b').bytes_le, 16)
    ole32, shell32, gdi32, user32 = ctypes.OleDLL('ole32'), ctypes.OleDLL('shell32'), ctypes.WinDLL('gdi32'), ctypes.WinDLL('user32')
    try:
        ole32.CoInitializeEx(None, 2)
    except OSError:
        pass
    factory = ctypes.c_void_p()
    shell32.SHCreateItemFromParsingName(ctypes.c_wchar_p(name), None, ctypes.byref(iid), ctypes.byref(factory))
    vtbl = ctypes.cast(factory, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    get_image = ctypes.WINFUNCTYPE(ctypes.HRESULT, ctypes.c_void_p, SIZE, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))(vtbl[3])
    release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtbl[2])
    bitmap = ctypes.c_void_p()
    try:
        get_image(factory, SIZE(px, px), 0x1 | 0x4, ctypes.byref(bitmap))   # SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY
    finally:
        release(factory)
    header = BIH(ctypes.sizeof(BIH), px, -px, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = (ctypes.c_ubyte * (px * px * 4))()
    dc = user32.GetDC(None)
    gdi32.GetDIBits(dc, bitmap, 0, px, pixels, ctypes.byref(header), 0)
    user32.ReleaseDC(None, dc)
    gdi32.DeleteObject(bitmap)
    bgra = np.frombuffer(bytes(pixels), np.uint8).reshape(px, px, 4).astype(np.float32)
    rgb, a = bgra[..., 2::-1], bgra[..., 3:4]
    if not a.any():
        a = np.full_like(a, 255)                      # legacy icon without an alpha channel
    elif not (rgb > a).any():                         # premultiplied (the usual Shell result): unpremultiply
        rgb = np.where(a > 0, rgb * 255 / np.maximum(a, 1), 0)
    out = np.concatenate([rgb, a], axis=-1).clip(0, 255).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


@lru_cache(None)
def source_path(key, dark=False):
    """PNG path for a brand or Shell icon key; None when unavailable."""
    if key in SHELL:
        path = CACHE / f'{key}.png'
        if not path.exists():
            try:
                CACHE.mkdir(parents=True, exist_ok=True)
                _shell_icon(SHELL[key]).save(path)
            except OSError:
                return None
        return path
    png = BRANDS / f'{asset_key(key, dark)}.png'
    return png if png.exists() else None


@lru_cache(None)
def source_image(key, dark=False):
    path = source_path(key, dark)
    return Image.open(path).convert('RGBA') if path else None


@dataclass(frozen=True)
class Treatment:
    key: str
    plated: bool
    optical: float
    content: tuple       # l, t, r, b normalized to the source canvas
    origin: str          # 'tokens' or 'derived'


def analyze(img):
    """Port of src/artwork.cpp Analyze: plated when the alpha >= 230 box is >= 90% covered
    (alpha >= 128) and matches a rounded rectangle; content = plate box, else ink box (alpha > 5)."""
    a = np.asarray(img)[..., 3].astype(np.int32)
    h, w = a.shape

    def bounds(mask):
        ys, xs = np.nonzero(mask)
        if not len(xs):
            return None
        return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
    ink = bounds(a > 5)
    if ink is None:
        return False, (0, 0, 1, 1)
    norm = lambda r: (r[0] / w, r[1] / h, r[2] / w, r[3] / h)
    plate = bounds(a >= 230)
    if plate is None:
        return False, norm(ink)
    l, t, r, b = plate
    solid = a[t:b, l:r] >= 128
    pw, ph = r - l, b - t
    coverage = solid.mean()
    limit = min(pw, ph) // 2
    steps = 0.0
    for x0, y0, sx, sy in ((0, 0, 1, 1), (pw - 1, 0, -1, 1), (0, ph - 1, 1, -1), (pw - 1, ph - 1, -1, -1)):
        k = 0
        while k < limit and not solid[y0 + sy * k, x0 + sx * k]:
            k += 1
        steps += k + .5
    radius = min(steps / 4 / (1 - 1 / math.sqrt(2)), float(limit))
    yy, xx = np.mgrid[0:ph, 0:pw].astype(np.float64) + .5
    cx, cy = np.clip(xx, radius, pw - radius), np.clip(yy, radius, ph - radius)
    ideal = (xx - cx) ** 2 + (yy - cy) ** 2 <= radius * radius
    both, either = (ideal & solid).sum(), (ideal | solid).sum()
    if coverage >= .90 and either and both / either >= .97:
        return True, norm(plate)
    return False, norm(ink)


@lru_cache(None)
def treatment(key, dark=False):
    """Per-brand treatment: tokens iconography.brands first, else the runtime alpha analysis."""
    img = source_image(key, dark)
    stem = key if key in SHELL else asset_key(key, dark)
    entry = brand_tokens().get(stem)
    if entry:
        return Treatment(stem, entry['class'] == 'plated', float(entry['opticalScale']), tuple(entry['content']), 'tokens')
    plated, content = analyze(img) if img is not None else (False, (0, 0, 1, 1))
    return Treatment(stem, plated, 1.0, content, 'derived')


def _crop_box(img, tr, aspect=0.0):
    """src/artwork.cpp CropRect: the content rect; plated art also trimmed to the tile aspect."""
    w, h = img.size
    l, t, r, b = tr.content[0] * w, tr.content[1] * h, tr.content[2] * w, tr.content[3] * h
    if tr.plated and aspect > 0:
        if (r - l) / (b - t) > aspect:
            trim = ((r - l) - (b - t) * aspect) / 2
            l, r = l + trim, r - trim
        else:
            trim = ((b - t) - (r - l) / aspect) / 2
            t, b = t + trim, b - trim
    x, y = math.floor(l + 1e-3), math.floor(t + 1e-3)
    return x, y, max(x + 1, math.ceil(r - 1e-3)), max(y + 1, math.ceil(b - 1e-3))


@lru_cache(None)
def placed(key, dark=False, tile=TILE):
    """(image, x, y, w, h, plated) relative to the tile center, in DIP.
    Plated art covers the tile; glyph marks fit their long ink side to keyline x opticalScale."""
    img = source_image(key, dark)
    if img is None:
        return None
    tr = treatment(key, dark)
    if tr.plated:
        box = _crop_box(img, tr, 1.0)
        w = h = tile
    else:
        box = _crop_box(img, tr)
        cw, ch = box[2] - box[0], box[3] - box[1]
        long_side = KEYLINE * tr.optical * tile / TILE
        ratio = min(cw, ch) / max(cw, ch)
        w, h = (long_side, long_side * ratio) if cw >= ch else (long_side * ratio, long_side)
    crop = img.crop(box)
    px = (max(1, round(w * ART_PX)), max(1, round(h * ART_PX)))
    if crop.size[0] > px[0] or crop.size[1] > px[1]:
        crop = _premultiplied_resize(crop, px)
    return crop, -w / 2, -h / 2, w, h, tr.plated


def _premultiplied_resize(img, size):
    a = np.asarray(img).astype(np.float32) / 255
    pm = np.concatenate([a[..., :3] * a[..., 3:4], a[..., 3:4]], axis=-1)
    chans = [np.asarray(Image.fromarray(pm[..., i]).resize(size, Image.LANCZOS)) for i in range(4)]
    out = np.stack(chans, axis=-1).clip(0, 1)
    rgb = np.where(out[..., 3:4] > 1e-4, out[..., :3] / np.maximum(out[..., 3:4], 1e-4), 0)
    return Image.fromarray((np.concatenate([rgb, out[..., 3:4]], axis=-1).clip(0, 1) * 255 + .5).astype(np.uint8), 'RGBA')


# Elevation ----------------------------------------------------------------------------------

SHADOW_PX = 2


@lru_cache(None)
def shadow(radius, blur, dy, opacity, knockout):
    """One directional shadow of a disk (std dev = blur / 2, offset dy), with the disk of radius
    `knockout` cleared. Returns (image, left, top, size) in DIP relative to the disk center."""
    sigma = blur / 2
    pad = math.ceil(sigma * 3 + dy)
    size = round((radius + pad) * 2 * SHADOW_PX)
    c = size / 2
    img = Image.new('L', (size, size), 0)
    d = ImageDraw.Draw(img)
    r, oy = radius * SHADOW_PX, dy * SHADOW_PX
    d.ellipse([c - r, c - r + oy, c + r, c + r + oy], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(sigma * SHADOW_PX))
    alpha = np.asarray(img).astype(np.float32) * opacity
    yy, xx = np.mgrid[0:size, 0:size]
    alpha[(xx + .5 - c) ** 2 + (yy + .5 - c) ** 2 <= (knockout * SHADOW_PX) ** 2] = 0
    out = np.zeros((size, size, 4), np.uint8)
    out[..., 3] = alpha.clip(0, 255).astype(np.uint8)
    dip = size / SHADOW_PX
    return Image.fromarray(out, 'RGBA'), -dip / 2, -dip / 2, dip


# Sample wallpaper and the frosted (acrylic) illustration -------------------------------------

WALL_PX = 2


@lru_cache(None)
def wallpaper(w, h, dark, seed=0, window=True):
    """Illustrative Windows desktop: a soft bloom wallpaper and one app window. w, h in layout units."""
    W, H = round(w * WALL_PX), round(h * WALL_PX)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    if dark:
        base = np.full((H, W, 3), (.035, .05, .10), np.float32)
        blobs = [(.18, .80, .55, (.10, .26, .66)), (.82, .22, .45, (.36, .16, .58)), (.62, .98, .40, (.02, .44, .52))]
    else:
        base = np.full((H, W, 3), (.86, .90, .96), np.float32)
        blobs = [(.15, .88, .55, (.50, .68, .96)), (.86, .18, .45, (.80, .70, .97)), (.66, 1.0, .40, (.98, .78, .84))]
    for bx, by, br, col in blobs:
        bx = (bx + .13 * seed) % 1.0
        dist = ((xx / W - bx) ** 2 + ((yy / H - by) * H / W) ** 2) / br ** 2
        weight = np.exp(-dist * 2.2)[..., None]
        base = base * (1 - weight) + np.array(col, np.float32) * weight
    img = Image.fromarray((base.clip(0, 1) * 255).astype(np.uint8), 'RGB')
    if not window:
        return img
    d = ImageDraw.Draw(img)
    S = WALL_PX
    win, bar = ('#202020', '#2B2B2B') if dark else ('#FFFFFF', '#F3F3F3')
    edge, ink = ('#3A3A3A', '#4A4A4A') if dark else ('#E1E1E1', '#D6D6D6')
    accent = '#4CC2FF' if dark else '#0067C0'
    x0, y0, x1, y1 = .07 * W, .12 * H, .58 * W, .86 * H
    d.rounded_rectangle([x0, y0, x1, y1], radius=8 * S, fill=win, outline=edge, width=S)
    d.rounded_rectangle([x0, y0, x1, y0 + 32 * S], radius=8 * S, fill=bar)
    d.rectangle([x0, y0 + 24 * S, x1, y0 + 32 * S], fill=bar)
    for i in range(3):
        d.rectangle([x1 - (46 * i + 30) * S, y0 + 15 * S, x1 - (46 * i + 20) * S, y0 + 16 * S], fill=ink)
    for i, frac in enumerate([.62, .88, .80, .48, .74, .58, .84, .40, .66]):
        ty = y0 + (58 + i * 26) * S
        if ty + 8 * S > y1 - 60 * S:
            break
        d.rounded_rectangle([x0 + 24 * S, ty, x0 + 24 * S + (x1 - x0 - 48 * S) * frac, ty + 7 * S], radius=3.5 * S, fill=ink)
    d.rounded_rectangle([x0 + 24 * S, y1 - 44 * S, x0 + 120 * S, y1 - 20 * S], radius=4 * S, fill=accent)
    return img


def _lum(c):
    return c[..., 0:1] * .3 + c[..., 1:2] * .59 + c[..., 2:3] * .11


def _clip_color(c):
    l = _lum(c)
    n, x = c.min(axis=-1, keepdims=True), c.max(axis=-1, keepdims=True)
    c = np.where(n < 0, l + (c - l) * l / np.maximum(l - n, 1e-6), c)
    c = np.where(x > 1, l + (c - l) * (1 - l) / np.maximum(x - l, 1e-6), c)
    return c


def _set_lum(c, l):
    return _clip_color(c + (l - _lum(c)))


SHELL_BLUR = 30     # DIP; illustrative only: the shell owns the real host-backdrop blur


def acrylic(backdrop, recipe, px_per_unit=WALL_PX, scale=1.0):
    """tokens systemAppearance.frosted over a backdrop: Luminosity blend with the luminosity
    color, Color blend with the tint, then the noise tile. Returns an RGB image."""
    blur = SHELL_BLUR * scale * px_per_unit / 2
    bg = np.asarray(backdrop.convert('RGB').filter(ImageFilter.GaussianBlur(blur))).astype(np.float32) / 255
    tint = np.broadcast_to(np.array(recipe['tint'][:3], np.float32), bg.shape)
    a = recipe['luminosityOpacity']
    step = bg * (1 - a) + _set_lum(bg, _lum(tint)) * a
    a = recipe['tintOpacity']
    step = step * (1 - a) + _set_lum(tint.copy(), _lum(step)) * a
    rng = np.random.default_rng(7)
    n = recipe['noiseSize']
    noise = rng.random((n, n, 1), np.float32)
    reps = (math.ceil(step.shape[0] / n), math.ceil(step.shape[1] / n), 1)
    noise = np.tile(noise, reps)[:step.shape[0], :step.shape[1]]
    a = recipe['noiseOpacity']
    step = step * (1 - a) + noise * a
    return Image.fromarray((step.clip(0, 1) * 255 + .5).astype(np.uint8), 'RGB')
