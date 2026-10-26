#include "tray_menu.h"
#include "vi_tokens.h"
#include <commctrl.h>
#include <d2d1.h>
#include <shellscalingapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

namespace reel {
namespace {
// uxtheme exports these by ordinal only (Windows 10 1903 and later). They are what makes
// Explorer's and Notepad's popup menus dark; without them a menu stays light on a dark desktop.
enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI*)();

COLORREF ToColorRef(const Rgba& color) {
    auto channel = [](float value) { return static_cast<BYTE>(std::clamp(value, 0.f, 1.f) * 255.f + .5f); };
    return RGB(channel(color.r), channel(color.g), channel(color.b));
}

// The first icon font installed with the code point, selected into `dc`; nullptr when none has it.
HFONT SelectIconFont(HDC dc, wchar_t glyph, int size) {
    for (const wchar_t* family : {vi::kIconFamily, vi::kIconFallbackFamily}) {
        HFONT font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
        if (!font) continue;
        SelectObject(dc, font);
        // GDI substitutes another face for a missing family; that face's glyph would be wrong.
        wchar_t face[LF_FACESIZE]{};
        WORD index = 0xFFFF;
        GetTextFaceW(dc, LF_FACESIZE, face);
        if (!_wcsicmp(face, family) && GetGlyphIndicesW(dc, &glyph, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == 1 &&
            index != 0xFFFF) return font;
        SelectObject(dc, GetStockObject(SYSTEM_FONT));
        DeleteObject(font);
    }
    return nullptr;
}

// A `size` pixel square, premultiplied 32 bpp bitmap of the glyph in `color`, as themed menus
// draw an item's hbmpItem with its alpha. The glyph is drawn white on black with grayscale
// antialiasing, so any channel is its coverage.
HBITMAP GlyphBitmap(wchar_t glyph, COLORREF color, int size) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    const auto previous = SelectObject(dc, bitmap);
    const auto previousFont = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    HFONT font = SelectIconFont(dc, glyph, size);
    if (font) {
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        RECT box{0, 0, size, size};
        DrawTextW(dc, &glyph, 1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        GdiFlush();
        auto* pixel = static_cast<DWORD*>(bits);
        for (int i = 0; i < size * size; ++i) {
            const DWORD coverage = pixel[i] & 0xFF;
            pixel[i] = coverage << 24 | (GetRValue(color) * coverage / 255) << 16 |
                (GetGValue(color) * coverage / 255) << 8 | GetBValue(color) * coverage / 255;
        }
    }
    SelectObject(dc, previousFont);
    if (font) DeleteObject(font);
    SelectObject(dc, previous);
    DeleteDC(dc);
    if (!font) { DeleteObject(bitmap); return nullptr; }
    return bitmap;
}
}

void ApplyMenuTheme(const Appearance& appearance) {
    static const auto setMode = [] {
        const HMODULE theme = GetModuleHandleW(L"uxtheme.dll");
        return theme ? reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(theme, MAKEINTRESOURCEA(135))) : nullptr;
    }();
    static const auto flush = [] {
        const HMODULE theme = GetModuleHandleW(L"uxtheme.dll");
        return theme ? reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(theme, MAKEINTRESOURCEA(136))) : nullptr;
    }();
    if (!setMode || !flush) return;
    // High contrast keeps the system menu; its colors come from the contrast theme.
    setMode(appearance.highContrast ? PreferredAppMode::Default :
        appearance.dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
    flush();
}

UINT ShowTrayMenu(HWND owner, POINT anchor, const Appearance& appearance,
                  std::initializer_list<TrayMenuItem> items) {
    ApplyMenuTheme(appearance);
    const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    UINT dpi = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &dpiY))) dpi = GetDpiForSystem();
    const int iconSize = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const COLORREF glyphColor = ToColorRef(appearance.foreground);

    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;
    std::vector<HBITMAP> bitmaps;
    for (const auto& item : items) {
        MENUITEMINFOW info{sizeof(info)};
        if (!item.command) {
            info.fMask = MIIM_FTYPE;
            info.fType = MFT_SEPARATOR;
        } else {
            info.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
            info.wID = item.command;
            info.dwTypeData = const_cast<wchar_t*>(item.text);
            info.fState = (item.isDefault ? MFS_DEFAULT : MFS_ENABLED) | (item.checked ? MFS_CHECKED : MFS_UNCHECKED);
            // Contrast themes invert the highlighted row, which a fixed-color bitmap cannot follow.
            if (item.glyph && !appearance.highContrast)
                if (HBITMAP bitmap = GlyphBitmap(item.glyph, glyphColor, iconSize)) {
                    info.fMask |= MIIM_BITMAP;
                    info.hbmpItem = bitmap;
                    bitmaps.push_back(bitmap);
                }
        }
        InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &info);
    }
    // Icons share the check column instead of adding a second gutter.
    MENUINFO style{sizeof(style)};
    style.fMask = MIM_STYLE;
    style.dwStyle = MNS_CHECKORBMP;
    SetMenuInfo(menu, &style);

    // The menu opens away from the taskbar edge the icon sits on.
    MONITORINFO bounds{sizeof(bounds)};
    GetMonitorInfoW(monitor, &bounds);
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY;
    flags |= anchor.y > (bounds.rcMonitor.top + bounds.rcMonitor.bottom) / 2 ? TPM_BOTTOMALIGN : TPM_TOPALIGN;
    flags |= anchor.x > (bounds.rcMonitor.left + bounds.rcMonitor.right) / 2 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    // A notification-area menu closes on an outside click only while its owner is in front.
    SetForegroundWindow(owner);
    const UINT choice = TrackPopupMenu(menu, flags, anchor.x, anchor.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    for (HBITMAP bitmap : bitmaps) DeleteObject(bitmap);
    return choice;
}

