#include "artwork.h"
#include "iconography.h"
#include "local_launch.h"
#include "official_icons.h"
#include "orbit_mark.h"
#include "vi_tokens.h"

#include <appmodel.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace reel {
namespace {
using winrt::com_ptr;

// ---- Command glyphs: Segoe Fluent Icons, Segoe MDL2 Assets, then vector paths.

struct IconFont { com_ptr<IDWriteFontFace> face; bool attempted = false; };
struct FontGlyph {
    IDWriteFontFace* face = nullptr;
    UINT16 index = 0;
    D2D1_RECT_F ink{};  // design ink box in em, relative to the baseline origin, y down
    bool attempted = false;
};
com_ptr<IDWriteFactory> writeFactory;
std::array<IconFont, 2> iconFonts;
std::array<FontGlyph, static_cast<size_t>(Glyph::GenericApp) + 1> fontGlyphs;
com_ptr<ID2D1Factory> strokeOwner;
com_ptr<ID2D1StrokeStyle> roundStroke;

wchar_t CodePoint(Glyph glyph) {
    switch (glyph) {
    case Glyph::Add: return vi::kGlyphAdd;
    case Glyph::Manage: return vi::kGlyphManage;
    case Glyph::Preferred: return vi::kGlyphPreferred;
    case Glyph::Sun: return vi::kGlyphSun;
    case Glyph::Moon: return vi::kGlyphMoon;
    case Glyph::Edit: return vi::kGlyphEdit;
    case Glyph::Remove: return vi::kGlyphRemove;
    case Glyph::Previous: return vi::kGlyphPrevious;
    case Glyph::Next: return vi::kGlyphNext;
    case Glyph::ChevronLeftSmall: return vi::kGlyphChevronLeftSmall;
    case Glyph::ChevronRightSmall: return vi::kGlyphChevronRightSmall;
    case Glyph::BackChevron: return vi::kGlyphBackChevron;
    case Glyph::About: return vi::kGlyphAbout;
    case Glyph::SwitchOn: return vi::kGlyphSwitchOn;
    case Glyph::SwitchOff: return vi::kGlyphSwitchOff;
    case Glyph::CheckUpdate: return vi::kGlyphCheckUpdate;
    case Glyph::CheckFailed: return vi::kGlyphCheckFailed;
    case Glyph::Download: return vi::kGlyphDownload;
    case Glyph::GenericApp: return vi::kGlyphGenericApp;
    case Glyph::Check: break;  // the badge check is always a path
    }
    return 0;
}

IDWriteFontFace* IconFace(size_t which) {
    auto& font = iconFonts[which];
    if (font.attempted) return font.face.get();
    font.attempted = true;
    if (!writeFactory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(writeFactory.put())))) return nullptr;
    com_ptr<IDWriteFontCollection> fonts;
    com_ptr<IDWriteFontFamily> family;
    com_ptr<IDWriteFont> match;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(writeFactory->GetSystemFontCollection(fonts.put())) &&
        SUCCEEDED(fonts->FindFamilyName(which ? vi::kIconFallbackFamily : vi::kIconFamily, &index, &exists)) && exists &&
        SUCCEEDED(fonts->GetFontFamily(index, family.put())) &&
        SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, match.put())))
        match->CreateFontFace(font.face.put());
    return font.face.get();
}

// The first icon font whose cmap has the code point; nullptr selects the vector fallback.
const FontGlyph* ResolveGlyph(Glyph glyph) {
    auto& resolved = fontGlyphs[static_cast<size_t>(glyph)];
    if (!resolved.attempted) {
        resolved.attempted = true;
        const UINT32 point = CodePoint(glyph);
        for (size_t which = 0; point && which < iconFonts.size() && !resolved.face; ++which) {
            auto* face = IconFace(which);
            UINT16 index = 0;
            DWRITE_GLYPH_METRICS metrics{};
            DWRITE_FONT_METRICS font{};
            if (!face || FAILED(face->GetGlyphIndices(&point, 1, &index)) || !index ||
                FAILED(face->GetDesignGlyphMetrics(&index, 1, &metrics))) continue;
            face->GetMetrics(&font);
            const float unit = 1.f / font.designUnitsPerEm;
            resolved.ink = D2D1::RectF(metrics.leftSideBearing * unit,
                -(metrics.verticalOriginY - metrics.topSideBearing) * unit,
                (static_cast<INT32>(metrics.advanceWidth) - metrics.rightSideBearing) * unit,
                -(metrics.verticalOriginY - static_cast<INT32>(metrics.advanceHeight) + metrics.bottomSideBearing) * unit);
            resolved.face = face;
            resolved.index = index;
        }
    }
    return resolved.face ? &resolved : nullptr;
}

// Device pixels = DIP * transform * context DPI / 96. Snapping applies only to
// axis-aligned uniform scale + translation; hit testing never uses these values.
struct PixelGrid { float scale = 1, dx = 0, dy = 0; bool aligned = false; };

PixelGrid Grid(ID2D1DeviceContext* context) {
    D2D1_MATRIX_3X2_F m{};
    context->GetTransform(&m);
    float dpiX = 96, dpiY = 96;
    context->GetDpi(&dpiX, &dpiY);
    PixelGrid grid;
    grid.aligned = m._12 == 0 && m._21 == 0 && m._11 > 0 && m._11 * dpiX == m._22 * dpiY;
    grid.scale = m._11 * dpiX / 96;
    grid.dx = m._31 * dpiX / 96;
    grid.dy = m._32 * dpiY / 96;
    return grid;
}

