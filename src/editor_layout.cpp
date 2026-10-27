#include "editor_layout.h"

#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string_view>

namespace reel::editor {
int EnterTarget(const wchar_t* focusedClass, int focusedId, int primary) noexcept {
    return focusedClass && focusedId && _wcsicmp(focusedClass, L"Button") == 0 ? focusedId : primary;
}

int Px(int dip, UINT dpi) noexcept { return MulDiv(dip, static_cast<int>(dpi), 96); }

int ViewportHeight(int rows, int rowDip, UINT dpi) noexcept { return rows * Px(rowDip, dpi); }

float StrokeDip(float dip, UINT dpi) noexcept {
    return (std::max)(1.f, std::round(dip * dpi / 96.f)) * 96.f / dpi;
}

D2D1_RECT_F FrameRect(int widthPx, int heightPx, float strokeDip, UINT dpi) noexcept {
    const float scale = 96.f / dpi, inset = StrokeDip(strokeDip, dpi) / 2;
    return D2D1::RectF(inset, inset, widthPx * scale - inset, heightPx * scale - inset);
}

float WidestText(IDWriteFactory* factory, IDWriteTextFormat* format, std::initializer_list<const wchar_t*> texts) {
    float widest = 0;
    if (!factory || !format) return widest;
    for (const auto* text : texts) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        const auto length = static_cast<UINT32>(std::wstring_view(text).size());
        if (SUCCEEDED(factory->CreateTextLayout(text, length, format, 4096, 4096, &layout)) &&
            SUCCEEDED(layout->GetMetrics(&metrics)))
            widest = (std::max)(widest, metrics.widthIncludingTrailingWhitespace);
    }
    return std::ceil(widest);
}

Rgba Over(Rgba over, Rgba under) noexcept {
    const auto blend = [&](float top, float bottom) { return top * over.a + bottom * (1 - over.a); };
    return {blend(over.r, under.r), blend(over.g, under.g), blend(over.b, under.b), 1};
}

double Contrast(Rgba first, Rgba second) noexcept {
    const auto luminance = [](Rgba color) {
        const auto channel = [](double value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
        return .2126 * channel(color.r) + .7152 * channel(color.g) + .0722 * channel(color.b);
    };
    const double a = luminance(first), b = luminance(second);
    return ((std::max)(a, b) + .05) / ((std::min)(a, b) + .05);
}
}
