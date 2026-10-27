#include "typography.h"

#include <dwrite_3.h>
#include <winrt/base.h>
#include <vector>

namespace reel {
namespace {
using winrt::com_ptr;

std::vector<DWRITE_UNICODE_RANGE> CjkRanges() {
    std::vector<DWRITE_UNICODE_RANGE> ranges;
    for (const auto& range : vi::kCjkRanges) ranges.push_back({range.first, range.last});
    return ranges;
}

// Kana ranges not already inside a CJK range (halfwidth kana sit in the CJK forms block).
std::vector<DWRITE_UNICODE_RANGE> KanaRanges() {
    std::vector<DWRITE_UNICODE_RANGE> ranges;
    for (const auto& kana : vi::kKanaRanges) {
        bool covered = false;
        for (const auto& range : vi::kCjkRanges) covered |= kana.first >= range.first && kana.last <= range.last;
        if (!covered) ranges.push_back({kana.first, kana.last});
    }
    return ranges;
}

// A bundled family from its files, or nullptr when any of them cannot be read.
template <std::size_t N>
com_ptr<IDWriteFontCollection> BundledFonts(IDWriteFactory2* factory, const std::filesystem::path& root,
                                          const wchar_t* const (&paths)[N], const wchar_t* family) {
    com_ptr<IDWriteFactory5> modern;
    if (root.empty() || FAILED(factory->QueryInterface(__uuidof(IDWriteFactory5), modern.put_void()))) return nullptr;
    com_ptr<IDWriteFontSetBuilder> builder;
    if (FAILED(modern->CreateFontSetBuilder(builder.put()))) return nullptr;
    const auto files = builder.try_as<IDWriteFontSetBuilder1>();
    if (!files) return nullptr;
    for (const wchar_t* file : paths) {
        com_ptr<IDWriteFontFile> reference;
        if (FAILED(modern->CreateFontFileReference((root / file).c_str(), nullptr, reference.put())) ||
            FAILED(files->AddFontFile(reference.get()))) return nullptr;
    }
    com_ptr<IDWriteFontSet> set;
    com_ptr<IDWriteFontCollection1> collection;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(builder->CreateFontSet(set.put())) ||
        FAILED(modern->CreateFontCollectionFromFontSet(set.get(), collection.put())) ||
        FAILED(collection->FindFamilyName(family, &index, &exists)) || !exists) return nullptr;
    return collection;
}
}

bool HasKana(std::wstring_view text) noexcept {
    for (const wchar_t c : text)
        for (const auto& range : vi::kKanaRanges)
            if (c >= range.first && c <= range.last) return true;
    return false;
}

std::filesystem::path ExecutableFolder() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size()) { path.resize(length); break; }
        path.resize(path.size() * 2);
    }
    return std::filesystem::path(path).parent_path();
}

HRESULT CreateCjkFallback(IDWriteFactory2* factory, const std::filesystem::path& root, IDWriteFontFallback** fallback) noexcept {
    if (!factory || !fallback) return E_POINTER;
    *fallback = nullptr;
    try {
        com_ptr<IDWriteFontFallbackBuilder> builder;
        com_ptr<IDWriteFontFallback> system;
        HRESULT result = factory->CreateFontFallbackBuilder(builder.put());
        if (SUCCEEDED(result)) result = factory->GetSystemFontFallback(system.put());
        if (FAILED(result)) return result;
        const auto bundled = BundledFonts(factory, root, vi::kJapaneseFontFiles, vi::kJapaneseFamily);
        const auto bundledChinese = BundledFonts(factory, root, vi::kChineseFontFiles, vi::kChineseFamily);
        auto all = CjkRanges();
        const auto kana = KanaRanges();
        const auto cjk = CjkRanges();
        all.insert(all.end(), kana.begin(), kana.end());
        const wchar_t* japanese[] = {vi::kJapaneseFamily};
        const wchar_t* japaneseSystem[] = {vi::kJapaneseSystemFamily};
        const wchar_t* chinese[] = {vi::kChineseFamily};
        const wchar_t* chineseSystem[] = {vi::kCjkFamily};
        // Mappings apply in order: a character a mapping's font lacks continues to the next one.
        auto map = [&](const std::vector<DWRITE_UNICODE_RANGE>& ranges, const wchar_t** family, IDWriteFontCollection* collection,
                       const wchar_t* locale) {
            if (SUCCEEDED(result)) result = builder->AddMapping(ranges.data(), static_cast<UINT32>(ranges.size()), family, 1,
                collection, locale, nullptr, 1.0f);
        };
        if (bundled) map(all, japanese, bundled.get(), vi::kJapaneseLocale);
        map(all, japaneseSystem, nullptr, vi::kJapaneseLocale);
        if (bundled) map(kana, japanese, bundled.get(), nullptr);
        map(kana, japaneseSystem, nullptr, nullptr);
        if (bundledChinese) map(cjk, chinese, bundledChinese.get(), nullptr);
        map(cjk, chineseSystem, nullptr, nullptr);
        if (SUCCEEDED(result)) result = builder->AddMappings(system.get());
        if (SUCCEEDED(result)) result = builder->CreateFontFallback(fallback);
        return result;
    } catch (...) {
        return E_FAIL;
    }
}
}