D2D1_POINT_2F Snap(const PixelGrid& grid, D2D1_POINT_2F point) {
    if (!grid.aligned) return point;
    return {(std::round(point.x * grid.scale + grid.dx) - grid.dx) / grid.scale,
            (std::round(point.y * grid.scale + grid.dy) - grid.dy) / grid.scale};
}

ID2D1StrokeStyle* RoundStroke(ID2D1DeviceContext* context) {
    com_ptr<ID2D1Factory> factory;
    context->GetFactory(factory.put());
    if (factory != strokeOwner) {
        roundStroke = nullptr;
        strokeOwner = factory;
        auto properties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        if (factory) factory->CreateStrokeStyle(properties, nullptr, 0, roundStroke.put());
    }
    return roundStroke.get();
}

// Vector fallback in em units around the ink center, y down. Every polyline is one
// figure of one geometry, so joins are real joins and overlaps never double-blend.
void FallbackFigures(ID2D1GeometrySink* sink, Glyph glyph, D2D1_POINT_2F center, float em) {
    auto at = [&](float x, float y) { return D2D1::Point2F(center.x + x * em, center.y + y * em); };
    auto poly = [&](std::initializer_list<D2D1_POINT_2F> points, bool closed = false) {
        auto point = points.begin();
        sink->BeginFigure(at(point->x, point->y), D2D1_FIGURE_BEGIN_HOLLOW);
        for (++point; point != points.end(); ++point) sink->AddLine(at(point->x, point->y));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    };
    auto arc = [&](float x, float y, float radius, D2D1_SWEEP_DIRECTION sweep, D2D1_ARC_SIZE size) {
        sink->AddArc(D2D1::ArcSegment(at(x, y), D2D1::SizeF(radius * em, radius * em), 0, sweep, size));
    };
    auto circle = [&](float x, float y, float radius) {
        sink->BeginFigure(at(x - radius, y), D2D1_FIGURE_BEGIN_HOLLOW);
        arc(x + radius, y, radius, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL);
        arc(x - radius, y, radius, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };
    auto box = [&](float left, float top, float right, float bottom, float r) {
        constexpr auto cw = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        sink->BeginFigure(at(left + r, top), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(at(right - r, top)); arc(right, top + r, r, cw, D2D1_ARC_SIZE_SMALL);
        sink->AddLine(at(right, bottom - r)); arc(right - r, bottom, r, cw, D2D1_ARC_SIZE_SMALL);
        sink->AddLine(at(left + r, bottom)); arc(left, bottom - r, r, cw, D2D1_ARC_SIZE_SMALL);
        sink->AddLine(at(left, top + r)); arc(left + r, top, r, cw, D2D1_ARC_SIZE_SMALL);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };
    switch (glyph) {
    case Glyph::Add: poly({{-.4f, 0}, {.4f, 0}}); poly({{0, -.4f}, {0, .4f}}); break;
    case Glyph::Manage:
        for (const float x : {-.215f, .215f}) for (const float y : {-.215f, .215f})
            box(x - .155f, y - .155f, x + .155f, y + .155f, .06f);
        break;
    case Glyph::Preferred: circle(0, 0, .43f); poly({{-.19f, .01f}, {-.06f, .14f}, {.2f, -.12f}}); break;
    case Glyph::Sun:
        circle(0, 0, .19f);
        for (int ray = 0; ray < 8; ++ray) {
            const float angle = ray * 3.14159265f / 4, c = std::cos(angle), s = std::sin(angle);
            poly({{c * .31f, s * .31f}, {c * .44f, s * .44f}});
        }
        break;
    case Glyph::Moon:
        // Outer disc r .42 at the origin minus a disc r .34 at (.22, -.22).
        sink->BeginFigure(at(-.058f, -.416f), D2D1_FIGURE_BEGIN_HOLLOW);
        arc(.416f, .058f, .42f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE);
        arc(-.058f, -.416f, .34f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        break;
    case Glyph::Edit:
        poly({{-.38f, .38f}, {-.33f, .17f}, {.2f, -.36f}, {.36f, -.2f}, {-.17f, .33f}}, true);
        poly({{.1f, -.26f}, {.26f, -.1f}});
        break;
    case Glyph::Remove:
        poly({{-.4f, -.26f}, {.4f, -.26f}});
        poly({{-.13f, -.26f}, {-.13f, -.4f}, {.13f, -.4f}, {.13f, -.26f}});
        poly({{-.28f, -.26f}, {-.24f, .42f}, {.24f, .42f}, {.28f, -.26f}});
        poly({{-.08f, -.08f}, {-.08f, .24f}}); poly({{.08f, -.08f}, {.08f, .24f}});
        break;
    case Glyph::Previous: case Glyph::BackChevron: poly({{.12f, -.375f}, {-.12f, 0}, {.12f, .375f}}); break;
    case Glyph::Next: poly({{-.12f, -.375f}, {.12f, 0}, {-.12f, .375f}}); break;
    case Glyph::ChevronLeftSmall: poly({{.1f, -.3f}, {-.1f, 0}, {.1f, .3f}}); break;
    case Glyph::ChevronRightSmall: poly({{-.1f, -.3f}, {.1f, 0}, {-.1f, .3f}}); break;
    case Glyph::Check: poly({{-.25f, 0}, {-.0833f, .1667f}, {.25f, -.1667f}}); break;
    case Glyph::GenericApp: box(-.4f, -.36f, .4f, .36f, .08f); poly({{-.4f, -.15f}, {.4f, -.15f}}); break;
    case Glyph::About: circle(0, 0, .42f); poly({{0, -.06f}, {0, .22f}}); poly({{0, -.2f}, {0, -.19f}}); break;
    // The vector switches are outlines: the knob sits left when off and right when on.
    case Glyph::SwitchOn: case Glyph::SwitchOff:
        box(-.44f, -.22f, .44f, .22f, .22f);
        circle(glyph == Glyph::SwitchOn ? vi::kSwitchKnobX : -vi::kSwitchKnobX, 0, vi::kSwitchKnobRadius);
        break;
    case Glyph::CheckUpdate: case Glyph::CheckFailed:
        sink->BeginFigure(at(.36f, -.2f), D2D1_FIGURE_BEGIN_HOLLOW);
        arc(.2f, .36f, .42f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        poly({{.36f, -.42f}, {.36f, -.2f}, {.14f, -.2f}});
        if (glyph == Glyph::CheckFailed) { poly({{0, -.14f}, {0, .06f}}); poly({{0, .17f}, {0, .18f}}); }
        break;
    case Glyph::Download:
        poly({{0, -.42f}, {0, .18f}}); poly({{-.22f, -.04f}, {0, .18f}, {.22f, -.04f}}); poly({{-.36f, .42f}, {.36f, .42f}});
        break;
    }
}

void DrawFallback(ID2D1DeviceContext* context, Glyph glyph, D2D1_POINT_2F center, float em,
                  ID2D1Brush* brush, float dpi, bool highContrast) {
    com_ptr<ID2D1Factory> factory;
    context->GetFactory(factory.put());
    com_ptr<ID2D1PathGeometry> path;
    com_ptr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(path.put())) || FAILED(path->Open(sink.put()))) return;
    FallbackFigures(sink.get(), glyph, center, em);
    if (FAILED(sink->Close())) return;
    // The badge check uses its own stroke at em = badge diameter; other glyphs scale
    // the fallback stroke with the em. Widths land on whole device pixels.
    float width = glyph == Glyph::Check ? vi::kBadgeCheckStroke * em / (2 * vi::kBadgeRadius) :
        (highContrast ? vi::kGlyphFallbackStrokeHighContrast : vi::kGlyphFallbackStroke) * em / vi::kGlyphEm;
    const float scale = dpi / 96;
    if (scale > 0) width = (std::max)(1.f, std::round(width * scale)) / scale;
    context->DrawGeometry(path.get(), brush, width, RoundStroke(context));
}

void DrawGlyphCore(ID2D1DeviceContext* context, Glyph glyph, D2D1_POINT_2F center, float em,
                   const D2D1_COLOR_F& color, float dpi, bool highContrast) {
    if (!context || !(em > 0)) return;
    com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED(context->CreateSolidColorBrush(color, brush.put()))) return;
    const auto grid = Grid(context);
    if (const auto* font = glyph == Glyph::Check ? nullptr : ResolveGlyph(glyph)) {
        // Center the ink box, then put the em origin on the pixel grid so the font's
        // 1/16 em design grid lines up with device pixels where the size allows.
        const auto origin = Snap(grid, {center.x - (font->ink.left + font->ink.right) * .5f * em,
                                        center.y - (font->ink.top + font->ink.bottom) * .5f * em});
        const FLOAT advance = 0;
        const DWRITE_GLYPH_RUN run{font->face, em, 1, &font->index, &advance, nullptr, FALSE, 0};
        context->DrawGlyphRun(origin, &run, brush.get(), DWRITE_MEASURING_MODE_NATURAL);
        return;
    }
    DrawFallback(context, glyph, Snap(grid, center), em, brush.get(), dpi, highContrast);
}

// ---- Raster artwork: vendor PNGs and Shell icons.

struct Treatment {
    bool plated = false;
    float opticalScale = 1;
    D2D1_RECT_F content{0, 0, 1, 1};  // normalized to the source canvas
};

struct Source {
    const std::vector<BYTE>* png = nullptr;  // vendor PNG (owned by official_icons.cpp)
    std::vector<BYTE> pixels;                // Shell icon, premultiplied BGRA, top-down
    UINT width = 0, height = 0;
    Treatment treatment;
};

using ScaledKey = std::tuple<std::wstring, UINT, UINT>;  // (source, device pixel width, height)
struct DeviceBitmaps {
    com_ptr<ID2D1Device> owner;  // retained so a recycled pointer cannot match stale bitmaps
    std::map<ScaledKey, com_ptr<ID2D1Bitmap1>> bitmaps;
};

com_ptr<IWICImagingFactory> imaging;
std::map<std::wstring, Source, std::less<>> sources;        // vendor key or "shell:<identity>"
std::map<std::wstring, std::wstring, std::less<>> shellTargets;  // target key -> Shell source ("" = none)
std::map<std::wstring, std::wstring, std::less<>> linkBrands;    // .lnk target key -> destination brand
std::map<ScaledKey, std::vector<BYTE>> scaled;
std::map<ID2D1Device*, DeviceBitmaps> devices;

std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}

