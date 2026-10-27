#pragma once
#include "app_types.h"
#include "vi_tokens.h"

#include <d2d1.h>
#include <dwrite.h>
#include <initializer_list>

// The editor's layout rules (design/vi/tokens.json, editor), free of windows and input so the
// unit tests check them directly (tests/editor_tests.cpp).
namespace reel::editor {
// The control Enter clicks: the focused control when it is a button, on any page; otherwise the
// page's primary action (E1). `focusedClass` is the focused window's class name, or null when
// nothing in the editor has focus.
int EnterTarget(const wchar_t* focusedClass, int focusedId, int primary) noexcept;

// Device pixels for `dip` at `dpi`, rounded as the controls' positions are (MulDiv).
int Px(int dip, UINT dpi) noexcept;

// The picker's viewport is whole rows tall: `rows` rows of a row's own pixel height, so DIP
// rounding never trims the last row (E3).
int ViewportHeight(int rows, int rowDip, UINT dpi) noexcept;

// A stroke `dip` wide, drawn in whole device pixels (at least one), as a DIP width.
float StrokeDip(float dip, UINT dpi) noexcept;

// The rectangle, in DIP, that a control of `widthPx` x `heightPx` strokes its frame on: its own
// edges inset by half the stroke, so a hairline covers whole pixels and the controls on a row
// share one outline (E5).
D2D1_RECT_F FrameRect(int widthPx, int heightPx, float strokeDip, UINT dpi) noexcept;

// The width of the widest of `texts` set in `format`, measured with DirectWrite in the format's
// own locale (E4); 0 when nothing can be measured.
float WidestText(IDWriteFactory* factory, IDWriteTextFormat* format, std::initializer_list<const wchar_t*> texts);

// `over` composited on an opaque `under`.
Rgba Over(Rgba over, Rgba under) noexcept;
// The WCAG contrast ratio of two opaque colors.
double Contrast(Rgba first, Rgba second) noexcept;
}
