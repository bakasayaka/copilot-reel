#include "tray_menu.h"
#include "appearance.h"
#include "orbit_mark.h"
#include "vi_tokens.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// The notification-area icons, drawn into memory only, and the tray menu, built and read but never
// shown: nothing is added to the taskbar and no menu opens. Linked with packaging/app.rc, so the
// color icon comes from the real package ICO.
namespace {
unsigned checks = 0;
void Check(bool okay, const std::string& label) {
    ++checks;
    if (!okay) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}

struct Pixels { int size = 0; std::vector<DWORD> bgra; };
Pixels Read(HICON icon) {
    Pixels result;
    ICONINFO parts{};
    if (!icon || !GetIconInfo(icon, &parts)) return result;
    BITMAP bitmap{};
    GetObjectW(parts.hbmColor, sizeof(bitmap), &bitmap);
    result.size = bitmap.bmWidth;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), bitmap.bmWidth, -bitmap.bmHeight, 1, 32, BI_RGB};
    result.bgra.resize(static_cast<size_t>(bitmap.bmWidth) * bitmap.bmHeight);
    HDC dc = GetDC(nullptr);
    if (GetDIBits(dc, parts.hbmColor, 0, bitmap.bmHeight, result.bgra.data(), &info, DIB_RGB_COLORS) != bitmap.bmHeight)
        result.bgra.clear();
    ReleaseDC(nullptr, dc);
    DeleteObject(parts.hbmColor);
    DeleteObject(parts.hbmMask);
    return result;
}
unsigned Alpha(const Pixels& p, int x, int y) { return p.bgra[static_cast<size_t>(y) * p.size + x] >> 24; }

void Monochrome(int size, COLORREF color) {
    const std::string at = std::to_string(size) + " px " + (color ? "white" : "black");
    reel::TrayIconStyle style;
    style.monochrome = true;
    style.size = size;
    style.color = color;
    HICON icon = reel::CreateTrayIcon(style);
    const auto pixels = Read(icon);
    Check(icon && pixels.size == size && !pixels.bgra.empty(), at + ": the icon has the small-icon size");
    // The AND mask is all zero, so the alpha alone decides, on every drawing path.
    ICONINFO parts{};
    Check(GetIconInfo(icon, &parts) != 0, at + ": the icon has its parts");
    std::vector<BYTE> mask(static_cast<size_t>((size + 15) / 16 * 2) * size, 0xFF);
    Check(GetBitmapBits(parts.hbmMask, static_cast<LONG>(mask.size()), mask.data()) == static_cast<LONG>(mask.size()) &&
        std::all_of(mask.begin(), mask.end(), [](BYTE b) { return b == 0; }), at + ": the AND mask is clear");
    DeleteObject(parts.hbmColor);
    DeleteObject(parts.hbmMask);
    const DWORD rgb = GetRValue(color) << 16 | GetGValue(color) << 8 | GetBValue(color);
    size_t covered = 0;
    bool oneColor = true, clearEdge = true;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const DWORD pixel = pixels.bgra[static_cast<size_t>(y) * size + x];
            if (pixel >> 24) { ++covered; oneColor = oneColor && (pixel & 0xFFFFFF) == rgb; }
            else oneColor = oneColor && pixel == 0;
            // The outermost pixels lie in the margin; antialiasing may only graze them.
            if ((x == 0 || y == 0 || x == size - 1 || y == size - 1) && (pixel >> 24) > 48) clearEdge = false;
        }
    Check(oneColor, at + ": every pixel is the mark's one color in straight alpha, or clear");
    Check(clearEdge, at + ": the mark stays inside its margin");
    const double share = static_cast<double>(covered) / (static_cast<double>(size) * size);
    Check(share > .12 && share < .5, at + ": the rings and the dot cover a plausible share of the icon");
    // The rings sit on whole pixels: a margin and a stroke of 1/16 of the icon, rounded, so at the
    // top, bottom and sides the outer ring is `stroke` solid pixels with clear pixels on either side.
    const int margin = std::max(1, static_cast<int>(std::floor(size / 16.f + .5f))), stroke = margin, mid = size / 2;
    auto solid = [&](int x, int y) { return Alpha(pixels, x, y) >= 230; };
    auto clear = [&](int x, int y) { return Alpha(pixels, x, y) <= 25; };
    for (int k = 0; k != stroke; ++k)
        for (const int middle : {mid - 1, mid})
            Check(solid(middle, margin + k) && solid(middle, size - 1 - margin - k) && solid(margin + k, middle) &&
                solid(size - 1 - margin - k, middle), at + ": the outer ring is solid at the top, bottom and sides");
    Check(clear(mid, margin - 1) && clear(mid, size - margin) && clear(margin - 1, mid) && clear(size - margin, mid) &&
        clear(mid, margin + stroke) && clear(mid, size - 1 - margin - stroke) && clear(margin + stroke, mid) &&
        clear(size - 1 - margin - stroke, mid), at + ": the outer ring's edges fall on whole pixels");
    const auto mark = reel::MeasureOrbitMark(static_cast<float>(size - 2 * margin), size / 16.f);
    Check(mark.stroke == stroke && mark.outer == mid - margin, at + ": the mark fills the icon less its margin");
    auto polar = [&](float radius, float degrees) {
        const float angle = degrees * 3.14159265f / 180;
        return Alpha(pixels, static_cast<int>(std::floor(mid + radius * std::cos(angle))), static_cast<int>(std::floor(mid + radius * std::sin(angle))));
    };
    Check(polar(mark.outer - stroke / 2.f, 288) < 64, at + ": the outer ring opens at the upper right, beside the dot");
    Check(polar(mark.outer, -50) > 128, at + ": the dot sits on the outer ring at the upper right");
    const int inner = static_cast<int>(mark.inner);
    if (stroke >= 2) {
        Check(mark.innerRing && solid(mid + inner - 1, mid) && solid(mid, mid - inner), at + ": the inner ring runs at the right and the top");
        Check(polar(mark.inner - stroke / 2.f, 115) < 64, at + ": the inner ring opens at the lower left");
    } else {
        Check(!mark.innerRing && clear(mid + inner - 1, mid) && clear(mid, mid - inner), at + ": a one-pixel mark is the outer orbit and the dot");
    }
    Check(Alpha(pixels, mid, mid) == 0, at + ": the center is clear");
    DestroyIcon(icon);
}