std::wstring Unquote(std::wstring_view value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == value.npos) return {};
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
    return std::wstring(value);
}

std::wstring TargetKey(const AppTarget& item) {
    return (item.kind == TargetKind::Aumid ? L"aumid|" : L"shell|") + Lower(Unquote(item.target));
}

// About shows two marks among its commands: the reel's own and GitHub's. The hub's Tray icon
// shows the reel's color mark while the tray icon is in color.
std::wstring_view MarkedCommand(const AppTarget& item) {
    if (item.target == L"reel:author" || item.target == L"reel:tray-icon:color") return L"copilot-reel";
    if (item.target == L"reel:github") return L"github";
    return {};
}
bool IsCommand(const AppTarget& item) { return item.target.starts_with(L"reel:") && MarkedCommand(item).empty(); }

const vi::BrandTreatment* FindTreatment(std::wstring_view key) {
    for (std::size_t i = 0; i < vi::kBrandCount; ++i)
        if (key == vi::kBrands[i].key) return &vi::kBrands[i];
    return nullptr;
}

IWICImagingFactory* Imaging() {
    if (!imaging) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(imaging.put()));
    return imaging.get();
}

com_ptr<IWICBitmapSource> Decode(const Source& source) {
    auto* factory = Imaging();
    if (!factory) return nullptr;
    com_ptr<IWICBitmapSource> result;
    if (source.png) {
        com_ptr<IWICStream> stream;
        com_ptr<IWICBitmapDecoder> decoder;
        com_ptr<IWICBitmapFrameDecode> frame;
        com_ptr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateStream(stream.put())) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(source.png->data()), static_cast<DWORD>(source.png->size()))) ||
            FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
            FAILED(decoder->GetFrame(0, frame.put())) || FAILED(factory->CreateFormatConverter(converter.put())) ||
            FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
        result = converter.as<IWICBitmapSource>();
    } else if (!source.pixels.empty()) {
        com_ptr<IWICBitmap> bitmap;
        if (FAILED(factory->CreateBitmapFromMemory(source.width, source.height, GUID_WICPixelFormat32bppPBGRA,
            source.width * 4, static_cast<UINT>(source.pixels.size()), const_cast<BYTE*>(source.pixels.data()),
            bitmap.put()))) return nullptr;
        result = bitmap.as<IWICBitmapSource>();
    }
    return result;
}

