#include "osd.h"
#include "appearance.h"
#include "artwork.h"
#include "display_names.h"
#include "editor_layout.h"
#include "iconography.h"
#include "localization.h"
#include "list_viewport.h"
#include "settings.h"
#include "typography.h"
#include "vi_tokens.h"

#include <commdlg.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite_2.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace reel {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t kOsdClass[] = L"CopilotReel.Osd";
// The sheet's grid (tokens.json editor), in DIP: margins of 24, controls 32 high, content rows of
// a label and a field every 72 from the top margin, and a footer row that ends a margin above the
// bottom edge. Every length sits on the 8 DIP grid.
constexpr int kWidth = vi::kEditorWidth, kHeight = vi::kEditorHeight, kMargin = vi::kEditorMargin, kGap = vi::kEditorGap;
constexpr int kControl = vi::kEditorControl, kRowPitch = vi::kEditorRowPitch, kLabelToField = vi::kEditorLabelToField;
constexpr int kLabelHeight = kLabelToField - 4, kTitleHeight = 28;  // a 14 DIP line, and a 20 DIP one
constexpr int kSideButton = vi::kEditorSideButton, kFooterY = kHeight - kMargin - kControl;
constexpr int kListRows = vi::kEditorListRows, kRowHeight = vi::kEditorRow, kListY = kMargin + kControl + kGap;
enum class Page { Editor, Advanced, Picker, Confirm };
enum Id : int {
    Back = 100, Save, Name, Target, Browse, Advanced, Kind, Arguments, Directory, Done, ChooseApp,
    Search, InstalledList, Pick, InstalledViewport, DontSave, Stay, LeaveTitle = 400, FirstLabel = 500
};
constexpr wchar_t kViewportClass[] = L"CopilotReel.ListViewport";
// Start's All apps arrive from a worker: the list, then each icon as it is extracted. The worker
// leaves them in an inbox both sides own and posts a bare notification, so nothing it produced
// is lost with a message the closing window never reads.
constexpr UINT kInstalledMessage = WM_APP + 30;
struct InstalledIcon { size_t index; std::vector<BYTE> pixels; };
struct InstalledInbox {
    std::mutex lock;
    bool closed = false, notified = false;
    std::optional<std::vector<InstalledApp>> apps;
    std::vector<InstalledIcon> icons;
};
// A picker row's Start icon and whether it carries its own plate, as the reel's artwork classes
// decide for a custom app's Shell icon (A1).
struct RowIcon { std::vector<BYTE> pixels; bool plated = false; };
HWND gOsd = nullptr;

D2D1_COLOR_F Color(Rgba value) { return {value.r, value.g, value.b, value.a}; }
COLORREF GdiColor(Rgba value) {
    return RGB(static_cast<BYTE>(value.r * 255), static_cast<BYTE>(value.g * 255), static_cast<BYTE>(value.b * 255));
}
Rgba Mix(Rgba first, Rgba second, float amount) {
    return {first.r + (second.r - first.r) * amount, first.g + (second.g - first.g) * amount,
        first.b + (second.b - first.b) * amount, 1};
}
std::wstring Text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<size_t>(std::max(0, copied)));
    return result;
}
bool EditorRequestValid(int index, size_t count) {
    return index >= -1 && index < static_cast<int>(count) && !(index == -1 && count >= 8);
}

struct State {
    HWND window = nullptr, notifyOwner = nullptr;
    UINT dpi = 96;
    // `leftPage` is where Cancel on the leave confirmation returns.
    Page page = Page::Editor, leftPage = Page::Editor;
    std::vector<AppTarget> apps;
    std::map<int, HWND> controls;
    // Text fields are borderless EDIT controls inside these painted frames (DIP, by control ID).
    std::map<int, RECT> frames;
    int selected = 0, nextLabel = FirstLabel;
    AppTarget draft;
    // `hidden`: never shown, focused or activated (tests/editor_tests.cpp).
    bool adding = false, dirty = false, creating = false, hidden = false;
    // What waits on the leave confirmation: closing, or opening another app's editor.
    struct Leave { bool open = false; int index = -1; std::optional<AppTarget> preset; } leave;
    bool updatingScroll = false;
    int scrollX = 0, scrollY = 0;
    // Start's All apps for the picker, read once per window; `visible` indexes the search result.
    std::vector<InstalledApp> installed;
    std::vector<RowIcon> icons;
    std::vector<size_t> visible;
    std::wstring query;
    UINT iconSize = 0;
    bool listing = false, listed = false;
    std::shared_ptr<InstalledInbox> inbox;
    // The picker list's pixel scroll; its windows are set while the picker page shows.
    ListViewport scroll;
    // The kind column, as wide as its widest label in the current language (E4).
    float kindColumn = 0;
    // The sheet's own words that did not fit where they were drawn, for the tests.
    std::set<std::wstring> trimmed;
    HFONT font = nullptr;
    HBRUSH surfaceBrush = nullptr, inputBrush = nullptr, seatBrush = nullptr;
    Appearance appearance;
    std::unique_ptr<AppearanceSource> source;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> textFactory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1DCRenderTarget> controlTarget;
    std::map<int, ComPtr<IDWriteTextFormat>> formats;
    Language formatLanguage = Language::English;
    ComPtr<IDWriteFontFallback> fallback;
    bool fallbackTried = false;
    ~State() {
        for (auto* brush : {surfaceBrush, inputBrush, seatBrush}) if (brush) DeleteObject(brush);
        if (font) DeleteObject(font);
    }
    int Px(int dip) const { return editor::Px(dip, dpi); }
    HWND Control(int id) const {
        const auto found = controls.find(id);
        return found == controls.end() ? nullptr : found->second;
    }
    // Fields and buttons take the reel's tile fill on the plate.
    Rgba Tile() const { return editor::Over(appearance.tile, appearance.surface); }
    // Disabled words: GRAYTEXT in a contrast theme (E7), otherwise the foreground at disabledOpacity.
    Rgba Disabled(Rgba under) const {
        if (appearance.highContrast) return appearance.disabledText;
        auto faded = appearance.foreground;
        faded.a = vi::kEditorDisabledOpacity;
        return editor::Over(faded, under);
    }
};
State* StateOf(HWND window) { return window ? reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA)) : nullptr; }

void BuildPage(State& state);
void NavigateBack(State& state);

const wchar_t* PageTitle(const State& state) {
    switch(state.page) {
    case Page::Advanced:return Tr(L"Advanced",L"高级选项",L"詳細設定");
    case Page::Picker:return Tr(L"Choose app",L"选择应用",L"アプリを選択");
    default:return state.adding?Tr(L"Add app",L"添加应用",L"アプリを追加"):Tr(L"Edit app",L"编辑应用",L"アプリを編集");
    }
}

void UpdateScrollbars(State& state) {
    if(state.updatingScroll)return;
    state.updatingScroll=true;
    // Derive the area before scrollbars. Measuring the already reduced client
    // area would make two unnecessary bars perpetuate each other's overflow.
    RECT client{};GetClientRect(state.window,&client);
    const auto style=GetWindowLongPtrW(state.window,GWL_STYLE);
    // The bars are the window's own, at its monitor's DPI; the sheet may draw at a capture's.
    const UINT frameDpi=GetDpiForWindow(state.window);
    const int barWidth=GetSystemMetricsForDpi(SM_CXVSCROLL,frameDpi);
    const int barHeight=GetSystemMetricsForDpi(SM_CYHSCROLL,frameDpi);
    const int fullWidth=client.right+((style&WS_VSCROLL)?barWidth:0);
    const int fullHeight=client.bottom+((style&WS_HSCROLL)?barHeight:0);
    bool horizontalNeeded=state.Px(kWidth)>fullWidth;
    bool verticalNeeded=state.Px(kHeight)>fullHeight;
    for(int pass=0;pass<2;++pass) {
        horizontalNeeded=state.Px(kWidth)>fullWidth-(verticalNeeded?barWidth:0);
        verticalNeeded=state.Px(kHeight)>fullHeight-(horizontalNeeded?barHeight:0);
    }
    ShowScrollBar(state.window,SB_HORZ,horizontalNeeded);
    ShowScrollBar(state.window,SB_VERT,verticalNeeded);
    GetClientRect(state.window,&client);
    SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE|SIF_POS,0,state.Px(kWidth)-1,
        static_cast<UINT>(std::max(1L,client.right)),state.scrollX,0};
    SCROLLINFO vertical{sizeof(vertical),SIF_RANGE|SIF_PAGE|SIF_POS,0,state.Px(kHeight)-1,
        static_cast<UINT>(std::max(1L,client.bottom)),state.scrollY,0};
    SetScrollInfo(state.window,SB_HORZ,&horizontal,TRUE);
    SetScrollInfo(state.window,SB_VERT,&vertical,TRUE);
    state.scrollX=GetScrollPos(state.window,SB_HORZ);
    state.scrollY=GetScrollPos(state.window,SB_VERT);
    state.updatingScroll=false;
}

