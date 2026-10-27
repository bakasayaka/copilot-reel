"""Draw the README's static art into docs/readme: language pills, the download button and step 3.

Every piece comes in a dark and a light variant for GitHub's two page colors, at twice its
displayed size, with the faces the reel itself uses: Segoe UI for Latin, Reel Sans SC for
Chinese and Reel Sans JP for Japanese. Accent fills are the Windows default accents the reel
falls back to (#60CDFF on dark, #005FB8 on light). The demo GIFs and step stills come from
tools/record-demos.py.

    uv run --python 3.14 --with pillow python tools/readme-art.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/readme'
FONTS = Path(r'C:\Windows\Fonts')
LATIN = (str(FONTS / 'seguisb.ttf'), 0)
CJK = {'zh': (str(ROOT / 'assets/fonts/ReelSansSC-SemiBold.ttf'), 0), 'ja': (str(ROOT / 'assets/fonts/ReelSansJP-SemiBold.ttf'), 0)}
ICONS = str(FONTS / 'SegoeIcons.ttf')
SCALE = 2
THEMES = {
    'dark': {'on': ((240, 246, 252), (13, 17, 23)), 'off': ((61, 68, 77), (145, 152, 161)), 'accent': ((96, 205, 255), (0, 0, 0))},
    'light': {'on': ((31, 35, 40), (255, 255, 255)), 'off': ((209, 217, 224), (89, 99, 110)), 'accent': ((0, 95, 184), (255, 255, 255))},
}
LANGUAGES = {'en': 'English', 'zh': '简体中文', 'ja': '日本語'}
DOWNLOAD = {'en': 'Download for Windows', 'zh': '下载 Windows 版', 'ja': 'Windows 版をダウンロード'}


def runs(text, language, size):
    """(text, font) runs: Latin in Segoe UI, CJK in the language's face."""
    cjk = CJK.get(language, CJK['zh'] if language == 'zh' else CJK['ja'])
    out = []
    for c in text:
        face = cjk if ord(c) >= 0x2E80 else LATIN
        if out and out[-1][1] == face:
            out[-1][0] += c
        else:
            out.append([c, face])
    return [(t, ImageFont.truetype(f[0], size, index=f[1])) for t, f in out]


def text_width(parts):
    return sum(font.getlength(t) for t, font in parts)


def draw_runs(draw, x, baseline, parts, fill):
    for t, font in parts:
        draw.text((x, baseline), t, font=font, fill=fill, anchor='ls')
        x += font.getlength(t)


def capsule(width, height, fill=None, outline=None):
    im = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, width - 1, height - 1), radius=height // 2, fill=fill and fill + (255,),
                        outline=outline and outline + (255,), width=2 * SCALE if outline else 0)
    return im, d


def pill(language, active, theme):
    size, height, pad = 14 * SCALE, 32 * SCALE, 16 * SCALE
    parts = runs(LANGUAGES[language], language, size)
    width = round(text_width(parts)) + 2 * pad
    colors = THEMES[theme]['on' if active else 'off']
    im, d = capsule(width, height, fill=colors[0] if active else None, outline=None if active else colors[0])
    draw_runs(d, pad, height / 2 + size * .36, parts, colors[1] + (255,))
    return im


def download(language, theme):
    size, height, pad, icon = 17 * SCALE, 48 * SCALE, 26 * SCALE, 18 * SCALE
    parts = runs(DOWNLOAD[language], language, size)
    glyph = ImageFont.truetype(ICONS, icon)
    gap = 10 * SCALE
    width = round(text_width(parts) + icon + gap) + 2 * pad
    fill, ink = THEMES[theme]['accent']
    im, d = capsule(width, height, fill=fill)
    d.text((pad + icon / 2, height / 2), '\uE896', font=glyph, fill=ink + (255,), anchor='mm')
    draw_runs(d, pad + icon + gap, height / 2 + size * .36, parts, ink + (255,))
    return im


def launch():
    """Step 3: Claude's own mark, opened, on the stills' canvas with a soft shadow."""
    canvas = 616
    mark = Image.open(ROOT / 'assets/brands/claude.png').convert('RGBA').resize((208, 208), Image.LANCZOS)
    im = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    shadow = Image.new('RGBA', (canvas, canvas), (0, 0, 0, 0))
    offset = ((canvas - mark.width) // 2, (canvas - mark.height) // 2)
    shadow.paste((0, 0, 0, 90), (offset[0], offset[1] + 12), mark.getchannel('A'))
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))
    im.alpha_composite(mark, offset)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        for language in LANGUAGES:
            for active in (True, False):
                pill(language, active, theme).save(OUT / f'lang-{language}-{"on" if active else "off"}-{theme}.png', optimize=True)
            download(language, theme).save(OUT / f'download-{language}-{theme}.png', optimize=True)
    launch().save(OUT / 'step-launch.png', optimize=True)
    print(f'{len(list(OUT.glob("lang-*.png")))} pills, {len(list(OUT.glob("download-*.png")))} buttons, step-launch.png')


if __name__ == '__main__':
    main()