// uxtheme's SetPreferredAppMode returns the mode it replaces, so setting one and putting the old
// one back reads the process' current mode.
enum class AppMode { Default, AllowDark, ForceDark, ForceLight, Unavailable };
AppMode CurrentAppMode() {
    using SetFn = AppMode(WINAPI*)(AppMode);
    static const auto set = reinterpret_cast<SetFn>(GetProcAddress(GetModuleHandleW(L"uxtheme.dll"), MAKEINTRESOURCEA(135)));
    if (!set) return AppMode::Unavailable;
    const AppMode mode = set(AppMode::Default);
    set(mode);
    return mode;
}

COLORREF ColorOf(const reel::vi::Color& color) {
    auto channel = [](float value) { return static_cast<BYTE>(std::clamp(value, 0.f, 1.f) * 255.f + .5f); };
    return RGB(channel(color.r), channel(color.g), channel(color.b));
}

// What the tray menu showed while it was open; filled by SeeMenu, which stands in for TrackPopupMenu.
struct SeenItem { UINT id = 0; bool separator = false, checked = false, isDefault = false; std::wstring text; bool glyph = false; COLORREF glyphColor = 0; };
struct SeenMenu { bool opened = false; AppMode mode = AppMode::Unavailable; std::vector<SeenItem> items; };
SeenMenu seen;
UINT SeeMenu(HMENU menu, UINT, POINT, HWND) {
    seen.opened = true;
    seen.mode = CurrentAppMode();
    for (int i = 0; i != GetMenuItemCount(menu); ++i) {
        wchar_t text[128]{};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_BITMAP;
        info.dwTypeData = text;
        info.cch = 128;
        GetMenuItemInfoW(menu, i, TRUE, &info);
        SeenItem item;
        item.id = info.wID;
        item.separator = (info.fType & MFT_SEPARATOR) != 0;
        item.checked = (info.fState & MFS_CHECKED) != 0;
        item.isDefault = (info.fState & MFS_DEFAULT) != 0;
        item.text = text;
        DIBSECTION section{};
        if (info.hbmpItem && GetObjectW(info.hbmpItem, sizeof(section), &section) == sizeof(section) && section.dsBm.bmBits) {
            // Premultiplied: a fully covered pixel carries the glyph's color as it is.
            const auto* pixel = static_cast<const DWORD*>(section.dsBm.bmBits);
            for (LONG k = 0; k != section.dsBm.bmWidth * std::abs(section.dsBm.bmHeight); ++k)
                if (pixel[k] >> 24 == 255) { item.glyph = true; item.glyphColor = RGB(pixel[k] >> 16 & 0xFF, pixel[k] >> 8 & 0xFF, pixel[k] & 0xFF); break; }
        }
        seen.items.push_back(item);
    }
    return 42;
}

