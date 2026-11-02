#include "local_launch.h"

#include <shobjidl.h>
#include <winrt/base.h>
#include <array>
#include <filesystem>
#include <set>

namespace reel {
namespace {
bool HasShortcutExtension(const std::wstring& path) {
    return LaunchIdentity(path).ends_with(L".lnk");
}

bool IsFile(const std::wstring& path) {
    const DWORD attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FullPath(const std::wstring& path) {
    std::wstring absolute(32768,L'\0');
    const DWORD size=GetFullPathNameW(path.c_str(),static_cast<DWORD>(absolute.size()),absolute.data(),nullptr);
    if(!size || size>=absolute.size())return {};
    absolute.resize(size);return absolute;
}

// `destination` receives the final executable of a verified chain.
bool VerifyShortcut(std::wstring path,unsigned remaining,std::set<std::wstring>& visited,std::wstring& destination) {
    if(!remaining)return false;
    path=FullPath(path);
    if(path.empty() || !IsFile(path) || !visited.insert(LaunchIdentity(path)).second)return false;
    AppTarget next{L"",TargetKind::Shell,ShortcutTarget(path),L"",L""};
    if(next.target.empty() || !IsLocalApplication(next))return false;
    std::filesystem::path resolved(next.target);
    if(resolved.is_relative())resolved=std::filesystem::path(path).parent_path()/resolved;
    next.target=resolved.lexically_normal().wstring();
    if(HasShortcutExtension(next.target))return VerifyShortcut(next.target,remaining-1,visited,destination);
    if(!LaunchIdentity(next.target).ends_with(L".exe") || !IsFile(next.target))return false;
    destination=FullPath(next.target);
    return !destination.empty();
}

// One verifier for IsVerifiedLocalApplication and InstalledIdentity. For a shortcut,
// `destination` receives its final executable.
bool Verify(const AppTarget& app,std::wstring& destination) {
    if(!IsLocalApplication(app))return false;
    if(app.kind!=TargetKind::Shell)return true;
    if(HasShortcutExtension(app.target)) {
        try {
            std::set<std::wstring> visited;
            return VerifyShortcut(app.target,4,visited,destination);
        } catch(...) {return false;}
    }
    if(LaunchIdentity(app.target).ends_with(L".exe") && std::filesystem::path(app.target).is_absolute())
        return IsNetworkPath(app.target) || IsFile(app.target);
    return true;
}
}

std::wstring ShortcutTarget(const std::wstring& link) {
    winrt::com_ptr<IShellLinkW> shell;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(shell.put()))))return {};
    const auto persisted=shell.try_as<IPersistFile>();
    if(!persisted || FAILED(persisted->Load(link.c_str(),STGM_READ)))return {};
    // Discovery reads every Start-menu link; skip zero-filling 128 KB per call.
    std::array<wchar_t,32768> raw,expanded;raw[0]=0;
    WIN32_FIND_DATAW metadata{};
    // Do not call Resolve: reading a link must not show UI, search the network,
    // update the shortcut, or execute the destination.
    if(FAILED(shell->GetPath(raw.data(),static_cast<int>(raw.size()),&metadata,SLGP_RAWPATH)) || !raw[0])return {};
    const DWORD required=ExpandEnvironmentStringsW(raw.data(),expanded.data(),static_cast<DWORD>(expanded.size()));
    if(!required || required>expanded.size())return {};
    return expanded.data();
}

std::wstring ShortcutArguments(const std::wstring& link) {
    // A shortcut to a shortcut launches with the arguments of every link on the way; the
    // verifier follows up to four of them, so the first non-empty arguments on that chain count.
    std::wstring path=link;
    for(int hop=0;hop<4 && LaunchIdentity(path).ends_with(L".lnk");++hop) {
        winrt::com_ptr<IShellLinkW> shell;
        if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(shell.put()))))return {};
        const auto persisted=shell.try_as<IPersistFile>();
        if(!persisted || FAILED(persisted->Load(path.c_str(),STGM_READ)))return {};
        std::array<wchar_t,32768> arguments;arguments[0]=0;
        if(SUCCEEDED(shell->GetArguments(arguments.data(),static_cast<int>(arguments.size()))) && arguments[0])
            return arguments.data();
        auto next=ShortcutTarget(path);
        if(next.empty())return {};
        std::filesystem::path resolved(next);
        if(resolved.is_relative())resolved=std::filesystem::path(path).parent_path()/resolved;
        path=resolved.lexically_normal().wstring();
    }
    return {};
}

bool IsVerifiedLocalApplication(const AppTarget& app) {
    std::wstring destination;
    return Verify(app,destination);
}

std::wstring InstalledIdentity(const AppTarget& app) {
    std::wstring destination;
    if(!Verify(app,destination))return {};
    const auto target=LaunchIdentity(app.target);
    constexpr std::wstring_view appsFolder=L"shell:appsfolder\\";
    if(app.kind==TargetKind::Aumid)return L"aumid:"+target;
    if(target.starts_with(appsFolder))return L"aumid:"+target.substr(appsFolder.size());
    if(target.starts_with(L"ms-settings:"))return L"uri:"+target;
    if(!destination.empty())return L"file:"+LaunchIdentity(destination);
    if(std::filesystem::path(app.target).is_absolute()) {
        const auto full=FullPath(app.target);
        return full.empty()?std::wstring():L"file:"+LaunchIdentity(full);
    }
    return L"file:"+target;
}

std::vector<std::wstring> InheritSessionEnvironment() {
    std::vector<std::wstring> set;
    DWORD session=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return set;
    HKEY key=nullptr;
    const auto path=L"Volatile Environment\\"+std::to_wstring(session);
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return set;
    std::array<wchar_t,256> name{};  // value names are at most 16383 characters; these are short
    std::array<wchar_t,1024> value{};
    for(DWORD index=0;;++index) {
        DWORD nameLength=static_cast<DWORD>(name.size()),type=0,bytes=static_cast<DWORD>(value.size()*sizeof(wchar_t));
        const LONG status=RegEnumValueW(key,index,name.data(),&nameLength,nullptr,&type,
            reinterpret_cast<BYTE*>(value.data()),&bytes);
        if(status==ERROR_NO_MORE_ITEMS)break;
        if(status!=ERROR_SUCCESS || type!=REG_SZ || nameLength==0)continue;
        const std::wstring variable(name.data(),nameLength);
        std::wstring text(value.data(),bytes/sizeof(wchar_t));
        while(!text.empty() && text.back()==L'\0')text.pop_back();
        SetLastError(ERROR_SUCCESS);
        if(GetEnvironmentVariableW(variable.c_str(),nullptr,0)!=0 || GetLastError()!=ERROR_ENVVAR_NOT_FOUND)continue;
        if(SetEnvironmentVariableW(variable.c_str(),text.c_str()))set.push_back(variable);
    }
    RegCloseKey(key);
    return set;
}
}
