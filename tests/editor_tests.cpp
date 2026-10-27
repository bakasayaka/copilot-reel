#include "appearance.h"
#include "editor_layout.h"
#include "localization.h"
#include "osd.h"
#include "typography.h"
#include "vi_tokens.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// The app editor (P4). Its rules first, then the editor itself in windows that are never shown,
// focused or activated: each page is printed into a bitmap with WM_PRINT, the message PrintWindow
// sends, and checked there. A capture's appearance and DPI come from COPILOT_REEL_VISUAL_OVERRIDE,
// which a process reads once, so every appearance runs in a child process of its own.
namespace {
using Microsoft::WRL::ComPtr;
using namespace reel;
unsigned checks = 0;
std::string scope;
void Check(bool okay, const std::string& label) {
    ++checks;
    if (!okay) { std::cerr << "FAIL: " << scope << label << '\n'; std::exit(1); }
}
std::string Narrow(const std::wstring& text) {
    std::string result;
    for (const wchar_t c : text) result += c < 128 ? static_cast<char>(c) : '?';
    return result;
}
Rgba ToRgba(vi::Color color) { return {color.r, color.g, color.b, color.a}; }

LRESULT CALLBACK Plain(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

void Rules() {
    using editor::EnterTarget;
    // E1: Enter clicks the focused button on any page; anywhere else it runs the primary action.
    constexpr int primary = 7;
    for (const int button : {100, 104, 105, 110, 113})
        Check(EnterTarget(L"Button", button, primary) == button, "Enter on a focused button clicks that button");
    Check(EnterTarget(L"BUTTON", 104, primary) == 104, "class names compare without case");
    for (const auto* type : {L"Edit", L"ListBox", L"ComboBox", L"Static", L"CopilotReel.ListViewport"})
        Check(EnterTarget(type, 103, primary) == primary, "Enter in a field or a list runs the primary action");
    Check(EnterTarget(nullptr, 0, primary) == primary && EnterTarget(L"Button", 0, primary) == primary,
        "Enter with nothing focused runs the primary action");

    // E3: five rows of the row's own pixel height fill the viewport exactly, at every scale.
    WNDCLASSEXW plain{sizeof(plain)};
    plain.lpfnWndProc = Plain;
    plain.hInstance = GetModuleHandleW(nullptr);
    plain.lpszClassName = L"CopilotReel.EditorTest";
    Check(RegisterClassExW(&plain) != 0, "test window class");
    for (const UINT dpi : {96u, 120u, 144u, 168u, 192u, 240u, 288u}) {
        const int row = editor::Px(vi::kEditorRow, dpi), view = editor::ViewportHeight(vi::kEditorListRows, vi::kEditorRow, dpi);
        Check(view == vi::kEditorListRows * row, "the viewport is whole rows tall");
        const HWND host = CreateWindowExW(0, L"CopilotReel.EditorTest", L"", WS_OVERLAPPEDWINDOW, 0, 0, 800, 800,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        const HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED |
            LBS_NOINTEGRALHEIGHT, 0, 0, 400, view, host, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(list, LB_SETITEMHEIGHT, 0, row);
        for (int i = 0; i != 8; ++i) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"row"));
        RECT last{};
        SendMessageW(list, LB_GETITEMRECT, vi::kEditorListRows - 1, reinterpret_cast<LPARAM>(&last));
        RECT client{};
        GetClientRect(list, &client);
        Check(last.bottom == client.bottom, "the last of five rows ends at the viewport's bottom at " + std::to_string(dpi) + " DPI");
        Check(!IsWindowVisible(host), "the test host stays hidden");
        DestroyWindow(host);
    }

    // E5: a frame strokes whole pixels on the control's own edges, so a row's controls share them.
    for (const UINT dpi : {96u, 120u, 144u, 168u, 192u})
        for (const float stroke : {vi::kEditorStroke, vi::kEditorFocusStroke, vi::kSeatStrokeHighContrast}) {
            const float px = editor::StrokeDip(stroke, dpi) * dpi / 96;
            Check(px >= 1 && std::abs(px - std::round(px)) < 1e-3f, "strokes cover whole pixels");
            const int width = editor::Px(vi::kEditorSideButton, dpi), height = editor::Px(vi::kEditorControl, dpi);
            const auto frame = editor::FrameRect(width, height, stroke, dpi);
            Check(std::abs(frame.top * dpi / 96 - px / 2) < 1e-3f && std::abs((height - frame.bottom * dpi / 96) - px / 2) < 1e-3f &&
                  std::abs(frame.left * dpi / 96 - px / 2) < 1e-3f, "the frame runs half a stroke inside the control's edges");
        }

    // E6: the field's bottom stroke reads at 3:1 or more against the field, light and dark.
    for (const auto* palette : {&vi::kLight, &vi::kDark}) {
        const auto field = editor::Over(ToRgba(palette->tile), ToRgba(palette->plate));
        Check(editor::Contrast(ToRgba(palette->secondary), field) >= vi::kEditorFieldStrokeContrast,
            "the field's bottom stroke has at least 3:1 against its fill");
    }
    // The picker's five rows fit between the search field and the footer.
    Check(vi::kEditorMargin + vi::kEditorControl + vi::kEditorGap + vi::kEditorListRows * vi::kEditorRow + vi::kEditorGap <=
          vi::kEditorHeight - vi::kEditorMargin - vi::kEditorControl, "the list fits above the footer");
    Check(vi::kEditorRow >= vi::kTile + 2 * 4, "a row holds the app's tile");
    // E8: the text fields take the GDI face of their script, so shared Han keeps Chinese forms.
    Check(std::wstring(ControlFamily(Language::SimplifiedChinese)) == vi::kCjkFamily &&
          std::wstring(ControlFamily(Language::Japanese)) == vi::kJapaneseSystemFamily &&
          std::wstring(ControlFamily(Language::English)) == L"Segoe UI", "each language's text fields take their script's face");
}

struct Capture {
    int width = 0, height = 0;
    std::vector<UINT32> pixels;  // top-down BGRA
    Rgba At(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return {-1, -1, -1, 1};
        const UINT32 value = pixels[static_cast<size_t>(y) * width + x];
        return {((value >> 16) & 255) / 255.f, ((value >> 8) & 255) / 255.f, (value & 255) / 255.f, 1};
    }
};
bool Near(Rgba a, Rgba b, float tolerance = 8 / 255.f) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}
// Prints each visible child at its own place, bottom of the z-order first, clipped to its parent:
// a window that was never shown has no screen to print its children from.
void PrintTree(HWND root, HWND parent, HDC dc, RECT clip) {
    const HWND first = GetWindow(parent, GW_CHILD);
    for (HWND child = first ? GetWindow(first, GW_HWNDLAST) : nullptr; child; child = GetWindow(child, GW_HWNDPREV)) {
        if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
        RECT bounds{};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, root, reinterpret_cast<POINT*>(&bounds), 2);
        RECT visible{};
        if (!IntersectRect(&visible, &bounds, &clip)) continue;
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, visible.left, visible.top, visible.right, visible.bottom);
        SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
        SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_NONCLIENT | PRF_CLIENT | PRF_ERASEBKGND);
        RestoreDC(dc, saved);
        RECT client{};
        GetClientRect(child, &client);
        MapWindowPoints(child, root, reinterpret_cast<POINT*>(&client), 2);
        RECT inner{};
        if (IntersectRect(&inner, &client, &visible)) PrintTree(root, child, dc, inner);
    }
}
Capture Print(HWND window) {
    Capture capture;
    RECT client{};
    GetClientRect(window, &client);
    capture.width = client.right;
    capture.height = client.bottom;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), capture.width, -capture.height, 1, 32, BI_RGB};
    void* bits = nullptr;
    const HDC screen = GetDC(nullptr);
    const HDC dc = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    Check(dc && bitmap && bits, "capture bitmap");
    const auto old = SelectObject(dc, bitmap);
    SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
    PrintTree(window, window, dc, client);
    GdiFlush();
    const auto* source = static_cast<const UINT32*>(bits);
    capture.pixels.assign(source, source + static_cast<size_t>(capture.width) * capture.height);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return capture;
}
void SavePng(const Capture& capture, const std::filesystem::path& path) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    std::vector<UINT32> opaque(capture.pixels);
    for (auto& pixel : opaque) pixel |= 0xFF000000u;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    const bool saved = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(capture.width, capture.height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(opaque.size() * 4),
            reinterpret_cast<BYTE*>(opaque.data()))) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    Check(saved, "saving " + path.filename().string());
}
// A child by its window text, or by its class when `type` is set.
HWND Find(HWND root, const std::wstring& text, const wchar_t* type = nullptr) {
    struct Query { const std::wstring* text; const wchar_t* type; HWND found; } query{&text, type, nullptr};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto& query = *reinterpret_cast<Query*>(data);
        wchar_t name[256]{};
        if (query.type) GetClassNameW(child, name, 256); else GetWindowTextW(child, name, 256);
        if ((query.type ? std::wstring(query.type) : *query.text) != name) return TRUE;
        query.found = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.found;
}
RECT Bounds(HWND root, HWND child) {
    RECT bounds{};
    GetWindowRect(child, &bounds);
    MapWindowPoints(nullptr, root, reinterpret_cast<POINT*>(&bounds), 2);
    return bounds;
}

