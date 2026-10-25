#pragma once
#include "app_types.h"
#include <initializer_list>

// The notification-area icon and menu: a native popup menu drawn in the reel's light or dark
// theme, with Segoe Fluent Icons beside its commands.
namespace reel {
struct TrayMenuItem {
    UINT command = 0;          // 0 is a separator
    const wchar_t* text = nullptr;
    wchar_t glyph = 0;         // Segoe Fluent Icons code point; 0 draws no icon
    bool isDefault = false;    // bold; the command a click on the icon runs
    bool checked = false;      // a check mark in the icon column
};

// Popup menus follow the process' preferred app mode: this sets it to the appearance's theme,
// so the tray menu and the editor's context menus match the reel. High contrast keeps the
// system menus.
void ApplyMenuTheme(const Appearance& appearance);

// Shows the menu at `anchor` (screen pixels, the icon's anchor point) and returns the chosen
// command, or 0. `owner` must be the window that receives the icon's messages. Applies the
// menu theme first; high contrast shows no icons.
UINT ShowTrayMenu(HWND owner, POINT anchor, const Appearance& appearance,
                  std::initializer_list<TrayMenuItem> items);

// What the notification-area icon looks like now; the icon is redrawn only when this changes.
struct TrayIconStyle {
    bool monochrome = false;
    int size = 16;             // pixels: the small-icon size at the system DPI
    COLORREF color = 0;        // the monochrome mark's color
    bool operator==(const TrayIconStyle&) const = default;
};
// The packaged color icon, or the Soft Orbit mark alone in the taskbar's foreground: white
// on a dark taskbar, black on a light one, the contrast theme's text color in high contrast,
// as the system's own notification-area icons are drawn.
TrayIconStyle CurrentTrayIconStyle(bool monochrome);
// The caller owns the icon (DestroyIcon). nullptr when it could not be created.
HICON CreateTrayIcon(const TrayIconStyle& style);
}
