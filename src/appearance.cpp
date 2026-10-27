#include "appearance.h"
#include "vi_tokens.h"
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Power.h>
#include <atomic>
#include <algorithm>

namespace reel {
namespace {
Rgba Color(COLORREF c) { return {GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f,1}; }
Rgba Color(winrt::Windows::UI::Color c) { return {c.R/255.f,c.G/255.f,c.B/255.f,1}; }
Rgba Color(const vi::Color& c) { return {c.r,c.g,c.b,c.a}; }
Rgba Mix(Rgba a,Rgba b,float amount) {
    return {a.r+(b.r-a.r)*amount,a.g+(b.g-a.g)*amount,a.b+(b.b-a.b)*amount,1};
}
std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD size=GetEnvironmentVariableW(name,nullptr,0);
    if(!size)return {};
    std::wstring value(size,L'\0');
    const DWORD copied=GetEnvironmentVariableW(name,value.data(),size);
    value.resize(copied<size?copied:0);
    return value;
}
struct CallbackState { std::atomic<HWND> window{nullptr}; };
}
const VisualOverride& CurrentVisualOverride() {
    static const VisualOverride value=ParseVisualOverride(EnvironmentValue(L"COPILOT_REEL_VISUAL_OVERRIDE"));
    return value;
}
struct AppearanceSource::Impl {
    winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
    winrt::event_token colors{}, effects{}, animations{}, power{};
    bool subscribedColors=false, subscribedEffects=false, subscribedAnimations=false, subscribedPower=false;
    std::shared_ptr<CallbackState> callback = std::make_shared<CallbackState>();
    explicit Impl(HWND hwnd) {
        callback->window.store(hwnd);
        auto notify=[state=callback](auto const&,auto const&) {
            if (HWND target=state->window.load()) PostMessageW(target,kAppearanceMessage,0,0);
        };
        try {
            settings=winrt::Windows::UI::ViewManagement::UISettings();
            colors=settings.ColorValuesChanged(notify); subscribedColors=true;
            effects=settings.AdvancedEffectsEnabledChanged(notify); subscribedEffects=true;
            animations=settings.AnimationsEnabledChanged(notify); subscribedAnimations=true;
            power=winrt::Windows::System::Power::PowerManager::EnergySaverStatusChanged(notify); subscribedPower=true;
        } catch (...) { /* Appearance::Read still provides a conservative solid fallback. */ }
    }
    ~Impl() {
        callback->window.store(nullptr);
        try { if(subscribedColors) settings.ColorValuesChanged(colors); } catch(...) {}
        try { if(subscribedEffects) settings.AdvancedEffectsEnabledChanged(effects); } catch(...) {}
        try { if(subscribedAnimations) settings.AnimationsEnabledChanged(animations); } catch(...) {}
        try { if(subscribedPower) winrt::Windows::System::Power::PowerManager::EnergySaverStatusChanged(power); } catch(...) {}
    }
};
AppearanceSource::AppearanceSource(HWND hwnd):impl_(std::make_unique<Impl>(hwnd)) {}
AppearanceSource::~AppearanceSource()=default;
Appearance AppearanceSource::Read() {
    using winrt::Windows::UI::ViewManagement::UIColorType;
    Appearance out;
    HIGHCONTRASTW hc{sizeof(hc)};
    out.highContrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags&HCF_HIGHCONTRASTON);
    BOOL enabled=TRUE;
    if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0)) out.animations=enabled!=FALSE;
    try {
        if(impl_->settings) {
            auto fg=impl_->settings.GetColorValue(UIColorType::Foreground);
            out.dark=(5*fg.G+2*fg.R+fg.B)>8*128;
            out.transparency=impl_->settings.AdvancedEffectsEnabled();
            out.animations=impl_->settings.AnimationsEnabled();
        }
        if(winrt::Windows::System::Power::PowerManager::EnergySaverStatus()==winrt::Windows::System::Power::EnergySaverStatus::On)
            out.transparency=false;
    } catch(...) { out.transparency=false; }
    const auto& visual=CurrentVisualOverride();
    if(visual.dark) out.dark=*visual.dark;
    if(visual.frosted) out.transparency=*visual.frosted;
    if(visual.highContrast) out.highContrast=*visual.highContrast;
    if(visual.animations) out.animations=*visual.animations;

    const auto& palette=out.dark?vi::kDark:vi::kLight;
    out.surface=Color(palette.plate); out.foreground=Color(palette.foreground);
    out.secondary=Color(palette.secondary); out.outline=Color(palette.outline);
    out.tile=Color(palette.tile); out.tileStroke=Color(palette.tileStroke);
    out.plateStroke=Color(palette.plateStroke); out.ringIdle=Color(palette.ringIdle);
    out.textOnAccent=Color(palette.textOnAccent);
    out.critical=Color(palette.critical); out.criticalFill=Color(palette.criticalFill);
    out.textOnCritical=Color(palette.textOnCritical);
    // Fluent fill and text accents come from the live ramp, never the raw Accent.
    out.accent=Color(palette.accentFallback); out.accentText=Color(palette.accentTextFallback);
    try {
        if(impl_->settings) {
            out.accent=Color(impl_->settings.GetColorValue(out.dark?UIColorType::AccentLight2:UIColorType::AccentDark1));
            out.accentText=Color(impl_->settings.GetColorValue(out.dark?UIColorType::AccentLight3:UIColorType::AccentDark2));
        }
    } catch(...) {
        out.accent=Color(palette.accentFallback); out.accentText=Color(palette.accentTextFallback);
    }
    if(out.transparency) out.tile.a=palette.frostedTileAlpha;
    out.selection=Mix(out.surface,out.accent,palette.seatTint);
    out.selectionText=out.foreground;
    out.shadowOpacity=palette.shadowOpacity;
    if(out.highContrast) {
        out.transparency=false;
        const auto window=Color(GetSysColor(COLOR_WINDOW)), text=Color(GetSysColor(COLOR_WINDOWTEXT));
        const auto highlight=Color(GetSysColor(COLOR_HIGHLIGHT)), highlightText=Color(GetSysColor(COLOR_HIGHLIGHTTEXT));
        out.surface=window; out.tile=window;
        out.foreground=out.secondary=out.outline=text;
        out.tileStroke=out.plateStroke=out.ringIdle=text;
        out.accent=out.accentText=out.selection=highlight;
        out.textOnAccent=out.selectionText=highlightText;
        // No contrast-theme role means destructive: the trash glyph carries it, armed like a selection.
        out.critical=text; out.criticalFill=highlight; out.textOnCritical=highlightText;
        out.disabledText=Color(GetSysColor(COLOR_GRAYTEXT));
        out.shadowOpacity=0;
    }
    return out;
}
}
