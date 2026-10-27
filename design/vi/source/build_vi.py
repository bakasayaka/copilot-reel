"""Build Copilot Reel VI v1.0: PDF, 2x PNG previews, SVG masters and the review zip.

    uv run --python 3.14 --with reportlab --with pillow --with fonttools --with numpy \
        python design/vi/source/build_vi.py [--pages 1,2] [--no-zip]

Reads design/vi/tokens.json (never writes it). Requires Windows fonts (Segoe UI Variable,
Segoe Fluent Icons, Microsoft YaHei UI, Consolas) and Poppler pdftoppm, or PyMuPDF, for previews.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from reportlab.pdfgen import canvas

import layout
import masters
import pages
from tokens import ROOT, VI, VERSION, APP_VERSION

PDF = ROOT / 'output' / 'pdf' / f'copilot-reel-vi-v{VERSION}.pdf'
PREVIEWS = ROOT / 'output' / 'previews' / f'v{VERSION}'
ZIP = ROOT / 'output' / f'copilot-reel-vi-v{VERSION}.zip'
VECTORS = VI / 'vectors'
SOURCES = Path(__file__).resolve().parent


def build_pdf(selection=None):
    builders = pages.PAGES
    total = len(builders)
    PDF.parent.mkdir(parents=True, exist_ok=True)
    doc = canvas.Canvas(str(PDF), pagesize=(layout.W * layout.PDF_SCALE, layout.H * layout.PDF_SCALE), pageCompression=1)
    doc.setTitle(f'Copilot Reel - Visual identity v{VERSION}')
    doc.setAuthor('Copilot Reel')
    doc.setSubject(f'Soft Orbit visual identity for the native {APP_VERSION} build')
    numbers = []
    for number, build in enumerate(builders, 1):
        if selection and number not in selection:
            continue
        page = build()
        page.finish(total).draw_pdf(doc, layout.PDF_SCALE)
        doc.showPage()
        numbers.append(number)
    doc.save()
    return numbers


def render_previews(numbers):
    """2x previews: the PDF page is 960 x 600 pt, so 144 dpi gives 1920 x 1200 px."""
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    tmp = PREVIEWS / '_render'
    tmp.mkdir(exist_ok=True)
    pdftoppm = shutil.which('pdftoppm')
    if pdftoppm:
        subprocess.run([pdftoppm, '-r', '144', '-png', str(PDF), str(tmp / 'p')], check=True)
        rendered = sorted(tmp.glob('p-*.png'))
    else:
        import fitz   # PyMuPDF
        rendered = []
        with fitz.open(PDF) as doc:
            for i, pg in enumerate(doc):
                out = tmp / f'p-{i + 1:02d}.png'
                pg.get_pixmap(dpi=144).save(out)
                rendered.append(out)
    for path, number in zip(rendered, numbers):
        path.replace(PREVIEWS / f'page-{number:02d}.png')
    shutil.rmtree(tmp)


def build_zip():
    files = [(PDF, f'pdf/{PDF.name}'), (VI / 'tokens.json', 'tokens.json'), (VI / 'stylesheet.typ', 'stylesheet.typ'),
             (ROOT / 'docs' / 'visual-identity.md', 'visual-identity.md')]
    files += [(p, f'vectors/{p.name}') for p in sorted(VECTORS.glob('*.svg'))]
    files += [(p, f'source/{p.name}') for p in sorted(SOURCES.glob('*.py'))]
    files += [(p, f'previews/{p.name}') for p in sorted(PREVIEWS.glob('*.png'))]
    with zipfile.ZipFile(ZIP, 'w', zipfile.ZIP_DEFLATED) as z:
        for path, name in files:
            z.write(path, name)
    return len(files)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--pages', help='comma-separated page numbers for a quick partial build (no zip)')
    ap.add_argument('--no-zip', action='store_true')
    ap.add_argument('--no-svg', action='store_true')
    args = ap.parse_args(argv)
    selection = {int(v) for v in args.pages.split(',')} if args.pages else None
    numbers = build_pdf(selection)
    render_previews(numbers)
    svgs = 0 if (args.no_svg or selection) else masters.write(VECTORS)
    zipped = 0 if (args.no_zip or selection) else build_zip()
    print(json.dumps({'pdf': str(PDF), 'pages': numbers, 'previews': str(PREVIEWS), 'svg': svgs,
                      'zip': str(ZIP) if zipped else None, 'zipFiles': zipped}))


if __name__ == '__main__':
    sys.exit(main())
