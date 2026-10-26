# Second design audit (2026-10-26, 0.3.12)

A second pass over `main` at `3bfc279` (0.3.12), against the first audit at 0.3.8 ([`../2026-10-25-audit/README.md`](../2026-10-25-audit/README.md)). Since then main shipped 0.3.9 to 0.3.12:

- the P1 legibility floor, with the name floor at 10 px;
- Japanese;
- the fork's tray icon (pull requests #2 and #3);
- the generic glyph for apps without an icon;
- a seventh hub tile, Tray icon;
- no center entry at screen edges;
- per-surface painting.

Method:

- Three review subagents read the renderer, the tray and editor, and the VI, docs and assets. The medium findings below were checked again in the source; the low findings and the contract table were then verified adversarially, two independent readers per item and a third where they disagreed, and corrected where a part did not hold.
- The 0.3.12 reference stills and the README GIFs were measured with Pillow.
- Nothing was built or run: the review ran in a Linux container. Items marked *hand check* need one look on Windows.
- Line numbers refer to `3bfc279`.

## What holds up

- **P1 works as shipped.** Windows tests, re-read code and the 0.3.12 stills agree:
  - The name never draws below 10 px and the monograms below 9 px (`renderer.cpp:331-358`, `typography.h:29-38`).
  - Text below 13 px uses Segoe UI Variable Small, with each face's own cap height.
  - The name's box grows with a raised size.
- **Per-surface painting is correct** (`surface_paint.h`, 0.3.12):
  - the local bitmap is at the reel DPI and copied 1:1 to whole-pixel offsets;
  - premultiplied alpha is kept with copy blend and nearest sampling;
  - device loss is recovered;
  - nothing leaks.
- **The seven-tile hub lays out cleanly.** In the 0.3.12 stills the tiles mirror to the pixel at 100, 150 and 200%, with centers within 0.5 px of the exact orbit. The nearest seat leaves the 160 DIP name line 5.2 DIP clear (4.7 with a badge). The Language chevron's hit box clears slot 1's seat by 3.6 DIP.
- **The edge gate is gone everywhere.** Tokens, generator, header, renderer, core, controller and tests no longer carry it, and no dead code is left; only some docs still mention it (see the contract table below).
- **A1 shipped.** An executable without its own icon now shows the generic glyph instead of a clipped stock icon (`artwork.cpp:595-612`).
- **The tray icon's handles and GUID are sound.**
  - No icon, bitmap or DC leaks.
  - When Windows refuses the GUID, the icon falls back to its window identity, which only resets the user's taskbar visibility choice.
  - Light and dark taskbars and high contrast are followed through the settings broadcasts.

## Status of the first audit's findings

| Area | Fixed | Partly | Still open |
| --- | --- | --- | --- |
| Reel (R1-R16) | R1 (text floor, at 10 px), R6 (Small optical master) | — | R2 (badge ring spills past the seat; 0.6 px worse at the 85% step since the dot floor), R3, R4, R5, R7, R8 (the gated ring is gone; idle and trash rings still differ), R9-R16 |
| Artwork (A1-A2) | — | A1 (generic glyph shipped; the plate-silhouette check was not added) | A2 (Shell icons still requested at 256 px) |
| Editor and tray (E1-E12) | — | E12 (the tray icon now loads at the small-icon size and reloads on change) | E1 (Enter on any button saves), E2 (Choose app selection barely visible), E3-E11: `src/osd.cpp` is essentially unchanged since 0.3.8 |
| Assets and README (D1-D6) | — | — | D1 (one Lanczos master, no 20 or 40 px frames), D2 (three scale-100 logos only), D3 (Chinese in YaHei Bold), D4 (three blues in the light README), D5 (step stills; the hold finale enlarges the Claude mark), D6 (GIFs baked on `#0d1117`, no reduced-motion still, duplicate files) |
| VI, tokens and generator | — | the generator dropped the gate scenes | everything else in the first audit's lists: version headers, `supportedLanguages` without ja-JP, `iconStates` EN/ZH, the generator's Preferred hub and page, page 5's overflow and SwitchOn |

The first audit's own wrong predictions are now corrected:

- **"Nothing changes at 150%" was wrong.** A Windows rebuild with the line set back to 132 tied three 150% stills to the 160 DIP line (`docs/validation.md:49`).
- **The 11 px floor trimmed Visual Studio Code at 70%.** The floor shipped at 10 px.
- **Remaining stale lines** (the first audit's notes on P1 and the hub, and `docs/visual-identity.md:55`) are fixed in the commit that adds this report.

## New findings

| # | Sev | Where | Finding | Fix |
| --- | --- | --- | --- | --- |
| N1 | M (*hand check*) | `radial_menu.h:178, 187-192`, `renderer.cpp:815-823, 890-903, 1255-1260` | Tiles that carry their state in their target rebuild the whole page when confirmed: Tray icon (`reel:tray-icon:color` ↔ `:monochrome`) and About's two switches (`reel:auto-check:on` ↔ `:off`). `SameStructure` compares targets, so `SetPage` builds a new page. The seat then plays its appear animation (fade-in and 0.94→1 scale), and the art swaps without a crossfade. Language and Reel size keep one target and only crossfade. Stills cannot show it. | Give each such tile one stable target and carry the state in `ArtKey`, as `sizeLabel` does. Let `UpdateInPlace` crossfade the art and handle a change of artwork class. |
| N2 | M | `radial_menu.h:190-192`, settled decision "every center name fits" | "Auto-check updates" measures 131 DIP at 14 DIP in the Small master (105 px in `output/runtime/v0.3.12/dark-100-about-switch.png` at 77 DPI). At the 70% step on a 100% display the floor raises the name to 17.8 DIP, so it needs about 166 DIP of the 160 DIP line and trims. "Auto-install updates" is longer. This is estimated from pixels; no 70% capture exists. | Add a test that lays out every `Tr` string at each size step and DPI and fails on trimming. Shorten the English switch names, or accept trimming at 70% and record it. Capture the 70% step to confirm. |
| N3 | M | `tray_menu.cpp:171-177`, `orbit_mark.h:23-24` | The monochrome tray icon's margin and stroke are size/16 and are not snapped. At 20 px (125%) they are 1.25 px, and the outer ring never reaches full coverage: a two-pixel 75%/50% gray line next to crisp system icons. 100%, 150% and 200% are clean. At 16 px the two rings, 1.7 px apart, read as a target. | Use a whole-pixel stroke (1 px up to 20 px, 2 px from 24) and a whole-pixel margin. Below 24 px, draw the outer orbit and dot only, as in the first audit's P3. See [`tray-mark-16-32.png`](tray-mark-16-32.png). |
| N4 | M | `main.cpp:369, 379, 1200-1205` | At startup `Reload()` runs before `AddTray()`. A configuration error's "needs attention" balloon therefore goes to an icon that does not exist yet, and a broken `apps.ini` at sign-in quietly loads the default apps. This was already the case at 0.3.8 but was not reported. | Add the tray icon before `Reload()`, or hold the notice until the icon exists. |
| N5 | L | `orbit_mark.h:24`, `artwork.cpp:757-759` | In its monochrome state the hub's Tray icon mark keeps a fractional stroke (1.10 px at 100% display scaling, 1.64 px at 150%), next to fallback glyphs rounded to whole pixels (`artwork.cpp:247-251`). At the 70% step on a 100% or 125% display the stroke is already floored to 1 px; there the 11.5 px mark's unsnapped ring radii, about 1.25 px apart, merge into one gray band. | Round the stroke to device pixels; below about 16 px draw the outer orbit and dot only (rounding alone does not fix the 70% case). |
| N6 | L | the hub's Tray icon tile | In color the tile is a full `#005FB8` plate, the loudest element of the hub at rest. Monochrome shrinks the mark from 28.7 to 21.2 DIP, so it reads as a different icon rather than "color removed". The two states are settled; their sizes are the user's call. | Optional: keep the mark's size across both states. |
| N7 | L | `main.cpp:1295`, `README.zh-CN.md:54` | Chinese 黑白图标 is inaccurate when a contrast theme's text color is not black or white (three of Windows 11's four built-in contrast themes use white, Desert uses #3D3D3D). | 单色图标, in the menu and in `README.zh-CN.md`. The Japanese katakana came with the fork's tray toggle; 0.3.11 added the half-width space, and it stays. |
| N8 | L (uncertain) | `tray_menu.cpp:80-98` vs `:189-213`, `docs/configuration.md:42` | Only with the opt-in Monochrome icon: the tray menu follows the app mode while the monochrome icon follows the Windows mode (by design, `configuration.md:42`). With dark Windows and light apps a light menu opens beside a white icon. The default color icon is the same blue plate in every mode. | Check which mode Windows' own notification-area menus follow, then match it. |
| N9 | L (unverified) | `main.cpp:1231-1253, 1617-1620` | A `--smoke-test` instance has its own lock and adds a tray icon with the installed reel's GUID; its stale-icon `NIM_DELETE` before the add may remove the installed reel's icon. Whether Windows lets one process delete another's icon by GUID was never tested, and no script runs `--smoke-test`, so this only happens by hand. | Give non-default instances no tray icon, or no GUID. |
| N10 | L | `tests/tray_icon_tests.cpp:78-96` | The tests check the outer ring only for alpha above 128 and skip 28, 36 and 56 px, so N3 passes. Adding sizes alone would not catch it: only 20 px stays below 0.9 coverage. | Require at least 0.9 alpha on the outer ring at top, bottom and sides, at every size. |
| N11 | L | `tools/record-demos.py:419-426, 460`; `tools/capture-seam.py:73` | The README's About GIFs come from `record-demos.py`, which pins version 0.3.8.0 and serves its own v0.3.9 feed, so they show "0.3.8" and "Install 0.3.9" next to a 0.3.12 release. `capture-seam.py` pins the same version for the reference stills, which show "0.3.8 · baka_sayaka". | Pin a neutral version in both, and make the recorded update the next version. |
| N12 | L | `tools/capture-seam.py:64-67` | The reference set has no light-100 or light-150 variant, no size step and no ZH or JA still. The case that forced the 10 px floor has no reference. | Add those variants, with WCAG ratios in `summary.json`. |
| N13 | L | `main.cpp:1312`, `osd.cpp:329`, `packaging/app.manifest:3`, `packaging/AppxManifest.xml:8`, `packaging/app.rc:5-16`, `CMakeLists.txt:2` | The version is typed by hand in all of these, and `app.manifest` still says 0.3.3.0. The About page users see is right: it reads the version at runtime (`Updater::CurrentVersion`, `updates.cpp:442-454`). | Generate the copies from one value; the diagnostics line can reuse `CurrentVersion()`. |
| N14 | L | `artwork.cpp:578-612, 669-687` | `HasOwnIcon` and `ShellImage` resolve a shortcut's icon location and target on the UI thread, with the network guard on the `.lnk` path only, so a local shortcut to a UNC or mapped-drive target can stall the reel. (The negative cache is documented in the comment at `:675`, and only stores files that exist; it lasts until restart.) | Check the resolved paths with `IsNetworkPath`. |
| N15 | L | various | Housekeeping: <ul><li>the orbit mark's geometry is copied in `orbit_mark.h`, `tools/generate-assets.py`, `tests/tray_icon_tests.cpp:39-44` and this report's mockup, without a token;</li><li>the name-clearance test (`typography_tests.cpp:169-176`) uses the seat's half-width and misses the badge ring, 0.5 DIP further, and snapping (worst real gap about 0.24 DIP, nothing overlaps);</li><li>`PaintLocal` allocates a bitmap per paint, which for the name means once per new hover target;</li><li>`app_types.h:47` still says "gated ring".</li></ul> | Add an `iconography.orbitMark` token, widen the test's seat, reuse a scratch bitmap per size, and delete the stale comment. |

## Contradictions in the design contract

| Where | Contradiction |
| --- | --- |
| `docs/visual-identity.md:133` | It says Appearance acts in place, but it opens Settings (`main.cpp:999-1000`); the same document gets it right at `:147`. The sentence also leaves out Reel size and Tray icon, which do act in place. |
| `docs/visual-identity.md:135, 151, 167`, `docs/architecture.md:127`, `docs/testing-plan.md:52, 158`, `design/vi/stylesheet.typ:31, 83` | The edge-gating heading, the "Gate arms" motion row, the "ring and gate" layer, "arming the gate" and the testing plan's "edge gate" survive the gate's removal. `stylesheet.typ` still reads `ringGatedStroke`, `gateDotRadius` and `gate.*`, which `tokens.json` no longer has, so the Typst token sheet no longer compiles. |
| `docs/visual-identity.md:33, 149, 160`, `design/vi/tokens.json:264` vs `:301`, `design/vi/source/pages.py:60, 1012` | Seat damping 0.85 (about 0.6% overshoot) is paired with "no overshoot" (R11). `docs/architecture.md:132` already limits that rule to easing and states the spring's overshoot, so it is consistent. |
| `docs/visual-identity.md:1-14, 173-196`, `design/vi/README.md:1-3`, `design/vi/source/pages.py:1117, 1353` | The headers and the deliverables still say v0.9 for 0.3.4, `visual-identity.md:9` says 15 pages where the PDF and `design/vi/README.md:9` have 16, and the evidence stops at 0.3.9. The generator still builds its runtime pages from v0.3.4 stills. The normative body (seven-item hub, Tray icon, generic glyph, no gate) is current. |
| `design/vi/tokens.json:2-3, 16, 50, 149, 151, 456, 494` | Stale entries: <ul><li>schema and app version;</li><li>no ja-JP in `supportedLanguages`;</li><li>"EN/ZH" in two descriptions;</li><li>the accent "only" the seat and badge;</li><li>`iconStates` EN/ZH;</li><li>`centerLabelPolicy` without About.</li></ul> |
| `design/vi/source/reel.py:48`, `pages.py:680-823`, `design/vi/vectors/radial-preferred-*.svg` | The VI still draws a Preferred agent hub entry and page, against the settled hub. |
| `docs/visual-identity.md:29` vs `:52`, `tokens.json:50` | Slots are numbered from 1 in one place and from 0 in the other. |
| `docs/validation.md:303-313` | "Repeatable checks" lists the 0.3.4 pointer-driving tools instead of `tools/test.ps1` and the seam. |
| `docs/brand-assets.md:185-198` | The class table and the light/dark note leave out the reel's own mark and GitHub. |
| `docs/architecture.md:115` | It credits `PaintLocal` for every surface; the acrylic noise draws its own CPU bitmap straight into the atlas instead. Its promise, pixels independent of the atlas position, still holds; only the attribution is loose. |

## Developer information, scanned again

Scanned: every commit, blob and tag reachable from `main` and `v0.3.12`, all 427 images added since 0.3.8, and the published release notes.

- **Identities:** every author and committer is a GitHub noreply address.
- **Handoff log:** `AGENTS.local.md` has never been committed. `AGENTS.md` holds rules and settled decisions only.
- **Images:** the new stills and GIFs carry no metadata beyond the GIF loop block, and they are window captures only.
- **Releases:** only `v0.3.12` is published, and its notes hold nothing personal.

**Left in history or on GitHub, none of it on the current tree:**

1. **The contributor's PC model.** It is in his own commit `2a6bdc2` (pull request #2), which `main` carries. 0.3.10 removed it from `docs/validation.md`; removing it from history would mean rewriting his merged commits.
2. **The old merge commit `0ae667e`.** It is still served by hash with the owner's personal address. The owner keeps that address public, so this is accepted.
3. **`refs/pull/1/head`.** It keeps the contributor's first commits with the old `AGENTS.md`, which includes his Windows user name. Only GitHub Support can remove it, together with the cached commit above.

## VI v1.0 plan, updated

- **Done:** P1; A1 from P8.
- **Partly done:**
  - P3: the tray uses the ICO's small frame; the snapped small master, the ICO frames and the MSIX asset set remain, now with N3 and N5.
  - P11: the gate scenes are gone.
- **Still open:** P2, P4, P5, P6, P7, P9, P10.
- **New:**
  - N2: a name-fit guard for every string at every size step.
  - N12: reference coverage for light, size steps and ZH/JA.
  - N1: in-place state tiles.
  - A documentation sweep for the contradictions above.

Suggested order:

1. N1, N2 and N4, which users can notice.
2. N3 and N5 together with P3: one small mark for the tray, the hub tile and the ICO.
3. The documentation sweep with P11, regenerating the VI for the seven-tile hub and About.
4. N12, then P2, P7 and P9 in the renderer.
5. P10 with N11.
6. P5 and P6.
7. P4.

[`tray-mark-16-32.png`](tray-mark-16-32.png) is a Python approximation of `orbit_mark.h` and `tray_menu.cpp` at 16, 20, 24 and 32 px on light and dark taskbars, shown 6x. The first two rows are the shipped mark; the last two are P3's outer orbit and dot with a snapped stroke. `python mockups/tray_mark.py <out dir>` (Pillow) redraws it.
