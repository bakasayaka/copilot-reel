// The CJK font fallback as DirectWrite resolves it: which family each character maps to, by
// locale, with and without the bundled Reel Sans JP faces next to the executable.
#include "typography.h"

#include <dwrite_3.h>
#include <winrt/base.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>

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
            const float dot = FloorDip(vi::kBadgeDotRadius, vi::kBadgeDotMinimumPx, dpi, vi::kBadgeRadius / vi::kBadgeDotRadius);
            Check(dot * px >= vi::kBadgeDotMinimumPx - 1e-3f && dot <= vi::kBadgeRadius, "the update dot keeps its floor under the check's disc");
        }
    Check(FloorDip(vi::kLabelSize, vi::kLabelMinimumPx, 10.f) == vi::kLabelSize * vi::kTextFloorMaxGrowth,
        "a raised size grows at most kTextFloorMaxGrowth times");
    Check(std::wstring(TextFamilyForPx(11.2f)) == vi::kTextSmallFamily && std::wstring(TextFamilyForPx(16.8f)) == vi::kTextFamily,
        "caption sizes use Segoe UI Variable's Small master");
    // The name line clears every seat that reaches the name's band, with the band at its largest.
    for (unsigned n = 1; n <= 9; ++n)
        for (unsigned k = 0; k != n; ++k) {
            const double angle = 2 * std::numbers::pi * k / n;
            const double x = vi::kOrbitRadius * std::sin(angle), y = -vi::kOrbitRadius * std::cos(angle);
            if (std::abs(y) < vi::kSeat / 2 + vi::kLabelHeight * vi::kTextFloorMaxGrowth / 2)
                Check(std::abs(x) - vi::kSeat / 2 >= vi::kLabelWidth / 2, "the name line clears the seats beside it");
        }
    std::cout << "Typography tests passed: " << checks << " checks; Japanese fall-through at U+" << std::hex << outside << ".\n";
}