void Scroll(State& state,int bar,int request) {
    SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(state.window,bar,&info);
    int position=info.nPos;
    switch(request) {
    case SB_LINEUP:position-=state.Px(32);break;
    case SB_LINEDOWN:position+=state.Px(32);break;
    case SB_PAGEUP:position-=static_cast<int>(info.nPage);break;
    case SB_PAGEDOWN:position+=static_cast<int>(info.nPage);break;
    case SB_THUMBTRACK:position=info.nTrackPos;break;
    case SB_TOP:position=info.nMin;break;
    case SB_BOTTOM:position=info.nMax;break;
    default:return;
    }
    info.fMask=SIF_POS;info.nPos=position;SetScrollInfo(state.window,bar,&info,TRUE);
    position=GetScrollPos(state.window,bar);
    const int dx=bar==SB_HORZ?state.scrollX-position:0;
    const int dy=bar==SB_VERT?state.scrollY-position:0;
    if(bar==SB_HORZ)state.scrollX=position;else state.scrollY=position;
    ScrollWindowEx(state.window,dx,dy,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE);
    RedrawWindow(state.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}

// The reel's CJK fallback (src/typography.h); the formats' locale picks its mapping.
IDWriteFontFallback* CjkFallback(State& state) {
    if (state.fallbackTried || !state.textFactory) return state.fallback.Get();
    state.fallbackTried = true;
    ComPtr<IDWriteFactory2> factory;
    if (FAILED(state.textFactory.As(&factory)) || FAILED(CreateCjkFallback(factory.Get(), ExecutableFolder(), &state.fallback)))
        state.fallback.Reset();
    return state.fallback.Get();
}

// The first of `families` the system has.
const wchar_t* InstalledFamily(State& state, std::initializer_list<const wchar_t*> families) {
    ComPtr<IDWriteFontCollection> fonts;
    if (state.textFactory && SUCCEEDED(state.textFactory->GetSystemFontCollection(&fonts)))
        for (const auto* name : families) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) return name;
        }
    return *(families.end() - 1);
}

// One text pipeline (P4): the sheet's words are set in the reel's face, Segoe UI Variable at the
// optical master of the drawn pixels, with the shared CJK fallback; formats carry the interface
// locale (zh-CN selects mainland CJK forms, ja-JP Japanese ones). Key 0 is the command glyph font.
IDWriteTextFormat* Format(State& state, float size, bool title = false, bool glyph = false) {
    if (state.formatLanguage != CurrentLanguage()) {
        state.formats.clear(); state.formatLanguage = CurrentLanguage();
    }
    const int key = glyph ? 0 : static_cast<int>(size * 10) + (title ? 10000 : 0);
    auto& format = state.formats[key];
    if (!format && state.textFactory) {
        const auto* family = glyph ? InstalledFamily(state, {vi::kIconFamily, vi::kIconFallbackFamily}) :
            InstalledFamily(state, {TextFamilyForPx(size * state.dpi / 96.f), vi::kTextFamily, vi::kTextFallbackFamily});
        state.textFactory->CreateTextFormat(family, nullptr,
            title ? static_cast<DWRITE_FONT_WEIGHT>(vi::kEditorTitleWeight) : static_cast<DWRITE_FONT_WEIGHT>(vi::kEditorBodyWeight),
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, glyph ? static_cast<float>(vi::kEditorGlyphSize) : size,
            LocaleName(state.formatLanguage), &format);
        if (format) {
            ComPtr<IDWriteTextFormat1> withFallback;
            if (auto* fallback = glyph ? nullptr : CjkFallback(state); fallback && SUCCEEDED(format.As(&withFallback)))
                withFallback->SetFontFallback(fallback);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            state.textFactory->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
            format->SetTrimming(&trimming, ellipsis.Get());
        }
    }
    return format.Get();
}
// Draws `text` centered on `rect`'s height. The sheet's own words (`chrome`) are expected to fit:
// one that does not is recorded for the tests.
void DrawText(State& state, ID2D1RenderTarget* target, const std::wstring& text,
    D2D1_RECT_F rect, Rgba color, float size, bool title = false,
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING, bool chrome = true) {
    auto* format = Format(state, size, title);
    if (!format) return;
    format->SetTextAlignment(alignment);
    if (chrome && editor::WidestText(state.textFactory.Get(), format, {text.c_str()}) > rect.right - rect.left + .5f)
        state.trimmed.insert(text);
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(Color(color), &brush))) return;
    // A name with kana is Japanese in every interface language, so its kanji take Japanese forms.
    ComPtr<IDWriteTextLayout> layout;
    if (HasKana(text) && state.formatLanguage != Language::Japanese &&
        SUCCEEDED(state.textFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
            rect.right - rect.left, rect.bottom - rect.top, &layout)) &&
        SUCCEEDED(layout->SetLocaleName(vi::kJapaneseLocale, {0, static_cast<UINT32>(text.size())})))
        target->DrawTextLayout({rect.left, rect.top}, layout.Get(), brush.Get());
    else target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush.Get());
}
// A Segoe Fluent Icons glyph centered on `rect` (P4).
void DrawGlyph(State& state, ID2D1RenderTarget* target, wchar_t glyph, D2D1_RECT_F rect, Rgba color) {
    auto* format = Format(state, 0, false, true);
    ComPtr<ID2D1SolidColorBrush> brush;
    if (!format || FAILED(target->CreateSolidColorBrush(Color(color), &brush))) return;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    target->DrawTextW(&glyph, 1, format, rect, brush.Get());
}

// A control's look (P4). Accent means one thing, as on the plate: focus and selection are the
// seat (the selection fill and an accent stroke), and the primary action is accent-filled. In a
// contrast theme focus is a WINDOWTEXT ring outside the fill, so it never merges with a
// HIGHLIGHT fill (E7).
struct Look {
    Rgba fill, stroke, text;
    float width = 0;           // the outline in DIP; 0 draws none
    Rgba ring{};
    bool ringInside = false;   // the focused primary action: a ring inside its accent fill
    bool ringOutside = false;  // focus in a contrast theme
};
Look ControlLook(const State& state, bool primary, bool focused, bool pressed, bool disabled) {
    const auto& a = state.appearance;
    Look look{state.Tile(), a.outline, disabled ? state.Disabled(state.Tile()) : a.foreground, vi::kEditorStroke};
    if (primary) {
        look.fill = pressed ? Mix(a.accent, a.surface, .1f) : a.accent;
        look.text = a.textOnAccent;
        look.width = 0;  // an accent stroke at rest would read as focus (E11)
    } else if (pressed || (focused && !a.highContrast)) {
        look.fill = a.selection;
        look.text = a.selectionText;
    }
    if (focused) {
        if (a.highContrast) { look.ring = a.foreground; look.ringOutside = true; }
        else if (primary) { look.ring = a.textOnAccent; look.ringInside = true; }
        else { look.stroke = a.accent; look.width = vi::kEditorFocusStroke; }
    }
    return look;
}
void RoundRect(ID2D1RenderTarget* target, D2D1_RECT_F rect, float radius, Rgba fill, Rgba stroke, float width) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(Color(fill), &brush))) return;
    const auto rounded = D2D1::RoundedRect(rect, radius, radius);
    if (fill.a > 0) target->FillRoundedRectangle(rounded, brush.Get());
    if (width > 0) {
        brush->SetColor(Color(stroke));
        target->DrawRoundedRectangle(rounded, brush.Get(), width);
    }
}
// One frame for every control (E5): `px` is the control's rectangle in device pixels on the
// target, and the outline runs on its own edges inset by half the stroke, so a hairline covers
// whole pixels and the fields and buttons of a row share their top and bottom. A text field adds
// a bottom stroke in `secondary` under its soft rim, at least 3:1 against its fill (E6).
void DrawFrame(State& state, ID2D1RenderTarget* target, RECT px, const Look& look, bool input) {
    const float scale = 96.f / state.dpi, x = px.left * scale, y = px.top * scale, radius = vi::kEditorRadius;
    const auto place = [&](D2D1_RECT_F rect, float inset) {
        return D2D1::RectF(x + rect.left + inset, y + rect.top + inset, x + rect.right - inset, y + rect.bottom - inset);
    };
    const int width = px.right - px.left, height = px.bottom - px.top;
    if (look.ringOutside) {
        const float ring = editor::StrokeDip(vi::kSeatStrokeHighContrast, state.dpi);
        const auto edge = place(editor::FrameRect(width, height, ring, state.dpi), 0);
        RoundRect(target, edge, radius - ring / 2, Rgba{0, 0, 0, 0}, look.ring, ring);
        RoundRect(target, place(editor::FrameRect(width, height, ring, state.dpi), ring + scale), radius - 1.5f * ring - scale,
            look.fill, look.fill, 0);
        return;
    }
    const float stroke = editor::StrokeDip(look.width > 0 ? look.width : vi::kEditorStroke, state.dpi);
    const auto frame = place(editor::FrameRect(width, height, stroke, state.dpi), 0);
    RoundRect(target, frame, radius - stroke / 2, look.fill, look.stroke, look.width > 0 ? stroke : 0);
    if (look.ringInside) {
        const float ring = editor::StrokeDip(vi::kEditorFocusStroke, state.dpi), inset = 2 * ring;
        RoundRect(target, D2D1::RectF(frame.left + inset, frame.top + inset, frame.right - inset, frame.bottom - inset),
            radius - inset, Rgba{0, 0, 0, 0}, look.ring, ring);
    }
    if (input && !state.appearance.highContrast && look.width == vi::kEditorStroke) {
        ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(target->CreateSolidColorBrush(Color(state.appearance.secondary), &brush)))
            target->DrawLine({frame.left + radius, frame.bottom}, {frame.right - radius, frame.bottom}, brush.Get(), stroke);
    }
}
RECT PxRect(const State& state, int x, int y, int width, int height) {
    const int left = state.Px(x), top = state.Px(y);
    return {left, top, left + state.Px(width), top + state.Px(height)};
}

