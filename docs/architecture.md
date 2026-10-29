# Architecture

## Platform and scope

Copilot Reel uses the official Windows Copilot hardware key provider contract and accepts its approximately 300 ms hold threshold. It does not install a keyboard driver or intercept Copilot with a global keyboard hook.

The implementation is one native C++20 process with a Win32 message loop. Windows Composition owns the visual tree and property animations; Direct2D and DirectWrite render geometry, icons, and text into reusable surfaces. The same sources build the x64 and ARM64 packages; there is no architecture-specific rendering or input path. The first visual scheme is Soft Orbit.

VI v1.0 defines a quiet radial interface with one combined + entry. Main contains configured agents and +; its idle center is empty. The hub contains language, appearance, reel size, the tray icon, management, adding, and About; an app's own actions set the preferred agent. EN/ZH/JA and sun/moon convey state in the icons. Idle submenus retain a center back chevron; selected applications and commands show one concise functional name as center text. No generic page heading or instruction is drawn.

Only the wheel registers as a provider. Its targets are ordinary executables, shortcuts, URIs, or applicable packaged application AUMIDs.

## Input and lifecycle

The package declares `com.microsoft.windows.copilotkeyprovider`. Its three states share one controller:

| State | URI | MessageWParam |
| --- | --- | --- |
| SingleTap | `copilot-reel://?state=Tap` | `0` |
| PressAndHoldStart | `copilot-reel://?state=Down` | `1` |
| PressAndHoldStop | `copilot-reel://?state=Up` | `2` |

Message values are application-defined, not Windows enum values. The manifest and handler must agree. The private protocol accepts only these three states, optionally followed by the `&inputTimestamp=<digits>` the Shell appends behind a feature flag, and never arbitrary launch destinations.

For the fast path, `SHGetPropertyStoreForWindow` obtains the HWND property store. The key is GUID `38652BCA-4329-4E74-86F9-39CF29345EEA`, ID `2`; its `VT_UINT` value identifies the application's `WM_APP`-range Copilot message.

A named mutex and resident controller window provide single-instance behavior. Subsequent processes use a bounded `WM_COPYDATA` handoff and exit after acceptance. Handoff and provider messages enter the same state machine. Synthetic tests have passed; Shell discovery of the hidden receiver and physical-key timing remain separate checks.

A packaged startup task (`CopilotReelStartup`) starts the reel in the background at sign-in, so the fast path exists before the first key press; users can turn it off under Settings > Apps > Startup. When the reel is not running anyway, the Shell does not use the documented URI: `twinui.pcshell.dll` activates the provider's AUMID with no arguments for the first press. Every start without arguments is therefore classified before the single-instance handoff, so a forwarded start gets the same meaning as a cold one:

| Start without arguments | Result |
| --- | --- |
| Startup task (`AppInstance::GetActivatedEventArgs`) | Background; a second process exits at once if the reel is already resident |
| Copilot key still down (F23, or Win+Shift) | Open the reel as a hold; its release is polled, because no release message follows |
| Reel already resident, key up | A deliberate start, for example from Start: the click-to-select preview |
| Reel not resident, Start or search in the foreground | A deliberate start: the click-to-select preview |
| Reel not resident, packaged, key up, any other foreground | The key's tap: the preferred agent |
| Unpackaged build | The click-to-select preview; the Shell's fallback never starts an unpackaged build |

The activation itself cannot tell a Start launch from the Shell's fallback: both are an argument-less activation of the provider's `App`. Moving the provider to a hidden application with a separate visible Start entry is not possible, because the Settings Copilot-key picker (`SettingsHandlers_nt.dll`) skips any provider whose declaring application has `AppListEntry="none"`, and Settings writes and the Shell activates exactly that application's AUMID. A cold start therefore also checks which window had the foreground when the process started: Start (`StartMenuExperienceHost.exe`) or search (`SearchHost.exe`) mean a deliberate start; the taskbar does not, because it keeps the foreground after Win+T, Win+B or a click on a taskbar button, and a key tap made then must still open the preferred agent. If none of them is in front, the start is taken as the key's tap. With the startup task a cold start only happens after the user exits the reel; the tray and `Ctrl+Alt+R` always open the preview. `run.log` records the activation kind, the parent process, whether the reel was resident, the foreground window and the key state of every such start.

