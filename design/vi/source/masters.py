"""Editable SVG masters: the active Soft Orbit screens at the 384 DIP canvas."""
from __future__ import annotations

import art
import reel as R
from scene import Scene
from tokens import CANVAS, VERSION

# The hub has no Preferred agent entry, so the Preferred page's masters are retired; About's replace them.
RETIRED = ('halo-*.svg', 'petals-*.svg', 'orbit-*.svg', 'radial-settings-*.svg', 'osd-*.svg', 'radial-preferred-*.svg')


def screens():
    """(name, state) for the active set; selection shows the seat and the single center name."""
    return [
        ('main', R.State(R.MAIN, preferred=0, selected=1)),
        ('hub', R.State(R.HUB, language=True, submenu=True)),
        ('language', R.State(R.HUB, language=True, submenu=True, selected=0)),
        ('about', R.State(R.ABOUT, submenu=True, selected=0)),
        ('manage', R.State(R.MANAGE, preferred=0, submenu=True, selected=len(R.MANAGE) - 1)),
        ('presets', R.State(R.PRESETS, submenu=True, selected=3)),
    ]


def _write(path, th, st, title, desc, backdrop=None):
    s = Scene(CANVAS, CANVAS)
    R.draw_reel(s, th, st, backdrop)
    path.write_text(s.svg(title, desc), encoding='utf-8', newline='\n')


def write(folder):
    folder.mkdir(parents=True, exist_ok=True)
    for pattern in RETIRED:
        for stale in folder.glob(pattern):
            stale.unlink()
    desc = (f'Copilot Reel VI v{VERSION} vector master at the {CANVAS:g} DIP canvas. Values from design/vi/tokens.json; '
            'vendor marks embedded from assets/brands PNGs exactly as the runtime crops them (provenance in '
            'assets/brands/provenance.json). Accent shown with the token fallback values.')
    count = 0
    for key in ('light', 'dark'):
        th = R.theme(key)
        for name, st in screens():
            _write(folder / f'radial-{name}-{key}.svg', th, st, f'Soft Orbit / {name} / {key}', desc)
            count += 1
    hc = R.theme('contrast')
    for name, st in screens()[:2]:
        _write(folder / f'radial-{name}-contrast.svg', hc, st, f'Soft Orbit / {name} / high contrast',
               desc + ' High contrast uses live system colors; this master shows the illustrative High Contrast Black sample.')
        count += 1
    for key in ('light', 'dark'):
        th = R.theme(key, 'frosted')
        wall = art.wallpaper(CANVAS, CANVAS, th.dark, 1)
        backdrop = R.frosted_backdrop(th, wall, (0, 0, CANVAS, CANVAS), R.C, R.C, 1.0)
        _write(folder / f'radial-main-{key}-frosted.svg', th, screens()[0][1], f'Soft Orbit / main / {key} frosted',
               desc + ' Frosted: tonal illustration of the acrylic recipe over a sample wallpaper; the live host backdrop is not captured.',
               backdrop)
        count += 1
    return count