HRESULT PaintSheet(State& state, ID2D1RenderTarget* target) {
    target->BeginDraw();
    target->Clear(Color(state.appearance.surface));
    target->SetTransform(D2D1::Matrix3x2F::Translation(-state.scrollX*96.f/state.dpi,-state.scrollY*96.f/state.dpi));
    for (const auto& [id,box] : state.frames) {
        const HWND field = state.Control(id);
        const bool focused = field && GetFocus() == field;
        DrawFrame(state,target,PxRect(state,box.left,box.top,box.right-box.left,box.bottom-box.top),
            ControlLook(state,false,focused,false,field && !IsWindowEnabled(field)),true);
        if (id == Search)
            DrawGlyph(state,target,vi::kEditorGlyphSearch,D2D1::RectF(box.left+12.f,static_cast<float>(box.top),
                box.left+12.f+vi::kEditorGlyphSize,static_cast<float>(box.bottom)),state.appearance.secondary);
    }
    if (state.page == Page::Picker) {
        // The list hides while there is nothing to show; its area then explains why.
        const float width = static_cast<float>(kWidth), listMiddle = kListY + kListRows*kRowHeight/2.f;
        if (!state.listed || state.visible.empty()) {
            const auto* message = !state.listed ? Tr(L"Reading apps from Start…",L"正在读取开始菜单中的应用…",L"スタートからアプリを読み込んでいます…") :
                state.installed.empty() ? Tr(L"No apps were found in Start.",L"开始菜单中没有找到应用。",L"スタートにアプリが見つかりませんでした。") :
                Tr(L"No apps match your search.",L"没有匹配的应用。",L"検索に一致するアプリはありません。");
            DrawText(state,target,message,D2D1::RectF(kMargin,listMiddle-12,width-kMargin,listMiddle+12),
                state.appearance.secondary,vi::kEditorBodySize,false,DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (state.listed) {
            const float left = static_cast<float>(kMargin+kSideButton+2*kGap);
            DrawText(state,target,std::to_wstring(state.visible.size())+Tr(L" apps",L" 个应用",L" 個のアプリ"),
                D2D1::RectF(left,kFooterY,width-kMargin-kSideButton-2*kGap,kFooterY+kControl),state.appearance.secondary,vi::kEditorCaptionSize);
        }
    }
    return target->EndDraw();
}
void Paint(State& state) {
    PAINTSTRUCT paint{};
    BeginPaint(state.window, &paint);
    RECT client{}; GetClientRect(state.window, &client);
    if (!state.target && state.factory) {
        state.factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(state.window, D2D1::SizeU(client.right, client.bottom)), &state.target);
        if (state.target) state.target->SetDpi(static_cast<float>(state.dpi), static_cast<float>(state.dpi));
    }
    if (state.target && PaintSheet(state, state.target.Get()) == D2DERR_RECREATE_TARGET) {
        state.target.Reset();
        InvalidateRect(state.window,nullptr,FALSE);
    }
    EndPaint(state.window, &paint);
}

bool BindControlTarget(State& state, HDC dc, RECT rectangle) {
    if (!state.controlTarget && state.factory) {
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        state.factory->CreateDCRenderTarget(&properties, &state.controlTarget);
    }
    if (!state.controlTarget || FAILED(state.controlTarget->BindDC(dc,&rectangle))) return false;
    state.controlTarget->SetDpi(static_cast<float>(state.dpi),static_cast<float>(state.dpi));
    return true;
}
// Paints the sheet into a device context: printing, and the tests' captures of a hidden editor.
void PrintSheet(State& state, HDC dc) {
    RECT client{}; GetClientRect(state.window, &client);
    if (BindControlTarget(state, dc, client) && PaintSheet(state, state.controlTarget.Get()) == D2DERR_RECREATE_TARGET)
        state.controlTarget.Reset();
}

std::wstring ComboItemText(HWND control,int item) {
    if(item<0)item=static_cast<int>(SendMessageW(control,CB_GETCURSEL,0,0));
    if(item<0)return {};
    const LRESULT length=SendMessageW(control,CB_GETLBTEXTLEN,item,0);
    if(length==CB_ERR)return {};
    std::wstring text(static_cast<size_t>(length)+1,L'\0');
    const LRESULT copied=SendMessageW(control,CB_GETLBTEXT,item,reinterpret_cast<LPARAM>(text.data()));
    text.resize(copied==CB_ERR?0:static_cast<size_t>(copied));
    return text;
}

void DrawClosedCombo(State& state,HWND control,HDC dc) {
    RECT client{};GetClientRect(control,&client);
    if(!BindControlTarget(state,dc,client))return;
    auto* target=state.controlTarget.Get();
    const float width=client.right*96.f/state.dpi,height=client.bottom*96.f/state.dpi;
    const bool open=GetFocus()==control||SendMessageW(control,CB_GETDROPPEDSTATE,0,0)!=0;
    const auto look=ControlLook(state,false,open,false,!IsWindowEnabled(control));
    target->BeginDraw();target->Clear(Color(state.appearance.surface));
    DrawFrame(state,target,client,look,true);
    DrawText(state,target,ComboItemText(control,-1),D2D1::RectF(12,0,width-12-vi::kEditorGlyphSize-kGap,height),look.text,vi::kEditorBodySize);
    DrawGlyph(state,target,vi::kEditorGlyphExpand,D2D1::RectF(width-12-vi::kEditorGlyphSize,0,width-12,height),state.appearance.secondary);
    if(target->EndDraw()==D2DERR_RECREATE_TARGET)state.controlTarget.Reset();
}

LRESULT CALLBACK ComboProc(HWND control,UINT message,WPARAM wParam,LPARAM lParam,
    UINT_PTR subclassId,DWORD_PTR reference) {
    auto* state=reinterpret_cast<State*>(reference);
    if(message==WM_NCDESTROY) {
        RemoveWindowSubclass(control,ComboProc,subclassId);
        return DefSubclassProc(control,message,wParam,lParam);
    }
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};BeginPaint(control,&paint);
        DrawClosedCombo(*state,control,paint.hdc);EndPaint(control,&paint);return 0;
    }
    if(message==WM_PRINTCLIENT) {
        DrawClosedCombo(*state,control,reinterpret_cast<HDC>(wParam));return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    const auto result=DefSubclassProc(control,message,wParam,lParam);
    if(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==CB_SETCURSEL||
        message==CB_SHOWDROPDOWN||message==WM_ENABLE||message==WM_THEMECHANGED)
        InvalidateRect(control,nullptr,FALSE);
    return result;
}

// Arrow and page keys in the search box move the list's selection, so typing and choosing
// never needs the mouse or a Tab; Enter then picks it through IDOK.
LRESULT CALLBACK SearchProc(HWND control,UINT message,WPARAM wParam,LPARAM lParam,
    UINT_PTR subclassId,DWORD_PTR reference) {
    auto* state=reinterpret_cast<State*>(reference);
    if(message==WM_NCDESTROY) {
        RemoveWindowSubclass(control,SearchProc,subclassId);
        return DefSubclassProc(control,message,wParam,lParam);
    }
    if(message==WM_KEYDOWN && (wParam==VK_DOWN||wParam==VK_UP||wParam==VK_NEXT||wParam==VK_PRIOR)) {
        const HWND list=state->Control(InstalledList);
        if(list && IsWindowVisible(list)) {SendMessageW(list,message,wParam,lParam);return 0;}
    }
    return DefSubclassProc(control,message,wParam,lParam);
}

// List scrollbars follow the Windows theme; the classic light bar shows on a dark surface otherwise.
void ThemeList(State& state,HWND list) {
    if(list)SetWindowTheme(list,state.appearance.dark && !state.appearance.highContrast?L"DarkMode_Explorer":L"Explorer",nullptr);
}