// The tray menu takes the Windows mode while it is open and gives the process back the app mode,
// which the editor's context menus follow, once it closes.
void TrayMenu(bool appDark, bool taskbarLight, bool highContrast) {
    const std::string at = std::string("app ") + (appDark ? "dark" : "light") + ", Windows " + (taskbarLight ? "light" : "dark") +
        (highContrast ? ", high contrast" : "");
    reel::Appearance app;
    app.dark = appDark;
    app.highContrast = highContrast;
    const auto menu = reel::TrayMenuAppearance(app, taskbarLight);
    Check(menu.dark == !taskbarLight && menu.highContrast == highContrast && menu.animations == app.animations,
        at + ": the menu takes the Windows mode and keeps the rest of the appearance");
    const reel::TrayMenuItem items[] = {{101, L"Preview reel (click to open)", reel::vi::kGlyphTrayPreview, true},
                                        {}, {102, L"单色图标", 0, false, true}, {103, L"終了", reel::vi::kGlyphTrayExit}};
    seen = {};
    const UINT choice = reel::ShowTrayMenu(nullptr, {0, 0}, app, taskbarLight, items, SeeMenu);
    Check(seen.opened && choice == 42, at + ": the menu is built and handed to the tracker, whose choice is returned");
    const AppMode during = highContrast ? AppMode::Default : taskbarLight ? AppMode::ForceLight : AppMode::ForceDark;
    const AppMode after = highContrast ? AppMode::Default : appDark ? AppMode::ForceDark : AppMode::ForceLight;
    Check(seen.mode == during, at + ": while the menu is open, popup menus take the Windows mode");
    Check(CurrentAppMode() == after, at + ": once it closes, the editor's menus take the app mode again");
    Check(seen.items.size() == 4 && seen.items[1].separator && seen.items[0].id == 101 && seen.items[0].isDefault &&
        seen.items[0].text == items[0].text && seen.items[2].text == L"单色图标" && seen.items[2].checked && !seen.items[0].checked &&
        seen.items[3].text == L"終了", at + ": the items keep their order, text, default and check mark");
    const COLORREF glyph = ColorOf((taskbarLight ? reel::vi::kLight : reel::vi::kDark).foreground);
    if (highContrast)
        Check(!seen.items[0].glyph && !seen.items[3].glyph, at + ": high contrast shows no icons");
    else
        Check(seen.items[0].glyph && seen.items[0].glyphColor == glyph && seen.items[3].glyph && seen.items[3].glyphColor == glyph &&
            !seen.items[2].glyph, at + ": the icons take the Windows mode's foreground, light on a dark menu");
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Every small-icon size from 100% to 400% display scaling.
    for (int size : {16, 20, 24, 28, 32, 36, 40, 48, 56, 64}) {
        Monochrome(size, RGB(255, 255, 255));
        Monochrome(size, RGB(0, 0, 0));
        reel::TrayIconStyle color;
        color.size = size;
        HICON icon = reel::CreateTrayIcon(color);
        const auto pixels = Read(icon);
        Check(icon && pixels.size == size, std::to_string(size) + " px color: the package icon loads at the small-icon size");
        DestroyIcon(icon);
    }
    // P3: the package ICO's frames to 40 px follow the hub tile's rule, so its rings sit on whole
    // pixels: up to 24 px the outer orbit and the dot (one white run from the center to the edge of
    // the middle row), from 32 px both rings (two runs).
    for (const int size : {16, 20, 24, 32, 40}) {
        reel::TrayIconStyle color;
        color.size = size;
        HICON icon = reel::CreateTrayIcon(color);
        const auto pixels = Read(icon);
        int runs = 0;
        bool inside = false;
        for (int x = size / 2; x < size; ++x) {
            const DWORD p = pixels.bgra[static_cast<size_t>(size / 2) * size + x];
            const bool white = (p >> 24) == 255 && (p >> 16 & 0xFF) >= 200 && (p >> 8 & 0xFF) >= 200 && (p & 0xFF) >= 200;
            runs += white && !inside;
            inside = white;
        }
        Check(runs == (size <= 24 ? 1 : 2), std::to_string(size) + " px package icon: " +
            (size <= 24 ? "the outer orbit alone" : "both rings") + ", each a crisp white run on the plate");
        DestroyIcon(icon);
    }
    const bool taskbarLight = reel::TaskbarUsesLightTheme();
    const auto color = reel::CurrentTrayIconStyle(false, taskbarLight), mono = reel::CurrentTrayIconStyle(true, taskbarLight);
    Check(!color.monochrome && mono.monochrome && color.size == mono.size && color.size >= 16,
        "the current style has the system's small-icon size");
    Check(mono.color == RGB(0, 0, 0) || mono.color == RGB(255, 255, 255) || mono.color == GetSysColor(COLOR_WINDOWTEXT),
        "the monochrome mark is black, white or the contrast theme's text color");
    Check(!(color == mono), "a style change redraws the icon");
    // 100%, 125% and 150% display scaling: 16 and 20 px icons are the outer orbit and the dot, and
    // the inner ring comes in at 24 px, white on a dark taskbar and black on a light one.
    struct Scaling { UINT dpi; int size; bool innerRing; };
    for (const auto& scaling : {Scaling{96, 16, false}, Scaling{120, 20, false}, Scaling{144, 24, true}}) {
        const std::string at = std::to_string(scaling.dpi) + " DPI";
        const auto dark = reel::TrayIconStyleFor(true, scaling.dpi, false, std::nullopt),
                   light = reel::TrayIconStyleFor(true, scaling.dpi, true, std::nullopt),
                   contrast = reel::TrayIconStyleFor(true, scaling.dpi, false, RGB(255, 255, 0)),
                   packaged = reel::TrayIconStyleFor(false, scaling.dpi, false, std::nullopt);
        Check(dark.size == scaling.size && light.size == scaling.size && packaged.size == scaling.size && !packaged.monochrome,
            at + ": the icon has that scaling's small-icon size");
        Check(dark.color == RGB(255, 255, 255) && light.color == RGB(0, 0, 0) && contrast.color == RGB(255, 255, 0),
            at + ": the mark is white on a dark taskbar, black on a light one and the contrast theme's text color");
        const int margin = std::max(1, static_cast<int>(std::floor(scaling.size / 16.f + .5f)));
        Check(reel::MeasureOrbitMark(static_cast<float>(scaling.size - 2 * margin), scaling.size / 16.f).innerRing == scaling.innerRing,
            at + (scaling.innerRing ? ": the mark has both rings" : ": the mark is the outer orbit and the dot"));
        Monochrome(dark.size, dark.color);
    }
    // The capture override stands in for the Windows mode.
    Check(reel::ParseVisualOverride(L"theme=light;taskbar=dark").taskbarLight == false &&
        reel::ParseVisualOverride(L"theme=light;taskbar=dark").dark == false &&
        reel::ParseVisualOverride(L"taskbar=light").taskbarLight == true && !reel::ParseVisualOverride(L"taskbar=gray").taskbarLight,
        "taskbar=light|dark overrides the Windows mode alone");
    // The process' popup menus, the tray menu's and the editor's, follow uxtheme's preferred mode.
    LoadLibraryW(L"uxtheme.dll");
    Check(CurrentAppMode() != AppMode::Unavailable, "uxtheme exports SetPreferredAppMode");
    for (const bool appDark : {false, true})
        for (const bool taskbar : {false, true}) TrayMenu(appDark, taskbar, false);
    TrayMenu(false, false, true);
    std::cout << "Tray icon tests passed: " << checks << " checks; monochrome on whole pixels and color at 16-64 px, "
                 "100-150% scaling, and the tray menu's theme.\n";
    return 0;
}
