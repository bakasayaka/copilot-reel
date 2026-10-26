# Copilot Reel development

The [README](../README.md) is for people who use the reel; this page is for people who build it.

A native Windows application wheel that opens centered on the pointer when the Copilot key is held. Move toward an application and release to open it. While the reel is closed, a short Copilot tap directly opens the saved preferred local application.

The native implementation builds and runs. Soft Orbit rendering, Windows appearance synchronization, configuration, single-instance activation, and launching are implemented. A signed development MSIX has been verified as eligible in Windows Settings. The key route remains user-controlled. Current build, test, installation, and remaining physical-input/display/performance evidence is recorded in [validation](validation.md).

The current interface uses nested radial menus: configured agents and one + entry. The + opens a combined hub for language, system appearance, reel size, the tray icon, managing and adding apps, and About; an app's own actions set it as the preferred agent. EN/ZH/JA and sun/moon icons convey state without fixed captions. Hovering an app or command shows its concise name in the center, without an instructional second line. English is the default; Simplified Chinese and Japanese are native persistent options, and Japanese text uses the bundled Reel Sans JP (a renamed Noto Sans JP subset, see [visual identity](visual-identity.md#typography)). Version 0.3.4 (VI v0.9) brings the Windows 11 neutral plate with a flyout shadow, a concentric selection seat that slides along the orbit, unified official artwork (plated marks fill their tile; open marks share one optical size; system apps use their Shell icons), Segoe Fluent Icons commands, and drill-in page transitions in which the chosen tile becomes the back control. Version 0.3.5 gives every input one meaning in every menu (a tap or a new hold confirms inside a submenu), discovers installed agents for one-tap adding, lets an app move earlier or later in the reel from its actions page, brings an already running agent forward instead of starting it again, and starts in the background at sign-in. Version 0.3.6 draws the whole reel at 0.8 of its former size, reorders apps by dragging their tiles in Manage apps, and takes an app out of the reel by dropping it on the trash that appears in the center while a tile is dragged; the app keeps its settings and waits first on Add. Version 0.3.7 adds Reel size to the hub (70%, 85%, 100% and 115% of that size), a Choose app page in the app editor that lists Start's All apps, an aligned editor layout and a tray menu in the reel's light or dark theme. Version 0.3.8 adds About to the hub: the version and author, a link to the GitHub repository, and switches to check for and install updates from GitHub automatically (checking is on by default, installing off); the update itself is verified and installed by Windows, and the reel starts again. Version 0.3.9 adds Japanese with the bundled Reel Sans JP, a legibility floor for the reel's text (the name never below 10 device pixels, the monograms below 9, caption sizes in Segoe UI Variable Small, a 160 DIP name line), the three-language README and reproducible Release builds. Version 0.3.10 adds a monochrome tray icon chosen from the tray menu, keeps the tray icon's place on the taskbar across updates with a fixed GUID, and draws the generic glyph for an executable without an icon of its own. Version 0.3.11 adds Tray icon to the hub, which switches the tray icon between color and monochrome and shows the choice in its tile, and lets a reel that an edge pushed away from the pointer select by direction at once, without entering the center first. Version 0.3.12 paints every composition surface in a bitmap of its own before it goes into the shared atlas, so tiles and names keep their pixels whatever other surfaces are added or removed. Design values live in one token file shared by the application and the VI. Runtime validation is tracked separately from the visual specification.

## Behavior

- The official Windows provider contract supplies the approximately 300 ms hold threshold and start/stop events.
- The wheel opens without taking keyboard focus. Moving outside its center cancellation zone selects a direction.
- Releasing commits at most one target, starts the closing animation, and activates that target once.
- With the reel closed, a short Copilot tap opens the preferred local application; inside an open menu, a tap or a new hold-and-release confirms the selected item, and the center, Esc or right-click goes back. Installed-agent discovery resolves supported legacy website defaults to their native application. If no local target is available, the native application picker asks for an executable or shortcut; a tap never falls back to a website. Optional `Ctrl+Alt+R` opens a click-to-select preview.
- The + entry combines settings and management for 1-8 targets: executables, shortcuts, URIs, and packaged-app AUMIDs. Presets and app selection remain radial; a focused native editor handles text and launch fields.
- Recognized entries use verified official artwork, identified by their launch target, with one tile system: plated marks fill and are masked to the tile, open marks sit on a neutral tile at equal optical size, and Windows Settings, Notepad and custom apps use their own Shell icons. Unknown targets, and executables without an icon of their own (for which Windows would show its stock application icon), use a neutral app glyph. Vendor artwork is never recolored or redrawn; on the reel the Windows accent ramp colors only the selection seat and the badge disc (the preferred check and the update dot).
- Ordinary opening leaves the mouse at the center. Only boundary conflicts move the canvas inward; wherever that leaves the mouse, on the plate or outside it, it selects by direction as soon as it moves, without entering the center first. No connector or accent perimeter is drawn.
- The + hub stays radial. EN/ZH/JA changes in place through the flanking arrows or A / D. In Manage apps, drag an app's tile to another slot to reorder it, or onto the red trash in the center to take it out of the reel; A / D move the selected app and Delete takes it out. Add offers the apps taken out of the reel first, with their own settings, then installed agents that are not in the reel yet. The center chevron or Esc returns one level. Main has an empty idle center; hovered apps and commands show one concise name at the center.
- The Soft Orbit base follows Windows light/dark appearance. Transparency effects request a frosted backdrop when supported; otherwise the base is opaque. Selection uses the system accent. High contrast, disabled animations, and energy saver take precedence where applicable.