// The picker list scrolls by pixels inside this viewport (ListViewport, src/list_viewport.h).
LRESULT CALLBACK ViewportProc(HWND viewport,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(viewport,GWLP_USERDATA));
    switch(message) {
    // The list's owner-draw, color and notification messages belong to the editor window.
    case WM_DRAWITEM:case WM_MEASUREITEM:case WM_CTLCOLORLISTBOX:
        return SendMessageW(GetParent(viewport),message,wParam,lParam);
    case WM_COMMAND:
        if(state && LOWORD(wParam)==InstalledList && HIWORD(wParam)==LBN_SELCHANGE)state->scroll.RevealSelection();
        return SendMessageW(GetParent(viewport),message,wParam,lParam);
    case WM_VSCROLL:if(state)state->scroll.ScrollBar(LOWORD(wParam));return 0;
    case WM_MOUSEWHEEL:if(state)state->scroll.Wheel(GET_WHEEL_DELTA_WPARAM(wParam));return 0;
    case WM_ERASEBKGND:return 1;  // the LISTBOX always covers the client area
    }
    return DefWindowProcW(viewport,message,wParam,lParam);
}

LRESULT CALLBACK ListProc(HWND list,UINT message,WPARAM wParam,LPARAM lParam,
    UINT_PTR subclassId,DWORD_PTR reference) {
    auto* state=reinterpret_cast<State*>(reference);
    if(message==WM_NCDESTROY) {
        RemoveWindowSubclass(list,ListProc,subclassId);
        return DefSubclassProc(list,message,wParam,lParam);
    }
    if(message==WM_MOUSEWHEEL) {state->scroll.Wheel(GET_WHEEL_DELTA_WPARAM(wParam));return 0;}
    if(message==WM_KEYDOWN && (wParam==VK_NEXT||wParam==VK_PRIOR)) {
        const int selected=state->scroll.PageSelection(wParam==VK_NEXT);
        if(selected<0)return 0;
        SendMessageW(list,LB_SETCURSEL,selected,0);
        SendMessageW(GetParent(list),WM_COMMAND,MAKEWPARAM(InstalledList,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
        return 0;
    }
    return DefSubclassProc(list,message,wParam,lParam);
}

void StyleCombo(State& state,HWND control) {
    // The closed field is the item height plus a 1 DIP border on each side: a 32 DIP field.
    SendMessageW(control,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),state.Px(kControl-2));
    SendMessageW(control,CB_SETITEMHEIGHT,0,state.Px(kControl));
    SetWindowSubclass(control,ComboProc,1,reinterpret_cast<DWORD_PTR>(&state));
}

// The tile the app gets on the reel (P4): a Start icon with its own plate fills the tile shape;
// any other sits at the glyph keyline on the neutral tile, as the reel's artwork classes draw them.
void DrawTile(State& state,ID2D1RenderTarget* target,const InstalledApp& app,size_t index,D2D1_RECT_F tile,Rgba glyph) {
    const auto& a=state.appearance;
    const auto shape=D2D1::RoundedRect(tile,vi::kTileRadius,vi::kTileRadius);
    const RowIcon* row=index<state.icons.size() && !state.icons[index].pixels.empty()?&state.icons[index]:nullptr;
    ComPtr<ID2D1Bitmap> icon;
    if(row)target->CreateBitmap(D2D1::SizeU(state.iconSize,state.iconSize),row->pixels.data(),state.iconSize*4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&icon);
    if(icon && row->plated) {
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        ComPtr<ID2D1Layer> layer;
        if(SUCCEEDED(state.factory->CreateRoundedRectangleGeometry(shape,&clip)) && SUCCEEDED(target->CreateLayer(&layer))) {
            target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),layer.Get());
            target->DrawBitmap(icon.Get(),tile,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            target->PopLayer();
            return;
        }
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if(FAILED(target->CreateSolidColorBrush(Color(a.tile),&brush)))return;
    target->FillRoundedRectangle(shape,brush.Get());
    const float stroke=editor::StrokeDip(vi::kTileStroke,state.dpi),half=stroke/2;
    brush->SetColor(Color(a.tileStroke));
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tile.left+half,tile.top+half,tile.right-half,tile.bottom-half),
        vi::kTileRadius-half,vi::kTileRadius-half),brush.Get(),stroke);
    const float x=(tile.left+tile.right)/2,y=(tile.top+tile.bottom)/2,keyline=vi::kArtworkKeyline/2;
    if(icon)target->DrawBitmap(icon.Get(),D2D1::RectF(x-keyline,y-keyline,x+keyline,y+keyline),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else {
        brush->SetColor(Color(glyph));
        DrawAppSymbol(target,app.target,D2D1::RectF(x-10,y-10,x+10,y+10),brush.Get(),1.5f);
    }
}
// A picker row: the app's tile, its name and its kind. The selected row is the seat whether or
// not the list has focus, so Enter never picks a row the user cannot see (E2).
void DrawRow(State& state,ID2D1RenderTarget* target,const DRAWITEMSTRUCT& item,int widthPx,int heightPx) {
    if(item.itemID>=state.visible.size())return;
    const auto index=state.visible[item.itemID];
    const auto& app=state.installed[index];
    const auto& a=state.appearance;
    const bool selected=(item.itemState&ODS_SELECTED)!=0;
    const float width=widthPx*96.f/state.dpi,height=heightPx*96.f/state.dpi;
    if(selected)DrawFrame(state,target,{0,0,widthPx,heightPx},
        Look{a.selection,a.accent,a.selectionText,a.highContrast?0.f:vi::kEditorFocusStroke},false);
    const auto text=selected?a.selectionText:a.foreground;
    const float inset=(height-vi::kTile)/2;
    const auto tile=D2D1::RectF(inset,inset,inset+vi::kTile,inset+vi::kTile);
    // The glyph sits on the tile, never on the seat: it keeps the foreground (HIGHLIGHTTEXT would
    // vanish on a contrast theme's window-colored tile).
    DrawTile(state,target,app,index,tile,a.foreground);
    const float column=state.kindColumn,right=width-12;
    DrawText(state,target,app.target.name,D2D1::RectF(tile.right+12,0,right-column-2*kGap,height),text,vi::kEditorBodySize,
        false,DWRITE_TEXT_ALIGNMENT_LEADING,false);
    DrawText(state,target,app.packaged?Tr(L"Packaged app",L"打包应用",L"パッケージ アプリ"):Tr(L"Desktop app",L"桌面应用",L"デスクトップ アプリ"),
        D2D1::RectF(right-column,0,right,height),selected?a.selectionText:a.secondary,vi::kEditorCaptionSize,false,DWRITE_TEXT_ALIGNMENT_TRAILING);
}
void DrawButton(State& state,ID2D1RenderTarget* target,const DRAWITEMSTRUCT& item,int widthPx,int heightPx) {
    const bool disabled=(item.itemState&ODS_DISABLED)!=0,pressed=(item.itemState&ODS_SELECTED)!=0;
    const bool focused=(item.itemState&ODS_FOCUS)!=0 && !(item.itemState&ODS_NOFOCUSRECT);
    // A disabled primary button (Choose with nothing to choose) draws as an ordinary disabled one.
    const bool primary=(item.CtlID==Save||item.CtlID==Done||item.CtlID==Pick) && !disabled;
    const auto look=ControlLook(state,primary,focused,pressed,disabled);
    DrawFrame(state,target,{0,0,widthPx,heightPx},look,false);
    const float width=widthPx*96.f/state.dpi,height=heightPx*96.f/state.dpi;
    const auto text=Text(item.hwndItem);
    // A glyph leads Back and Browse… and trails Advanced (P4).
    const wchar_t glyph=item.CtlID==Back?vi::kEditorGlyphBack:item.CtlID==Browse?vi::kEditorGlyphBrowse:
        item.CtlID==Advanced?vi::kEditorGlyphAdvanced:L'\0';
    if(!glyph) {
        DrawText(state,target,text,D2D1::RectF(vi::kEditorButtonPadding,0,width-vi::kEditorButtonPadding,height),look.text,
            vi::kEditorBodySize,false,DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    const float room=width-2*vi::kEditorButtonPadding-vi::kEditorGlyphSize-kGap;
    const float words=(std::min)(editor::WidestText(state.textFactory.Get(),Format(state,vi::kEditorBodySize),{text.c_str()}),room);
    const float left=(width-vi::kEditorGlyphSize-kGap-words)/2;
    const bool trailing=item.CtlID==Advanced;
    const float glyphLeft=trailing?left+words+kGap:left,textLeft=trailing?left:left+vi::kEditorGlyphSize+kGap;
    DrawGlyph(state,target,glyph,D2D1::RectF(glyphLeft,0,glyphLeft+vi::kEditorGlyphSize,height),look.text);
    DrawText(state,target,text,D2D1::RectF(textLeft,0,textLeft+room,height),look.text,vi::kEditorBodySize);
}
void DrawControl(State& state, const DRAWITEMSTRUCT& item) {
    if (item.CtlType == ODT_LISTBOX && item.itemID == static_cast<UINT>(-1)) return;
    if (!BindControlTarget(state,item.hDC,item.rcItem)) return;
    auto* target = state.controlTarget.Get();
    const int widthPx = item.rcItem.right-item.rcItem.left, heightPx = item.rcItem.bottom-item.rcItem.top;
    const float width = widthPx*96.f/state.dpi, height = heightPx*96.f/state.dpi;
    const bool selected = (item.itemState&ODS_SELECTED) != 0;
    target->BeginDraw();
    target->Clear(Color(state.appearance.surface));
    switch (item.CtlType) {
    case ODT_STATIC:  // labels and the leave question, in the sheet's own text pipeline (P4)
        DrawText(state,target,Text(item.hwndItem),D2D1::RectF(0,0,width,height),state.appearance.foreground,
            item.CtlID==LeaveTitle?vi::kEditorTitleSize:vi::kEditorBodySize,item.CtlID==LeaveTitle);
        break;
    case ODT_COMBOBOX:
        target->Clear(Color(selected?state.appearance.selection:state.Tile()));
        DrawText(state,target,ComboItemText(item.hwndItem,static_cast<int>(item.itemID)),D2D1::RectF(12,0,width-12,height),
            selected?state.appearance.selectionText:state.appearance.foreground,vi::kEditorBodySize);
        break;
    case ODT_LISTBOX:DrawRow(state,target,item,widthPx,heightPx);break;
    default:DrawButton(state,target,item,widthPx,heightPx);break;
    }
    if (target->EndDraw() == D2DERR_RECREATE_TARGET) state.controlTarget.Reset();
}

HWND Control(State& state, int id, const wchar_t* type, const wchar_t* text, DWORD style,
    int x, int y, int width, int height, DWORD extended = 0) {
    HWND control=CreateWindowExW(extended,type,text,WS_CHILD|WS_VISIBLE|style,
        state.Px(x)-state.scrollX,state.Px(y)-state.scrollY,state.Px(width),state.Px(height),state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    state.controls[id]=control;
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(state.font),TRUE);
    return control;
}
void Button(State& state,int id,const wchar_t* text,int x,int y,int width=kSideButton,int height=kControl) {
    Control(state,id,L"BUTTON",text,WS_TABSTOP|BS_OWNERDRAW,x,y,width,height);
}
// Labels are owner-drawn, so they share the D2D text of the buttons, while their window text still
// names the field that follows them for assistive technology.
void Label(State& state,const wchar_t* text,int x,int y,int width,int height=kLabelHeight,int id=0) {
    Control(state,id?id:state.nextLabel++,L"STATIC",text,SS_OWNERDRAW,x,y,width,height);
}
// A single-line EDIT draws its text at its top, so a bordered one never centers it. The field
// is a painted rounded frame like the buttons and the combo box (PaintSheet), with a borderless
// EDIT one text line high centered inside it, after `lead` DIP for a glyph.
HWND InputField(State& state,int id,const std::wstring& value,int x,int y,int width,int limit=8192,int lead=0) {
    constexpr int line=20,inset=12;
    state.frames[id]=RECT{x,y,x+width,y+kControl};
    const auto control=Control(state,id,L"EDIT",value.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,
        x+inset+lead,y+(kControl-line)/2,width-2*inset-lead,line);
    SendMessageW(control,EM_SETLIMITTEXT,limit,0);
    SendMessageW(control,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
    return control;
}
// The label spans the content width whatever the field's own width.
void EditField(State& state,int id,const wchar_t* label,const std::wstring& value,
    int x,int y,int width,int limit=8192) {
    Label(state,label,x,y,kWidth-2*kMargin);
    InputField(state,id,value,x,y+kLabelToField,width,limit);
}
// The text fields' GDI font: Segoe UI, Yu Gothic UI in Japanese and Microsoft YaHei UI in Chinese,
// so each script takes its own forms (E8).
void UpdateFont(State& state) {
    auto font=CreateFontW(-state.Px(static_cast<int>(vi::kEditorBodySize)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,
        ControlFamily(CurrentLanguage()));
    if (font) { if(state.font)DeleteObject(state.font); state.font=font; }
}
void ResizeToPage(State& state) {
    state.updatingScroll=true;
    ShowScrollBar(state.window,SB_BOTH,FALSE);
    RECT bounds{0,0,state.Px(kWidth),state.Px(kHeight)};
    const DWORD style=static_cast<DWORD>(GetWindowLongPtrW(state.window,GWL_STYLE))&~(WS_HSCROLL|WS_VSCROLL);
    // The client holds the sheet at its DPI; the frame around it is the window's, at its monitor's.
    AdjustWindowRectExForDpi(&bounds,style,FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(state.window,GWL_EXSTYLE)),GetDpiForWindow(state.window));
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(state.window,MONITOR_DEFAULTTONEAREST),&monitor);
    RECT current{};GetWindowRect(state.window,&current);
    const LONG width=std::min(bounds.right-bounds.left,monitor.rcWork.right-monitor.rcWork.left);
    const LONG height=std::min(bounds.bottom-bounds.top,monitor.rcWork.bottom-monitor.rcWork.top);
    const LONG x=std::clamp(current.left,monitor.rcWork.left,monitor.rcWork.right-width);
    const LONG y=std::clamp(current.top,monitor.rcWork.top,monitor.rcWork.bottom-height);
    SetWindowPos(state.window,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    state.updatingScroll=false;
    UpdateScrollbars(state);
}

// Lists the installed apps whose name matches the search and selects the first, so Enter picks it.
void Filter(State& state) {
    state.visible.clear();
    for(size_t i=0;i<state.installed.size();++i)
        if(MatchesQuery(state.installed[i].target.name,state.query))state.visible.push_back(i);
    if(const auto list=state.Control(InstalledList)) {
        SendMessageW(list,WM_SETREDRAW,FALSE,0);
        SendMessageW(list,LB_RESETCONTENT,0,0);
        for(const auto index:state.visible)
            SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(state.installed[index].target.name.c_str()));
        SendMessageW(list,LB_SETCURSEL,state.visible.empty()?-1:0,0);
        state.scroll.offset=0;state.scroll.Layout();
        SendMessageW(list,WM_SETREDRAW,TRUE,0);
        InvalidateRect(list,nullptr,TRUE);
        // An empty list would be a blank box; PaintSheet explains the empty or loading state instead.
        ShowWindow(state.Control(InstalledViewport),state.listed && !state.visible.empty()?SW_SHOWNA:SW_HIDE);
    }
    EnableWindow(state.Control(Pick),!state.visible.empty());
    InvalidateRect(state.window,nullptr,FALSE);
}

void BuildPage(State& state) {
    state.creating=true;
    for (const auto& [id,control]:state.controls) { (void)id; DestroyWindow(control); }
    state.controls.clear(); state.frames.clear(); state.scroll={}; state.nextLabel=FirstLabel;
    state.scrollX=0;state.scrollY=0;
    const auto windowTitle=std::wstring(L"Copilot Reel — ")+PageTitle(state);
    SetWindowTextW(state.window,windowTitle.c_str());
    ResizeToPage(state);
    // Content rows, the right-hand side buttons of a field, and the footer's primary button. The
    // caption carries the title and Close, so the sheet starts with its first row (P4).
    const auto row=[](int index){return kMargin+index*kRowPitch;};
    constexpr int contentWidth=kWidth-2*kMargin, primaryX=kWidth-kMargin-kSideButton;
    switch(state.page) {
    case Page::Editor: {
        EditField(state,Name,Tr(L"Name",L"名称",L"名前"),AppDisplayName(state.draft),kMargin,row(0),contentWidth,128);
        // The target shares its row with two equal buttons, each kGap apart.
        const int targetWidth=contentWidth-2*(kSideButton+kGap), fieldY=row(1)+kLabelToField;
        EditField(state,Target,state.draft.kind==TargetKind::Aumid?
            Tr(L"Application ID (AUMID)",L"应用 ID (AUMID)",L"アプリ ID (AUMID)"):
            Tr(L"Open with · app, shortcut or website",L"打开目标 · 应用、快捷方式或网页",L"開く対象 · アプリ、ショートカット、Web サイト"),state.draft.target,kMargin,row(1),targetWidth);
        Button(state,ChooseApp,Tr(L"Choose app…",L"选择应用…",L"アプリを選択…"),kMargin+targetWidth+kGap,fieldY);
        Button(state,Browse,Tr(L"Browse…",L"浏览…",L"参照…"),primaryX,fieldY);
        EnableWindow(state.Control(Browse),state.draft.kind==TargetKind::Shell);
        // Secondary navigation sits opposite the primary action in the footer.
        Button(state,Advanced,Tr(L"Advanced",L"高级选项",L"詳細設定"),kMargin,kFooterY);
        Button(state,Save,Tr(L"Save",L"保存",L"保存"),primaryX,kFooterY);
        break;
    }
    case Page::Advanced: {
        Label(state,Tr(L"Launch method",L"启动方式",L"起動方法"),kMargin,row(0),contentWidth);
        const auto combo=Control(state,Kind,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,
            kMargin,row(0)+kLabelToField,contentWidth,150);
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Application, shortcut or URI",L"应用、快捷方式或 URI",L"アプリ、ショートカット、URI")));
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Tr(L"Packaged application (AUMID)",L"打包应用 (AUMID)",L"パッケージ アプリ (AUMID)")));
        SendMessageW(combo,CB_SETCURSEL,state.draft.kind==TargetKind::Aumid?1:0,0);
        StyleCombo(state,combo);
        EditField(state,Arguments,Tr(L"Arguments (optional)",L"启动参数（可选）",L"引数 (省略可)"),state.draft.arguments,kMargin,row(1),contentWidth);
        EditField(state,Directory,Tr(L"Working directory (optional)",L"工作目录（可选）",L"作業フォルダー (省略可)"),state.draft.workingDirectory,kMargin,row(2),contentWidth);
        EnableWindow(state.Control(Directory),state.draft.kind==TargetKind::Shell);
        Button(state,Done,Tr(L"Done",L"完成",L"完了"),primaryX,kFooterY);
        break;
    }
    case Page::Picker: {
        const auto search=InputField(state,Search,state.query,kMargin,kMargin,contentWidth,128,vi::kEditorGlyphSize+kGap);
        SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(Tr(L"Search apps",L"搜索应用",L"アプリを検索")));
        SetWindowSubclass(search,SearchProc,1,reinterpret_cast<DWORD_PTR>(&state));
        // Whole rows at rest: the viewport is five rows of the row's own pixel height, since a
        // half-cut last row reads as a rendering fault (E3).
        const int view=editor::ViewportHeight(kListRows,kRowHeight,state.dpi);
        const auto viewport=Control(state,InstalledViewport,kViewportClass,L"",WS_VSCROLL|WS_CLIPCHILDREN,
            kMargin,kListY,contentWidth,kListRows*kRowHeight,WS_EX_CONTROLPARENT);
        SetWindowPos(viewport,nullptr,0,0,state.Px(contentWidth),view,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowLongPtrW(viewport,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        const auto list=CreateWindowExW(0,L"LISTBOX",Tr(L"Installed apps",L"已安装的应用",L"インストール済みのアプリ"),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_NOTIFY|LBS_HASSTRINGS|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT,
            0,0,state.Px(contentWidth),view,viewport,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(InstalledList)),GetModuleHandleW(nullptr),nullptr);
        state.controls[InstalledList]=list;
        SendMessageW(list,WM_SETFONT,reinterpret_cast<WPARAM>(state.font),TRUE);
        SetWindowSubclass(list,ListProc,1,reinterpret_cast<DWORD_PTR>(&state));
        SendMessageW(list,LB_SETITEMHEIGHT,0,state.Px(kRowHeight));
        state.scroll.viewport=viewport;state.scroll.list=list;state.scroll.row=state.Px(kRowHeight);
        ThemeList(state,viewport);
        // The kind column is as wide as its widest label in this language (E4).
        state.kindColumn=editor::WidestText(state.textFactory.Get(),Format(state,vi::kEditorCaptionSize),
            {Tr(L"Packaged app",L"打包应用",L"パッケージ アプリ"),Tr(L"Desktop app",L"桌面应用",L"デスクトップ アプリ")});
        Button(state,Back,Tr(L"Back",L"返回",L"戻る"),kMargin,kFooterY);
        Button(state,Pick,Tr(L"Choose",L"选择",L"選択"),primaryX,kFooterY);
        Filter(state);
        break;
    }
    case Page::Confirm: {
        // The leave question, in the sheet itself rather than a message box that is always light (E11).
        Label(state,Tr(L"Save changes to this app?",L"保存对此应用的更改？",L"このアプリへの変更を保存しますか?"),
            kMargin,kMargin,contentWidth,kTitleHeight,LeaveTitle);
        Label(state,Tr(L"If you don't save, your changes are lost.",L"如果不保存，所做的更改将会丢失。",L"保存しない場合、変更は失われます。"),
            kMargin,kMargin+kTitleHeight+kGap,contentWidth);
        Button(state,Save,Tr(L"Save",L"保存",L"保存"),primaryX-2*(kSideButton+kGap),kFooterY);
        Button(state,DontSave,Tr(L"Don't save",L"不保存",L"保存しない"),primaryX-kSideButton-kGap,kFooterY);
        Button(state,Stay,Tr(L"Cancel",L"取消",L"キャンセル"),primaryX,kFooterY);
        break;
    }
    }
    state.creating=false;
    InvalidateRect(state.window,nullptr,TRUE);
    const int focus=state.page==Page::Editor?Name:state.page==Page::Advanced?Kind:state.page==Page::Picker?Search:Save;
    if(const HWND control=state.Control(focus); control && !state.hidden)SetFocus(control);
}

