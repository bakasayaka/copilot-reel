// The CJK font fallback as DirectWrite resolves it: which family each character maps to, by
// locale, with and without the bundled Reel Sans JP faces next to the executable. Then the
// reel's text sizes, and whether every name the reel gives a tile fits the center's one line.
#include "typography.h"
#include "agent_catalog.h"
#include "radial_menu.h"

#include <dwrite_3.h>
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace {
using winrt::com_ptr;
unsigned checks = 0;

void Check(bool okay, const char* label) {
    ++checks;
    if (!okay) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}

// A single run of text with one locale, as MapCharacters reads it.
class Source final : public IDWriteTextAnalysisSource {
public:
    Source(std::wstring text, const wchar_t* locale) : text_(std::move(text)), locale_(locale) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (id == __uuidof(IUnknown) || id == __uuidof(IDWriteTextAnalysisSource)) { *object = this; return S_OK; }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text, UINT32* length) override {
        *text = position < text_.size() ? text_.data() + position : nullptr;
        *length = position < text_.size() ? static_cast<UINT32>(text_.size() - position) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text, UINT32* length) override {
        *text = position > 0 && position <= text_.size() ? text_.data() : nullptr;
        *length = position <= text_.size() ? position : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length, const WCHAR** locale) override {
        *length = static_cast<UINT32>(text_.size()) - position;
        *locale = locale_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) override {
        *length = static_cast<UINT32>(text_.size()) - position;
        *substitution = nullptr;
        return S_OK;
    }
private:
    std::wstring text_;
    const wchar_t* locale_;
};

struct Mapped { std::wstring family; DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL; bool covers = false; };

// The font the fallback picks for the first character of `text` under Segoe UI at `weight`.
Mapped Map(IDWriteFontFallback* fallback, IDWriteFontCollection* system, const std::wstring& text, const wchar_t* locale,
           DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD) {
    Source source(text, locale);
    UINT32 length = 0;
    com_ptr<IDWriteFont> font;
    FLOAT scale = 1;
    winrt::check_hresult(fallback->MapCharacters(&source, 0, static_cast<UINT32>(text.size()), system, L"Segoe UI", weight,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &length, font.put(), &scale));
    Mapped mapped;
    if (!font) return mapped;
    com_ptr<IDWriteFontFamily> family;
    com_ptr<IDWriteLocalizedStrings> names;
    winrt::check_hresult(font->GetFontFamily(family.put()));
    winrt::check_hresult(family->GetFamilyNames(names.put()));
    UINT32 index = 0, size = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &index, &exists);
    winrt::check_hresult(names->GetStringLength(exists ? index : 0, &size));
    mapped.family.resize(size + 1);
    winrt::check_hresult(names->GetString(exists ? index : 0, mapped.family.data(), size + 1));
    mapped.family.resize(size);
    mapped.weight = font->GetWeight();
    const UINT32 first = text[0] >= 0xD800 && text[0] < 0xDC00 ? 0x10000 + ((text[0] - 0xD800) << 10) + (text[1] - 0xDC00) : text[0];
    BOOL covers = FALSE;
    font->HasCharacter(first, &covers);
    mapped.covers = covers != FALSE;
    return mapped;
}

bool Has(IDWriteFontCollection* collection, const wchar_t* familyName, UINT32 character) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    com_ptr<IDWriteFontFamily> family;
    com_ptr<IDWriteFont> font;
    if (FAILED(collection->FindFamilyName(familyName, &index, &exists)) || !exists ||
        FAILED(collection->GetFontFamily(index, family.put())) ||
        FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put())))
        return false;
    BOOL has = FALSE;
    font->HasCharacter(character, &has);
    return has;
}