Targets inside the wheel do not need to register as Copilot providers. Only Copilot Reel registers with Windows.

## Build and preview

The two release architectures are Windows 11 x64 and Windows 11 ARM64. Both use the same source and the same Win32, Composition, Direct2D, and DirectWrite contracts. The package declares build 22621 as its minimum; this is not a claim that every Copilot hold-state feature has been verified on that build, and the ARM64 package has not been executed on ARM hardware.

```powershell
.\tools\bootstrap-toolchain.ps1
.\tools\build.ps1
.\tools\build.ps1 -Architecture arm64
.\build\Release\CopilotReel.exe --settings
.\build\Release\CopilotReel.exe --preview
```

Bootstrap downloads pinned Microsoft MSVC and Windows SDK packages for both architectures into the ignored `tools/local` directory, without installing an IDE or changing the machine PATH. The default build is x64 and produces `build/Release/CopilotReel.exe`. `-Architecture arm64` cross-compiles `build/arm64/Release/CopilotReel.exe` and its unit-test binaries, then checks that their PE machine is ARM64 (`0xAA64`). It does not run them on an x64 host. See [toolchain notes](toolchain.md).

The application uses C++20, Win32, Windows Composition, Direct2D, and DirectWrite. Geometry and text render at the target DPI on the device-pixel grid; official raster artwork is pre-scaled per DPI with high-quality cubic filtering. `design/vi/tokens.json` is the single source of design values: the build generates `src/vi_tokens.h` from it, and the VI generator reads it directly. PNG/PDF UI previews are reference material, never animation frames. State changes and compositor timelines drive rendering instead of a permanent application frame loop.

## Packaging and Windows integration

```powershell
.\tools\package.ps1
.\tools\package.ps1 -Architecture arm64
.\tools\verify-registration.ps1
```

A new version is typed in five files: `CMakeLists.txt`, `packaging/app.rc` (the executable's version resource, twice as numbers and twice as text), `packaging/app.manifest`, `packaging/AppxManifest.xml` and `design/vi/tokens.json` (`appVersion`, the build the VI tokens describe). `tools/check-versions.ps1` fails when any of the eight copies differs; `tools/test.ps1` runs it first, and `tools/package.ps1` runs it and refuses an executable whose file version is not the package's. About, the quick menu and diagnostics read the version at run time (`ExecutableVersion` in `src/updates.cpp`, or the package's), so no source file names it.

Packaging without a certificate produces an unsigned MSIX for inspection. The default package is `artifacts/CopilotReel_<version>_x64.msix`, staged in `artifacts/package`. `-Architecture arm64` stages `artifacts/package-arm64`, writes `ProcessorArchitecture="arm64"` into that package only, and leaves `packaging/AppxManifest.xml` as the x64 source manifest. To sign with an existing certificate whose subject matches the manifest publisher, pass `-CertificateThumbprint`. Neither packaging nor registration verification imports certificates or installs the package automatically. `tools/install-development.ps1` accepts only the x64 package, so an ARM64 package cannot replace the installed x64 app on this host. Development trust is machine-specific; the certificate authorized for this machine is recorded in local, ignored signing artifacts.

The package declares `com.microsoft.windows.copilotkeyprovider` and the private `copilot-reel:` protocol. Existing processes accept the HWND message contract; new instances forward commands to the resident process. Windows controls picker eligibility and the user's selected key binding. See [validation status](validation.md) for demonstrated behavior and remaining checks.

## Documentation

- [Architecture](architecture.md)
- [Provider research and evidence](research.md)
- [Validation status](validation.md)
- [Appearance and performance testing plan](testing-plan.md)
- [Configuration and isolated tests](configuration.md)
- [Native toolchain](toolchain.md)
- [Soft Orbit visual identity](visual-identity.md)
- [Read-only provider inventory](../tools/inspect-copilot-providers.ps1)

Development documentation, code comments, diagnostics, and commit messages use English. User-facing localization (English, Simplified Chinese, Japanese) is maintained separately.

## Tests and README media

`tools/test.ps1` is the one entry point for unit, smoke and regression tests (`-Tier unit|smoke|regression`; `-Tier installed` only compares the installed package with `build\Release` without building, and `-Installed` adds that comparison to another tier). Release builds are reproducible (`/Brepro`): one source tree gives one executable SHA-256. It writes every step's log and a `summary.json` under `artifacts/qa/test-<stamp>/` and exits 1 when a step fails. See [testing plan](testing-plan.md).

`tools/record-demos.py` records the README's GIFs and step stills in each README language through the test seam, and `tools/readme-art.py` draws its language pills, download buttons and the launch image; both write to `docs/readme/`. `tools/build-fonts.py` rebuilds `assets/fonts/` from Noto Sans JP. The still comparison and `tools/record-demos.py` need `build\Release\capture-window.exe`, which `tools/build-capture-tool.ps1` builds and `tools/build.ps1` does not.