void UpdateAppearance(State& state) {
    state.appearance=state.source?state.source->Read():Appearance{};
    for(auto* brush:{state.surfaceBrush,state.inputBrush,state.seatBrush})if(brush)DeleteObject(brush);
    state.surfaceBrush=CreateSolidBrush(GdiColor(state.appearance.surface));
    state.inputBrush=CreateSolidBrush(GdiColor(state.Tile()));
    state.seatBrush=CreateSolidBrush(GdiColor(state.appearance.selection));
    BOOL dark=state.appearance.dark&&!state.appearance.highContrast;
    DwmSetWindowAttribute(state.window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;
    DwmSetWindowAttribute(state.window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    // One plate (P4): the caption takes the plate color and the border the plate's rim; a contrast
    // theme keeps the system's own.
    const auto& a=state.appearance;
    COLORREF caption=a.highContrast?DWMWA_COLOR_DEFAULT:GdiColor(a.surface);
    COLORREF border=a.highContrast?DWMWA_COLOR_DEFAULT:GdiColor(editor::Over(a.plateStroke,a.surface));
    COLORREF text=a.highContrast?DWMWA_COLOR_DEFAULT:GdiColor(a.foreground);
    DwmSetWindowAttribute(state.window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(state.window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    DwmSetWindowAttribute(state.window,DWMWA_TEXT_COLOR,&text,sizeof(text));
    ThemeList(state,state.Control(InstalledViewport));
    RedrawWindow(state.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}

bool Persist(State& state,const std::vector<AppTarget>& candidate) {
    std::wstring error;
    if(!SaveTargets(candidate,error)) {
        MessageBoxW(state.window,error.c_str(),Tr(L"Cannot save applications",L"无法保存应用",L"アプリを保存できません"),MB_OK|MB_ICONWARNING);
        return false;
    }
    state.apps=candidate;
    PostMessageW(state.notifyOwner,kConfigChangedMessage,0,0);
    return true;
}
bool SaveDraft(State& state) {
    auto candidate=state.apps;
    auto app=state.draft;
    if(app.kind==TargetKind::Aumid)app.workingDirectory.clear();
    if(state.adding)candidate.push_back(app); else candidate.at(static_cast<size_t>(state.selected))=app;
    if(!Persist(state,candidate))return false;
    if(state.adding)state.selected=static_cast<int>(state.apps.size())-1;
    state.dirty=false;
    return true;
}
void ChangePage(State& state,Page page) {
    state.page=page;
    BuildPage(state);
}
void StartEdit(State& state,bool adding,const AppTarget* preset=nullptr) {
    state.adding=adding; state.dirty=false;
    state.draft=adding?AppTarget{}:state.apps.at(static_cast<size_t>(state.selected));
    if(adding && preset)state.draft=*preset;
    ChangePage(state,Page::Editor);
}
// Carries out what waited on the leave confirmation: closing, or another app's editor.
void Proceed(State& state) {
    auto leave=std::move(state.leave);
    state.leave={};
    if(!leave.open) {DestroyWindow(state.window);return;}
    std::wstring error;
    auto apps=LoadTargets(error);
    if(!EditorRequestValid(leave.index,apps.size())) {ChangePage(state,Page::Editor);return;}
    state.apps=std::move(apps);
    state.selected=std::max(0,leave.index);
    StartEdit(state,leave.index==-1,leave.preset?&*leave.preset:nullptr);
}
// Closing, or opening another app's editor, asks first in the sheet when the draft has unsaved
// changes; otherwise it happens at once.
void Leave(State& state,State::Leave request) {
    state.leave=std::move(request);
    if(!state.dirty) {Proceed(state);return;}
    if(state.page!=Page::Confirm)state.leftPage=state.page;
    ChangePage(state,Page::Confirm);
}
void NavigateBack(State& state) {
    if(state.page==Page::Advanced || state.page==Page::Picker)ChangePage(state,Page::Editor);
    else if(state.page==Page::Confirm)ChangePage(state,state.leftPage);
    else Leave(state,{});
}
void CompleteEdit(State& state) {
    if(SaveDraft(state))DestroyWindow(state.window);
}
// Start's All apps are read on a worker once per window; the list arrives first, then the icons.
// Each delivery notifies the window once until it drains the inbox; a closed inbox stops the worker.
void ReadInstalledApps(State& state) {
    if(state.listing || state.listed)return;
    state.listing=true;
    state.iconSize=static_cast<UINT>(state.Px(static_cast<int>(vi::kTile)));
    state.inbox=std::make_shared<InstalledInbox>();
    const HWND window=state.window;
    const UINT size=state.iconSize;
    try {
        std::thread([window,size,inbox=state.inbox] {
            const auto deliver=[&](auto&& store) {
                std::lock_guard guard(inbox->lock);
                if(inbox->closed)return false;
                store();
                if(!inbox->notified)inbox->notified=PostMessageW(window,kInstalledMessage,0,0)!=FALSE;
                return true;
            };
            const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
            try {
                std::vector<InstalledApp> apps;
                try {apps=InstalledApps();} catch(...) {apps.clear();}
                std::vector<std::wstring> items;
                for(const auto& app:apps)items.push_back(app.item);
                if(deliver([&] {inbox->apps=std::move(apps);}))
                    for(size_t i=0;i<items.size();++i) {
                        auto pixels=InstalledAppIcon(items[i],size);
                        if(pixels.empty())continue;
                        if(!deliver([&] {inbox->icons.push_back({i,std::move(pixels)});}))break;
                    }
            } catch(...) {}
            if(SUCCEEDED(apartment))CoUninitialize();
        }).detach();
    } catch(...) {state.listing=false;state.inbox.reset();}
}
// Takes what the worker delivered: the list (shown at once when the picker is open), then icons,
// each repainting only its own row while the user scrolls.
void ReceiveInstalled(State& state) {
    if(!state.inbox)return;
    std::optional<std::vector<InstalledApp>> apps;
    std::vector<InstalledIcon> icons;
    {
        std::lock_guard guard(state.inbox->lock);
        state.inbox->notified=false;
        apps.swap(state.inbox->apps);
        icons.swap(state.inbox->icons);
    }
    if(apps) {
        state.listing=false;state.listed=true;
        state.installed=std::move(*apps);
        state.icons.assign(state.installed.size(),{});
        if(state.page==Page::Picker)Filter(state);
    }
    const HWND list=state.Control(InstalledList);
    for(auto& icon:icons) {
        if(icon.index>=state.icons.size())continue;
        auto& row=state.icons[icon.index];
        row.plated=LooksPlated(icon.pixels.data(),state.iconSize,state.iconSize);
        row.pixels=std::move(icon.pixels);
        const auto shown=std::find(state.visible.begin(),state.visible.end(),icon.index);
        RECT bounds{};
        if(state.page==Page::Picker && list && shown!=state.visible.end() &&
            SendMessageW(list,LB_GETITEMRECT,static_cast<WPARAM>(shown-state.visible.begin()),reinterpret_cast<LPARAM>(&bounds))!=LB_ERR)
            InvalidateRect(list,&bounds,FALSE);
    }
}
// The worker sees a closed inbox and stops; whatever it delivered is freed with the last owner.
void CloseInstalled(State& state) {
    if(!state.inbox)return;
    std::lock_guard guard(state.inbox->lock);
    state.inbox->closed=true;
    state.inbox->apps.reset();
    state.inbox->icons.clear();
}
// The chosen app replaces the whole draft: its name, launch method, target and arguments.
void PickInstalled(State& state) {
    const auto selected=SendMessageW(state.Control(InstalledList),LB_GETCURSEL,0,0);
    if(selected==LB_ERR || static_cast<size_t>(selected)>=state.visible.size())return;
    state.draft=state.installed[state.visible[static_cast<size_t>(selected)]].target;
    state.dirty=true;
    ChangePage(state,Page::Editor);
}

void BrowseTarget(State& state) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=state.window;
    dialog.lpstrFilter=Tr(L"Applications and shortcuts\0*.exe;*.lnk;*.url\0All files\0*.*\0\0",
        L"应用和快捷方式\0*.exe;*.lnk;*.url\0所有文件\0*.*\0\0",
        L"アプリとショートカット\0*.exe;*.lnk;*.url\0すべてのファイル\0*.*\0\0");
    dialog.lpstrFile=path; dialog.nMaxFile=static_cast<DWORD>(std::size(path));
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if(GetOpenFileNameW(&dialog)) {
        SetWindowTextW(state.Control(Target),path);
        if(state.draft.name.empty()) {
            std::wstring name=path;
            const auto slash=name.find_last_of(L"\\/");if(slash!=std::wstring::npos)name.erase(0,slash+1);
            const auto dot=name.find_last_of(L'.');if(dot!=std::wstring::npos)name.resize(dot);
            SetWindowTextW(state.Control(Name),name.c_str());
        }
    }
}

void Command(State& state,int id,int notification);
// Enter clicks the focused button, or else runs the page's primary action (E1): owner-drawn buttons
// are no push buttons to the dialog manager, which sends IDOK whichever of them has focus.
void Enter(State& state) {
    const HWND focus=GetFocus();
    wchar_t type[32]{};
    const bool inside=focus && IsChild(state.window,focus) && GetClassNameW(focus,type,32)>0;
    const int primary=state.page==Page::Advanced?Done:state.page==Page::Picker?Pick:Save;
    const int id=editor::EnterTarget(inside?type:nullptr,inside?GetDlgCtrlID(focus):0,primary);
    if(const HWND button=state.Control(id); button && IsWindowEnabled(button))Command(state,id,BN_CLICKED);
}

// Every path that ends the window returns at once: DestroyWindow frees the state.
void Command(State& state,int id,int notification) {
    if(state.creating)return;
    // A field's frame carries its focus, and its EDIT the frame's fill.
    if((notification==EN_SETFOCUS||notification==EN_KILLFOCUS) && state.frames.contains(id)) {
        InvalidateRect(state.window,nullptr,FALSE);InvalidateRect(state.Control(id),nullptr,TRUE);return;
    }
    if(id==Search && notification==EN_CHANGE) {state.query=Text(state.Control(Search));Filter(state);return;}
    if(id==InstalledList && notification==LBN_DBLCLK) {PickInstalled(state);return;}
    if(notification==EN_CHANGE) {
        if(id==Name)state.draft.name=Text(state.Control(Name));
        else if(id==Target)state.draft.target=Text(state.Control(Target));
        else if(id==Arguments)state.draft.arguments=Text(state.Control(Arguments));
        else if(id==Directory)state.draft.workingDirectory=Text(state.Control(Directory));
        else return;
        state.dirty=true;return;
    }
    if(id==Kind && notification==CBN_SELCHANGE) {
        state.draft.kind=SendMessageW(state.Control(Kind),CB_GETCURSEL,0,0)==1?TargetKind::Aumid:TargetKind::Shell;
        EnableWindow(state.Control(Directory),state.draft.kind==TargetKind::Shell);
        state.dirty=true;return;
    }
    if(notification!=BN_CLICKED)return;
    switch(id) {
    case IDCANCEL:case Back:NavigateBack(state);break;
    case IDOK:Enter(state);break;
    case Advanced:ChangePage(state,Page::Advanced);break;
    case Done:ChangePage(state,Page::Editor);break;
    case Browse:BrowseTarget(state);break;
    case ChooseApp:state.query.clear();ChangePage(state,Page::Picker);ReadInstalledApps(state);break;
    case Pick:PickInstalled(state);break;
    case Save:
        if(state.page!=Page::Confirm)CompleteEdit(state);
        else if(SaveDraft(state))Proceed(state);
        break;
    case DontSave:state.dirty=false;Proceed(state);break;
    case Stay:ChangePage(state,state.leftPage);break;
    }
}

LRESULT CALLBACK OsdProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* state=StateOf(window);
    if(message==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* owner=static_cast<std::unique_ptr<State>*>(create->lpCreateParams);
        state=owner->release();state->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(!state)return DefWindowProcW(window,message,wParam,lParam);
    try {
        switch(message) {
        case WM_CREATE:
            // A capture's DPI override stands in for the monitor's, as it does for the reel.
            state->dpi=CurrentVisualOverride().dpi?CurrentVisualOverride().dpi:GetDpiForWindow(window);
            state->source=std::make_unique<AppearanceSource>(window);
            if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,state->factory.GetAddressOf())) ||
                FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown**>(state->textFactory.GetAddressOf()))))return -1;
            UpdateFont(*state);UpdateAppearance(*state);BuildPage(*state);return 0;
        case WM_PAINT:Paint(*state);return 0;
        case WM_PRINTCLIENT:PrintSheet(*state,reinterpret_cast<HDC>(wParam));return 0;
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:
            if(state->target)state->target->Resize(D2D1::SizeU(LOWORD(lParam),HIWORD(lParam)));
            UpdateScrollbars(*state);
            return 0;
        case WM_HSCROLL:Scroll(*state,SB_HORZ,LOWORD(wParam));return 0;
        case WM_VSCROLL:Scroll(*state,SB_VERT,LOWORD(wParam));return 0;
        case WM_MOUSEWHEEL:
            Scroll(*state,SB_VERT,GET_WHEEL_DELTA_WPARAM(wParam)>0?SB_LINEUP:SB_LINEDOWN);return 0;
        case WM_DRAWITEM:DrawControl(*state,*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight=state->Px(
                reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->CtlType==ODT_COMBOBOX?kControl:kRowHeight);return TRUE;
        // A click on a field's padding focuses its text, as on the text itself.
        case WM_LBUTTONDOWN: {
            const float x=(GET_X_LPARAM(lParam)+state->scrollX)*96.f/state->dpi,y=(GET_Y_LPARAM(lParam)+state->scrollY)*96.f/state->dpi;
            for(const auto& [id,frame]:state->frames)
                if(x>=frame.left && x<frame.right && y>=frame.top && y<frame.bottom) {
                    const HWND field=state->Control(id);
                    if(field && IsWindowEnabled(field)) {SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);}
                    return 0;
                }
            break;
        }
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX: {
            HDC dc=reinterpret_cast<HDC>(wParam);
            const auto& a=state->appearance;
            // The list's rows sit on the sheet: fewer than five leave the plate, not a gray block (E11).
            if(message==WM_CTLCOLORLISTBOX) {
                SetTextColor(dc,GdiColor(a.foreground));SetBkColor(dc,GdiColor(a.surface));
                return reinterpret_cast<LRESULT>(state->surfaceBrush);
            }
            // A field's text sits on its frame's fill: the seat while it has focus. A disabled EDIT
            // asks for static colors and dims its text.
            const bool seat=!a.highContrast && GetFocus()==reinterpret_cast<HWND>(lParam);
            const auto fill=seat?a.selection:state->Tile();
            SetTextColor(dc,GdiColor(message==WM_CTLCOLORSTATIC?state->Disabled(fill):seat?a.selectionText:a.foreground));
            SetBkColor(dc,GdiColor(fill));
            return reinterpret_cast<LRESULT>(seat?state->seatBrush:state->inputBrush);
        }
        case WM_COMMAND:Command(*state,LOWORD(wParam),HIWORD(wParam));return 0;
        case WM_KEYDOWN:if(wParam==VK_ESCAPE){NavigateBack(*state);return 0;}break;
        case WM_SETTINGCHANGE:case WM_THEMECHANGED:case kAppearanceMessage:UpdateAppearance(*state);return 0;
        case kLanguageChangedMessage:UpdateFont(*state);BuildPage(*state);return 0;
        case kInstalledMessage:ReceiveInstalled(*state);return 0;
        case WM_DPICHANGED: {
            if(!CurrentVisualOverride().dpi)state->dpi=HIWORD(wParam);
            const auto* suggested=reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,
                suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
            state->target.Reset();state->controlTarget.Reset();state->formats.clear();UpdateFont(*state);
            // The icons were drawn for the old DPI: read Start again at the new size (E11).
            if(!state->hidden && (state->listed||state->listing)) {
                CloseInstalled(*state);state->inbox.reset();
                state->listed=state->listing=false;
                state->installed.clear();state->icons.clear();state->visible.clear();
            }
            BuildPage(*state);
            if(state->page==Page::Picker)ReadInstalledApps(*state);
            return 0;
        }
        case WM_CLOSE: {
            const bool hidden=state->hidden;
            Leave(*state,{});
            // A close that has to ask first brings the question forward.
            if(IsWindow(window) && !hidden) {ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}
            return 0;
        }
        case WM_NCDESTROY:
            CloseInstalled(*state);
            if(gOsd==window)gOsd=nullptr;
            if(IsWindow(state->notifyOwner))PostMessageW(state->notifyOwner,kTargetEditorClosedMessage,0,0);
            SetWindowLongPtrW(window,GWLP_USERDATA,0);delete state;
            return DefWindowProcW(window,message,wParam,lParam);
        }
    } catch(...) {
        if(message==WM_CREATE)return -1;
        MessageBoxW(window,Tr(L"The editor could not complete that action.",
            L"编辑器无法完成此操作。",
            L"エディターでこの操作を完了できませんでした。"),kAppName,MB_OK|MB_ICONWARNING);
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

bool RegisterClasses() {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc=OsdProc;cls.hInstance=GetModuleHandleW(nullptr);
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(cls.hInstance,MAKEINTRESOURCEW(1));
    // The caption and Alt+Tab take the ICO's own small frame, not the large icon shrunk (E12).
    LoadIconMetric(cls.hInstance,MAKEINTRESOURCEW(1),LIM_SMALL,&cls.hIconSm);
    cls.lpszClassName=kOsdClass;
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    WNDCLASSEXW viewport{sizeof(viewport)};
    viewport.lpfnWndProc=ViewportProc;viewport.hInstance=cls.hInstance;
    viewport.hCursor=LoadCursorW(nullptr,IDC_ARROW);viewport.lpszClassName=kViewportClass;
    return RegisterClassExW(&viewport)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
}
// A real caption preserves drag, taskbar, system-menu and keyboard accessibility.
constexpr DWORD kStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN|WS_HSCROLL|WS_VSCROLL;
} // namespace