std::string Utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// The width of `text` in DIP as the renderer lays it out at `dip` on a reel drawn at `dpi`
// (renderer.cpp, MakeTextStyle and DrawCentered): the optical master for the drawn pixels, the
// interface locale, the CJK fallback, and ja-JP for a name with kana. DirectWrite trims a
// line wider than its box.
float Width(IDWriteFactory5* factory, IDWriteFontCollection* system, IDWriteFontFallback* fallback,
            const std::wstring& text, reel::Language language, float dip, float dpi) {
    using namespace reel;
    const wchar_t* family = vi::kTextFallbackFamily;
    for (const wchar_t* name : {TextFamilyForPx(dip * dpi / 96), vi::kTextFamily, vi::kTextFallbackFamily}) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(system->FindFamilyName(name, &index, &exists)) && exists) { family = name; break; }
    }
    com_ptr<IDWriteTextFormat> format;
    winrt::check_hresult(factory->CreateTextFormat(family, nullptr, static_cast<DWRITE_FONT_WEIGHT>(vi::kTextWeight),
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dip, LocaleName(language), format.put()));
    winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
    winrt::check_hresult(format.as<IDWriteTextFormat1>()->SetFontFallback(fallback));
    com_ptr<IDWriteTextLayout> layout;
    winrt::check_hresult(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.get(),
        10000, dip * 4, layout.put()));
    if (HasKana(text)) winrt::check_hresult(layout->SetLocaleName(vi::kJapaneseLocale, {0, static_cast<UINT32>(text.size())}));
    DWRITE_TEXT_METRICS metrics{};
    winrt::check_hresult(layout->GetMetrics(&metrics));
    return metrics.widthIncludingTrailingWhitespace;
}

// Every name the reel itself gives a tile, in the current language: each page, the Add page
// with Previous and Next, App actions with Remove, and About at every update stage, its first
// tile named by a version as wide as 0.3.12. App and catalog names are the user's or the
// product's and are not included.
std::vector<std::wstring> ReelNames() {
    using namespace reel;
    const std::vector<AppTarget> apps{{L"Notepad", TargetKind::Shell, L"C:\\Windows\\notepad.exe"},
        {L"Paint", TargetKind::Shell, L"C:\\Windows\\System32\\mspaint.exe"}};
    const std::vector<AppTarget> presets(12, AppTarget{L"Preset", TargetKind::Shell, L"C:\\preset.exe"});
    std::vector<std::vector<MenuEntry>> menus{BuildMenu(MenuPage::Main, apps, presets), BuildMenu(MenuPage::Manage, apps, presets),
        BuildMenu(MenuPage::Presets, apps, presets, 5), BuildMenu(MenuPage::Settings, apps, presets),
        BuildMenu(MenuPage::Entry, apps, presets, 0, 0)};
    for (const auto stage : {UpdateStage::Idle, UpdateStage::Checking, UpdateStage::Current, UpdateStage::Available,
                             UpdateStage::Installing, UpdateStage::Failed, UpdateStage::Unavailable})
        for (const wchar_t* offered : {L"", L"0.3.12"})
            menus.push_back(BuildMenu(MenuPage::About, apps, presets, 0, -1, 0, AboutState{L"0.3.12", true, false, stage, offered}));
    std::vector<std::wstring> names;
    for (const auto& menu : menus)
        for (const auto& entry : menu) {
            const auto& visual = entry.visual;
            if (visual.target.starts_with(L"reel:") && std::find(names.begin(), names.end(), visual.name) == names.end())
                names.push_back(visual.name);
        }
    return names;
}
}

