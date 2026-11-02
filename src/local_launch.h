#pragma once
#include "app_types.h"
#include <algorithm>
#include <cwctype>
#include <optional>
#include <string_view>
#include <vector>

namespace reel {
// Checks shortcut destinations without executing or rewriting them. Requires
// COM on the calling thread; unresolved, cyclic, and non-application links fail.
// An absolute .exe must exist as a file; a bare name such as notepad.exe is found
// on the search path at launch. An executable on a network path is not touched here:
// a slow or disconnected share must never stall the caller, so it counts as present.
bool IsVerifiedLocalApplication(const AppTarget& app);

// The application a verified target launches, all lowercase: "aumid:<family!app>" for
// a package (TargetKind::Aumid and shell:AppsFolder\ alike), "file:<full path>" for an
// absolute .exe or the final .exe of a shortcut, "file:<name>" for a bare name such as
// notepad.exe, and "uri:<target>" for ms-settings:. Arguments and working directory are
// ignored. Empty unless IsVerifiedLocalApplication(app). Requires COM.
std::wstring InstalledIdentity(const AppTarget& app);

// A shortcut's raw destination with environment strings expanded, read without
// Resolve; empty when the link cannot be read. Requires COM.
std::wstring ShortcutTarget(const std::wstring& link);

// Copies this session's variables from HKCU\Volatile Environment\<session id>, SESSIONNAME and,
// in a remote session, CLIENTNAME, into the process environment where they are missing. App
// activation starts the reel without them, unlike a start from Explorer, and the programs the
// reel starts inherit its environment: Claude Science opens its window only when SESSIONNAME
// is set. A variable that is already set is kept, and an empty value (CLIENTNAME in a console
// session) stays undefined, as Explorer leaves it. Returns the names it set.
std::vector<std::wstring> InheritSessionEnvironment();

// The arguments a shortcut launches with (a profile, a project, a URL): the first non-empty
// arguments along a chain of up to four links, read without Resolve; empty when there are
// none or a link cannot be read. Requires COM.
std::wstring ShortcutArguments(const std::wstring& link);

inline std::wstring LaunchIdentity(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    return value;
}

// UNC paths (also behind the \\?\UNC\ and \\.\UNC\ device prefixes) and mapped network
// drives. GetDriveTypeW reads the local drive mapping and never contacts the share, so this
// is safe on the UI thread; nothing that can wait on a server should touch such a path there.
inline bool IsNetworkPath(std::wstring_view path) {
    const auto separator=[&](size_t i){return i<path.size() && (path[i]==L'\\' || path[i]==L'/');};
    if(separator(0) && separator(1) && path.size()>=4 && (path[2]==L'?' || path[2]==L'.') && separator(3)) {
        path.remove_prefix(4);
        if(path.size()>=3 && (path[0]==L'U' || path[0]==L'u') && (path[1]==L'N' || path[1]==L'n') &&
           (path[2]==L'C' || path[2]==L'c') && (path.size()==3 || separator(3)))return true;
    } else if(separator(0) && separator(1))return true;
    if(path.size()<2 || path[1]!=L':')return false;
    const wchar_t root[]{path[0],L':',L'\\',0};
    return GetDriveTypeW(root)==DRIVE_REMOTE;
}

// Short tap is an application activation, never a browser fallback. Explicit
// website entries may remain in the wheel, but cannot be dispatched by Tap.
inline bool IsLocalApplication(const AppTarget& app) {
    const auto target=LaunchIdentity(app.target);
    if(app.kind==TargetKind::Aumid)return !target.empty() && target.find(L'!')!=target.npos && target.find(L':')==target.npos;
    if(target.starts_with(L"ms-settings:"))return true;
    if(target.starts_with(L"shell:appsfolder\\") && target.find(L'!')!=target.npos)return true;
    if(target.find(L"://")!=target.npos || target.find_first_of(L"?\r\n")!=target.npos)return false;
    const auto colon=target.find(L':');
    if(colon!=target.npos && (colon!=1 || target.size()<3 || (target[2]!=L'\\' && target[2]!=L'/')))return false;
    return target.ends_with(L".exe") || target.ends_with(L".lnk");
}

inline std::optional<AppTarget> LocalReplacement(const AppTarget& source,const std::vector<AppTarget>& presets) {
    if(IsLocalApplication(source))return source;
    if(source.kind!=TargetKind::Shell || !source.arguments.empty() || !source.workingDirectory.empty())return {};
    auto target=LaunchIdentity(source.target);
    if(target.ends_with(L"/"))target.pop_back();
    std::wstring_view name;
    if(target==L"https://chatgpt.com" || target==L"https://chat.openai.com")name=L"ChatGPT / Codex";
    else if(target==L"https://claude.ai")name=L"Claude Desktop";
    else return {};
    for(const auto& preset:presets)if(preset.name==name && IsLocalApplication(preset)) {
        auto result=preset;result.name=source.name;return result;
    }
    return {};
}
}
