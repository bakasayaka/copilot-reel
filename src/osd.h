#pragma once
#include "app_types.h"
#include "installed_apps.h"

#include <string>
#include <vector>

namespace reel {
inline constexpr UINT kTargetEditorClosedMessage = WM_APP + 12;
// Opens the focused app editor, a native task sheet on the Soft Orbit plate. -1 adds a blank
// entry; an existing index edits that entry. Saving or closing returns control to the radial menu.
void ShowTargetEditor(HWND owner, int index);
// Opens a focused addition with the preset's fields. The launch target may be
// empty until the user supplies the installed application's path or identity.
void ShowPresetTargetEditor(HWND owner, const AppTarget& preset);
void RefreshOsdAppearance();
// Returns false when the editor has unsaved changes: it then asks in the sheet instead of closing.
bool CloseOsd();
// True while the editor window exists; its destruction posts kTargetEditorClosedMessage.
bool OsdOpen();

// Tests (tests/editor_tests.cpp): the editor adding `draft` in a window that is never shown,
// focused or activated, with `installed` standing in for Start's apps. DestroyWindow closes it.
HWND CreateHiddenEditor(const AppTarget& draft, std::vector<InstalledApp> installed);
enum class EditorPage { Editor, Advanced, Picker, Confirm };
// Confirm marks the draft changed and asks the leave question.
void ShowEditorPage(HWND editor, EditorPage page);
// The sheet's own words that did not fit where they were drawn.
std::vector<std::wstring> TrimmedEditorText(HWND editor);
}
