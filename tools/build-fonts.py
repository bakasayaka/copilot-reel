"""Build the faces the reel bundles: Reel Sans JP from Noto Sans JP and Reel Sans SC from Noto Sans SC.

Noto Sans JP (Source Han Sans, SIL Open Font License 1.1) is the open face closest to Apple's
Hiragino Sans: no uroko at stroke shoulders, wide counters, an even stroke rhythm and slightly
enlarged kana (docs/visual-identity.md, Typography). Noto Sans SC is the same design for Simplified
Chinese; at 600 it matches Segoe UI Variable's and Reel Sans JP's weight, where Microsoft YaHei UI
only has Regular and Bold (P5, chosen 2026-10-27). The reel needs two weights of each, so this
script makes two static instances, 400 and 600, and subsets each to:

- Japanese: every character of Windows-31J (JIS X 0208 levels 1 and 2 with the NEC and IBM
  extensions), the kana blocks, CJK symbols and punctuation, and the half- and fullwidth forms;
- Chinese: every character of GB 2312, CJK symbols and punctuation, and the fullwidth forms;
- both: printable ASCII, and every character of their language's interface strings in src/.

A subset is a Modified Version under the OFL, so it is renamed (the license reserves only
"Source"; the new name also keeps it apart from a system Noto Sans) and keeps the copyright and
license records. Its line metrics are set to Segoe UI's, so a Japanese or Chinese label's line is
as tall as an English one. assets/fonts/OFL.txt travels with them.

The sources are the variable Noto Sans JP 2.004 and Noto Sans SC that Windows 11 installs, or the
same files from https://github.com/google/fonts/tree/main/ofl; their SHA-256 are pinned.

    uv run --python 3.14 --with fonttools python tools/build-fonts.py [--script jp|sc|all]
"""
import argparse
import hashlib
import re
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'assets/fonts'
WEIGHTS = {400: 'Regular', 600: 'SemiBold'}
# Segoe UI's Windows metrics (2210 and 514 of 2048 units) in Noto's 1000-unit em.
ASCENT, DESCENT = 1079, 251


def windows_31j():
    characters = set()
    for lead in list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD)):
        for trail in list(range(0x40, 0x7F)) + list(range(0x80, 0xFD)):
            try:
                characters.update(bytes([lead, trail]).decode('cp932'))
            except UnicodeDecodeError:
                pass
    characters.update(bytes([b]).decode('cp932') for b in range(0xA1, 0xE0))
    return characters


def gb2312():
    characters = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                characters.update(bytes([lead, trail]).decode('gb2312'))
            except UnicodeDecodeError:
                pass
    return characters


# Per face: the source, its pinned digest, the characters it keeps, and which argument of Tr holds
# its language (2 Chinese, 3 Japanese).
SCRIPTS = {
    'jp': {'family': 'Reel Sans JP', 'postscript': 'ReelSansJP', 'upstream': 'Noto Sans JP',
           'source': Path(r'C:\Windows\Fonts\NotoSansJP-VF.ttf'),
           'sha256': '5113756F8A3B5D01B2211025E267C50121E3B36F465B7BBAF3CDAF4C3430BFD0',
           'characters': windows_31j, 'argument': 3,
           'blocks': [(0x3000, 0x303F), (0x3040, 0x309F), (0x30A0, 0x30FF), (0x31F0, 0x31FF), (0xFF00, 0xFFEF)]},
    'sc': {'family': 'Reel Sans SC', 'postscript': 'ReelSansSC', 'upstream': 'Noto Sans SC',
           'source': Path(r'C:\Windows\Fonts\NotoSansSC-VF.ttf'),
           'sha256': '763146584CF0710223441356B4395E279021B0806C196614377A7A0174AE074A',
           'characters': gb2312, 'argument': 2,
           'blocks': [(0x3000, 0x303F), (0xFF00, 0xFFEF)]},
}