// Alpha analysis shared with the offline token script (scratch analyze_brands.py):
// plated = the alpha >= 230 bounding box is >= 90% covered and matches a rounded rectangle.
Treatment Analyze(const BYTE* pixels, UINT width, UINT height) {
    Treatment result;
    auto alpha = [&](UINT x, UINT y) { return pixels[(static_cast<size_t>(y) * width + x) * 4 + 3]; };
    auto bounds = [&](BYTE threshold, bool inclusive) {
        UINT left = width, top = height, right = 0, bottom = 0;
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const BYTE a = alpha(x, y);
            if (inclusive ? a < threshold : a <= threshold) continue;
            left = (std::min)(left, x); right = (std::max)(right, x + 1);
            top = (std::min)(top, y); bottom = (std::max)(bottom, y + 1);
        }
        return std::array<UINT, 4>{left, top, right, bottom};
    };
    const auto ink = bounds(5, false);
    if (ink[0] >= ink[2]) return result;
    const auto plate = bounds(230, true);
    const auto normalized = [&](const std::array<UINT, 4>& r) {
        return D2D1::RectF(static_cast<float>(r[0]) / width, static_cast<float>(r[1]) / height,
                           static_cast<float>(r[2]) / width, static_cast<float>(r[3]) / height);
    };
    result.content = normalized(ink);
    if (plate[0] >= plate[2]) return result;
    const UINT plateWidth = plate[2] - plate[0], plateHeight = plate[3] - plate[1];
    auto solid = [&](UINT x, UINT y) { return alpha(plate[0] + x, plate[1] + y) >= 128; };
    size_t covered = 0;
    for (UINT y = 0; y < plateHeight; ++y) for (UINT x = 0; x < plateWidth; ++x) covered += solid(x, y);
    const double coverage = static_cast<double>(covered) / (static_cast<double>(plateWidth) * plateHeight);
    // Corner radius from the first solid pixel along each corner diagonal.
    const UINT limit = (std::min)(plateWidth, plateHeight) / 2;
    double steps = 0;
    for (const auto [x0, y0, sx, sy] : {std::array<int, 4>{0, 0, 1, 1}, {static_cast<int>(plateWidth) - 1, 0, -1, 1},
         {0, static_cast<int>(plateHeight) - 1, 1, -1},
         {static_cast<int>(plateWidth) - 1, static_cast<int>(plateHeight) - 1, -1, -1}}) {
        UINT t = 0;
        while (t < limit && !solid(static_cast<UINT>(x0 + sx * static_cast<int>(t)), static_cast<UINT>(y0 + sy * static_cast<int>(t)))) ++t;
        steps += t + .5;
    }
    const double radius = (std::min)(steps / 4 / (1 - 1 / std::sqrt(2.0)), limit * 1.0);
    size_t both = 0, either = 0;
    for (UINT y = 0; y < plateHeight; ++y) for (UINT x = 0; x < plateWidth; ++x) {
        const double px = x + .5, py = y + .5;
        const double cx = std::clamp(px, radius, plateWidth - radius), cy = std::clamp(py, radius, plateHeight - radius);
        const bool ideal = (px - cx) * (px - cx) + (py - cy) * (py - cy) <= radius * radius;
        const bool actual = solid(x, y);
        both += ideal && actual;
        either += ideal || actual;
    }
    if (coverage >= .90 && either && static_cast<double>(both) / either >= .97) {
        result.plated = true;
        result.content = normalized(plate);
    }
    return result;
}

