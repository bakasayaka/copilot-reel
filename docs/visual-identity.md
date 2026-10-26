# Visual identity v0.9: Soft Orbit

Normative visual specification for the native **Copilot Reel 0.3.4** build. Every value below comes from [`design/vi/tokens.json`](../design/vi/tokens.json) (schema 0.9, hand-edited); if this page and the tokens ever disagree, the tokens win. `src/vi_tokens.h` is generated from the same file.

## Deliverables

| Item | Location |
| --- | --- |
| Illustrated specification, 15 pages | [output/pdf/copilot-reel-vi-v0.9.pdf](../output/pdf/copilot-reel-vi-v0.9.pdf) |
| Page previews at 2x (1920 x 1200) | [output/previews/v0.9/](../output/previews/v0.9) |
| Editable SVG masters (384 DIP canvas) | [design/vi/vectors/](../design/vi/vectors): `radial-{main,hub,language,preferred,manage,presets}-{light,dark}.svg`, `radial-{main,hub}-contrast.svg`, `radial-main-{light,dark}-frosted.svg` |
| Token sheet (Typst, reads tokens.json) | [design/vi/stylesheet.typ](../design/vi/stylesheet.typ) |
| Generator sources | [design/vi/source/](../design/vi/source): `build_vi.py`, `pages.py`, `reel.py` (mirror of `src/renderer.cpp`), `scene.py`, `art.py`, `fonts.py`, `tokens.py`, `layout.py`, `masters.py` |
| Runtime evidence | [output/runtime/v0.3.4/](../output/runtime/v0.3.4) and [validation](validation.md) |
| Review bundle | `output/copilot-reel-vi-v0.9.zip` (PDF, SVG masters, tokens, sources, previews) |
| Artwork provenance | [assets/brands/provenance.json](../assets/brands/provenance.json) |

Rebuild (Windows; needs Segoe UI Variable, Segoe Fluent Icons, Microsoft YaHei UI, Consolas and Poppler `pdftoppm` or PyMuPDF):

```powershell
uv run --python 3.14 --with reportlab --with pillow --with fonttools --with numpy python design/vi/source/build_vi.py
```

The generator never writes `tokens.json`. Scenes use the token accent fallbacks; frosted scenes are tonal illustrations over a sample wallpaper.

## Principles

1. **Windows-neutral surface.** One plate in the system flyout color (`#F9F9F9` light, `#2C2C2C` dark), one soft shadow, a 1 DIP inset rim; frosted acrylic when Windows transparency is on.
2. **One orbit.** Every item sits on the 112 DIP orbit: slot 1 at 12 o'clock, clockwise, 360/n apart. Slots never reflow and icons never rotate.
3. **A quiet center.** Idle Main shows only a faint ring; idle submenus show the ring and a back chevron. A hovered item shows exactly one name; the ring hides while it shows. While an app tile is dragged on Manage apps the center shows only the trash.
4. **Selection is a seat.** A 48 DIP accent-tinted seat replaces the tile fill. Command glyphs and EN/ZH/JA stay foreground.
5. **Marks stay true.** Vendor marks are never recolored, redrawn or distorted.
6. **Brief motion.** 83 to 250 ms, no overshoot, scaled from the plate center; reduced motion and high contrast settle at once.

## Anatomy (DIP)