def interface_strings(argument):
    """The given argument (2 Chinese, 3 Japanese) of every Tr call in src/."""
    literal = r'L"((?:[^"\\]|\\.)*)"'
    call = re.compile(r'\bTr\(\s*' + literal + r'\s*,\s*' + literal + r'\s*,\s*' + literal + r'\s*\)')
    found = set()
    for path in sorted((ROOT / 'src').glob('*.*')):
        for match in call.finditer(path.read_text(encoding='utf-8')):
            found.update(match.group(argument))
    return found


def rename(font, script, weight, style):
    family, postscript = script['family'], script['postscript']
    full = family if style == 'Regular' else f'{family} {style}'
    name = f'{postscript}-{style}'
    names = font['name']
    version = names.getDebugName(5) or ''
    kept = {0, 7, 13, 14}
    names.names = [record for record in names.names if record.nameID in kept]
    records = {1: full, 2: 'Regular', 3: f'{version.split(";")[0].replace("Version ", "")};REEL;{name}', 4: full,
               5: version.split(';')[0] + '; Reel subset', 6: name,
               10: f'A subset of {script["upstream"]}, renamed for Copilot Reel under the SIL Open Font License 1.1.'}
    if style != 'Regular':
        records.update({16: family, 17: style})
    for name_id, text in records.items():
        names.setName(text, name_id, 3, 1, 0x409)
    os2 = font['OS/2']
    os2.usWeightClass = weight
    os2.fsSelection = (os2.fsSelection & ~0b1100001) | (0b1000000 if style == 'Regular' else 0)
    font['head'].macStyle = 0
    os2.usWinAscent, os2.usWinDescent = ASCENT, DESCENT
    font['hhea'].ascent, font['hhea'].descent, font['hhea'].lineGap = ASCENT, -DESCENT, 0
    for table in ('STAT', 'meta', 'DSIG'):
        if table in font:
            del font[table]


def build(key, source):
    script = SCRIPTS[key]
    source = source or script['source']
    digest = hashlib.sha256(source.read_bytes()).hexdigest().upper()
    if digest != script['sha256']:
        raise SystemExit(f'{source} is not the pinned {script["upstream"]} (SHA-256 {digest}).')
    strings = interface_strings(script['argument'])
    wanted = script['characters']() | set(map(chr, range(0x20, 0x7F))) | strings
    for first, last in script['blocks']:
        wanted.update(map(chr, range(first, last + 1)))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for weight, style in WEIGHTS.items():
        font = instancer.instantiateVariableFont(TTFont(source), {'wght': weight})
        cmap = font.getBestCmap()
        missing = sorted(c for c in strings if ord(c) not in cmap and not c.isspace())
        if missing:
            raise SystemExit(f'{script["upstream"]} lacks interface characters: {"".join(missing)}')
        options = subset.Options()
        options.name_IDs = ['*']
        options.name_languages = ['*']
        options.notdef_outline = True
        options.glyph_names = False
        options.hinting = False
        # Horizontal interface text: fontTools' default features without the vertical forms,
        # which would keep every alternate glyph the subset drops anyway.
        options.layout_features = [f for f in options.layout_features if f not in ('vert', 'vrt2', 'vkrn', 'vpal', 'valt', 'vhal')]
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=[ord(c) for c in wanted if ord(c) in cmap])
        subsetter.subset(font)
        rename(font, script, weight, style)
        path = OUTPUT / f'{script["postscript"]}-{style}.ttf'
        font.save(path)
        print(f'{path.relative_to(ROOT)}: {len(font.getBestCmap())} characters, {path.stat().st_size / 1e6:.2f} MB')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--script', choices=['jp', 'sc', 'all'], default='all')
    parser.add_argument('--source', type=Path, help='the variable source font, when building one script')
    args = parser.parse_args()
    if args.source and args.script == 'all':
        raise SystemExit('--source needs --script jp or --script sc.')
    for key in (['jp', 'sc'] if args.script == 'all' else [args.script]):
        build(key, args.source)


if __name__ == '__main__':
    main()
