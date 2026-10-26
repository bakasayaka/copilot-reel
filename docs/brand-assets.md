# Official application artwork and preset discovery

Copilot Reel uses vendor artwork to identify the applications the user can launch.
The common visual treatment belongs to the surrounding tile: size, padding,
alignment, corner radius, hover feedback, and the Windows accent-color ring.
The vendor's silhouette, internal colors, and proportions are preserved.

## Preset catalog

`AgentPresets()` returns the Add page's catalog in the order below. The catalog
and its matching rules live in `src/agent_catalog.h`. Core products have bundled
artwork and are always listed; a product that is not installed has an empty
target. The executable column is the file name and its install folder, which is
the executable's parent, or the folder above `bin\` for a JetBrains IDE.

| Preset | Verified source | Desktop resolution |
| --- | --- | --- |
| ChatGPT / Codex | Installed OpenAI package manifest and icons | Registered `OpenAI.Codex_2p2nqsd0c76g0!App`. OpenAI ships no desktop build outside the Store, so there is no Win32 rule |
| Claude Desktop | Installed Anthropic package manifest and icon | Registered `Claude_pzs8sxrjxfjjc!Claude`; only without the package, the legacy Squirrel stub `%LOCALAPPDATA%\AnthropicClaude\claude.exe` |
| Claude Science | [Official product page](https://claude.com/product/claude-science) | `claude-science.exe` in `ClaudeScience` |
| Antigravity | [Google press assets](https://antigravity.google/press) | `Antigravity.exe` in `Antigravity` |
| ZCode | [Official installation guide](https://zcode.z.ai/en/docs/install), installed app artwork | `ZCode.exe` in `ZCode` |
| DeepSeek Harness (DSH) | [Official desktop download](https://www.deepseek.com/en/download/), [official source repository](https://github.com/deepseek-ai/deepseek-harness) | `DeepSeek Harness.exe` in `DeepSeek Harness` |
| Visual Studio Code | [Microsoft brand assets](https://code.visualstudio.com/brand) | `Code.exe` in exactly `Microsoft VS Code`; Insiders is a separate product |
| JetBrains IDE | [JetBrains product artwork](https://www.jetbrains.com/company/brand/) | `idea64`, `pycharm64`, `webstorm64`, `rider64`, `clion64`, `goland64`, `phpstorm64`, `rubymine64` or `rustrover64.exe` in `bin\` of a folder that starts with the product name (`PyCharm 2025.2`, `JetBrains Rider 2025.2`). One row per install, named by its Start-menu shortcut or install folder; a single empty `JetBrains IDE` row when none is found |
| Junie | [Official product site](https://junie.jetbrains.com/) and JetBrains artwork | Never discovered. Junie has no desktop executable of its own, so the user chooses the actual host |

Discover-only products have no bundled artwork. They are listed only when
installed, after the core products, and show their executable's own Shell icon:

| Preset | Desktop resolution |
| --- | --- |
| Cursor | `Cursor.exe` in `cursor` |
| Grok Bot | `Grok Bot.exe` in `Grok Bot` |
| Zed, Zed Preview, Zed Nightly | `Zed.exe` in `Zed`, `Zed Preview` or `Zed Nightly`, one row per channel; `bin\zed.exe` is the CLI |
| Windsurf, Devin Desktop | `Windsurf.exe` in `Windsurf`; `Devin.exe` or `Windsurf.exe` in `Devin`. The Devin Desktop executable name is not yet confirmed on an install |
| Kiro | `Kiro.exe` in `Kiro` |
| Trae, Trae CN | `Trae.exe` in `Trae`; `Trae CN.exe` in `Trae CN` |

### Discovery

Discovery reads four sources. A registered package always wins and is stored as
its AUMID.

1. **Packages:** `GetPackagesByPackageFamily` for the ChatGPT and Claude families.
2. **Start-menu shortcuts** in both Programs roots, read with `IShellLink`
   `GetPath(SLGP_RAWPATH)` with environment strings expanded. `Resolve` is never
   called. Loading a link costs 1 to 3 ms, so a link is loaded only when its raw
   bytes contain a catalog executable name in ANSI or UTF-16. On the development
   machine that is 7 of 277 links.
3. **Uninstall keys** in HKCU, HKLM and HKLM\WOW6432Node. The executable comes from
   `DisplayIcon` when it names an `.exe` (with `,N` removed). Otherwise it is
   `InstallLocation` plus the executable of each rule whose folder matches.
4. **Exact folder probes:** `%LOCALAPPDATA%\Programs\<folder>\<exe>` and
   `%ProgramFiles%\<folder>\<exe>`. Folders are never searched recursively.

A candidate is accepted only if all of these hold:

- It matches a rule by executable file name **and** install folder. A display
  name or shortcut stem alone never identifies a product, so the Claude Code CLI's
  `claude.exe` in its WinGet folder is not Claude Desktop.
- It exists and is not a reparse point, such as a 0-byte app execution alias.
- It is a PE image with the Windows GUI subsystem; console programs are excluded.
- It is not an uninstaller or updater: `unins*.exe`, `uninst*.exe`,
  `Uninstall *.exe`, `Update.exe` or `inno_updater.exe`.
- Its own folder is not an updater staging folder: `_`, `_bak` or
  `app-<version>`. Folders higher up the path are not checked, so a profile or
  drive folder with such a name never hides an install.

Win32 products are stored as the **executable path**, not as a shortcut.
Updaters delete and recreate shortcuts (DSH on 2026-10-20, Cursor on 2026-10-21),
and an orphaned shortcut can outlive its application. Candidates are de-duplicated
by their lowercase long path, so Cursor's two shortcuts (`Programs\Cursor.lnk` and
`Programs\Cursor\Cursor.lnk`) become one row. For a single-row product or
channel, the first source to find it wins.

`AgentPresets()` can run on any thread with COM initialized, STA or MTA. It shows
no UI and never executes or resolves a target. On the development machine it
takes about 80 ms on its first call in a process and about 40 ms (MTA) or 30 ms
(STA) after that. The first read of a large executable after an antivirus
signature update can be slower, because the scanner checks the file once.

`AgentPresets()` does not replace saved entries. A missing desktop target is an
empty `AppTarget.target`; the caller must open the editor or file picker before
saving it. It must not silently open a marketing website, invent a protocol,
execute `npx`, install an application, or claim that an IDE has Junie installed.
The returned catalog can exceed eight entries when several JetBrains IDEs or
discover-only products are installed. The picker must paginate it; the active
wheel still has its own limit.

`InstalledIdentity()` (`src/local_launch.h`) names the application that a
verified target launches. The Add page uses it to hide products that are
already in the reel, whether they were saved as a package, an executable or a
shortcut.

The OpenAI package's legacy name contains `Codex`, but its manifest names the
application ChatGPT. The combined preset uses that verified application ID.
It does not claim that two distinct application IDs exist. An installed Claude
Science shows the mark embedded in its own executable (Anthropic, PBC) as its
Shell icon; an unresolved Claude Science preset uses the Claude family mark. It
is not given a fabricated science symbol.

DSH is resolved to **DeepSeek's own DeepSeek Harness desktop**, not a similarly
named community wrapper. The official download page links to Windows and macOS
builds; the upstream repository identifies the desktop package as
`@deepseek-ai/dsh-desktop` and contains its Windows icon. The development machine
has a per-user installation at
`%LOCALAPPDATA%\Programs\DeepSeek Harness\DeepSeek Harness.exe`. It is signed by
Hangzhou DeepSeek Artificial Intelligence Co., Ltd. and registered under the HKCU
Uninstall key `1bf39983-50d0-5fe0-9ef4-cece76f67c5e`, which has no Publisher.
Its Start-menu shortcut is recreated by the updater, which is why discovery
stores the executable.

## Existing defaults and Windows system targets

The saved default catalog also includes Gemini, Perplexity, Windows Settings,
and Notepad. Gemini uses Google's 512-pixel sparkle artwork linked by
[gemini.google.com](https://gemini.google.com/). Perplexity uses its own
[favicon SVG](https://www.perplexity.ai/favicon.svg) and
[PNG fallback](https://www.perplexity.ai/favicon.png), both advertised by its
[official application page](https://www.perplexity.ai/). These source files are
preserved byte-for-byte and included in the same startup asset cache.

Windows Settings, Notepad, the discover-only products and every custom
application without vendor artwork use their own Windows Shell icon (`SHCreateItemFromParsingName` +
`IShellItemImageFactory::GetImage`, 256 px, icon only): `ms-settings:` targets use
the Settings package identity, Notepad its package, and executables or shortcuts
their resolved file. Icons are extracted once per target while the configuration
loads (`PrepareArtwork`), never during a draw. `IdentifySystemIcon` still
recognizes actual system destinations for localized display names; it never
infers identity from display names or arbitrary substrings.

Vendor artwork is chosen by the launch target alone (`IdentifyBrand`): the package
family of an AUMID, the file name of an executable or shortcut together with its
install folder, or a URL host. A profile folder such as `C:\Users\<name>` never
counts, and the display name is consulted only while a preset has no target yet.
Renaming an app therefore cannot change its artwork, and a user named Claude
does not stamp the Anthropic mark on unrelated applications. The VS Code mark
requires the install folder `Microsoft VS Code` exactly, so VS Code Insiders
(`Microsoft VS Code Insiders\Code - Insiders.exe`) keeps its own Shell icon.

The earlier approximate ChatGPT, Claude, Gemini, Codex, and Perplexity shapes and
the hand-drawn Settings and Notepad glyphs are removed from the reel; verified
vendor artwork and Shell icons are its only application images. The focused
editor's list keeps neutral line glyphs (`DrawAppSymbol`).

## Files and provenance

[The machine-readable manifest](../assets/brands/provenance.json) records the
SHA-256 digest, exact source, owner, and processing for every artwork file.
GitHub sources are pinned to the retrieved commit. Installed application assets
retain their package version and internal resource path. No asset comes from
an unofficial logo aggregator or a hand-drawn approximation.

PNG variants for JetBrains products without an upstream PNG in the selected
resource set were rasterized from the adjacent original SVG at 256 x 256 with
`sharp`. This conversion does not recolor, crop, round, or redraw the brand mark.
The original SVG remains in the repository and package.

### About

About (0.3.8) shows two marks that do not identify a launchable application:

- `copilot-reel.png` is the reel's own mark, generated by `tools/generate-assets.py`
  with the package icons, as plated artwork for About's first tile.
- `github-light` and `github-dark` (PNG and SVG) are GitHub's Invertocat, black and
  white, unmodified from GitHub's [logo package](https://brand.github.com/foundations/logo)
  (`GitHub_Logos.zip`, retrieved 2026-10-25). GitHub permits the Invertocat as a
  button that links to a project on GitHub; About uses it only for the tile that
  opens this repository, after the reel's own mark, in black or white as the
  plate requires. That tile is named by the author, `baka_sayaka`. It is not used to identify any application in the reel.

## Native rendering and resource lifetime

`PreloadOfficialIconAssets()` reads the PNG of every brand listed in
`design/vi/tokens.json` (`iconography.brands`) once on the UI thread. The original
SVGs stay in the repository and package for provenance; the reel renders the
official PNGs, which avoids partial rendering of SVG masks and filters that
Direct2D does not implement.

Every reel item belongs to one artwork class (`src/artwork.h`):

| Class | Assets | Treatment |
| --- | --- | --- |
| Plated | Claude, Perplexity, ZCode, DeepSeek Harness, JetBrains Air, the reel's own mark (`copilot-reel`: About's first tile, and the hub's Tray icon tile while the tray icon is in color) | The mark's own plate replaces the neutral tile: cropped to the plate's own bounds (DeepSeek Harness drops its baked shadow margin), scaled to cover the 40 DIP tile and masked to the tile's 10 DIP corner radius, with the tile hairline on top. |
| Glyph | ChatGPT, Gemini, VS Code, Antigravity, the JetBrains IDE family, Junie, Toolbox, GitHub's Invertocat (`github-light` / `github-dark`, About's repository tile), Shell icons | Centered on the neutral tile. The ink bounding box fits a 28 DIP keyline times a per-brand optical scale (0.86 for the dense JetBrains product squares, 0.92 for the Invertocat, 1.04 for the Gemini sparkle), so marks read at equal visual weight. |
| Command | Internal reel actions | Segoe Fluent Icons glyphs at 24 DIP in the foreground color. The hub's Tray icon tile, while the tray icon is monochrome, draws the reel's mark without its plate (`iconography.orbitMark`, `src/orbit_mark.h`) in that color instead. |
| Generic | No artwork | A neutral window glyph (Segoe Fluent Icons E737). |

The class, optical scale and content rectangle of each brand are tokens, measured
from the artwork's alpha (coverage of its bounding box and match with a rounded
rectangle) and tuned on native contact sheets. Shell icons are classified by the
same alpha analysis at run time.

Masking a plated mark to the tile shape is the only geometric treatment. Vendor
marks are never recolored, redrawn, distorted, or placed inside a second plate;
only the reel's own mark has a second form, drawn alone in the glyph color on the
monochrome Tray icon tile. Light and dark variants are used only where the vendor
ships them (ChatGPT, and GitHub's black and white Invertocat: `github-light` on a
light plate, `github-dark` on a dark one); in high contrast the variant follows
the brightness of the system window color.

Raster artwork is pre-scaled with WIC high-quality cubic filtering to the exact
device-pixel size for the current DPI and drawn 1:1 on the pixel grid; any other
size uses Direct2D high-quality cubic interpolation. Caches are keyed by source
and pixel size, held per Direct2D device, and released on DPI change and device
loss (`ReleaseArtwork`).

Missing or unverified artwork uses the neutral glyph. It must never be replaced by
an invented branded logo.

## Vendor notices

All names and artwork identify their respective vendors and products. Copilot
Reel is an independent launcher and does not imply sponsorship or endorsement.

- OpenAI and ChatGPT marks belong to OpenAI. See the [OpenAI design guidelines](https://openai.com/brand/).
- Claude marks belong to Anthropic. The source image is the unmodified official Windows application resource.
- Antigravity artwork belongs to Google and comes from its [press asset collection](https://antigravity.google/press).
- Gemini artwork belongs to Google and comes from the official Gemini application page.
- Perplexity artwork belongs to Perplexity and comes from its official application favicon resources.
- ZCode artwork belongs to Z.ai and comes from the installed official Windows application resource.
- DeepSeek Harness is a DeepSeek trademark. Its source repository is MIT licensed; the [license](../assets/brands/deepseek-LICENSE.txt) is retained. Its [brand guidelines](https://github.com/deepseek-ai/deepseek-harness/blob/master/BRAND_GUIDELINES.md) prohibit misleading claims of affiliation.
- Visual Studio Code artwork belongs to Microsoft. Its [brand guidelines](https://code.visualstudio.com/brand) prohibit redrawing or restyling the icon, so its internal artwork remains unchanged.
- GitHub and the Invertocat belong to GitHub, Inc. The `github-light` and `github-dark` marks are unmodified from GitHub's [logo package](https://brand.github.com/foundations/logo) and appear only on About's tile that opens this repository, as GitHub permits for a link to a project on GitHub.
- Copyright © 2026 JetBrains s.r.o. JetBrains, Junie, IntelliJ IDEA, PyCharm, WebStorm, Rider, CLion, GoLand, PhpStorm, RubyMine, RustRover, Air, Toolbox, and their logos are trademarks of JetBrains s.r.o. See [JetBrains](https://www.jetbrains.com/), its [brand guidelines](https://www.jetbrains.com/company/brand/), and the retained [asset license](../assets/brands/jetbrains-LICENSE.txt). Product icons identify products; the company icon identifies the generic JetBrains IDE picker entry.

The repository's source-code license does not replace these vendor trademark
terms or grant additional rights to the bundled artwork.
