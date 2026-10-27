# Appearance and performance testing plan

This plan started from the 0.3.4 application and VI v0.9 baseline. It combines deterministic appearance tests with native rendering and ETW/WPR measurements, and defines regression coverage for the proposed unified menu interaction. Existing results belong in [validation.md](validation.md); the work below is planned unless explicitly marked verified.

The product has two packages, x64 and ARM64, from one source tree. ARM64 is cross-compiled on the x64 development host and is not part of this host's runtime matrix. Further x64 appearance, interaction, and performance acceptance uses the layers in this plan. The existing pointer-injection suite remains historical evidence for 0.3.4; it is not the acceptance run for later x64 work.

Reviewed on 2026-10-20 against application commit `47a63e9` and VI commit `2bc3534`. No new Copilot Reel ETW recording, system-setting change, or interaction implementation was performed during this planning review.

## Existing coverage and gaps

| Existing component | What is covered | Remaining gap |
| --- | --- | --- |
| `tools/capture-v034.py` (0.3.4 evidence; `tools/capture-seam.py` replaced it from 0.3.5) | Seven variants covering light/dark solid and frosted requests, the high-contrast branch, and 96/144/192 DPI; native stills and motion strips | Mostly checks labels and absence of renderer errors. It does not assert every expected material/fallback or measure frame pacing. |
| `COPILOT_REEL_VISUAL_OVERRIDE` | Process-local theme, material, contrast, motion, DPI and taskbar (Windows mode) overrides; no persisted system changes | Accent colors, high-contrast palette, energy saver, device capabilities and creation failures are not injectable. The override is parsed once per process. |
| `AppearanceSource::Read` | Reads live Windows preferences and resolves visual roles | Collection and policy are combined. `contrast=on` still uses the current `GetSysColor` values. A forced frosted override is applied after the energy-saver decision and can re-enable transparency. |
| `OrbitRenderer::ApplyMaterial` | Real capability checks, DWM host-backdrop setup and solid fallback | Results still depend on the host GPU, DWM and actual creation outcomes. A successful request is not proof of visible background blur. |
| `CreateAcrylicBrush` | Production luminosity, tint and deterministic noise graph | It creates its own host-backdrop brush. The shell supplies the blurred background; the graph does not perform an additional blur. |
| Interaction and identity tests | Launch deduplication, placement, nested menus, local targets, language persistence and pixel invariance | Menu-open Copilot confirmation (covered through the test seam since 0.3.5) and comprehensive appearance transitions were not covered by the 0.3.4 behavior contract. |

The existing frosted HWND captures exclude the desktop host backdrop. The existing forced-contrast captures use the ordinary system palette. Preserve these as branch/capture evidence, not complete acrylic or real high-contrast acceptance.

## Three testing layers

### 1. Deterministic policy

Separate Windows collection from policy without creating another UI process. The following interfaces are proposed, not current APIs:

```cpp
SystemVisualState ReadSystemVisualState();
Appearance ResolveAppearance(const SystemVisualState& state);
MaterialPlan ResolveMaterial(const SystemVisualState& state,
                             const MaterialCapabilities& capabilities);
```

`SystemVisualState` should contain light/dark mode, transparency requested, animations enabled, high contrast, energy saver, the accent ramp and the complete contrast palette. `MaterialCapabilities` should distinguish supported effects, fast effects and software rendering. The renderer executes the plan and reports an actual creation failure separately.

Apply test overrides to the input snapshot before policy evaluation. A test that requests acrylic while also injecting high contrast or energy saver must still obtain solid. To test acrylic independently of the host's power state, explicitly inject `energySaver=false`; do not bypass the production precedence rules.

Record requested material, resolved material, effective material, fallback reason and palette source. Distinguish `HighContrast`, `EnergySaver`, `TransparencyDisabled`, `EffectsUnsupported`, `EffectsSlow`, `SoftwareDevice` and `CreationFailed`. Missing capabilities must have a documented conservative policy, not an assumed successful effect.

### 2. Deterministic native rendering