After a fast-path tap the Shell toggles the provider window: it sends `SC_MINIMIZE` when the reel has the foreground and calls `SwitchToThisWindow` otherwise. The reel ignores minimize, maximize and restore commands; a window minimized by an older build is restored to its new placement before it is shown.

Every input means Confirm, Back or Adjust in every page; only the closed reel gives the Copilot key its two shortcuts. `ReelInput` in `src/core.h` routes the Copilot events and is unit-tested on its own.

| State and event | Result |
| --- | --- |
| Closed + hold start | Capture the cursor anchor, open Main, select by direction |
| Closed + short tap | Launch the preferred local application; request a local target if unavailable |
| Closed + hold stop | Ignore without launching (the tombstone still orders later events) |
| Opening hold + pointer movement | Once the pointer moves 4 DIP, select by direction from the visible center, wherever the edge clamp left it; the center cancels |
| Opening hold + hold stop | Resolve once: launch the item, or enter its submenu; nothing selected closes |
| Opening hold + tap or duplicate hold start | Ignore; the gesture is preserved |
| Persistent menu (a submenu, or a preview) + tap | Confirm the selected item; with nothing selected nothing happens |
| Persistent menu + hold start, then stop | Resume direction selection around the same center without moving the plate; the release confirms |
| Persistent menu + click, Return or Space | Confirm |
| Persistent menu + center, Esc, Backspace or right-click | Return one level; close at Main |
| Persistent menu + A/D, or a chevron beside slot 0 | Adjust slot 0: the language on the hub; on Manage apps, A/D move the selected app |
| Manage apps + press on an app tile, moved past the drag threshold | Lift the tile: it follows the pointer, the other apps open the slot nearest it, and the center shows the trash. Release on a slot saves the order; on the armed trash it parks the app; on its own slot, or after Esc or a right click, the tile returns and the release is not a click. The Copilot key is ignored while a tile is lifted |
| Focused editor or file picker open | Ignore every Copilot event |
| Session lock/disconnect, display/power interruption | Cancel |

A page that has just appeared ignores the cursor until it moves 4 DIP, so the release or click that opened it cannot also select the item beneath; an unmoved cursor never overrides a keyboard selection. A click counts only when its press began on the page now shown. Release is independent of animation completion. Gesture state owns deduplication and invalidates its generation when canceled. URI handoff packets include the producer process creation time and the pointer captured at its entry point. A release tombstone rejects a delayed older Down, including when Up initializes the resident first. Same-process HWND events use their receipt timestamp. Unit tests cover reordering; native tests cover cold Down, cold Tap, and orphan Up. The provider contract has no physical gesture sequence ID, so process timestamps cannot prove perfect ordering under every Shell scheduling race.

## Window and pointer

The held wheel is a non-activating tool popup. Opening a hold preserves keyboard focus and records one cursor anchor. Raw Input is registered only while visible; pointer messages can also update hit testing. Movement within the same sector does not trigger selection repaint. Deliberate preview and submenu navigation activate the window for keyboard adjustment; closing restores the prior foreground window where permitted. Production code never warps the pointer.

Layout and hit testing share DIP geometry and the final visible center. The process is Per-Monitor DPI aware. Capture the cursor before initialization or placement. If the complete canvas fits, place its center exactly at that pointer with no offset or cursor warping. At a boundary, apply only the minimum complete-footprint translation into the monitor work area. Wherever the clamp leaves the pointer, on the plate or outside it (168 DIP), selection goes by direction from the visible center as soon as the pointer moves, and there is no center entry (0.3.5.1 to 0.3.10 required it off the plate). The opening position itself selects nothing: until the pointer moves 4 DIP, a release cancels, so a clamp that leaves the pointer over a tile never launches it by accident. The reel draws its DIP geometry at `geometry.scale` (0.8): its effective DPI is the monitor DPI times the scale, rounded to a whole number, so placement, hit testing, surfaces and artwork share one device-pixel grid. Tiny work areas scale the full circle further, uniformly, including text and targets. Mixed-DPI and small-display usability remain part of the validation matrix.