void ShowOsdAt(HWND owner,int editorIndex,const AppTarget* preset=nullptr) {
    try {
        if(gOsd&&IsWindow(gOsd)) {
            if(auto* state=StateOf(gOsd)) {
                state->notifyOwner=owner;
                std::wstring error;
                if(EditorRequestValid(editorIndex,LoadTargets(error).size()))
                    Leave(*state,{true,editorIndex,preset?std::optional<AppTarget>(*preset):std::nullopt});
            }
            ShowWindow(gOsd,SW_RESTORE);SetForegroundWindow(gOsd);return;
        }
        if(!RegisterClasses()) {
            MessageBoxW(owner,Tr(L"The editor window could not be registered.",L"无法注册编辑器窗口。",L"エディターのウィンドウを登録できませんでした。"),kAppName,MB_OK|MB_ICONERROR);
            return;
        }
        auto state=std::make_unique<State>();state->notifyOwner=owner;
        std::wstring error;state->apps=LoadTargets(error);
        if(!EditorRequestValid(editorIndex,state->apps.size()))return;
        state->adding=editorIndex == -1;
        state->selected=std::max(0,editorIndex);
        state->draft=state->adding?AppTarget{}:state->apps.at(static_cast<size_t>(editorIndex));
        if(state->adding && preset)state->draft=*preset;
        POINT pointer{};GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(pointer,MONITOR_DEFAULTTONEAREST),&monitor);
        const DWORD extended=WS_EX_CONTROLPARENT;
        const UINT dpi=GetDpiForSystem();
        RECT bounds{0,0,MulDiv(kWidth,static_cast<int>(dpi),96),MulDiv(kHeight,static_cast<int>(dpi),96)};
        AdjustWindowRectExForDpi(&bounds,kStyle,FALSE,extended,dpi);
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        const int x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2;
        const int y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2;
        gOsd=CreateWindowExW(extended,kOsdClass,L"Copilot Reel",kStyle,x,y,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),&state);
        if(!gOsd) {
            MessageBoxW(owner,Tr(L"The editor could not be created.",L"无法创建编辑器。",L"エディターを作成できませんでした。"),kAppName,MB_OK|MB_ICONERROR);
            return;
        }
        // WM_CREATE uses the actual monitor DPI. Recenter after its size correction.
        RECT actual{};GetWindowRect(gOsd,&actual);
        SetWindowPos(gOsd,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-(actual.right-actual.left))/2,
            monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-(actual.bottom-actual.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
        ShowWindow(gOsd,SW_SHOWNORMAL);UpdateWindow(gOsd);SetForegroundWindow(gOsd);
        if(!error.empty())MessageBoxW(gOsd,error.c_str(),kAppName,MB_OK|MB_ICONWARNING);
    } catch(...) {
        MessageBoxW(owner,Tr(L"The editor could not be opened.",L"无法打开编辑器。",L"エディターを開けませんでした。"),kAppName,MB_OK|MB_ICONERROR);
    }
}
void ShowTargetEditor(HWND owner,int index) { ShowOsdAt(owner,index); }
void ShowPresetTargetEditor(HWND owner,const AppTarget& preset) { ShowOsdAt(owner,-1,&preset); }
void RefreshOsdAppearance() {
    if(gOsd&&IsWindow(gOsd))PostMessageW(gOsd,kAppearanceMessage,0,0);
}
bool OsdOpen() {return gOsd && IsWindow(gOsd);}
bool CloseOsd() {
    const auto window=gOsd;
    if(!window||!IsWindow(window))return true;
    SendMessageW(window,WM_CLOSE,0,0);
    return !IsWindow(window);
}

HWND CreateHiddenEditor(const AppTarget& draft,std::vector<InstalledApp> installed) {
    if(!RegisterClasses())return nullptr;
    auto state=std::make_unique<State>();
    state->hidden=true;state->adding=true;state->draft=draft;
    state->icons.assign(installed.size(),{});
    state->installed=std::move(installed);state->listed=true;
    return CreateWindowExW(WS_EX_CONTROLPARENT,kOsdClass,L"Copilot Reel",kStyle,0,0,
        kWidth,kHeight,nullptr,nullptr,GetModuleHandleW(nullptr),&state);
}
void ShowEditorPage(HWND editor,EditorPage page) {
    auto* state=StateOf(editor);
    if(!state)return;
    if(page==EditorPage::Confirm) {state->dirty=true;Leave(*state,{});return;}
    ChangePage(*state,page==EditorPage::Advanced?Page::Advanced:page==EditorPage::Picker?Page::Picker:Page::Editor);
}
std::vector<std::wstring> TrimmedEditorText(HWND editor) {
    const auto* state=StateOf(editor);
    return state?std::vector<std::wstring>(state->trimmed.begin(),state->trimmed.end()):std::vector<std::wstring>{};
}
} // namespace reel