Make the acrylic graph accept a backdrop brush supplied by its caller. Production supplies the real host backdrop. Tests supply known color fields or a fixed preblurred image and use the same luminosity/tint/noise graph. This tests material composition, alpha and clipping; it does not test how DWM generates the blur.

Use fixed accent and high-contrast palettes, fixed artwork, the shared VI tokens and explicit DPI. Keep reference images and color/bounds assertions separate: font and GPU differences may require documented pixel tolerances, while geometry, state and fallback invariants remain exact. Do not update reference images automatically to make a failing test pass.

| Matrix area | Required cases | Acceptance |
| --- | --- | --- |
| Base and material | Light/dark solid; light/dark injected acrylic | Expected color roles, source kind, alpha and circular clipping; no accidental desktop dependency |
| High contrast | Black background/white foreground and inverse palette, each with explicit highlight colors | System-role colors replace decorative roles, no backdrop/shadow, readable selected and disabled states |
| Precedence | Transparency off, energy saver, high contrast, unsupported/slow effects, WARP, factory failure | Exact effective material and fallback reason; no silent successful-acrylic claim |
| Item states | Idle, hover, selected, disabled, preferred badge, a corner hold that selects by direction from off the plate, Back and EN/ZH/JA | Correct glyphs, labels, order, focus and hit geometry |
| Scale | 96/144/192 DPI plus geometry tests across the supported range | No clipped icons or labels; rendering and hit testing use the same final center |
| Reduced motion | Motion off and high contrast: open, selection, drill in/out, commit, cancel and reopen during exit | Correct final state; no surviving leaving page, delayed launch or orphaned seat expression. Existing implementation uses immediate paths or 1 ms animation durations. |
| Live changes | Solid/acrylic, normal/contrast, motion on/off, capability loss/recovery, device recreation; include changes during exit | Preserve menu/selection and recover once; no stale backdrop, invisible interactive items or reappearing exit content |
| Resource lifetime | Repeat identical appearance, stationary menu, hidden menu, rapid appearance changes | No unchanged-surface redraw loop; release obsolete resources and rebuild only when required |

Keep `COPILOT_REEL_DATA_DIR` isolated. Until override handling is refactored, start a fresh process per environment fixture because `CurrentVisualOverride()` caches its first value. A dynamic-change test should inject a new snapshot/event through a test seam, not mutate that cached environment variable.

### 3. Real Windows integration and performance

Retain a smaller real-system matrix for actual high-contrast themes, transparency and energy-saver notifications, DWM host backdrop, hardware capability changes and mixed displays. Use a controlled background window to assess the visible material over content. ETW measurements and visual captures answer different questions and should remain separate evidence.

Do not change the user's persistent settings to run deterministic tests. Real-system changes, when part of an authorized experiment, need the original values recorded and restored. Keep ASUS services, OLED/Pixel Shift, wallpaper ownership and unrelated display settings unchanged for Copilot tests.

## Reusing the ETW/WPR workspace

The existing local workspace is a separate checkout outside this repository. Its components are external development tools, not runtime dependencies of Copilot Reel. Paths below identify the reviewed installation; future wrappers must accept dependency locations rather than hard-code this machine.

| Component relative to that workspace | Reuse | Required adaptation |
| --- | --- | --- |
| `scripts/wallpaper/Rotation.wprp` | CPU sampled stacks, CSwitch/ReadyThread, process/module context, GPU, DWM, Win32k, DirectComposition and WPR markers | Derive a Copilot-specific profile and retain source provenance/hash. Configured system/event buffers total 256 MiB; File-mode output can still grow. |
| `Control-RotationTrace.ps1`, `Record-RotationTrace.ps1`, `Etw.Common.ps1` in that directory | Bounded worker, unique WPR instance, queued commands, status files, profile hash, automatic stop/save | Existing scripts are LPT-03-specific and use wallpaper paths, phases and names. Preserve them; adapt a separate wrapper with `CopilotReel-<run-id>`, explicit output paths and no wallpaper probe. |
| `Test-RotationTraceFile.ps1` and `RecorderControl.wpaProfile` | Actual WPAExporter decoding, marker-range resolution and loss checks | Parameterize the ETL and output path. Marker success in a worker log is insufficient until markers are decoded from the ETL. |
| `RotationGraphics.wpaProfile` | DWM frames, GPU execution packets and CPU scheduling tables | Its CPU view currently filters `dwm.exe`. A Copilot profile must retain DWM and add the application/process instance; do not overwrite the original profile. |
| `trace-code-bridge/adapters/dotnet` | TraceEvent 3.2.8 offline ETL-to-runtime export with process/time filters | Exports CPU sampled IPs and stacks only, not GPU packets, DWM frames, CSwitch, function parameters or generic Present events. |
| `trace-code-bridge` 0.2.0 | Offline address/lifetime/identity correlation and evidence-gap review | Current `index` consumes REA evidence. Prefer matching PDBs in WPA for our own source; any future PDB adapter must identify its source honestly. |

