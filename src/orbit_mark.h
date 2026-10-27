#pragma once
#include "vi_tokens.h"
#include <d2d1.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace reel {
// The Soft Orbit mark of the package icon without its plate (iconography.orbitMark), measured in
// device pixels for a mark about `extentPx` wide. `outer` is the outer ring's edge from the center,
// a whole or half pixel, so that a center placed with the mark's edge on the pixel grid puts every
// ring edge there; `stroke` is whole pixels and never under `minimumStrokePx`; `inner` is the inner
// ring's edge on the same grid. A one-pixel stroke leaves no room for two rings, so that mark is the
// outer orbit and the dot (`innerRing` false), and the dot keeps a radius of kOrbitMarkMinimumDotPx.
struct OrbitMarkPixels { float outer = 0, stroke = 1, inner = 0, dot = 0; bool innerRing = false; };

inline OrbitMarkPixels MeasureOrbitMark(float extentPx, float minimumStrokePx) {
    OrbitMarkPixels mark;
    const float edge = vi::kOrbitMarkOuterRadius;
    mark.outer = std::max(2.f, std::round(extentPx)) / 2;
    mark.stroke = std::max(1.f, std::floor(std::max(extentPx * vi::kOrbitMarkStroke / vi::kOrbitMarkExtent, minimumStrokePx) + .5f));
    mark.inner = mark.outer - std::floor(mark.outer * (edge - vi::kOrbitMarkInnerRadius) / edge + .5f);
    mark.innerRing = mark.stroke >= vi::kOrbitMarkInnerRingFromStrokePx;
    mark.dot = std::max(mark.outer * vi::kOrbitMarkDotRadius / edge, vi::kOrbitMarkMinimumDotPx);
    return mark;
}

// Draws `mark` around `center` in `brush` on a target whose device pixels are `pixel` DIP wide.
// The caller places the center `mark.outer` pixels from a pixel boundary. The monochrome tray
// icon and the hub's Tray icon tile draw it.
inline void DrawOrbitMark(ID2D1RenderTarget* target, D2D1_POINT_2F center, const OrbitMarkPixels& mark, float pixel,
                          ID2D1Brush* brush) {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    if (!target || !brush) return;
    target->GetFactory(factory.GetAddressOf());
    if (!factory) return;
    auto point = [&](float radius, float degrees) {
        const float angle = degrees * 3.14159265f / 180;
        return D2D1::Point2F(center.x + radius * pixel * std::cos(angle), center.y + radius * pixel * std::sin(angle));
    };
    struct Ring { float edge, from, to; };
    const Ring rings[] = {{mark.outer, vi::kOrbitMarkOuterFrom, vi::kOrbitMarkOuterTo},
                          {mark.inner, vi::kOrbitMarkInnerFrom, vi::kOrbitMarkInnerTo}};
    for (std::size_t i = 0; i != (mark.innerRing ? 2u : 1u); ++i) {
        // Each ring's stroke lies inside its edge, as in the package icon.
        const auto& ring = rings[i];
        const float middle = ring.edge - mark.stroke / 2;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(point(middle, ring.from), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(point(middle, ring.to), D2D1::SizeF(middle * pixel, middle * pixel), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, ring.to - ring.from > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            // Direct2D flattens an arc it strokes to a quarter of a pixel, with chords inside the
            // circle, which pulls a one- or two-pixel ring about an eighth of a pixel off the grid
            // (223 instead of 255 on the ring's row at 16 px). The ring is widened at a thousandth of a
            // pixel and filled instead, so its edges land on the pixel boundaries.
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> outline;
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> outlineSink;
            if (SUCCEEDED(factory->CreatePathGeometry(outline.GetAddressOf())) && SUCCEEDED(outline->Open(outlineSink.GetAddressOf())) &&
                SUCCEEDED(path->Widen(mark.stroke * pixel, nullptr, nullptr, pixel / 1000, outlineSink.Get())) &&
                SUCCEEDED(outlineSink->Close()))
                target->FillGeometry(outline.Get(), brush);
        }
    }
    target->FillEllipse(D2D1::Ellipse(point(mark.outer, vi::kOrbitMarkDotAngle), mark.dot * pixel, mark.dot * pixel), brush);
}
}
