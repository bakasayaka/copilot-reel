#include "tray_menu.h"
#include "orbit_mark.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// The notification-area icons, drawn into memory only: nothing is added to the taskbar.
// Linked with packaging/app.rc, so the color icon comes from the real package ICO.
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
    const auto color = reel::CurrentTrayIconStyle(false), mono = reel::CurrentTrayIconStyle(true);
    Check(!color.monochrome && mono.monochrome && color.size == mono.size && color.size >= 16,
        "the current style has the system's small-icon size");
    Check(mono.color == RGB(0, 0, 0) || mono.color == RGB(255, 255, 255) || mono.color == GetSysColor(COLOR_WINDOWTEXT),
        "the monochrome mark is black, white or the contrast theme's text color");
    Check(!(color == mono), "a style change redraws the icon");
    std::cout << "Tray icon tests passed: " << checks << " checks; monochrome on whole pixels and color at 16-64 px.\n";
    return 0;
}
