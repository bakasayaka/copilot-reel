#pragma once

namespace reel {
// The interface languages, in the order the hub's language tile steps through them.
enum class Language { English, SimplifiedChinese, Japanese };
inline constexpr unsigned kLanguageCount = 3;

Language CurrentLanguage() noexcept;
// BCP 47 locale for text shaping, preferences and diagnostics.
inline const wchar_t* LocaleName(Language language) noexcept {
    return language == Language::SimplifiedChinese ? L"zh-CN" : language == Language::Japanese ? L"ja-JP" : L"en-US";
}
// The language one step later (+1) or earlier (-1) than `language`, wrapping.
inline Language StepLanguage(Language language, int direction) noexcept {
    const int count = static_cast<int>(kLanguageCount);
    return static_cast<Language>(((static_cast<int>(language) + direction) % count + count) % count);
}
void SetLanguage(Language language) noexcept;
const wchar_t* Tr(const wchar_t* english, const wchar_t* chinese, const wchar_t* japanese) noexcept;
// Missing or invalid preferences select English. Loading also sets the current language.
Language LoadLanguage() noexcept;
// Preferences are independent of apps.ini and use the same data-directory override.
bool SaveLanguage(Language language) noexcept;

// preferences.ini also holds the reel size: an index into vi::kReelSizeSteps, stored as its
// percentage. Missing or unknown values select vi::kReelSizeDefault. Loading also sets it.
unsigned CurrentReelSize() noexcept;
void SetReelSize(unsigned step) noexcept;
unsigned LoadReelSize() noexcept;
// Each save keeps the other preferences as they are now.
bool SaveReelSize(unsigned step) noexcept;
// The step's percentage, as stored and as the Reel size tile shows it.
unsigned ReelSizePercent(unsigned step) noexcept;

// And the notification-area icon: TrayIcon=monochrome draws the mark alone in the taskbar's
// foreground; anything else keeps the color icon. Loading also sets it.
bool CurrentTrayMonochrome() noexcept;
void SetTrayMonochrome(bool monochrome) noexcept;
bool LoadTrayMonochrome() noexcept;
bool SaveTrayMonochrome(bool monochrome) noexcept;

// preferences.ini's [Updates]: checking is on and installing off unless saved otherwise.
// lastCheck is when a check last completed, in Unix seconds, or 0.
struct UpdatePreferences { bool autoCheck = true, autoInstall = false; long long lastCheck = 0; };
UpdatePreferences CurrentUpdatePreferences() noexcept;
UpdatePreferences LoadUpdatePreferences() noexcept;
// Keeps the other preferences as they are now; sets the current preferences when saved.
bool SaveUpdatePreferences(const UpdatePreferences& preferences) noexcept;
}