Closing resolves input immediately, starts the short compositor exit, and uses a one-shot timer (the duration `BeginHide` returns) to hide the HWND and detach the visual tree. While closing, and whenever no gesture is active, the window returns `HTTRANSPARENT` and never acts on a click. Windows forwards `HTTRANSPARENT` only to windows of the same thread, so a click over the plate during the 83-133 ms exit still does not reach another application; true pass-through would need `WS_EX_LAYERED | WS_EX_TRANSPARENT`, which is not yet validated with the composition target. A new opening first finishes a pending exit, then places the window; an opening that cannot be placed leaves it hidden. Launches run on a short-lived COM STA worker, exactly once per commit, so Shell activation never holds the exit; failures return to the UI thread for the message box. A packaged app, an absolute executable, or a shortcut that resolves to one, without arguments or a working directory of its own, is brought forward instead of started again when it is already running. A bare name found on the search path (such as `notepad.exe`) and URI targets (`ms-settings:`) are always started. The reel finds its most recent visible, unowned, uncloaked top-level window whose process has the target's AUMID (a package such as ChatGPT or Claude), is the target's executable, or is a same-named child of it, up to two levels: a launcher stub such as Claude Science's keeps running while the app runs as another copy of the same file elsewhere. It restores the window if minimized (asynchronously, and never for a hung window) and sets the foreground while it still holds the right the Copilot key gave it, falling back to `SwitchToThisWindow`, as the Shell does. Starting such an app again is not equivalent: Cursor and other VS Code forks open an empty window for every start without arguments, and an Electron single-instance handoff only flashes the taskbar once the foreground right has lapsed. A target with arguments or a working directory, or a shortcut that carries arguments anywhere along its chain (a profile, a project), is always started. Network targets get no Shell icon lookup on the UI thread (an offline server would stall every page); they keep their vendor mark or the generic glyph. For every real launch the reel passes on its foreground right with `AllowSetForegroundWindow(ASFW_ANY)` and logs when it holds none. Forced cancellation and reduced motion can hide immediately. The message loop remains; lifecycle timers are not frame timers.

## Responsibility boundaries

UI and logic are separated within one process. The design currently needs neither a separate UI process nor a dedicated per-frame rendering thread.

| Component | Responsibility |
| --- | --- |
| Win32 controller | Copilot/URI commands, handoff, pointer input, tray, lifecycle |
| GestureState and hit testing | Selection, cancellation, release, deduplication, angular geometry |
| AppearanceSource | Windows colors, effects, animations, high contrast, power settings |
| OrbitRenderer | Layered composition tree, Direct2D/DirectWrite surfaces, acrylic material, all motion |
| Artwork (`artwork.cpp`) | Target-based vendor identity, Shell icons, artwork classes, per-DPI raster prescale, Segoe Fluent Icons command glyphs |
| Design tokens | `design/vi/tokens.json`, generated into `src/vi_tokens.h` |
| Configuration/settings | Radial hierarchy, preferred-agent and language persistence, focused editor, validation |
| Platform activation | Shell and packaged-app activation |

The renderer consumes state and cannot decide what to launch. Appearance changes preserve anchor and selection. Configuration reload cancels a gesture before replacing targets.

## Appearance and materials

Material precedence:

1. High contrast: system solid colors, no backdrop.
2. Transparency disabled, energy saver active, or effects unavailable: opaque light/dark neutral surface.
3. Transparency enabled and supported: frosted host backdrop.

