#pragma once
#include <d2d1.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace reel {
// The Soft Orbit mark of the package icon (tools/generate-assets.py), in its 256-unit canvas:
// an outer ring of radius 92 and an inner one of 56, both 12 wide inside their radius, each
// with its own gap, and a dot on the outer ring. Without the plate the mark fills `bounds`,
// which the outer ring's edge touches, in `brush`; its stroke is never under `minimumStroke`.
// The monochrome tray icon and the hub's Tray icon tile draw it.
inline void DrawOrbitMark(ID2D1RenderTarget* target, const D2D1_RECT_F& bounds, float minimumStroke, ID2D1Brush* brush) {
    constexpr float kMarkCenter = 128, kOuterRadius = 92, kInnerRadius = 56, kMarkStroke = 12;
    constexpr float kOuterFrom = -30, kOuterTo = 280, kInnerFrom = 150, kInnerTo = 440;
    constexpr float kDotAngle = -50, kDotRadius = 16;
    constexpr float kMarkLeft = 36, kMarkExtent = 184;  // the outer ring's edge bounds the mark
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    if (!target || !brush) return;
    target->GetFactory(factory.GetAddressOf());
    if (!factory) return;
    const float scale = (bounds.right - bounds.left) / kMarkExtent;
    const float stroke = std::max(kMarkStroke * scale, minimumStroke);
    auto point = [&](float radius, float degrees) {
        const float angle = degrees * 3.14159265f / 180;
        return D2D1::Point2F(bounds.left + (kMarkCenter - kMarkLeft + radius * std::cos(angle)) * scale,
                             bounds.top + (kMarkCenter - kMarkLeft + radius * std::sin(angle)) * scale);
    };
    // Each ring's stroke lies inside its radius, as in the package icon.
    for (const auto& [radius, from, to] : {std::tuple{kOuterRadius, kOuterFrom, kOuterTo}, std::tuple{kInnerRadius, kInnerFrom, kInnerTo}}) {
        const float middle = radius - stroke / scale / 2;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(point(middle, from), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(point(middle, to), D2D1::SizeF(middle * scale, middle * scale), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, to - from > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            target->DrawGeometry(path.Get(), brush, stroke);
        }
    }
    target->FillEllipse(D2D1::Ellipse(point(kOuterRadius, kDotAngle), kDotRadius * scale, kDotRadius * scale), brush);
}
}
