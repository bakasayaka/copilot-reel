#pragma once

#include "vi_tokens.h"
#include <windows.h>
#include <string>
#include <vector>

namespace reel {
inline constexpr wchar_t kWindowClass[] = L"CopilotReel.Controller";
inline constexpr wchar_t kAppName[] = L"Copilot Reel";
inline constexpr UINT kCopilotMessage = WM_APP + 1;
inline constexpr UINT kAppearanceMessage = WM_APP + 2;
inline constexpr UINT kConfigChangedMessage = WM_APP + 3;
inline constexpr UINT kTrayMessage = WM_APP + 4;
inline constexpr UINT kLanguageChangedMessage = WM_APP + 6;
// Hit testing uses the drawn geometry itself (design/vi/tokens.json), so a token edit moves both.
inline constexpr float kCanvasDip = vi::kCanvas;
inline constexpr float kCenterDip = kCanvasDip / 2.0f;
inline constexpr float kRadiusDip = vi::kPlateRadius;
inline constexpr float kDeadZoneDip = vi::kCenterZoneRadius;

enum class TargetKind { Shell, Aumid };
struct AppTarget {
    std::wstring name;
    TargetKind kind = TargetKind::Shell;
    std::wstring target;
    std::wstring arguments;
    std::wstring workingDirectory;
};

// A mark at a tile's upper right: the check of the preferred agent or of a switch that is on,
// or a dot for an update that is ready.
enum class TileBadge { None, Check, Dot };

// Where the in-app update stands. Unavailable: a development build has no package to update.
enum class UpdateStage { Idle, Checking, Current, Available, Installing, Failed, Unavailable };

struct Rgba { float r = 0, g = 0, b = 0, a = 1; };
// Resolved visual roles. Values come from design/vi/tokens.json (src/vi_tokens.h)
// and the live Windows accent ramp; defaults are the light solid theme.
// Roles with alpha < 1 composite over the plate.
struct Appearance {
    bool dark = false;
    bool transparency = false;   // frosted requested and permitted
    bool animations = true;
    bool highContrast = false;
    Rgba surface{.976f,.976f,.976f,1};        // solid plate
    Rgba foreground{.106f,.106f,.106f,1};     // primary text and command glyphs
    Rgba secondary{.365f,.365f,.365f,1};      // chevrons and the back chevron
    Rgba outline{.80f,.80f,.80f,1};           // opaque hairline for the native editor
    Rgba tile{1,1,1,1};                       // neutral tile fill
    Rgba tileStroke{0,0,0,.08f};              // 1 DIP tile hairline
    Rgba plateStroke{0,0,0,.08f};             // 1 DIP plate rim, inset
    Rgba ringIdle{0,0,0,.10f};                // idle center-zone ring
    Rgba accent{0,.404f,.753f,1};             // AccentDark1 (light) / AccentLight2 (dark)
    Rgba accentText{0,.243f,.573f,1};         // AccentDark2 (light) / AccentLight3 (dark)
    Rgba textOnAccent{1,1,1,1};               // white (light) / black (dark)
    Rgba selection{.878f,.918f,.953f,1};      // seat fill: Mix(surface, accent, seat tint)
    Rgba selectionText{.106f,.106f,.106f,1};  // glyphs and text on the seat
    Rgba critical{.769f,.169f,.11f,1};        // drag trash glyph and ring
    Rgba criticalFill{.769f,.169f,.11f,1};    // armed trash disc
    Rgba textOnCritical{1,1,1,1};             // glyph on the armed trash
    Rgba disabledText{.365f,.365f,.365f,1};   // high contrast: GRAYTEXT for disabled items and their names
    float shadowOpacity = .14f;               // 0 in high contrast
};
}