// Vendor sources are decoded from preloaded bytes on first use (no file I/O).
Source* FindSource(std::wstring_view key) {
    if (const auto found = sources.find(key); found != sources.end())
        return found->second.width ? &found->second : nullptr;
    auto& source = sources[std::wstring(key)];
    const auto* treatment = FindTreatment(key);
    source.png = treatment ? OfficialArtworkBytes(key) : nullptr;
    const auto bitmap = source.png ? Decode(source) : nullptr;
    if (!bitmap || FAILED(bitmap->GetSize(&source.width, &source.height))) { source.width = 0; return nullptr; }
    source.treatment.plated = treatment->plated;
    source.treatment.opticalScale = treatment->opticalScale;
    source.treatment.content = D2D1::RectF(treatment->contentLeft, treatment->contentTop,
                                           treatment->contentRight, treatment->contentBottom);
    return &source;
}

std::wstring VendorKey(const AppTarget& item, bool dark) {
    auto brand = IdentifyBrand(item);
    if (brand.empty()) if (const auto link = linkBrands.find(TargetKey(item)); link != linkBrands.end()) brand = link->second;
    if (const auto marked = MarkedCommand(item); !marked.empty()) brand = marked;
    if (brand == L"chatgpt") brand = dark ? L"chatgpt-dark" : L"chatgpt-light";
    if (brand == L"github") brand = dark ? L"github-dark" : L"github-light";
    return !brand.empty() && OfficialArtworkBytes(brand) ? brand : std::wstring();
}

// Vendor artwork wins over the Shell icon; an empty key means the generic glyph.
std::wstring SourceKey(const AppTarget& item, bool dark) {
    if (auto key = VendorKey(item, dark); !key.empty()) return key;
    const auto shell = shellTargets.find(TargetKey(item));
    return shell == shellTargets.end() ? std::wstring() : shell->second;
}

struct Placement { D2D1_RECT_F rect{}; UINT width = 0, height = 0; };

// Source pixels to crop: the content rect; for plated art also trimmed to the tile
// aspect, so scaling covers the tile without distortion.
WICRect CropRect(const Source& source, float aspect) {
    const auto& c = source.treatment.content;
    double left = c.left * source.width, top = c.top * source.height;
    double right = c.right * source.width, bottom = c.bottom * source.height;
    if (source.treatment.plated && aspect > 0) {
        if ((right - left) / (bottom - top) > aspect) {
            const double trim = ((right - left) - (bottom - top) * aspect) / 2;
            left += trim; right -= trim;
        } else {
            const double trim = ((bottom - top) - (right - left) / aspect) / 2;
            top += trim; bottom -= trim;
        }
    }
    const int x = static_cast<int>(std::floor(left + 1e-3)), y = static_cast<int>(std::floor(top + 1e-3));
    return {x, y, (std::max)(1, static_cast<int>(std::ceil(right - 1e-3)) - x),
            (std::max)(1, static_cast<int>(std::ceil(bottom - 1e-3)) - y)};
}

// Plated art covers the tile; glyph marks fit their long ink side to the keyline
// times opticalScale. Sizes are whole device pixels at `dpi`, centered on the tile.
Placement Place(const Source& source, const D2D1_RECT_F& tile, float dpi) {
    const float scale = dpi / 96;
    const float tileWidth = tile.right - tile.left, tileHeight = tile.bottom - tile.top;
    Placement place;
    if (source.treatment.plated) {
        place.width = static_cast<UINT>((std::max)(1.f, std::round(tileWidth * scale)));
        place.height = static_cast<UINT>((std::max)(1.f, std::round(tileHeight * scale)));
    } else {
        const auto crop = CropRect(source, 0);
        const float longSide = (std::max)(1.f, std::round(vi::kArtworkKeyline * source.treatment.opticalScale *
                                                          tileWidth / vi::kTile * scale));
        const float ratio = static_cast<float>((std::min)(crop.Width, crop.Height)) / (std::max)(crop.Width, crop.Height);
        const float shortSide = (std::max)(1.f, std::round(longSide * ratio));
        place.width = static_cast<UINT>(crop.Width >= crop.Height ? longSide : shortSide);
        place.height = static_cast<UINT>(crop.Width >= crop.Height ? shortSide : longSide);
    }
    const float width = place.width / scale, height = place.height / scale;
    const float x = (tile.left + tile.right - width) / 2, y = (tile.top + tile.bottom - height) / 2;
    place.rect = D2D1::RectF(x, y, x + width, y + height);
    return place;
}

// WIC crop + high-quality cubic scale to exactly width x height device pixels.
bool Prescale(const Source& source, UINT width, UINT height, std::vector<BYTE>& out) {
    auto* factory = Imaging();
    const auto input = Decode(source);
    com_ptr<IWICBitmapClipper> clipper;
    com_ptr<IWICBitmapScaler> scaler;
    com_ptr<IWICFormatConverter> converter;
    const auto crop = CropRect(source, static_cast<float>(width) / height);
    if (!factory || !input || FAILED(factory->CreateBitmapClipper(clipper.put())) ||
        FAILED(clipper->Initialize(input.get(), &crop)) || FAILED(factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(clipper.get(), width, height, WICBitmapInterpolationModeHighQualityCubic)) ||
        FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    out.resize(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(out.size()), out.data()))) return false;
    // Cubic ringing may push a premultiplied channel above alpha; keep it valid.
    for (size_t i = 0; i < out.size(); i += 4)
        for (size_t c = 0; c < 3; ++c) out[i + c] = (std::min)(out[i + c], out[i + 3]);
    return true;
}

const std::vector<BYTE>* Scaled(const std::wstring& key, const Source& source, const Placement& place) {
    ScaledKey id{key, place.width, place.height};
    if (const auto found = scaled.find(id); found != scaled.end()) return &found->second;
    std::vector<BYTE> pixels;
    if (!Prescale(source, place.width, place.height, pixels)) return nullptr;
    return &scaled.emplace(std::move(id), std::move(pixels)).first->second;
}