| Element | Value |
| --- | --- |
| Reel scale | 0.8 (0.3.6): every DIP value in this table draws at 0.8; the effective DPI is the monitor DPI x 0.8, rounded (a 200% display draws the canvas at 616 px) |
| Canvas | 384 x 384, including the 24 DIP shadow margin |
| Plate | radius 168; rim 1, inset 0.5 (high contrast 2) |
| Orbit | radius 112; slot 1 at 12 o'clock, clockwise, step 360/n |
| Center zone | radius 44; idle ring 1 (`ringIdle`) |
| Tile | 40, r 10; hairline 1, inset 0.5; disabled items at 0.36 |
| Seat | 48, r 14, concentric with the tile; stroke 1.5, inset 0.75 (high contrast 2.5) |
| Command glyph | Segoe Fluent Icons, em 24, centered on its ink bounds; vector fallback stroke 1.75 (high contrast 2) |
| Glyph-class artwork | long ink side on the 28 DIP keyline x per-brand `opticalScale` |
| Plated artwork | cropped to its plate, covers 40, clipped to r 10, tile hairline on top |
| Preferred badge | accent disc r 6 at (+17, -17); plate-colored ring r 7.5 (1.5 cutout); path check 1.5 in `textOnAccent` |
| Update dot | the badge without its check, smaller: accent disc r 4 (never below 3 device pixels, never above the check's r 6), plate-colored ring 1.5 wider, same position; on the hub's About tile and About's update tile while an update waits |
| Switch | in the command glyph's em: off is ToggleLeft (F19E); on is ToggleFilled (EC11) with the knob (center +0.25, r 0.125) cut out, so the tile or the seat shows through; `foreground`, never accent |
| Adjust chevrons | ChevronLeft/Right (E76B/E76C) at em 14, centers (+-40, -112), hit 40 x 40, flanking slot 0, the EN/ZH/JA tile on the + hub; `secondary`, `foreground` while slot 0 is selected |
| Back chevron | E76B at em 14, `secondary`, idle submenus only |
| Drag trash | Manage apps, only while a tile is lifted: Remove (E74D) at em 20 in `critical` inside a `critical` ring of 1.5 at the center-zone edge (r 44); armed (pointer inside r 44): a `criticalFill` disc r 30 with the glyph in `textOnCritical`, over the shrunken tile; disabled at 0.36 while one app remains |
| Center name | 14 DIP / 600 (never below 10 device pixels), one line, 160 x 20 box on the plate center (the box grows with a raised size) |
| Shadow | blur 16 (std dev 8), offset y 8; knocked out under the plate |
| Pixel grid | centers, tiles and seats snap to device pixels; hairlines at least 1 px; hit testing keeps exact geometry |

## Color roles

| Role | Light | Dark | High contrast |
| --- | --- | --- | --- |
| plate | `#F9F9F9` | `#2C2C2C` | COLOR_WINDOW |
| foreground | `#1B1B1B` | `#FFFFFF` | COLOR_WINDOWTEXT |
| secondary | `#5D5D5D` | `#CFCFCF` | COLOR_WINDOWTEXT |
| tile | `#FFFFFF` (frosted 70%) | `#FFFFFF` 10% | COLOR_WINDOW + WINDOWTEXT stroke |
| tileStroke / plateStroke | `#000000` 8% / 8% | `#FFFFFF` 8% / 10% | COLOR_WINDOWTEXT (plate 2 DIP) |
| ringIdle | `#000000` 10% | `#FFFFFF` 10% | COLOR_WINDOWTEXT |
| accent | AccentDark1 (fallback `#005FB8`) | AccentLight2 (fallback `#60CDFF`) | COLOR_HIGHLIGHT |
| accentText | AccentDark2 (`#003E92`) | AccentLight3 (`#99EBFF`) | COLOR_HIGHLIGHT |
| textOnAccent | `#FFFFFF` | `#000000` | COLOR_HIGHLIGHTTEXT |
| selection (seat fill) | Mix(plate, accent, 0.10) | Mix(plate, accent, 0.18) | COLOR_HIGHLIGHT, HIGHLIGHTTEXT 2.5 stroke |
| critical (trash glyph, ring) | `#C42B1C` | `#FF99A4` | COLOR_WINDOWTEXT |
| criticalFill / textOnCritical (armed trash) | `#C42B1C` / `#FFFFFF` | `#FF99A4` / `#000000` | COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT |
| selectionText | foreground | foreground | COLOR_HIGHLIGHTTEXT |
| outline (native editor only) | `#CCCCCC` | `#4A4A4A` | COLOR_WINDOWTEXT |
| shadowOpacity | 14% | 26% | none |

- The accent is a role: UISettings ramp steps, never the raw `UIColorType::Accent`. Fallbacks apply only when the ramp is unavailable. A sample ramp (Windows default blue on the capture machine) is on page 3.
- Mix(a, b, t) = a + (b - a) * t per channel, opaque. Translucent roles composite over the plate.
- WCAG 2.2 with the fallback accents (light / dark): label on plate 16.36 / 13.97, glyph on seat 14.09 / 9.43, chevrons on plate 6.25 / 8.96, seat stroke on plate 5.99 / 7.76, badge check 6.31 / 11.67. All pass (4.5:1 text, 3:1 non-text). The critical colors are WinUI `SystemFillColorCritical`, used for the drag trash only; red is never used elsewhere.

## Typography

The reel speaks English, Simplified Chinese and Japanese with one voice: a quiet, open sans in each script, at one weight, on one baseline. Apple's system pairing is the reference: SF Pro, PingFang SC and Hiragino Sans share a large x-height or face, low stroke contrast, flat terminals and generous counters, so mixed-script names sit evenly. On Windows the reel keeps the system's own Latin and Chinese faces and bundles one Japanese face, because the system's Japanese UI face differs most from that reference.

| Script | Face | Why |
| --- | --- | --- |
| Latin | Segoe UI Variable (WWS), 600: the Small master below 13 device pixels, Text from 13; fallback Segoe UI | The Windows 11 system face: hinted, optical sizes, and the face every neighbouring window uses. SF Pro may not ship on Windows. |
| Chinese (Han, CJK punctuation) | Microsoft YaHei UI (zh-CN), 600 resolves to Bold | Founder Lanting Hei: hinted, mainland forms, open counters like PingFang SC, and on every Windows. |
| Japanese (kana, and Han in Japanese text) | Reel Sans JP 400 / 600, then Yu Gothic UI | See below. |

Japanese. Hiragino Sans (SCREEN, Apple's Japanese system face) drops the uroko at stroke shoulders, keeps wide counters (futokoro) and an even stroke rhythm, centres each glyph's weight, and draws kana slightly large so solid setting does not look gappy. Fontworks' Rodin and UD families share that modern gothic build, but neither they nor Hiragino may be redistributed without a commercial licence, and Fontworks' open faces (Klee One, LINE Seed JP and the display faces) are a pencil script or geometric brand faces without a 600. Yu Gothic UI, the Windows face, is hinted but draws condensed kana (0.66 em wide on a 0.82 em advance), so Japanese names look narrow next to Segoe. The closest open face is Noto Sans JP (Source Han Sans, SIL Open Font License 1.1): the same uroko-free modern gothic, a large face (kanji 0.898 em) and full-width kana. Other open candidates fall short: IBM Plex Sans JP has a grotesque flavour and a deep descent, BIZ UDPGothic draws small universal-design kana and has no 600, M PLUS lacks part of JIS level 2, and Zen Kaku Gothic New has no 600.

- Reel Sans JP is Noto Sans JP 2.004 in two static instances, 400 (the quick menu) and 600 (the reel), subset to Windows-31J (JIS X 0208 levels 1 and 2 with the NEC and IBM extensions, 7,617 characters with kana, CJK symbols and the interface strings) and renamed, as the OFL asks of a Modified Version. Its line metrics are Segoe UI's (1.079 / 0.251 em), so a Japanese line is as tall as an English one. `tools/build-fonts.py` builds it into `assets/fonts/` (2.4 MB a weight) next to `OFL.txt`.
- [`design/vi/evidence/typography-ja.png`](../design/vi/evidence/typography-ja.png) compares it with Yu Gothic UI on the hub at 96, 144 and 192 DPI: wider, rounder kana; slightly softer at 96 DPI, because Noto carries no hinting and the reel draws grayscale antialiasing.

Mapping. One custom DirectWrite fallback serves every D2D text format (`src/typography.cpp`); the text's locale picks the mapping, and a character a mapping's font lacks continues to the next (`tests/typography_tests.cpp` checks each rule against `MapCharacters`):

1. ja-JP text: Han, CJK punctuation and kana (U+3040-30FF, 31F0-31FF, FF65-FF9F) to Reel Sans JP, then Yu Gothic UI.
2. Any other locale: kana to Reel Sans JP, then Yu Gothic UI; Han and CJK punctuation (U+2E80-2FDF, 3000-303F, 3400-4DBF, 4E00-9FFF, F900-FAFF, FF00-FFEF) to Microsoft YaHei UI.
3. The system fallback for everything else.

The formats carry the interface locale (en-US, zh-CN, ja-JP), and a name that contains kana is laid out as ja-JP in every interface, so "メモ帳" in the English reel takes Japanese kanji forms, not Chinese ones. A Chinese name keeps mainland forms in the Japanese reel only where it shares no kana; Han alone cannot tell the two languages apart. The native controls (GDI) use Segoe UI with the system's font linking, or Yu Gothic UI in Japanese; they never see the bundled faces.

- Center name: 14 DIP, one line in 160 x 20, character-ellipsis trimming. Baseline = center + capHeight x size / 2, using the cap height of the Latin face actually chosen (0.70 em), also for Chinese and Japanese. At the default reel size that is 11.2 px at 100% scaling.
- Legibility floor (`typography.minimumPx`, approved 2026-10-25): the reel's geometry keeps scaling with `geometry.scale` and the size step, but the center name never draws below 10 device pixels and the monograms below 9; a raised size is at most 1.5 times its DIP size. At 100% scaling the floor holds the name at 10 px at the 70% and 85% steps, where it would be 7.9 and 9.5 px. The proposed 11 px trimmed Visual Studio Code at the 70% step, because the raised text outgrew the 160 DIP line; at 10 px it fits. Text drawn below 13 px uses Segoe UI Variable Small, the master Windows uses for captions.
- The 160 DIP line (132 before 2026-10-25) clears every seat that reaches the name's band, even with the band grown 1.5 times (`tests/typography_tests.cpp`).
- Measured at 14 DIP in the Text master: Visual Studio Code 119.7, Windows Settings 114.1, IntelliJ IDEA Ultimate 134.2 and DeepSeek Harness (DSH) 158.6, all within 160 (the last two were trimmed at 132). The Small master sets a little wider, so the longest can still trim at caption sizes, as can long names at the 70% step, where the floor raises the size in DIP. Every Japanese interface name fits; the widest, インストール 0.3.9, is under 120.
- EN / ZH / JA monogram: 16 DIP / 600 (never below 9 device pixels), centered on the cap height, foreground.
- Japanese wording follows Microsoft's Japanese Windows terms and prefers kanji where a natural kanji word exists (簡易メニュー, 既定, 最新版, 大きさ, 個人用設定); system nouns Windows writes in katakana stay katakana (アプリ, インストール, テーマ, ダーク モード), with Microsoft's half-width space between katakana words.
- Never drawn: page titles, captions, helper copy, A/D hints, "Move here", a second label line.

## Iconography

| Class | Rule |
| --- | --- |
| Command | Segoe Fluent Icons on the neutral tile (fallback Segoe MDL2 Assets, then vector paths) |
| Glyph | open mark on the neutral tile at keyline 28 x `opticalScale` (ChatGPT, Gemini, VS Code, Antigravity, JetBrains IDEs, Junie, Toolbox; Notepad, Windows Settings and custom apps through their Shell icons) |
| Plated | own filled plate (alpha coverage >= 90%, rounded rectangle): Claude, Perplexity, ZCode, DeepSeek Harness, JetBrains Air |
| Generic | no artwork: GenericApp E737 on the neutral tile; never initials. An executable without an icon of its own, or a shortcut that takes its icon from one, is Generic too: Windows would show its stock application icon, which passes for plated art |

Command glyphs: Add E710, Manage F0E2, Preferred E930, Sun E706, Moon E708, Edit E70F, Remove E74D, Previous E76B, Next E76C, ChevronLeftSmall E76B, ChevronRightSmall E76C, BackChevron E76B, GenericApp E737, About (Info) E946, SwitchOn (ToggleFilled) EC11, SwitchOff (ToggleLeft) F19E, CheckUpdate (Refresh) E72C, CheckFailed (SyncError) EA6A, Download E896. The badge check is a path. About's first two tiles are marks, not commands: the reel's own (`copilot-reel`, plated) and GitHub's Invertocat (`github-light` black on light plates, `github-dark` white on dark ones, glyph class), used as GitHub permits for a button that links to a project. The tray menu is the native popup menu in the reel's light or dark theme with View E890 (Preview, bold as the click default), Settings E713 (Quick menu), Diagnostic E9D9 and PowerButton E7E8 (Exit) at the small-icon size in `foreground`; high contrast keeps the system menu without icons. Monochrome icon is a check item without a glyph. The hub's Tray icon tile shows the same choice in its icon: the reel's own plated mark while the tray icon is in color, the Soft Orbit mark alone, 7/8 em wide in the glyph color, while it is monochrome. The tray icon itself is the color ICO's own small frame, or, when monochrome, the Soft Orbit mark without its plate at the small-icon size, in white on a dark taskbar and black on a light one (`iconography.commandGlyphNotes.TrayIcon`). Per-brand treatment (class, `opticalScale`, normalized content rect) is `iconography.brands`; page 6 shows every entry on light and dark tiles.

## States

Idle (neutral tiles, faint ring); selected (the seat replaces the tile fill, one name, ring hidden); disabled (0.36, named on hover, never seated); preferred (badge); dragged (Manage apps: the tile lifts to 1.08 and follows the pointer, the others slide to open its slot, the trash alone owns the center; over the armed trash the tile shrinks to 0.7 at 0.4 beneath it); keyboard focus (the same seat); commit (the choice holds 50 ms while the rest fades). High contrast uses live system colors and paints the seat COLOR_HIGHLIGHT.

## Layouts and navigation

- One to nine items: App actions (3-4: the app itself in slot 0, Edit, Preferred, and Remove while more than one app remains), Main (2-9: up to 8 apps and +), the + hub (7: Language, Appearance, Reel size, Tray icon, Manage apps, Add, About), About (5: the reel's own mark named by the version and the author, GitHub, Auto-check updates, Auto-install updates, the update tile), Manage apps (2-9: the apps and Add), Presets (up to 8: parked apps first, then presets, five per page, Custom, Previous, Next).
- At 8 apps the hub and Manage keep their Add slot, disabled, without reflow.
- Main (depth 0) -> + hub (1) -> Manage apps or About (2) -> Presets / App actions (3) -> focused native editor for names and targets. Language (chevrons or A/D), Appearance and About's switches act in place. On Manage apps a dragged tile reorders the reel in place, and the center trash takes an app out of it; App actions has no reorder control. The center or Esc returns one level.

## Placement and edge gating

The pointer is the plate center whenever the 384 DIP canvas fits (offset 0). At an edge or corner the whole canvas moves inward by the minimum needed; order, angles, radius and icon size are unchanged. Wherever that leaves the pointer, on the plate or outside it (168 DIP), it selects by direction as soon as it moves; there is no center entry and no gate visual (removed in 0.3.11). The opening position itself never selects. No cursor warp, no line from the pointer.

Reel size (`geometry.sizeSteps` 0.7, 0.85, 1, 1.15; default 1) multiplies `geometry.scale` into the same whole effective DPI, so the canvas, the clamp and the work-area shrink above apply unchanged at every step. The hub's Reel size tile shows its step as a 12 DIP monogram, never below 9 device pixels (70%, 85%, 100%, 115%); a confirm moves to the next step and the open reel is placed again around the point it opened at.

## Motion (Full tier, Windows.UI.Composition)

| Event | Spec |
| --- | --- |
| Open | opacity 0 -> 1, 83 ms linear; scale 0.96 -> 1, 167 ms decelerate (0, 0, 0, 1); origin = plate center |
| Close (cancel, Esc, timeout) | opacity -> 0, 83 ms linear; scale -> 0.98, 83 ms accelerate (0.4, 0, 1, 1) |
| Commit (launch, appearance) | plate, other items and name fade in 83 ms; the choice holds 50 ms then fades 83 ms; press 1 -> 0.975 -> 1 over 100 ms; 133 ms total; the launch never waits |
| Seat appears | opacity 0 -> 1, 83 ms; scale 0.94 -> 1, 167 ms decelerate; tile fill -> 0, 83 ms |
| Seat travels | along the orbit, shortest arc; ScalarNaturalMotion spring, damping 0.85, period 50 ms (observed settle about 140 ms) |
| Name | crossfade 83 ms linear; the ring hides in 83 ms |
| Gate arms | ring to idle style in 167 ms decelerate; dot fades in 83 ms |
| Drill in / out | out 100 ms to 1.04 / 0.96 and 0; in 250 ms from 0.94 / 1.04 with (0.1, 0.9, 0.2, 1); fade 83 ms after 50 ms |
| Handoff | the tapped tile travels to the center in 200 ms, scaling to 0.5, and fades over the last 83 ms as the back chevron appears; the outgoing name clears in 33 ms |
| Swap (presets) | out 83 ms; in 120 ms from 0.98, decelerate |
| Language | only the monogram crossfades (83 ms) |
| Drag (Manage apps) | lift: the tile scales to 1.08 in 120 ms and then follows the pointer without easing; the other apps slide to their open slots in 167 ms; the trash fades in over 83 ms and arms with a 0.88 -> 1 pop |
| Drop | every app settles from where it is to its saved slot in 200 ms on the drill curve; a removed tile falls into the trash (to 0.3, transparent) in 167 ms while the trash fades; a canceled drag returns every tile in 200 ms |
| Reduced motion, high contrast | the same path with every duration 1 ms |

Never: staggered entrances, hover magnification, overshoot or elastic curves, a rim notch, pointer-anchored origins, perimeter stroke animation, connector lines.

## Material and elevation

- Solid (transparency off): opaque plate, one baked shadow, 1 DIP rim.
- Frosted (transparency on): HostBackdrop -> Blend(Luminosity) with the tint at luminosity opacity (light `#FCFCFC` at 0.85, dark `#2C2C2C` at 0.96) -> Blend(Color) with the tint (0 / 0.15) -> 2% noise, 256 px tile. Tiles use the same roles (light tile at 70%).
- Solid whenever CompositionCapabilities reports effects unsupported or slow, the backdrop cannot be created, high contrast is on, or energy saver is on.
- Layers, back to front: shadow, backdrop, plate and rim, ring and gate, seat, page items, handoff clone, back chevron, drag trash, two label visuals.

## Do and don't

One plate (no plate inside the plate); the name owns the center (never over the ring); no titles, captions or helper copy; no perimeter sweep or connector; vendor marks never recolored or redrawn; glyphs never accent-colored and no dot under the selection.

## Evidence

Runtime-verified in 0.3.4 (native window captures in `output/runtime/v0.3.4/`, see [validation](validation.md)):

- Dark and light solid plates: tiles, seat, badge, chevrons, name, ring and gate.
- The high-contrast code path (seat in COLOR_HIGHLIGHT) with the machine's current, non-high-contrast system colors.
- 100, 150 and 200% through the DPI override.
- Motion strips: open, cancel, seat appear and travel, drill-in with handoff, drill-out, commit (visual evidence, not frame-rate measurement).
- EN/ZH/JA: idle Main pixel-identical; the hub differs only inside the monogram.
- Adjust chevrons (0.3.5): each chevron is its own sprite showing its half of the unchanged two-glyph raster. Main, the hub and its selections stay within 2 levels per channel of the 0.3.4 captures in five variants (0.3.5.1, every channel compared; the 0.3.5 report of 0 differing pixels compared only alpha); App actions frames at the first, a middle and the last position show the dimmed blocked side (`output/runtime/v0.3.5/*-entry*.png`, captured through the test seam). 0.3.6 removed those chevrons with the reorder they served.
- Drag and trash (0.3.6): `output/runtime/v0.3.6/*-manage-drag.png` and `*-manage-trash.png` show a tile lifted over another slot and over the armed trash in seven variants; the 0.8 reel scale and the four-item hub make every 0.3.6 still a new reference.
- Reel size (0.3.7): `output/runtime/v0.3.7/` is the reference set for the five-item hub, with the Reel size tile showing `100%` as a 12 DIP monogram in seven variants.
- About (0.3.8): `output/runtime/v0.3.8/` adds the six-item hub and About in seven variants: the reel's mark named by the version and the author, GitHub's mark in black or white, and the on switch with its cut-out knob over the seat.
- Legibility floor (0.3.9): `output/runtime/v0.3.9/` changes ten stills from 0.3.8: at 100% display scaling the name and the monograms in Segoe UI Variable Small, at 150% the name moved by a fraction of a pixel by the 160 DIP line; the other 88 are within tolerance.
- Placement: zero interior offset, minimal clamp at eight edge and corner positions, no cursor warp.

Not verified:

- The live frosted backdrop over real content: window capture omits the DWM host backdrop, so frosted pages are tonal illustrations; `blendModeVerified` is still `pending`.
- Real High Contrast themes.
- Physical Copilot-key tap and hold timing.
- Reduced motion, high-refresh smoothness and GPU cost.

Known limit (0.3.4): retargeting the seat twice within one spring settle measures the shortest arc from the previous target, so a fast 180-degree flick can swing the long way round.

## References

- [Windows motion](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/motion)
- [Composition animation](https://learn.microsoft.com/en-us/windows/uwp/composition/composition-animation)
- [Windows materials](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/materials)
- [Brand asset policy](brand-assets.md)

Specification date: 2026-10-20. Development documentation, tokens and sources are English; product localization (English, Simplified Chinese, Japanese) is separate.
