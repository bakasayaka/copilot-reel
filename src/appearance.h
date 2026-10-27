#pragma once
#include "app_types.h"
#include <cwctype>
#include <memory>
#include <optional>
#include <string_view>

namespace reel {
// Capture-only override from COPILOT_REEL_VISUAL_OVERRIDE, applied after the system
// values are read and never persisted:
// "theme=light|dark;material=solid|frosted;contrast=on|off;motion=on|off;dpi=<96..384>;taskbar=light|dark".
// Every key is optional; unknown keys and invalid values are ignored. `taskbar` stands in for the
// Windows mode, which the tray icon and the tray menu follow, as `theme` does for the app mode.
struct VisualOverride {
    std::wstring raw;   // empty when the variable is unset
    std::optional<bool> dark, frosted, highContrast, animations, taskbarLight;
    UINT dpi = 0;       // 0 follows the monitor
};

inline VisualOverride ParseVisualOverride(std::wstring_view text) {
    VisualOverride result;
    result.raw = text;
    auto trim = [](std::wstring_view value) {
        while (!value.empty() && std::iswspace(value.front())) value.remove_prefix(1);
        while (!value.empty() && std::iswspace(value.back())) value.remove_suffix(1);
        return std::wstring(value);
    };
    auto lower = [](std::wstring value) {
        for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
        return value;
    };
    while (!text.empty()) {
        const auto end = text.find(L';');
        const auto pair = text.substr(0, end);
        text = end == text.npos ? std::wstring_view{} : text.substr(end + 1);
        const auto equals = pair.find(L'=');
        if (equals == pair.npos) continue;
        const auto key = lower(trim(pair.substr(0, equals))), value = lower(trim(pair.substr(equals + 1)));
        auto flag = [&](const wchar_t* on, const wchar_t* off) -> std::optional<bool> {
            if (value == on) return true;
            if (value == off) return false;
            return std::nullopt;
        };
        if (key == L"theme") { if (auto v = flag(L"dark", L"light")) result.dark = v; }
        else if (key == L"material") { if (auto v = flag(L"frosted", L"solid")) result.frosted = v; }
        else if (key == L"contrast") { if (auto v = flag(L"on", L"off")) result.highContrast = v; }
        else if (key == L"motion") { if (auto v = flag(L"on", L"off")) result.animations = v; }
        else if (key == L"taskbar") { if (auto v = flag(L"light", L"dark")) result.taskbarLight = v; }
        else if (key == L"dpi" && !value.empty() && value.size() <= 3 &&
                 value.find_first_not_of(L"0123456789") == value.npos) {
            const UINT dpi = static_cast<UINT>(std::stoul(value));
            if (dpi >= 96 && dpi <= 384) result.dpi = dpi;
        }
    }
    return result;
}

// Parsed once from the process environment on first use.
const VisualOverride& CurrentVisualOverride();

class AppearanceSource {
public:
    explicit AppearanceSource(HWND target);
    ~AppearanceSource();
    AppearanceSource(const AppearanceSource&) = delete;
    AppearanceSource& operator=(const AppearanceSource&) = delete;
    // Resolves every role from the VI palettes, the live accent ramp and system state.
    Appearance Read();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