bool DrawRaster(ID2D1DeviceContext* context, const std::wstring& key, const Source& source,
                const D2D1_RECT_F& tile, float dpi) {
    if (!(dpi > 0)) return false;
    const auto grid = Grid(context);
    auto place = Place(source, tile, dpi);
    const auto origin = Snap(grid, {place.rect.left, place.rect.top});
    place.rect = D2D1::RectF(origin.x, origin.y, origin.x + place.rect.right - place.rect.left,
                             origin.y + place.rect.bottom - place.rect.top);
    com_ptr<ID2D1Device> device;
    context->GetDevice(device.put());
    if (!device) return false;
    auto& cache = devices[device.get()];
    if (!cache.owner) cache.owner = device;
    auto& bitmap = cache.bitmaps[{key, place.width, place.height}];
    if (!bitmap) {
        const auto* pixels = Scaled(key, source, place);
        if (!pixels || FAILED(context->CreateBitmap(D2D1::SizeU(place.width, place.height), pixels->data(),
            place.width * 4, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi),
            bitmap.put()))) return false;
    }
    // 1:1 when the context maps DIP to device pixels at `dpi`; otherwise resample.
    const bool exact = grid.aligned && std::abs(grid.scale - dpi / 96) < 1e-3f;
    const auto interpolation = exact ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
    if (!source.treatment.plated) {
        context->DrawBitmap(bitmap.get(), place.rect, 1.f, interpolation, nullptr, nullptr);
        return true;
    }
    // Plated art is masked to the tile shape: the bitmap brush fills the r10 tile.
    com_ptr<ID2D1BitmapBrush1> brush;
    if (FAILED(context->CreateBitmapBrush(bitmap.get(), D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP,
            D2D1_EXTEND_MODE_CLAMP, interpolation),
        D2D1::BrushProperties(1.f, D2D1::Matrix3x2F::Scale((place.rect.right - place.rect.left) * dpi / 96 / place.width,
            (place.rect.bottom - place.rect.top) * dpi / 96 / place.height) *
            D2D1::Matrix3x2F::Translation(place.rect.left, place.rect.top)), brush.put()))) return false;
    const float radius = vi::kTileRadius * (tile.right - tile.left) / vi::kTile;
    context->FillRoundedRectangle(D2D1::RoundedRect(place.rect, radius, radius), brush.get());
    return true;
}

// ---- Shell icons (PrepareArtwork only).

bool HasPackage(const wchar_t* family) {
    UINT32 count = 0, length = 0;
    return GetPackagesByPackageFamily(family, &count, nullptr, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER && count;
}

std::wstring ResolvePath(const std::wstring& target) {
    std::array<wchar_t, 32768> buffer{};
    std::wstring path = target;
    const DWORD expanded = ExpandEnvironmentStringsW(target.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (expanded && expanded <= buffer.size()) path = buffer.data();
    if (path.find_first_of(L"\\/") == std::wstring::npos) {
        const DWORD found = SearchPathW(nullptr, path.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (!found || found >= buffer.size()) return {};
        path = buffer.data();
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ? std::wstring() : path;
}

// Windows gives an executable that carries no icon its stock application icon, which would pass
// for plated art. Such a target, and a shortcut whose icon comes from one, keeps the generic
// glyph instead (VI: a custom app without an icon). Other files keep their type's Shell icon.
bool HasOwnIcon(const std::wstring& path, bool followLink = true) {
    const auto dot = path.find_last_of(L'.'), slash = path.find_last_of(L"\\/");
    const auto extension = dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) ? std::wstring() : Lower(path.substr(dot));
    if (extension == L".lnk") {
        com_ptr<IShellLinkW> link;
        std::array<wchar_t, 32768> location{}, destination{};
        int index = 0;
        if (!followLink || FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()))) ||
            FAILED(link.as<IPersistFile>()->Load(path.c_str(), STGM_READ))) return true;
        if (SUCCEEDED(link->GetIconLocation(location.data(), static_cast<int>(location.size()), &index)) && location[0])
            if (const auto icon = ResolvePath(location.data()); !icon.empty()) return HasOwnIcon(icon, false);
        if (link->GetPath(destination.data(), static_cast<int>(destination.size()), nullptr, 0) == S_OK && destination[0])
            if (const auto target = ResolvePath(destination.data()); !target.empty()) return HasOwnIcon(target, false);
        return true;
    }
    if (extension != L".exe" && extension != L".dll" && extension != L".com" && extension != L".scr" && extension != L".cpl") return true;
    return ExtractIconExW(path.c_str(), -1, nullptr, nullptr, 0) > 0;
}

// Shell parsing name and the identity used for vi::kBrands overrides ("shell:<aumid or path>").
std::pair<std::wstring, std::wstring> ShellName(const AppTarget& item) {
    constexpr wchar_t settings[] = L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel";
    constexpr wchar_t notepad[] = L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App";
    auto packaged = [](std::wstring_view aumid) {
        return std::pair{L"shell:AppsFolder\\" + std::wstring(aumid), L"shell:" + Lower(aumid)};
    };
    const auto system = IdentifySystemIcon(item);
    if (system == SystemIconKind::Settings) return packaged(settings);
    if (system == SystemIconKind::Notepad && HasPackage(L"Microsoft.WindowsNotepad_8wekyb3d8bbwe")) return packaged(notepad);
    const auto target = Unquote(item.target);
    if (item.kind == TargetKind::Aumid) return packaged(target);
    constexpr std::wstring_view appFolder = L"shell:appsfolder\\";
    if (Lower(target).starts_with(appFolder)) return packaged(std::wstring_view(target).substr(appFolder.size()));
    const auto colon = target.find(L':');
    if (target.empty() || target.find(L"://") != target.npos || (colon != target.npos && colon != 1) ||
        target.find_first_of(L"?\r\n") != target.npos) return {};
    auto path = ResolvePath(target);
    if (path.empty()) return {};
    return {path, L"shell:" + Lower(path)};
}