Verified reuse readiness on 2026-10-20: all 79 bridge unit tests passed; the existing ETL exporter CLI loaded; synthetic `correlate` and `review` completed. Evidence: `artifacts/qa/trace-bridge-reuse-20261020-200016/readiness.json` and adjacent synthetic outputs. These are tooling checks, not Copilot runtime or performance measurements. No new recorder was started.

The bridge reports version 0.2.0, but its current local checkout has no Git commit and contains untracked source. Before a measured run, retain a content-hashed source/tool snapshot; do not invent a revision pin. The reviewed `Rotation.wprp` SHA-256 is `DC8DE29936A9E28169095CDCF313A63B4FD87336001218B56D046799519CFF2F`.

## Capture and evidence contract

Use short, bounded File-mode recordings, initially 20-40 seconds per scenario. Keep an uninstrumented visual run and separate instrumented performance runs so screenshot capture does not silently change the measured compositor workload. Include an app-closed baseline and a resident-hidden baseline under the same display and power conditions.

Each run should retain:

- Run/scenario ID, application version, source revision or dirty-source snapshot, tokens hash, actual EXE/PDB hashes, compiler/linker flags and matched PDB identity.
- Actual process ID and creation time, HWND, monitor/DPI/refresh mode, power source, requested and effective appearance, injected-versus-system origin and capability/fallback details.
- ETL, profile hash, recorder/decoder versions, start/end markers, status/worker logs, dropped-event/buffer information, selected analysis interval and table exports.
- Application phase events, optional visual evidence, diagnostics, configuration isolation path and cleanup result. Store private traces and symbols under ignored `artifacts/perf/<run-id>`; do not package or upload them by default.

Use only the recorder instance owned by the run. Do not cancel another WPR session or discard an incomplete trace. Preserve timed stop, disk-space checks and failure evidence. Elevate only the bounded recorder when required; application/input fixtures stay at normal integrity. The current wallpaper controller's `-NoProbe` suppresses its unrelated probe, but its machine/path coupling still requires adaptation.

Before recording, verify profile/provider availability, recorder/exporter versions, available disk capacity and conflicting active sessions. Record unsupported capabilities or missing required events as untested/inconclusive, never as a performance pass.

The existing marker queue is serviced about every 100 ms. It is adequate for multi-second scenario boundaries, not input-to-frame latency or exact 83-250 ms animation timing. Add an application TraceLogging provider for precise phase events in the ETW clock domain. Proposed events include input received, selection changed, page transition requested, animation submitted/completed, material resolved/applied/fallback, device recovery and launch dispatch. Include session/gesture ID, page generation, selection and material reason; omit user paths and launch arguments unless required by a specifically scoped trace.

An animation-submitted or completion callback is not proof of presentation. Correlate it with the available composition/presentation evidence and record when the first-visible-frame boundary cannot be determined. Keep request, receipt, compositor completion and observed display timing distinct.

## Performance scenarios and measurements