namespace {
// The Soft Orbit mark of the package icon (tools/generate-assets.py), in its 256-unit canvas:
// an outer ring of radius 92 and an inner one of 56, both 12 wide inside their radius, each
// with its own gap, and a dot on the outer ring. Without the plate the mark fills the icon.
constexpr float kMarkCenter = 128, kOuterRadius = 92, kInnerRadius = 56, kMarkStroke = 12;
constexpr float kOuterFrom = -30, kOuterTo = 280, kInnerFrom = 150, kInnerTo = 440;
constexpr float kDotAngle = -50, kDotRadius = 16;
constexpr float kMarkLeft = 36, kMarkExtent = 184;  // the outer ring's edge bounds the mark

// Coverage of the mark, `size` pixels square, as premultiplied BGRA from a software target.
std::vector<DWORD> MarkCoverage(int size) {
    using Microsoft::WRL::ComPtr;
    std::vector<DWORD> pixels;
    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return pixels;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return pixels;
    HDC dc = CreateCompatibleDC(nullptr);
    const auto previous = SelectObject(dc, bitmap);
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1DCRenderTarget> target;
    RECT bounds{0, 0, size, size};
    if (SUCCEEDED(factory->CreateDCRenderTarget(&properties, target.GetAddressOf())) &&
        SUCCEEDED(target->BindDC(dc, &bounds))) {
        // A one-pixel margin at 16 px; the stroke never falls below one pixel.
        const float margin = size / 16.f, scale = (size - 2 * margin) / kMarkExtent;
        const float stroke = std::max(kMarkStroke * scale, size / 16.f);
        auto point = [&](float radius, float degrees) {
            const float angle = degrees * 3.14159265f / 180;
            return D2D1::Point2F(margin + (kMarkCenter - kMarkLeft + radius * std::cos(angle)) * scale,
                                 margin + (kMarkCenter - kMarkLeft + radius * std::sin(angle)) * scale);
        };
        ComPtr<ID2D1SolidColorBrush> brush;
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0, 0, 0, 0));
        target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), brush.GetAddressOf());
        // Each ring's stroke lies inside its radius, as in the package icon.
        for (const auto& [radius, from, to] : {std::tuple{kOuterRadius, kOuterFrom, kOuterTo}, std::tuple{kInnerRadius, kInnerFrom, kInnerTo}}) {
            const float middle = radius - stroke / scale / 2;
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (brush && SUCCEEDED(factory->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(point(middle, from), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(point(middle, to), D2D1::SizeF(middle * scale, middle * scale), 0,
                    D2D1_SWEEP_DIRECTION_CLOCKWISE, to - from > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                target->DrawGeometry(path.Get(), brush.Get(), stroke);
            }
        }
        if (brush) target->FillEllipse(D2D1::Ellipse(point(kOuterRadius, kDotAngle), kDotRadius * scale, kDotRadius * scale), brush.Get());
        if (SUCCEEDED(target->EndDraw())) {
            const auto* source = static_cast<const DWORD*>(bits);
            pixels.assign(source, source + size * size);
        }
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
    return pixels;
}

bool TaskbarUsesLightTheme() {
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS && value != 0;
}
}

TrayIconStyle CurrentTrayIconStyle(bool monochrome) {
    TrayIconStyle style;
    style.monochrome = monochrome;
    style.size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
    if (monochrome) {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON);
        // The taskbar follows the Windows mode, not the app mode the reel draws in.
        style.color = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : TaskbarUsesLightTheme() ? RGB(0, 0, 0) : RGB(255, 255, 255);
    }
    return style;
}

HICON CreateTrayIcon(const TrayIconStyle& style) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!style.monochrome) {
        // The small size from the icon's own frames, not a 32 px frame squeezed by the Shell.
        HICON icon = nullptr;
        if (FAILED(LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(1), style.size, style.size, &icon)) || !icon)
            icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, style.size, style.size, 0));
        return icon;
    }
    auto pixels = MarkCoverage(style.size);
    if (pixels.empty()) return nullptr;
    // Icons take straight alpha: the mark is one color, so every covered pixel is that color.
    for (auto& pixel : pixels) {
        const DWORD alpha = pixel >> 24;
        pixel = alpha ? alpha << 24 | GetRValue(style.color) << 16 | GetGValue(style.color) << 8 | GetBValue(style.color) : 0;
    }
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), style.size, -style.size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    // A bitmap created without bits is undefined; an all-zero AND mask leaves the alpha in charge.
    const std::vector<BYTE> zeros(static_cast<size_t>((style.size + 15) / 16 * 2) * style.size, 0);
    HBITMAP mask = CreateBitmap(style.size, style.size, 1, 1, zeros.data());
    HICON icon = nullptr;
    if (color && mask) {
        std::copy(pixels.begin(), pixels.end(), static_cast<DWORD*>(bits));
        ICONINFO parts{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&parts);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}
}