The base follows app light/dark appearance with Windows neutrals (`#F9F9F9` / `#2C2C2C`). Selection uses the live accent ramp, never the raw accent: `AccentDark1` in light and `AccentLight2` in dark for fills and strokes, `AccentDark2` / `AccentLight3` for accent text. Solid surfaces have alpha `1` and no background sampling. High contrast uses `GetSysColor` window, text, highlight, and highlighted-text colors. `COPILOT_REEL_VISUAL_OVERRIDE` (theme, material, contrast, motion, DPI, and the taskbar's Windows mode, which the tray icon and the tray menu follow) exists only for native captures and is never persisted.

`UISettings` supplies effects/animation preferences and foreground/accent colors. Its change events and energy-saver events post to the UI thread, where updates are coalesced. Win32 setting/theme/color messages also refresh the snapshot; `SPI_GETHIGHCONTRAST` checks high contrast. Exit revokes subscriptions and clears callback access to the HWND. There is no per-frame registry polling.

The frosted path (`acrylic.cpp`) follows the WinUI acrylic graph over the shell's host backdrop, which the shell has already blurred: a luminosity blend and a tint blend with the token colors, then a tiled 2% noise layer, through public Direct2D effects described with `IGraphicsEffectD2D1Interop` (no Win2D, no extra blur). The circular visual is clipped to the plate. The transparency preference permits effects but does not prove this HWND can display them; capability changes re-apply the material, and capability or error paths fall back to solids. `SetAppearance` is idempotent, so an unchanged appearance never rebuilds the material on open. Successful API setup is not visual proof of the backdrop.

## Composition tree and resources

The renderer keeps one composition tree per window, back to front:

1. Root: open/close opacity and scale around the plate center.
2. Acrylic backdrop (frosted only), then the base surface: one baked flyout shadow (Direct2D Shadow effect, knocked out under the plate), the plate fill when solid, and the inset rim.
3. Center: the idle ring.
4. Pages: one container per orbit page. Each holds the selection seat (driven by an `Angle` property and an expression along the orbit) and one container per item with its neutral tile fill and its artwork sprite (plus a selected variant where the badge ring or high-contrast glyph color differs), and the language chevrons.
5. Handoff layer for the tile that travels to the center on drill-in.
6. Back chevron, the drag trash (idle and armed sprites) and two center-label sprites for crossfades.

Every surface has an even device-pixel size and a snapped offset, so items, tiles and hairlines land on the pixel grid at 100-250% scale; hit testing keeps exact geometry. Each surface is painted in a bitmap of its own and copied into the composition atlas (`PaintLocal`, `src/surface_paint.h`), so its pixels never depend on where the atlas keeps it. The one exception is the acrylic noise tile: `NoiseBrush` (`src/renderer.cpp`) draws its CPU bitmap (`AcrylicNoisePixels`) straight into its own surface, pixel for pixel at the surface's whole-pixel atlas offset with nearest-neighbor sampling, so its pixels do not depend on the atlas position either. Item artwork is rasterized once per identity, theme and DPI; selection and hover never redraw it. The idle reel produces no surface draws and no running expressions: the seat's expression stops when its travel settles, when the seat hides mid-travel, and on every reset. Updates that arrive during the exit animation are deferred until the next opening. Device loss replaces the rendering device in place and redraws everything once.

## Motion

All motion is compositor-driven with values from `tokens.motion`; interruptions start from presentation values.

| Event | Motion |
| --- | --- |
| Open | Opacity 0 to 1 in 83 ms linear; scale 0.96 to 1 in 167 ms decelerate, at the plate center |
| Cancel | Opacity to 0 in 83 ms linear; scale to 0.98, accelerate |
| Commit | Everything else fades in 83 ms; the chosen item and seat hold 50 ms with a 1 to 0.975 to 1 press, then fade (133 ms) |
| Selection | The seat fades in (83 ms, 0.94 to 1 in 167 ms), then travels the shortest arc on a critically damped spring (damping 1, period 45 ms) as the tile fill crossfades with it |
| Center | One thing at a time: a name, the ring, the back chevron or the trash that leaves clears in 33 ms, and what enters fades in over 83 ms once it has; the ring gives way while a name shows |
| Drill in / out | The outgoing page scales to 1.04 / 0.96 and fades in 100 ms; the incoming page scales from 0.94 / 1.04 in 250 ms (0.1, 0.9, 0.2, 1) and fades in after 50 ms; the plate never moves |
| Handoff | On drill-in the tapped tile travels to the center in 200 ms, scaling to 0.5, and hands over to the back chevron |
| Swap | Preset paging crossfades with a 0.98 to 1 settle in 120 ms |

Reduced motion and high contrast run the same path with 1 ms durations. There is no pointer tether, circular reveal, perimeter sweep, rotating artwork, staggered entrance or hover magnification; easing has no overshoot, and the seat spring is critically damped (damping 1), so nothing overshoots.

## Design tokens

`design/vi/tokens.json` is the hand-edited source for geometry, palettes, accent roles, material, motion, typography and per-brand artwork treatment. `tools/gen-vi-tokens.ps1` validates it and writes the committed `src/vi_tokens.h`; `tools/build.ps1` and CMake run the generator before compiling, and `-Check` fails when the header is stale. The Python VI generator reads the same file, so the identity document and the application cannot drift apart silently.

There is no pre-rendered frame sequence or fixed 60 Hz application timer. The compositor evaluates animation at presentation cadence without application redraws on every refresh. This reduces work but does not guarantee frame rate or GPU cost.

Performance validation must cover 60/144/240 Hz, mixed refresh rates, different DPI, integrated graphics, and battery/energy saver. Measure first-visible latency, selection latency, frame-time variation, and static/hidden CPU/GPU activity against an application-closed baseline. See [validation](validation.md).

The [testing plan](testing-plan.md) defines the proposed separation between system-state collection, pure appearance/material policy, injected native rendering and real ETW/WPR measurements. These seams are not yet implemented: current forced high contrast still reads live system colors, and the frosted override can supersede the earlier energy-saver decision. The plan also records a proposed shared Confirm/Back/Adjust interaction contract; it does not describe current menu-open Copilot behavior as already fixed.

## Language and identity

Text uses the `Segoe UI Variable Text` family (static `Segoe UI` where it is missing) with the interface locale, and one custom DirectWrite fallback, chosen by the text's locale, maps ja-JP text (the Japanese interface, or any name with kana) to the bundled Reel Sans JP and then Yu Gothic UI, and maps Han and CJK punctuation in other text to `Microsoft YaHei UI`, so Chinese names keep mainland forms and Japanese names Japanese ones (`src/typography.cpp`). `AppDisplayName` localizes recognized Notepad and Windows Settings default labels at the rendering boundary, including historical Chinese names. It does not mutate `AppTarget`, launch fields, or persisted preferences. Every language passes the same stored target and name into icon identification; menu action labels are localized separately from their invariant `reel:` action IDs. The editor also presents built-in labels in its current language, and its initialization guard prevents presentation changes from becoming edits.

One active profile supplies apps, ordering, favorite, and language. The application and diagnostic paths come from `ConfigPath`; language does not choose a path. Windows packaging redirects an installed app's `%LOCALAPPDATA%` into its own LocalCache scope, independently of standalone development files; `ConfigPath` asks for the redirection target, so the package reads and replaces its files there directly and a temporary file never crosses a volume on its way to the configuration.

## Configuration and activation

The native editor supports 1-8 targets. Main contains configured apps and a single +. The + opens the combined hub, ordered Language, Appearance, Reel size, Tray icon, Manage apps, Add app, and About. Language displays EN/ZH/JA at the top tile with flanking 40 DIP arrow hit regions, or A/D: the later arrow and D step forward through English, Simplified Chinese and Japanese, the earlier arrow and A step back. Reel size shows its step (70%, 85%, 100%, 115%) in the tile; each confirm saves the next step in `preferences.ini` and places the open reel again around the point it opened at (`NextReelSize`, `PlaceReel`). In Manage, dragging an app's tile reorders the reel and saves at once, keeping the preferred agent (`DropSlot`, `MoveEntry`); dropping it on the center trash, Delete, or Remove from reel on its actions page parks it in `parked.ini` with its settings (`Park`). An app's actions page shows the app itself at the top (choosing it returns to Manage), Edit app, Set as preferred, and Remove from reel. Add lists the parked apps first (one tap puts one back as saved), then the installed agents that are not in the reel yet (one tap adds them), then catalog entries that still need a target, then Custom. Discovery runs on a worker thread after startup and again whenever the hub or Manage opens; it runs synchronously only when seeding a first profile or migrating a website default needs it. Appearance uses sun/moon for the current system theme and opens ms-settings:colors. Tray icon (`MenuAction::TrayIcon`) switches the notification-area icon between color and monochrome, as the tray menu does (`ToggleTrayIcon`), and stays selected; its tile is the reel's plated mark (`reel:tray-icon:color`) or the mark alone in the glyph color (`reel:tray-icon:monochrome`, drawn by `DrawOrbitMark` in `src/orbit_mark.h`, which the monochrome tray icon shares). The center chevron or Esc returns one level. Missing presets open the focused native editor with an empty launch target. The editor's **Choose app…** page lists Start's All apps (`InstalledApps`, `src/installed_apps.*`): a worker thread enumerates `shell:AppsFolder`, keeps packages (AUMID parsing names) and shortcuts to an existing absolute `.exe` (`StartEntryApp`, which reads `System.Link.TargetParsingPath` and `System.Link.Arguments`), then posts each Start icon as it is extracted. The worker hands the list and the icons over through an inbox both sides own (`InstalledInbox`) and posts only a bare notification, so nothing it produced is lost when the editor closes first; a closed inbox stops it. The list scrolls by pixels through `ListViewport` (`src/list_viewport.*`): the LISTBOX is one row taller than its clipping viewport, its top index shows the whole rows of the offset and its position the rest, so its height never grows with the number of apps. See [configuration and isolation](configuration.md).

About (`MenuPage::About`, depth 2 like Manage apps) is a radial page of five tiles: the reel's own mark named by the version, GitHub's mark named by the author (opens the repository), two switches for automatic update checks and installs, and the update tile. The switches act in place and are saved in `preferences.ini` `[Updates]`. The update tile checks, or installs the update a check found; while one waits it and the hub's About tile carry the badge dot (`TileBadge::Dot`).

Updates come from the GitHub releases of `bakasayaka/copilot-reel` (`Updater`, `src/updates.*`). Nothing is pushed: the reel asks. Two minutes after a start and then hourly, a tick checks when automatic checks are on and the last completed check is a day old; About checks at once. A check reads `GET /repos/bakasayaka/copilot-reel/releases` (prereleases included) through `Windows.Web.Http`, which follows the user's proxy, and offers the newest release above the running package version that has `CopilotReel_<version>_<architecture>.msix`. An install downloads it into the package's real LocalCache folder (`Updates\`), requires the size and GitHub's SHA-256 digest to match, the signature to verify with Windows trust and come from a pinned thumbprint (`kTrustedSigners`), and the manifest identity to be this package's name, publisher and architecture at the offered, higher version. Then it registers `--background` for restart and hands the file to `PackageManager.AddPackageAsync` with `ForceApplicationShutdown`; Windows closes the reel (`WM_ENDSESSION` with `ENDSESSION_CLOSEAPP`), updates it and starts it again. The call returns only when the update did not happen. A reel that started less than 70 seconds ago waits first, since Windows restarts only a process that has run for a minute. Automatic installs wait until the reel is closed and no editor is open, and try each version once. A development build has no package and shows the switches and the update tile disabled. `--update-check` and `--update-install` ask a running reel to do the same.

Short tap first resolves a usable local target. Installed-agent discovery can replace an exact legacy default website profile with the matching verified native application. Before automatically saving a migration, the existing configuration is backed up. Only known ChatGPT and Claude base-URL default profiles are eligible; arbitrary pages and custom URLs are not migrated. A missing native destination opens the native application picker, restricted to executable or shortcut files, instead of a browser. Selecting a web profile as preferred uses the same picker. A successful choice replaces that launch profile with the chosen local target. Explicit custom web targets remain available to wheel selection, but never serve as a short-tap fallback.

Shell targets use `ShellExecuteExW`; AUMIDs use `IApplicationActivationManager`. Existing instances should use each target's activation contract. Windows foreground rules apply; cold, running, and minimized states need per-application verification.

## Current milestone

Compilation, unit tests, controller-to-test-launcher integration, native captures across light/dark, solid, the high-contrast code path and 100/150/200% scale, motion frame strips, and signed development installation succeeded. Actual Windows Settings recognition and the currently selected route were confirmed on build 26200.9711. Physical-key events, pathological cross-process scheduling, the live frosted backdrop over real content, real high-contrast themes, and performance acceptance remain open. The prototype is not fully validated.

References: [Windows themes](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes), [composition tailoring](https://learn.microsoft.com/en-us/windows/apps/develop/composition/composition-tailoring), [high contrast](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter).