| Scenario | Comparison and measurements |
| --- | --- |
| App closed / resident hidden | CPU/GPU baseline, wakeups, private memory and residual composition activity |
| Visible, stationary solid / acrylic | Incremental application and DWM cost; unchanged surface draws; material-specific activity |
| Repeated open/cancel and open/commit | First-visible latency where observable, tail latency, CPU ready delay, allocation/rebuild activity, exactly-once launch |
| Rapid selection, including reversal across 180 degrees | Seat retargeting, long-arc regressions, frame continuity and unnecessary artwork redraws |
| Drill in/out and preset paging | Page handoff, retained center, input continuity, old-page cleanup and queued-input correctness |
| Appearance and capability transitions | Fallback/recovery latency, resource reconstruction and absence of stale/invisible layers |
| 60/144/240 Hz where available, mixed DPI/refresh, AC/battery | Distribution of presentation intervals, missed opportunities and incremental cost under recorded hardware conditions |

Report sample count, run duration, median and tail distributions with comparable baselines; select acceptance budgets before comparing implementations. Nominal display periods are 16.67 ms at 60 Hz, 6.94 ms at 144 Hz and 4.17 ms at 240 Hz. They are reference intervals, not measured application frame times or automatic pass thresholds. Mark unavailable hardware modes as untested rather than simulating a physical refresh rate with a timer.

Windows Composition can continue animating with no application surface redraws. Inspect DWM/GPU as well as `CopilotReel.exe`. A DWM frame belongs to desktop composition and must not automatically be attributed to this wheel. Few or no application swap-chain Present events do not prove that a compositor animation failed to render.

For CPU attribution, add an optimized Profile/RelWithDebInfo build using `/O2`, debug information and matching PDB output, preserving release-equivalent optimization/link settings. Current Release does not emit a PDB. Do not benchmark an `/Od` Debug build as Release. Use PDB GUID+Age to match symbols; retain capture-time binary identity separately. A later on-disk hash cannot prove which bytes were loaded in an earlier trace.

Decode graphics/scheduling tables through WPAExporter and feed only CPU-location evidence to the bridge. Its `coverage.selected_samples` counts samples; sampled IP plus stack observations are not function calls. Exporter exit code 2 means reported loss/truncation, not an ordinary successful run. Do not use WPAExporter switches that suppress loss or time-ordering errors. No sample, no matching function, or an empty graphics table must remain a coverage limitation.

## Unified interaction regression

0.3.5 implements this contract (the user approved the change on 2026-10-21): every confirmation input goes through one action, and the short-tap shortcut applies only while the reel is closed. 0.3.4 entered click/preview mode after `Navigate`, ignored hold-start while a gesture was active and treated every Tap as preferred-app activation. The table below remains the regression contract.

| State | Behavior |
| --- | --- |
| Menu closed | Tap launches the preferred local app; HoldStart opens Main |
| Any menu open | Tap, HoldStop from a new valid hold, click and Return confirm the current selection; HoldStart continues on the current page |
| Confirm an application / container / value | Launch once and close / enter child and retain the menu / adjust and retain the current page |
| Back | Center Back, Esc or Backspace returns one level; root cancellation closes |
| Native editor or file picker open | Copilot events cannot launch the preferred app or confirm behind the dialog |

Test input parity at every depth; no-selection/disabled confirmations; duplicate and stale releases; no extra preferred launch while a menu is open; page-generation invalidation; old pointer/queued-click positions after transitions; A/D adjust slot 0 of the hub, while Left/Right adjust only when slot 0 is selected and otherwise move around the ring; on Manage apps A/D move the selected app and Delete parks it; a pointer drag on Manage apps (press, moves past the drag threshold, release on a slot, on the trash, or after Esc) and its capture loss; cancellation and focus restoration; appearance changes during navigation. One release must never both enter a container and execute a child item. Keep this state testing independent of animation completion.

ASUS's OSD provides a consistent select/confirm/back model across levels; macOS menus support different input methods, while Control Center distinguishes toggling from expanding details. These motivate the contract, not a claim that either platform specifies this radial implementation. Dwell-to-open submenus remain an optional experiment, not an accepted default.

## Interaction test seam