int Captures(const std::wstring& override, const std::wstring& name) {
    SetEnvironmentVariableW(L"COPILOT_REEL_VISUAL_OVERRIDE", override.c_str());
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    const UINT dpi = CurrentVisualOverride().dpi;
    Check(dpi != 0, "the override sets the DPI");
    const auto px = [&](int dip) { return editor::Px(dip, dpi); };
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const auto folder = std::filesystem::path(module).parent_path() / L"editor-captures";
    std::filesystem::create_directories(folder);
    std::vector<InstalledApp> installed;
    for (int i = 1; i <= 12; ++i) {
        InstalledApp app;
        app.target.name = L"App " + std::to_wstring(i);
        app.target.target = L"C:\\Apps\\app" + std::to_wstring(i) + L".exe";
        app.item = app.target.target;
        app.packaged = i % 2 == 0;
        installed.push_back(app);
    }
    AppTarget draft;
    draft.name = L"Notepad";
    draft.target = L"C:\\Windows\\System32\\notepad.exe";
    const std::pair<Language, const char*> languages[]{{Language::English, "en"}, {Language::SimplifiedChinese, "zh"}, {Language::Japanese, "ja"}};
    unsigned pages = 0;
    for (const auto& [language, code] : languages) {
        SetLanguage(language);
        scope = Narrow(name) + " " + code + ": ";
        const HWND editor = CreateHiddenEditor(draft, installed);
        Check(editor != nullptr, "the hidden editor opens");
        AppearanceSource source(editor);
        const auto look = source.Read();
        const auto save = [&](const Capture& capture, const char* page) {
            SavePng(capture, folder / (name + L"-" + std::wstring(code, code + 2) + L"-" + std::wstring(page, page + std::strlen(page)) + L".png"));
            ++pages;
        };
        const auto never = [&] {
            Check(!IsWindowVisible(editor) && GetForegroundWindow() != editor && GetActiveWindow() != editor,
                "the editor is never shown or activated");
            const HWND focus = GetFocus();
            Check(!focus || !IsChild(editor, focus), "nothing in the editor takes focus");
        };
        // A primary button is accent-filled; an ordinary one keeps the tile fill.
        const auto filled = [&](const Capture& capture, HWND button) {
            const RECT r = Bounds(editor, button);
            return capture.At(r.left + px(4), (r.top + r.bottom) / 2);
        };
        // A control's frame covers its own top and bottom pixel rows, with the plate just outside.
        const auto framed = [&](const Capture& capture, int x, int top, int bottom) {
            return !Near(capture.At(x, top), look.surface, 2 / 255.f) && Near(capture.At(x, top - 1), look.surface, 2 / 255.f) &&
                   !Near(capture.At(x, bottom - 1), look.surface, 2 / 255.f) && Near(capture.At(x, bottom), look.surface, 2 / 255.f);
        };

        // The editor page.
        auto capture = Print(editor);
        save(capture, "editor");
        never();
        const HWND saveButton = Find(editor, Tr(L"Save", L"保存", L"保存"));
        const HWND chooseApp = Find(editor, Tr(L"Choose app…", L"选择应用…", L"アプリを選択…"));
        const HWND browse = Find(editor, Tr(L"Browse…", L"浏览…", L"参照…"));
        Check(saveButton && chooseApp && browse, "the editor page's buttons");
        Check(Near(filled(capture, saveButton), look.accent), "Save is accent-filled");
        Check(Near(filled(capture, chooseApp), editor::Over(look.tile, look.surface)), "Choose app… keeps the tile fill");
        // E5: the target field and its two buttons share their top and bottom rows.
        const RECT choose = Bounds(editor, chooseApp), folderButton = Bounds(editor, browse);
        Check(choose.top == px(vi::kEditorMargin + vi::kEditorRowPitch + vi::kEditorLabelToField) && folderButton.top == choose.top &&
              choose.bottom - choose.top == px(vi::kEditorControl), "the target row's buttons sit on the field's row");
        const int fieldX = px(vi::kEditorMargin) + px(2 * static_cast<int>(vi::kEditorRadius));
        Check(framed(capture, fieldX, choose.top, choose.top + px(vi::kEditorControl)), "the target field is framed on the row's top and bottom");
        Check(framed(capture, (choose.left + choose.right) / 2, choose.top, choose.bottom) &&
              framed(capture, (folderButton.left + folderButton.right) / 2, folderButton.top, folderButton.bottom),
            "Choose app… and Browse… are framed on the same rows");
        // E6: the field's bottom stroke against its fill.
        const auto stroke = capture.At(fieldX, choose.bottom - 1), fill = capture.At(fieldX, choose.top + px(4));
        Check(editor::Contrast(stroke, fill) >= vi::kEditorFieldStrokeContrast, "the field's bottom stroke reads at 3:1");

        // Advanced.
        ShowEditorPage(editor, EditorPage::Advanced);
        capture = Print(editor);
        save(capture, "advanced");
        never();
        const HWND done = Find(editor, Tr(L"Done", L"完成", L"完了"));
        Check(done && Near(filled(capture, done), look.accent), "Done is accent-filled");

        // Choose app.
        ShowEditorPage(editor, EditorPage::Picker);
        capture = Print(editor);
        save(capture, "picker");
        never();
        const HWND viewport = Find(editor, L"", L"CopilotReel.ListViewport"), list = Find(editor, L"", L"ListBox");
        Check(viewport && list, "the picker's list");
        RECT view{};
        GetClientRect(viewport, &view);
        const int row = px(vi::kEditorRow);
        Check(view.bottom == editor::ViewportHeight(vi::kEditorListRows, vi::kEditorRow, dpi) &&
              SendMessageW(list, LB_GETITEMHEIGHT, 0, 0) == row, "the viewport holds five whole rows");
        const RECT shown = Bounds(editor, viewport);
        const int gapX = shown.left + px(4 + static_cast<int>(vi::kTile) + 6), seatX = shown.left + px(3 * static_cast<int>(vi::kEditorRadius));
        // E2: the selected row is the seat although nothing has focus.
        Check(Near(capture.At(gapX, shown.top + row / 2), look.selection), "the selected row takes the seat's fill without focus");
        if (!look.highContrast) Check(Near(capture.At(seatX, shown.top), look.accent), "the selected row carries the seat's accent stroke");
        Check(Near(capture.At(gapX, shown.top + row + row / 2), look.surface, 2 / 255.f), "other rows sit on the plate");
        // The selected row's tile keeps its glyph: it sits on the tile, not on the seat.
        int ink = 0;
        const int tileX = shown.left + px(4 + static_cast<int>(vi::kTile) / 2), tileY = shown.top + row / 2;
        for (int y = tileY - px(8); y <= tileY + px(8); ++y)
            for (int x = tileX - px(8); x <= tileX + px(8); ++x)
                ink += editor::Contrast(capture.At(x, y), editor::Over(look.tile, look.surface)) >= 3;
        Check(ink > px(8), "the selected row's tile keeps a visible glyph");
        // E3: the fifth row's seat ends inside the viewport.
        SendMessageW(list, LB_SETCURSEL, vi::kEditorListRows - 1, 0);
        capture = Print(editor);
        Check(Near(capture.At(gapX, shown.top + view.bottom - row / 2), look.selection) &&
              (look.highContrast || Near(capture.At(seatX, shown.top + view.bottom - 1), look.accent)),
            "the fifth row's seat ends at the viewport's bottom");
        // E11: fewer than five matches leave the plate below them.
        SetWindowTextW(Find(editor, L"", L"Edit"), L"App 1");
        capture = Print(editor);
        save(capture, "picker-filtered");
        Check(SendMessageW(list, LB_GETCOUNT, 0, 0) == 4, "the search keeps App 1 and App 10 to 12");
        Check(Near(capture.At(gapX, shown.top + 4 * row + row / 2), look.surface, 2 / 255.f), "no gray block under fewer rows");

        // The leave question.
        ShowEditorPage(editor, EditorPage::Confirm);
        capture = Print(editor);
        save(capture, "confirm");
        never();
        const HWND keep = Find(editor, Tr(L"Save", L"保存", L"保存")), drop = Find(editor, Tr(L"Don't save", L"不保存", L"保存しない")),
            stay = Find(editor, Tr(L"Cancel", L"取消", L"キャンセル"));
        Check(keep && drop && stay, "the leave question's buttons");
        Check(Near(filled(capture, keep), look.accent) && !Near(filled(capture, drop), look.accent) && !Near(filled(capture, stay), look.accent),
            "only Save is accent-filled");

        // Every word of the sheet fits where it is drawn, in each language (E4 and the buttons' labels).
        std::string trimmed;
        for (const auto& text : TrimmedEditorText(editor)) trimmed += " [" + Narrow(text) + "]";
        Check(trimmed.empty(), "the sheet's words fit:" + trimmed);
        DestroyWindow(editor);
        Check(!IsWindow(editor), "the hidden editor closes");
    }
    std::cout << "  " << Narrow(name) << ": " << checks << " checks on " << pages << " captures\n";
    CoUninitialize();
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--capture") return Captures(argv[2], argv[3]);
    Rules();
    std::cout << "Editor rules: " << checks << " checks." << std::endl;
    const std::pair<const wchar_t*, const wchar_t*> appearances[]{
        {L"theme=light;material=solid;contrast=off;dpi=96", L"light-100"},
        {L"theme=dark;material=solid;contrast=off;dpi=120", L"dark-125"},
        {L"theme=light;material=frosted;contrast=off;dpi=144", L"frosted-150"},
        {L"theme=dark;material=solid;contrast=on;dpi=168", L"contrast-175"},
    };
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    for (const auto& [override, name] : appearances) {
        std::wstring command = L"\"" + std::wstring(module) + L"\" --capture \"" + override + L"\" " + name;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        Check(CreateProcessW(module, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process) != FALSE,
            "starting the capture of " + Narrow(name));
        WaitForSingleObject(process.hProcess, 120000);
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Check(code == 0, "the capture of " + Narrow(name) + " passed");
    }
    std::cout << "Editor tests passed: the rules, and every page in EN, ZH and JA under four appearances in hidden editors, "
                 "printed to editor-captures without showing, focusing or activating a window.\n";
    return 0;
}