// IShellItemImageFactory returns straight-alpha BGRA; the cache keeps premultiplied pixels.
bool ShellImage(const std::wstring& parsing, std::vector<BYTE>& pixels, UINT& width, UINT& height) {
    com_ptr<IShellItemImageFactory> factory;
    HBITMAP bitmap = nullptr;
    if (FAILED(SHCreateItemFromParsingName(parsing.c_str(), nullptr, IID_PPV_ARGS(factory.put()))) ||
        FAILED(factory->GetImage(SIZE{256, 256}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) || !bitmap) return false;
    DIBSECTION dib{};
    bool copied = false;
    if (GetObjectW(bitmap, sizeof(dib), &dib) == sizeof(dib) && dib.dsBm.bmBitsPixel == 32 && dib.dsBm.bmBits &&
        dib.dsBm.bmWidth > 0 && dib.dsBm.bmWidth <= 1024 && dib.dsBm.bmHeight > 0 && dib.dsBm.bmHeight <= 1024) {
        width = static_cast<UINT>(dib.dsBm.bmWidth);
        height = static_cast<UINT>(dib.dsBm.bmHeight);
        pixels.resize(static_cast<size_t>(width) * height * 4);
        const bool bottomUp = dib.dsBmih.biHeight > 0;
        for (UINT y = 0; y < height; ++y)
            std::copy_n(static_cast<const BYTE*>(dib.dsBm.bmBits) + static_cast<size_t>(bottomUp ? height - 1 - y : y) *
                dib.dsBm.bmWidthBytes, width * 4, pixels.data() + static_cast<size_t>(y) * width * 4);
        copied = true;
    }
    DeleteObject(bitmap);
    if (!copied) return false;
    bool alpha = false, straight = false;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        alpha |= pixels[i + 3] != 0;
        straight |= pixels[i] > pixels[i + 3] || pixels[i + 1] > pixels[i + 3] || pixels[i + 2] > pixels[i + 3];
    }
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (!alpha) { pixels[i + 3] = 255; continue; }  // legacy icon without an alpha channel
        if (straight) for (size_t c = 0; c < 3; ++c) pixels[i + c] = static_cast<BYTE>((pixels[i + c] * pixels[i + 3] + 127) / 255);
    }
    return true;
}

// A target on a network share is never touched on the UI thread: an offline server would
// stall every page for its connect timeout. It keeps its vendor mark or the generic glyph.
bool OnNetwork(const AppTarget& item) {
    return item.kind == TargetKind::Shell && IsNetworkPath(Unquote(item.target));
}

// Only successes are cached, and executables known to carry no icon: a target whose drive,
// package or file is not available yet (logon, an app installed later) is looked up again on
// the next preparation.
void PrepareShell(const AppTarget& item) {
    const auto key = TargetKey(item);
    if (shellTargets.contains(key) || OnNetwork(item)) return;
    const auto [parsing, identity] = ShellName(item);
    if (parsing.empty()) return;
    // A known answer: no artwork of its own, so SourceKey is empty and the generic glyph draws.
    if (!HasOwnIcon(parsing)) { shellTargets.emplace(key, std::wstring()); return; }
    if (!sources.contains(identity)) {
        Source source;
        if (!ShellImage(parsing, source.pixels, source.width, source.height)) return;
        source.treatment = Analyze(source.pixels.data(), source.width, source.height);
        if (const auto* brand = FindTreatment(identity)) {
            const bool measured = source.treatment.plated;
            source.treatment.plated = brand->plated;
            source.treatment.opticalScale = brand->opticalScale;
            // The measured content rect stays: Shell pixels change with Windows builds.
            if (brand->plated != measured)
                source.treatment.content = D2D1::RectF(brand->contentLeft, brand->contentTop,
                                                       brand->contentRight, brand->contentBottom);
        }
        sources.emplace(identity, std::move(source));
    }
    shellTargets.emplace(key, identity);
}

// A shortcut's own name and folder identify it first; otherwise its destination does.
void PrepareLink(const AppTarget& item) {
    const auto key = TargetKey(item);
    if (item.kind != TargetKind::Shell || !key.ends_with(L".lnk") || linkBrands.contains(key) || OnNetwork(item)) return;
    const auto path = ResolvePath(Unquote(item.target));
    com_ptr<IShellLinkW> link;
    std::array<wchar_t, 32768> destination{};
    if (path.empty() || FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()))) ||
        FAILED(link.as<IPersistFile>()->Load(path.c_str(), STGM_READ)) ||
        link->GetPath(destination.data(), static_cast<int>(destination.size()), nullptr, 0) != S_OK) return;
    linkBrands.emplace(key, IdentifyBrand({{}, TargetKind::Shell, destination.data(), {}, {}}));
}
} // namespace

