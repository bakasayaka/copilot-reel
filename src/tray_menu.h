#pragma once
#include "app_types.h"
#include <optional>
#include <span>

// The notification-area icon and menu: a native popup menu drawn in the Windows mode's light or
// dark theme, as the taskbar's own menus are, with Segoe Fluent Icons beside its commands.
namespace reel {
struct TrayMenuItem {
    UINT command = 0;          // 0 is a separator
    const wchar_t* text = nullptr;
    wchar_t glyph = 0;         // Segoe Fluent Icons code point; 0 draws no icon
    bool isDefault = false;    // bold; the command a click on the icon runs
    bool checked = false;      // a check mark in the icon column
};

// The Windows mode (SystemUsesLightTheme), which the taskbar and its notification area follow;
// the app mode may differ.
bool TaskbarUsesLightTheme();

// Popup menus follow the process' preferred app mode: this sets it to the appearance's theme,
// so the editor's context menus match the reel. High contrast keeps the system menus.
void ApplyMenuTheme(const Appearance& appearance);

// The appearance the tray menu opens in: `appearance` with the Windows mode (`taskbarLight`) in
// place of the app mode, as the taskbar's own menus are drawn.
Appearance TrayMenuAppearance(const Appearance& appearance, bool taskbarLight);

// Shows `menu` at `anchor` with TrackPopupMenu's `flags` and returns the chosen command, or 0.
using TrackTrayMenuFn = UINT (*)(HMENU menu, UINT flags, POINT anchor, HWND owner);

// Shows the menu at `anchor` (screen pixels, the icon's anchor point) and returns the chosen
// command, or 0. `owner` must be the window that receives the icon's messages. The menu takes the
// Windows mode (`taskbarLight`) rather than the app mode in `appearance`, which is applied again
// once it closes; high contrast shows no icons. `track` shows the built menu: TrackPopupMenu from
// the notification area by default, a function that only reads the menu in the tray tests.
UINT ShowTrayMenu(HWND owner, POINT anchor, const Appearance& appearance, bool taskbarLight,
                  std::span<const TrayMenuItem> items, TrackTrayMenuFn track = nullptr);

// What the notification-area icon looks like now; the icon is redrawn only when this changes.
struct TrayIconStyle {
    bool monochrome = false;
    int size = 16;             // pixels: the small-icon size at the system DPI
    COLORREF color = 0;        // the monochrome mark's color
    bool operator==(const TrayIconStyle&) const = default;
};
// The packaged color icon, or the Soft Orbit mark alone in the taskbar's foreground, at the
// small-icon size for `dpi`: white on a dark taskbar, black on a light one, `contrastText` while
// a contrast theme is on, as the system's own notification-area icons are drawn.
TrayIconStyle TrayIconStyleFor(bool monochrome, UINT dpi, bool taskbarLight, std::optional<COLORREF> contrastText);
// The same at the primary monitor's current DPI, with the current contrast theme.
TrayIconStyle CurrentTrayIconStyle(bool monochrome, bool taskbarLight);
// The caller owns the icon (DestroyIcon). nullptr when it could not be created.
HICON CreateTrayIcon(const TrayIconStyle& style);
}
