# VI source package v0.9

Soft Orbit for Copilot Reel 0.3.13: a Windows-neutral plate with one flyout shadow, a concentric selection seat that replaces the tile fill and travels along the orbit, official artwork in one tile system (plated marks fill the tile; open marks share one optical size; system apps use their Shell icons), Segoe Fluent Icons commands, and drill-in/out page transitions in which the tapped tile becomes the back chevron. The center stays quiet: one name on hover, never a caption or helper copy.

The sources here are current for 0.3.13: they draw the seven-item hub and the About page, and the review PDF, its previews, the SVG masters and the review zip were rebuilt from them on Windows for 0.3.13.

- `tokens.json`: the single, hand-edited source of design values (geometry, palettes and accent roles, material, motion, typography, per-brand artwork treatment). `tools/gen-vi-tokens.ps1` generates `src/vi_tokens.h` from it for the application; this package reads it directly.
- `vectors/`: active SVG masters on the 400 DIP canvas: `radial-{main,hub,language,about,manage,presets}-{light,dark}.svg`, `radial-{main,hub}-contrast.svg`, `radial-main-{light,dark}-frosted.svg`. The About masters (the reel's mark selected and named `0.3.13`) replace the Preferred page's `radial-preferred-*`, which are retired.
- `stylesheet.typ`: Typst token sheet that reads `tokens.json`; it repeats no values.
- `source/build_vi.py`: builds the PDF, the SVG masters, the 2x previews and the review zip.
- `source/pages.py`: the 16 specification pages (page 4 sets the label in all three scripts; page 16 compares them with Apple's pairing and the Windows faces); `source/reel.py`: vector mirror of `src/renderer.cpp`; `source/scene.py`, `art.py`, `fonts.py`, `tokens.py`, `layout.py`, `masters.py`: drawing, artwork, font, token, layout and master helpers.
- [Review PDF](../../output/pdf/copilot-reel-vi-v0.9.pdf) and [normative summary](../../docs/visual-identity.md).

## Rebuild

```powershell
uv run --python 3.14 --with reportlab --with pillow --with fonttools --with numpy python design/vi/source/build_vi.py
```

`--pages 2,7` rebuilds selected pages for review; it writes a PDF of those pages only, so finish with a full build. The build needs the Windows fonts Segoe UI Variable, Segoe Fluent Icons, Microsoft YaHei UI and Consolas, and the repository's `assets/fonts/` Reel Sans JP, and renders previews with Poppler `pdftoppm` or PyMuPDF. It never writes `tokens.json`.

Scenes are vector and use the token accent fallbacks; frosted scenes are labelled tonal illustrations. Official vendor PNGs are embedded unmodified, with plated marks clipped to the tile shape exactly as the application does. Page 15 shows the 0.3.13 reference stills from `output/runtime/v0.3.13/` (`<variant>-<state>.png`, the reference set of `tools/capture-seam.py`), page 12 the motion strips from `output/runtime/v0.3.4/`, the last build that recorded them, whose timings 0.3.13 keeps, and page 16 the Japanese face comparison in `evidence/typography-ja.png`. Neither the PDF nor the PNG previews ever become application UI frames. Older PDF versions are historical reviews, not the current design contract.

Asset ownership and sources live in `assets/brands/provenance.json`. The live frosted backdrop, real High Contrast themes, physical Copilot-key timing and high-refresh performance remain runtime validation items in `docs/validation.md`.