Interaction acceptance for later x64 work runs through a test seam in the one Release binary. It does not move the pointer, send input, take focus, or quit or restart the installed reel. A separate test binary would break the rule that the installed executable is the tested one (`tools/install-development.ps1` compares their hashes). `src/test_input.h` and `src/test_input.cpp` define the seam, `tests/reel_seam.py` drives it, and `tests/run-reel-seam.py` is the suite. `tests/run-radial.py` stays unchanged as the 0.3.4 pointer-injection evidence.

### Gate

`CurrentTestInput()` reads the environment and the package identity once per process. `ParseTestInputGate()` is pure and unit-tested in `tests/test_input_tests.cpp`.

| Process state | Result |
| --- | --- |
| `COPILOT_REEL_TEST_INPUT` unset or empty | Off. The production path reads nothing else. |
| Set, and the process has package identity (the installed app) | Off with reason `packaged`. The value is ignored, so a normal user cannot enable the seam. |
| Set and unpackaged, but the run id is not `[A-Za-z0-9-]{8,40}`, `COPILOT_REEL_DATA_DIR` is not an absolute drive or UNC path, or `COPILOT_REEL_TEST_POINTER` is not `x,y` | The process logs the reason and exits with 9 before the instance mutex. A misconfigured runner can never fall into the user's namespace. |
| Set, unpackaged and valid | Active. |

`COPILOT_REEL_TEST_POINTER` is the initial synthetic cursor in physical screen pixels, and negative coordinates are valid. The default is the center of the primary work area. Never persist these variables.

### What an active instance changes