int main() {
    using namespace reel;
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    com_ptr<IDWriteFactory5> factory;
    winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
        reinterpret_cast<IUnknown**>(factory.put())));
    com_ptr<IDWriteFontCollection> system;
    winrt::check_hresult(factory->GetSystemFontCollection(system.put()));
    const std::wstring bundled = vi::kJapaneseFamily, yuGothic = vi::kJapaneseSystemFamily, yaHei = vi::kCjkFamily;

    Check(HasKana(L"メモ帳") && HasKana(L"ｱﾌﾟﾘ") && HasKana(L"ひらがな") && !HasKana(L"记事本") && !HasKana(L"設定") && !HasKana(L"Claude"),
        "kana, not Han or Latin, mark a name as Japanese");
    Check(std::wstring(TextLocale(L"メモ帳", Language::English)) == L"ja-JP" &&
        std::wstring(TextLocale(L"记事本", Language::SimplifiedChinese)) == L"zh-CN" &&
        std::wstring(TextLocale(L"設定", Language::Japanese)) == L"ja-JP", "a name with kana is laid out as Japanese");

    com_ptr<IDWriteFontFallback> fallback;
    winrt::check_hresult(CreateCjkFallback(factory.get(), ExecutableFolder(), fallback.put()));
    for (const auto* text : {L"漢字", L"あ", L"カ", L"。", L"ｱ", L"〆"}) {
        const auto mapped = Map(fallback.get(), system.get(), text, L"ja-JP");
        Check(mapped.family == bundled && mapped.covers, "Japanese text maps Han, kana and punctuation to Reel Sans JP");
    }
    Check(Map(fallback.get(), system.get(), L"漢", L"ja-JP", DWRITE_FONT_WEIGHT_SEMI_BOLD).weight == DWRITE_FONT_WEIGHT_SEMI_BOLD &&
        Map(fallback.get(), system.get(), L"漢", L"ja-JP", DWRITE_FONT_WEIGHT_NORMAL).weight == DWRITE_FONT_WEIGHT_NORMAL,
        "the reel's 600 and the quick menu's 400 each get their own face");
    for (const auto* locale : {L"zh-CN", L"en-US"}) {
        Check(Map(fallback.get(), system.get(), L"汉", locale).family == yaHei, "Han outside Japanese text keeps YaHei UI's mainland forms");
        Check(Map(fallback.get(), system.get(), L"あ", locale).family == bundled, "kana in any interface take Reel Sans JP");
    }
    // A kanji the subset leaves out continues to Yu Gothic UI instead of a missing glyph.
    UINT32 outside = 0;
    for (UINT32 c = 0x4E00; c <= 0x9FFF && !outside; ++c) {
        if (!Has(system.get(), yuGothic.c_str(), c)) continue;
        const auto mapped = Map(fallback.get(), system.get(), std::wstring(1, static_cast<wchar_t>(c)), L"ja-JP");
        Check(mapped.covers, "every kanji Yu Gothic UI has maps to a face that has it");
        if (mapped.family != bundled) {
            Check(mapped.family == yuGothic, "a kanji outside the subset falls through to Yu Gothic UI");
            outside = c;
        }
    }
    Check(outside != 0, "a kanji outside the subset exists to test the fall-through");

    com_ptr<IDWriteFontFallback> without;
    winrt::check_hresult(CreateCjkFallback(factory.get(), ExecutableFolder() / L"missing", without.put()));
    Check(Map(without.get(), system.get(), L"漢", L"ja-JP").family == yuGothic &&
        Map(without.get(), system.get(), L"汉", L"zh-CN").family == yaHei, "without the bundled faces Japanese text uses Yu Gothic UI");

    // The reel's text and the update dot scale with the reel and its size step down to a device-pixel floor.
    for (const float monitor : {96.f, 120.f, 144.f, 168.f, 192.f})
        for (const float step : vi::kReelSizeSteps) {
            const float dpi = std::round(monitor * vi::kReelScale * step), px = dpi / 96;
            const float label = FloorDip(vi::kLabelSize, vi::kLabelMinimumPx, dpi);
            Check(label >= vi::kLabelSize && label * px >= vi::kLabelMinimumPx - 1e-3f, "the center name never draws below its floor");
            Check(label == vi::kLabelSize || label * px <= vi::kLabelMinimumPx + 1e-3f, "a raised name stops at its floor");
            for (const float size : {vi::kMonogramSize, vi::kSizeMonogramSize})
                Check(FloorDip(size, vi::kMonogramMinimumPx, dpi) * px >= vi::kMonogramMinimumPx - 1e-3f, "monograms never draw below their floor");
            const float percentSize = FloorDip(vi::kSizePercentSize, vi::kMonogramMinimumPx, dpi, vi::kSizeMonogramSize / vi::kSizePercentSize);
            Check(percentSize * px >= vi::kMonogramMinimumPx - 1e-3f && percentSize <= FloorDip(vi::kSizeMonogramSize, vi::kMonogramMinimumPx, dpi),
                "the size step's % keeps the floor and never outgrows its digits");
            const float dot = FloorDip(vi::kBadgeDotRadius, vi::kBadgeDotMinimumPx, dpi, vi::kBadgeRadius / vi::kBadgeDotRadius);
            Check(dot * px >= vi::kBadgeDotMinimumPx - 1e-3f && dot <= vi::kBadgeRadius, "the update dot keeps its floor under the check's disc");
        }
    Check(FloorDip(vi::kLabelSize, vi::kLabelMinimumPx, 10.f) == vi::kLabelSize * vi::kTextFloorMaxGrowth,
        "a raised size grows at most kTextFloorMaxGrowth times");
    Check(std::wstring(TextFamilyForPx(11.2f)) == vi::kTextSmallFamily && std::wstring(TextFamilyForPx(16.8f)) == vi::kTextFamily,
        "caption sizes use Segoe UI Variable's Small master");
    // The name line clears every seat that reaches the name's band, with the band at its largest. A
    // seat's footprint takes in the badge ring beside its tile, and the seat and the name each snap to
    // whole device pixels, so together they may close up by one pixel at the smallest reel DPI.
    const float smallest = std::round(96 * vi::kReelScale * *std::min_element(std::begin(vi::kReelSizeSteps), std::end(vi::kReelSizeSteps)));
    const double reach = (std::max)(vi::kSeat / 2, vi::kBadgeOffset + vi::kBadgeRingRadius), snap = 96 / smallest;
    for (unsigned n = 1; n <= 9; ++n)
        for (unsigned k = 0; k != n; ++k) {
            const double angle = 2 * std::numbers::pi * k / n;
            const double x = vi::kOrbitRadius * std::sin(angle), y = -vi::kOrbitRadius * std::cos(angle);
            if (std::abs(y) < reach + vi::kLabelHeight * vi::kTextFloorMaxGrowth / 2 + snap)
                Check(std::abs(x) - reach - snap >= vi::kLabelWidth / 2, "the name line clears the seats and badges beside it");
        }

    // Every name the reel gives a tile fits the 160 DIP line in every language, at every size step
    // and display scaling: the floor raises the name in DIP at the small steps, so the 70% step at
    // 100% scaling, where the name draws at 10 px and 17.8 DIP, is the tightest. The monograms fit
    // their tile. Catalog names are reported where they trim, without failing: the reel cannot
    // shorten a product's name.
    std::map<std::wstring, std::string> trimmed;
    unsigned names = 0;
    for (const auto language : {Language::English, Language::SimplifiedChinese, Language::Japanese}) {
        SetLanguage(language);
        const auto reelNames = ReelNames();
        Check(reelNames.size() > 20, "the reel's own names are collected from every page");
        names += static_cast<unsigned>(reelNames.size());
        for (const float monitor : {96.f, 120.f, 144.f, 168.f, 192.f})
            for (unsigned step = 0; step != std::size(vi::kReelSizeSteps); ++step) {
                const float dpi = std::round(monitor * vi::kReelScale * vi::kReelSizeSteps[step]);
                const float label = FloorDip(vi::kLabelSize, vi::kLabelMinimumPx, dpi);
                const auto at = " at " + std::to_string(ReelSizePercent(step)) + "% on " + std::to_string(static_cast<int>(monitor)) + " DPI";
                for (const auto& name : reelNames) {
                    const float width = Width(factory.get(), system.get(), fallback.get(), name, language, label, dpi);
                    if (width > vi::kLabelWidth)
                        std::cerr << Utf8(name) << ": " << width << " of " << vi::kLabelWidth << " DIP" << at << '\n';
                    Check(width <= vi::kLabelWidth, "every name the reel gives a tile fits the center line");
                }
                const float monogram = FloorDip(vi::kMonogramSize, vi::kMonogramMinimumPx, dpi);
                for (const auto* text : {L"EN", L"ZH", L"JA"})
                    Check(Width(factory.get(), system.get(), fallback.get(), text, language, monogram, dpi) <= vi::kTile,
                        "the language monograms fit their tile");
                // The Reel size tile shows the step the reel is drawn at (P6): its digits as large as the
                // language monogram and a smaller % on their baseline, together inside the tile.
                const float digits = FloorDip(vi::kSizeMonogramSize, vi::kMonogramMinimumPx, dpi);
                const float percent = FloorDip(vi::kSizePercentSize, vi::kMonogramMinimumPx, dpi, vi::kSizeMonogramSize / vi::kSizePercentSize);
                Check(digits == monogram, "the size step's digits are as large as the language monogram");
                Check(Width(factory.get(), system.get(), fallback.get(), std::to_wstring(ReelSizePercent(step)), language, digits, dpi) +
                      Width(factory.get(), system.get(), fallback.get(), L"%", language, percent, dpi) <= vi::kTile,
                    "the size step's digits and % fit their tile");
                std::vector<std::wstring> reported;
                for (const auto& entry : catalog::kEntries) reported.emplace_back(entry.name);
                for (const auto& rule : catalog::kRules) reported.emplace_back(catalog::RowName(rule));
                for (const auto& name : reported)
                    if (const float width = Width(factory.get(), system.get(), fallback.get(), name, language, label, dpi);
                        width > vi::kLabelWidth && !trimmed.contains(name))
                        trimmed[name] = std::to_string(static_cast<int>(std::ceil(width))) + " DIP" + at;
            }
    }
    SetLanguage(Language::English);
    for (const auto& [name, where] : trimmed) std::cout << "Trims (reported, not failed): " << Utf8(name) << ", " << where << ".\n";
    std::cout << "Typography tests passed: " << checks << " checks; " << names << " reel names fit; Japanese fall-through at U+"
              << std::hex << outside << ".\n";
}