void PrepareArtwork(const std::vector<AppTarget>& items, float dpi) {
    PreloadOfficialIconAssets();
    // Caches are keyed by pixel size, so moving between monitors keeps both scales.
    const D2D1_RECT_F tile{0, 0, vi::kTile, vi::kTile};
    for (const auto& item : items) {
        if (IsCommand(item)) continue;
        PrepareLink(item);
        if (VendorKey(item, false).empty()) PrepareShell(item);
        for (const bool dark : {false, true}) {
            const auto key = SourceKey(item, dark);
            if (const auto* source = key.empty() ? nullptr : FindSource(key); source && dpi > 0)
                Scaled(key, *source, Place(*source, tile, dpi));
        }
    }
}

ArtworkClass ClassifyArtwork(const AppTarget& item) {
    if (IsCommand(item)) return ArtworkClass::Command;
    const auto key = SourceKey(item, false);
    const auto* source = key.empty() ? nullptr : FindSource(key);
    if (!source) return ArtworkClass::Generic;
    return source->treatment.plated ? ArtworkClass::Plated : ArtworkClass::Glyph;
}

std::wstring ArtworkIdentity(const AppTarget& item, bool dark) {
    if (IsCommand(item)) return L"command|" + item.target;
    const auto key = SourceKey(item, dark);
    return key.empty() || !FindSource(key) ? std::wstring(L"generic") : L"source|" + key;
}

bool DrawArtwork(ID2D1DeviceContext* context, const AppTarget& item,
                 const D2D1_RECT_F& tile, const ArtworkStyle& style) {
    if (!context || !(tile.right > tile.left) || !(tile.bottom > tile.top)) return false;
    const D2D1_POINT_2F center{(tile.left + tile.right) / 2, (tile.top + tile.bottom) / 2};
    const float em = vi::kGlyphEm * (tile.right - tile.left) / vi::kTile;
    if (item.target == L"reel:tray-icon:monochrome") {
        // The monochrome tray icon's mark in the glyph color, as wide as the round command glyphs'
        // ink (7/8 em, the circle of Info), its stroke at least one device pixel.
        com_ptr<ID2D1SolidColorBrush> brush;
        if (FAILED(context->CreateSolidColorBrush(style.glyph, brush.put()))) return false;
        const float half = em * 7 / 16;
        DrawOrbitMark(context, D2D1::RectF(center.x - half, center.y - half, center.x + half, center.y + half),
                      96.f / style.dpi, brush.get());
        return true;
    }
    if (IsCommand(item)) {
        Glyph glyph{};
        if (!CommandGlyph(item, style.dark, glyph)) return false;
        DrawGlyphCore(context, glyph, center, em, style.glyph, style.dpi, style.highContrast);
        // An on switch is ToggleFilled with its knob cut out, so the tile or the seat beneath the
        // artwork shows through it. The vector fallback already draws an outlined knob.
        if (const auto* font = glyph == Glyph::SwitchOn ? ResolveGlyph(glyph) : nullptr) {
            // The ink center as DrawGlyphCore placed it on the pixel grid.
            const auto origin = Snap(Grid(context), {center.x - (font->ink.left + font->ink.right) * .5f * em,
                                                     center.y - (font->ink.top + font->ink.bottom) * .5f * em});
            const D2D1_POINT_2F ink{origin.x + (font->ink.left + font->ink.right) * .5f * em,
                                    origin.y + (font->ink.top + font->ink.bottom) * .5f * em};
            com_ptr<ID2D1SolidColorBrush> clear;
            if (SUCCEEDED(context->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), clear.put()))) {
                const float r = vi::kSwitchKnobRadius * em;
                context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
                context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ink.x + vi::kSwitchKnobX * em, ink.y), r, r), clear.get());
                context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
            }
        }
        return true;
    }
    const auto key = SourceKey(item, style.dark);
    if (const auto* source = key.empty() ? nullptr : FindSource(key))
        if (DrawRaster(context, key, *source, tile, style.dpi)) return true;
    DrawGlyphCore(context, Glyph::GenericApp, center, em, style.glyph, style.dpi, style.highContrast);
    return true;
}

void DrawGlyph(ID2D1DeviceContext* context, Glyph glyph, D2D1_POINT_2F center,
               float emDip, const D2D1_COLOR_F& color, float dpi) {
    DrawGlyphCore(context, glyph, center, emDip, color, dpi, false);
}

bool CommandGlyph(const AppTarget& item, bool darkTheme, Glyph& glyph) {
    static constexpr std::pair<std::wstring_view, Glyph> commands[] = {
        {L"reel:add", Glyph::Add}, {L"reel:manage", Glyph::Manage}, {L"reel:preferred", Glyph::Preferred},
        {L"reel:edit", Glyph::Edit}, {L"reel:remove", Glyph::Remove},
        {L"reel:previous", Glyph::Previous}, {L"reel:next", Glyph::Next}, {L"reel:about", Glyph::About},
        {L"reel:check-update", Glyph::CheckUpdate}, {L"reel:check-failed", Glyph::CheckFailed},
        {L"reel:install-update", Glyph::Download}};
    if (item.target == L"reel:appearance") { glyph = darkTheme ? Glyph::Moon : Glyph::Sun; return true; }
    // About's switches carry their state in the target, as Appearance takes it from the theme.
    if (item.target.starts_with(L"reel:auto-") && (item.target.ends_with(L":on") || item.target.ends_with(L":off"))) {
        glyph = item.target.ends_with(L":on") ? Glyph::SwitchOn : Glyph::SwitchOff;
        return true;
    }
    for (const auto& [target, value] : commands)
        if (item.target == target) { glyph = value; return true; }
    return false;
}

void ReleaseArtwork(ID2D1Device* device) {
    if (device) { devices.erase(device); return; }
    devices.clear();
    scaled.clear();
    roundStroke = nullptr;
    strokeOwner = nullptr;
}
} // namespace reel