- Namespace: the mutex and ready event get the suffix `.Test.<run id>` (`TestInstanceSuffix()`), and the window title is `Copilot Reel Test <run id>`. There is no tray icon, no Ctrl+Alt+R hotkey and no fast-path registration, so the installed instance stays resident and the Shell never routes to the test process. `COPILOT_REEL_TEST_TRAY=hidden` gives the instance a hidden tray icon (`NIS_HIDDEN`, by its window and uID, never the installed reel's GUID) so group `tray` can check the tray paths; a configuration notice on it changes only its tooltip, so nothing shows.
- Pointer: `CursorPosition()` returns the synthetic cursor instead of `GetCursorPos`, for command capture, selection and release alike. Placement, selection by direction, hit-test math and rendering are unchanged.
- Real input is disconnected. Raw Input is never registered, `WM_NCHITTEST` returns `HTTRANSPARENT`, and real mouse, key and Raw Input messages are dropped and counted. A preview records `activationRequested` instead of calling `SetForegroundWindow` or `SetFocus`.
- Updates: only an active instance honors `COPILOT_REEL_TEST_UPDATE_FEED` (a releases feed; plain HTTP is accepted from `127.0.0.1` only), `COPILOT_REEL_TEST_VERSION` (the version it updates from) and `COPILOT_REEL_TEST_UPDATE_DELAY` (milliseconds to its first automatic check; without it, it checks only when asked). Its installs stop after downloading and verifying the package (`updateVerified=1`) and never reach the package manager. Without a feed it has no updater, as a development build.
- Launches: an active instance with `COPILOT_REEL_TEST_LAUNCH=log` logs `Test launch logged: <name>` instead of opening a target, so `tools/record-demos.py` can show real web and app targets without opening any.
- Diagnostics add `testInput=on`, `testRun`, `realInputIgnored` and `activationRequested` only in a test instance. Every 0.3.5 instance also writes `held`, `menuNames` (the display names of the page on screen, so a runner finds items by name), `adjust`, `addCatalog` and `addInstalled` when `--diagnostics` is requested; 0.3.13 adds the tray menu as it would open (`trayMenu`, `trayMenuChecked`, `trayMenuTheme`, `trayTaskbar`) and the renderer's `pages built` and `seat appears` counts. The file stays in the local data directory.

### Channels

| Channel | Message | Payload | Purpose |
| --- | --- | --- | --- |
| Commands | `WM_COPYDATA`, dwData `0x5245454C` ('REEL') | Existing 24-byte `CommandPacket`: version 1, command (Background 0 to Diagnostics 8, `src/core.h`), FILETIME stamp, pointer | Opening, release, Tap, Quit and Diagnostics through the production decode and hold ordering, exactly as a second `CopilotReel.exe` sends them |
| Fast path | `PostMessage` `0x8001` | wParam 0 Tap, 1 HoldStart, 2 HoldStop | The Shell's registered path; stamped on receipt; uses the synthetic cursor |
| Test input | `WM_COPYDATA`, dwData `0x52545354` ('RTST') | 32-byte `TestPacket`: version 1, action, 64-bit sequence, screen point, virtual key, reserved 0 | Pointer, Click, RightClick, Key, and Press and Release (the two halves of a click, so Pointer moves between them drag) along the real input paths; a Release during a drag reaches the reel anywhere, as the real mouse capture does |

Test packets are decoded only while the seam is active. Inactive, truncated, future-version, unknown-action, out-of-range-key and nonzero-reserved packets return FALSE, like any unknown dwData. Test packets share the command FIFO, so a click can never overtake a queued release. Every test packet logs `Test input <seq> done; page=<n>; selected=<n>; active=<0|1>; drag=<n>; target=<none|trash|slot:n>` to `run.log` (`drag` is the lifted app's index, or -1 when no tile is lifted), and the driver waits on that line as its ordering barrier. A Key packet with virtual key 0 routes nowhere and is a pure barrier.

### Runner

Unit, smoke and regression runs go through one script, never through hand-run steps:

```text
powershell -File tools\test.ps1 [-Tier unit|smoke|regression|installed] [-Installed]
```

| Tier | Steps |
| --- | --- |
| `unit` | `tools/check-versions.ps1` (the eight version copies agree), then `tools/build.ps1`: Release x64 and every unit test |
| `smoke` | `unit`, then the seam preflight and groups `a` and `hub` |
| `regression` (default) | `unit`, every seam group, and `tools/capture-seam.py` against the reference stills |
| `installed` | No build: the installed package's executable equals `build\Release\CopilotReel.exe` and is running |

`-Installed` adds the `installed` check after another tier's build and tests. Release builds are reproducible (`/Brepro`), so rebuilding the same tree keeps the executable's SHA-256 and the check holds; when it fails, it also compares the staged `artifacts\package\CopilotReel.exe` and says whether the install or the build is out of date. Each step writes its log, and the run its `summary.json` (tier, commit, dirty tree, the built executable's SHA-256, each step's status, time and the runners' own evidence), under `artifacts/qa/test-<stamp>/`; the script exits 1 when any step fails. The seam suite can still run alone:

```text
uv run --python 3.14 python tests\run-reel-seam.py [--exe build\Release\CopilotReel.exe] [--groups a,b,...] [--list]
```

Before any driver code runs, a static self-check refuses to start if the driver or the runner names `SendInput`, `SetCursorPos`, `mouse_event` or `keybd_event`. The driver also binds user32 through an allowlist that contains no input, cursor or foreground API, and shell32 for `Shell_NotifyIconGetRect` alone, which only reads where a tray icon is. The runner also refuses a binary without the seam strings, because such a binary would start in the user's own namespace. Every group starts its own test instance with a fresh run id, its own data directory under `artifacts/qa/seam-<stamp>-<id>/`, three `launch_probe.exe` apps, and finds menu entries by display name through `menuNames` in the diagnostics, never by a fixed index. A preflight runs first, and the suite stops if it fails. It checks:

- a malformed run id and a relative data directory exit with 9;
- malformed packets are refused;
- the test instance reports `testInput=on` and registers neither Raw Input nor the fast path;
- a preview records its activation request without taking the foreground.

| Group | Checks |
| --- | --- |
| `a` | Opening hold: 0 px displacement at an interior point; sector release launches once; duplicate start and stop ignored; Escape cancels; eight edges and corners 2 px in leave the pointer outside the plate and clamp minimally; there the opening position selects nothing and a move selects by direction at once, without entering the center; holds 30 DIP inside the right and top edges leave it on the plate: an unmoved release (or a 3 DIP move) selects nothing and cancels, and a 10 DIP move selects that direction at once and launches it once; release during the 167 ms opening cancels without launching; a re-hold during a cancel or commit exit logs `interrupted=exit` (wall-clock timings are recorded, not asserted) |
| `b` | Unified confirmation: a closed-reel Tap launches the preferred probe once; release on `Apps and settings` opens a persistent hub in place; Tap with no selection does nothing; a new hold does not move the plate and its release confirms; duplicate release ignored; fast-path and protocol Tap confirm and never launch the preferred agent; Escape and right click go back one level, and Back at Main closes |
| `c` | Manage apps (0.3.6): the actions page starts with the app itself and has no reorder (A, D, Right, Delete and the old chevron spot change nothing); a press and release in place is a click; a drag (Press, Pointer moves, Release) shows the idle trash, opens the slot under the pointer and saves the order with a reflow, keeping the preferred agent and selecting the moved app; a drag back to its own slot or canceled by Escape saves nothing and its release is not a click; a drop on the armed trash parks the app in `parked.ini`; Add lists it first and one tap restores it with its arguments; A/D move the selected app and Delete parks it; Remove from reel parks too; with one app the trash is disabled |
| `d` | Add page, after the discovery that opening the hub starts: installed entries come first (`addInstalled`); choosing the first one saves it into `apps.ini` without launching, after which it appears on no Add page and `addCatalog` and `addInstalled` drop by one; `Custom app` plus Previous/Next within 8 entries on every page; the center returns to whichever page opened Add. On a host without discovered agents the add step is recorded as skipped |
| `e` | `SC_MINIMIZE` on an open or hidden reel never makes it iconic; a window made iconic without activation (`SW_SHOWMINNOACTIVE`) is restored before the next placement, which is exact |
| `fast-path`, `interior`, `hub`, `preferred`, `draws`, `cold` | Ported quiet-tier groups of `tests/run-radial.py`: the 0x8001 contract, an exact off-center interior placement, EN/ZH/JA hub hover names with Tab and the language stepping forward and back by keys, chevrons and the tile, Tray icon switching between color and monochrome in place (no page built and the seat never appearing again), Set as preferred on an app's actions page, stable surface draws, and cold tap, cold hold, protocol Tap through a second process in the test namespace, orphan release, and a bare start without arguments, which an unpackaged build classifies as a preview |
| `about` | About from the hub: the reel mark named by its version and GitHub's by the author, the two switches turning over in place (no page built, the seat never appearing again) and saved, and the updater against a local feed, whose installs a test instance verifies and stops |
| `tray` | On a hidden test icon: a profile broken at start sends its notice to the icon added before `Reload`; the tray menu lists its items in EN, ZH (单色图标) and JA (モノクロ アイコン) and checks Monochrome icon; with `taskbar=dark;theme=light` it opens dark with a white mark while the reel stays light, and the reverse; diagnostics and About show the executable's version; a `--smoke-test` instance adds its icon without the GUID, and the installed reel's GUID icon (`Shell_NotifyIconGetRect`) stays through the whole run |

`results.json` records per-group checks and errors, placements, interruption and hover samples, draw samples, foreground samples, `inputInjection: none`, whether the installed instance was resident and unchanged, the seam run ids, `realInputIgnored` and the binary hash.

### What stays outside the quiet tier

- Focus tier, not yet ported: the editor groups (Edit app, Custom app, a missing preset) and the native picker groups. The native editor and `GetOpenFileNameW` activate themselves. They stay out of the quiet suite until the editor can open without activation in seam mode.
- Real-input smoke, run only with the user's explicit consent and with the installed instance stopped:
  - Raw Input wake-up and `RIDEV_INPUTSINK` delivery while another app has focus, including Escape during a hold;
  - system `WM_NCHITTEST` and `WM_MOUSEACTIVATE` routing;
  - real foreground activation, `WM_KEYDOWN` with the auto-repeat filter, and focus restore on hide;
  - A/D under a Chinese IME;
  - cold-start capture of the real cursor;
  - `WM_DPICHANGED` across monitors;
  - clicks on the tray icon, the menu it opens, and the hotkey.
- Manual: the physical Copilot key, Shell fast-path discovery and the `AllowSetForegroundWindow` outcome.

Status on 2026-10-22: the seam unit tests pass in `tools/build.ps1`, and `tests/run-reel-seam.py` passed all 12 groups against the 0.3.5 build (`artifacts/qa/seam-20261022-114532-01aac4/`) while the installed reel stayed resident and the foreground never changed. `tools/capture-seam.py` exits 1 on any pixel difference or missing 0.3.4 reference; and added the App actions frames (both sides, first and last position) to `output/runtime/v0.3.5/` (`artifacts/qa/capture-seam-20261022-114552-32fb7d/`). Its 0.3.5 report of 0 differing pixels was wrong: it compared only alpha.

Status for 0.3.5.1: `tests/run-reel-seam.py` passed all 12 groups (`artifacts/qa/seam-20261022-143948-f00ae0/`), including holds that the right and top edges clamp with the pointer still on the plate. `tools/capture-seam.py` now compares every channel after a 1 s settle and allows 2 levels per channel (`--tolerance 0` for exact; every changed pixel is still counted). All 30 comparable stills are within that tolerance: the 96 and 144 DPI dark variants match exactly except the 144 DPI hub-language seat, and the rest differ by 1-2 levels in 0 to 492 pixels, mostly around the settled selection seat. It also walks every Add page and requires that tiles with different `menuArt` identities differ in a pixel and equal identities match, on one page and across all Add pages of a variant, because the renderer keeps cached artwork across pages (13 tiles on two pages here, 0 violations; `artifacts/qa/capture-seam-20261022-144013-4ac9e0/`). A build with the 0.3.5 target-keyed cache put back fails it with exactly the three pairs Antigravity, JetBrains IDE and Junie (`artifacts/qa/capture-seam-20261022-162819-1143e3/`, exit 1). Without `--output` the stills go to the run's evidence directory, so routine runs never rewrite the tracked `output/runtime/` captures.

## Implementation sequence

| Order | Deliverable | Exit condition | Status |
| --- | --- | --- | --- |
| 1 | Pure appearance/material policy with complete injected snapshots | Deterministic precedence, palette and fallback tests; no Windows mutation | Planned |
| 2 | Injectable acrylic source and native visual fixtures | Solid, synthetic-acrylic, contrast and reduced-motion matrix with explicit source provenance | Planned |
| 3 | Shared Confirm/Back/Adjust routing | Input parity across menu levels, no event penetration, no shortcut behind an editor | Implemented in 0.3.5 (`ReelInput`, `RouteKey`); unit-tested and covered by the seam runner |
| 4 | Optimized symbol-bearing build and TraceLogging phases | Matched artifacts, phase IDs and documented timing semantics | Planned |
| 5 | Copilot-specific WPR controller/profile/export views | Bounded owned session, decoded markers/loss checks, correct app and DWM filters | Planned; external tooling readiness verified |
| 6 | Controlled performance comparisons and real-system checks | Repeated baselines, separate visual evidence, recorded unknowns and hardware scope | Not run |

This documentation update is not an application release. When implementation produces a new release, retain the existing backup, uninstall, install, restore and installed-hash verification procedure.

## References

- [Current validation evidence](validation.md), [architecture](architecture.md), [configuration isolation](configuration.md), [toolchain](toolchain.md).
- [Microsoft Composition tailoring](https://learn.microsoft.com/en-us/windows/apps/develop/composition/composition-tailoring): user preferences, effect capabilities and cost.
- [Microsoft high-contrast state](https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.accessibilitysettings.highcontrast): system preference semantics.
- ASUS monitor user guides, the OSD menu section: hierarchical navigation and confirmation.
- [Apple menu tracking](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/MenuList/Articles/ViewsInMenuItems.html) and [Control Center](https://support.apple.com/guide/mac-help/quickly-change-settings-with-control-center-mchlc9d0e1f2/mac): menu tracking and distinct adjustment/expansion actions.
