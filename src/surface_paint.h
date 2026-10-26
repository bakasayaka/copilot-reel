#pragma once
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <winrt/base.h>

namespace reel {
// Draws a composition surface's content into a bitmap of its own, `width` x `height` device
// pixels at `dpi`, and copies it to `offset`, where the atlas keeps the surface. Drawing in place
// would carry the offset as a fractional DIP translation (offset x 96 / dpi): pixel snapping and
// glyph positions would then depend on where the atlas puts the surface, and a position exactly
// half a device pixel off the grid could round either way whenever surfaces are added or removed.
// `draw` gets a cleared target in the surface's own DIP, with no transform; the atlas target
// comes back even if it throws.
template <class Draw>
void PaintLocal(ID2D1DeviceContext* context, POINT offset, UINT width, UINT height, float dpi, Draw&& draw) {
    struct Restore {
        ID2D1DeviceContext* context;
        winrt::com_ptr<ID2D1Image> target;
        ~Restore() { context->SetTarget(target.get()); }
    } restore{context};
    context->GetTarget(restore.target.put());
    winrt::com_ptr<ID2D1Bitmap1> local;
    winrt::check_hresult(context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi), local.put()));
    context->SetTarget(local.get());
    context->SetDpi(dpi, dpi);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->Clear(D2D1::ColorF(0, 0.f));
    draw(context);
    // One device pixel per bitmap pixel: the copy is exact.
    context->SetTarget(restore.target.get());
    context->SetDpi(96, 96);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    context->DrawBitmap(local.get(), D2D1::RectF(static_cast<float>(offset.x), static_cast<float>(offset.y),
        static_cast<float>(offset.x + static_cast<LONG>(width)), static_cast<float>(offset.y + static_cast<LONG>(height))),
        1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}
}
