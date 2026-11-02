#include "agent_catalog.h"
#include "local_launch.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
unsigned checks=0;
void Check(bool value,const char* label) {
    ++checks;if(!value)throw std::runtime_error(label);
}
struct Fixture {
    std::wstring directory;
    std::vector<std::wstring> files,folders;
    Fixture() {
        std::array<wchar_t,MAX_PATH+1> temp{},unique{};
        Check(GetTempPathW(static_cast<DWORD>(temp.size()),temp.data())>0,"temporary root exists");
        Check(GetTempFileNameW(temp.data(),L"CRL",0,unique.data())!=0,"unique owned test file created");
        directory=unique.data();
        Check(DeleteFileW(directory.c_str()) && CreateDirectoryW(directory.c_str(),nullptr),"owned fixture directory created");
    }
    ~Fixture() {
        for(const auto& file:files)DeleteFileW(file.c_str());
        for(const auto& folder:folders)RemoveDirectoryW(folder.c_str());
        RemoveDirectoryW(directory.c_str());
    }
    std::wstring Path(const wchar_t* name) const {return directory+L"\\"+name;}
    std::wstring Folder(const wchar_t* name) {
        const auto path=Path(name);
        Check(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"owned fixture subdirectory created");
        folders.push_back(path);return path;
    }
    std::wstring Link(const wchar_t* name,const std::wstring& destination,const wchar_t* arguments=nullptr) {
        const auto path=Path(name);

        winrt::com_ptr<IShellLinkW> link;
        winrt::check_hresult(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put())));
        if(destination.starts_with(L"https://")) {
            PIDLIST_ABSOLUTE item=nullptr;
            winrt::check_hresult(SHParseDisplayName(destination.c_str(),nullptr,&item,0,nullptr));
            const HRESULT result=link->SetIDList(item);CoTaskMemFree(item);
            winrt::check_hresult(result);
        } else winrt::check_hresult(link->SetPath(destination.c_str()));
        if(arguments)winrt::check_hresult(link->SetArguments(arguments));
        winrt::check_hresult(link.as<IPersistFile>()->Save(path.c_str(),TRUE));
        files.push_back(path);return path;
    }
};
reel::AppTarget Target(const std::wstring& path) {return {L"Test app",reel::TargetKind::Shell,path,L"",L""};}

// The agent catalog's pure rules: no file is touched.
void CatalogChecks() {
    using namespace reel::catalog;
    auto agent=[](const wchar_t* path)->const Rule* {return MatchRule(path);};
    auto is=[&](const wchar_t* path,Agent expected) {const auto* rule=agent(path);return rule && rule->agent==expected;};
    auto row=[&](const std::wstring& path) {const auto* rule=agent(path.c_str());return rule?RowName(*rule):std::wstring_view();};
    const std::wstring programs=L"C:\\Users\\x\\AppData\\Local\\Programs\\";

    Check(is((programs+L"cursor\\Cursor.exe").c_str(),Agent::Cursor),"Cursor by exe and install folder");
    Check(is(L"C:\\USERS\\X\\APPDATA\\LOCAL\\PROGRAMS\\CURSOR\\cursor.EXE",Agent::Cursor),"matching ignores case");
    Check(is(L"D:/Apps/cursor/Cursor.exe",Agent::Cursor),"any root and either separator");
    Check(!agent(L"C:\\Tools\\Cursor.exe"),"an exe name alone is not a product");
    Check(!agent((programs+L"cursor\\Code.exe").c_str()),"an install folder alone is not a product");
    Check(!agent((programs+L"cursor\\resources\\app\\bin\\cursor.cmd").c_str()),"a CLI shim is not the app");
    Check(is((programs+L"ClaudeScience\\claude-science.exe").c_str(),Agent::ClaudeScience),"Claude Science");
    Check(is((programs+L"DeepSeek Harness\\DeepSeek Harness.exe").c_str(),Agent::Dsh),"DeepSeek Harness");
    Check(is((programs+L"Grok Bot\\Grok Bot.exe").c_str(),Agent::GrokBot),"Grok Bot");
    Check(is(L"C:\\Program Files\\Microsoft VS Code\\Code.exe",Agent::VsCode),"VS Code");
    Check(!agent((programs+L"Microsoft VS Code Insiders\\Code - Insiders.exe").c_str()) &&
          !agent((programs+L"Microsoft VS Code Insiders\\Code.exe").c_str()),"VS Code Insiders is not VS Code");
    Check(is(L"D:\\Trae CN\\Trae CN.exe",Agent::TraeCn) && is(L"D:\\Trae\\Trae.exe",Agent::Trae),"Trae and Trae CN in custom roots");

    // Zed channels share Zed.exe; the folder names the row. bin\zed.exe is its CLI.
    const auto* zed=agent((programs+L"Zed\\Zed.exe").c_str());
    const auto* preview=agent((programs+L"Zed Preview\\Zed.exe").c_str());
    const auto* nightly=agent(L"C:\\Program Files\\Zed Nightly\\Zed.exe");
    Check(zed && preview && nightly && zed!=preview && preview!=nightly && zed!=nightly,"each Zed channel has its own rule");
    Check(RowName(*zed)==L"Zed" && RowName(*preview)==L"Zed Preview" && RowName(*nightly)==L"Zed Nightly","Zed channel row names");
    Check(!agent((programs+L"Zed\\bin\\zed.exe").c_str()),"the Zed CLI is not an app");
    Check(row(programs+L"Devin\\Devin.exe")==L"Devin Desktop" && row(programs+L"Devin\\Windsurf.exe")==L"Devin Desktop" &&
          row(programs+L"Windsurf\\Windsurf.exe")==L"Windsurf","Windsurf and Devin Desktop rows");
    Check(row(programs+L"cursor\\Cursor.exe")==L"Cursor" && row(programs+L"Kiro\\Kiro.exe")==L"Kiro","single-row products use their product name");

    // JetBrains IDEs live in <versioned folder>\bin\.
    const wchar_t* pycharm=L"C:\\Program Files\\JetBrains\\PyCharm Community Edition 2025.2\\bin\\pycharm64.exe";
    const auto* ide=agent(pycharm);
    Check(ide && ide->agent==Agent::JetBrains && ide->ide,"a standalone PyCharm by bin\\ and its product folder");
    Check(InstallFolder(pycharm,*ide)==L"PyCharm Community Edition 2025.2","an IDE row is named by its versioned folder");
    Check(is(L"C:\\Program Files\\JetBrains\\JetBrains Rider 2025.2\\bin\\rider64.exe",Agent::JetBrains) &&
          is((programs+L"IntelliJ IDEA Ultimate\\bin\\idea64.exe").c_str(),Agent::JetBrains),"Rider and Toolbox 2.x IntelliJ IDEA");
    Check(!agent(L"C:\\Program Files\\JetBrains\\PyCharm 2025.2\\pycharm64.exe"),"an IDE exe outside bin\\ is not matched");
    Check(!agent(L"C:\\Users\\x\\AppData\\Local\\JetBrains\\Toolbox\\apps\\PyCharm-P\\ch-0\\252.1\\bin\\pycharm64.exe"),
          "a versioned Toolbox 1.x build is never matched");
    Check(!agent(L"C:\\Games\\Riders Republic\\bin\\RidersRepublic.exe"),"a game named like an IDE is not matched");

    // The Claude Code CLI is not Claude Desktop; the legacy Squirrel stub is.
    const wchar_t* cli=L"C:\\Users\\x\\AppData\\Local\\Microsoft\\WinGet\\Packages\\Anthropic.ClaudeCode_Microsoft.Winget.Source_8wekyb3d8bbwe\\claude.exe";
    Check(!agent(cli) && !MatchCandidate(cli,kGuiSubsystem),"the Claude Code CLI never matches Claude Desktop");
    Check(is(L"C:\\Users\\x\\AppData\\Local\\AnthropicClaude\\claude.exe",Agent::Claude),"the legacy Squirrel Claude stub");

    // Uninstallers, updaters, staged copies and console programs are rejected.
    const auto cursor=programs+L"cursor\\Cursor.exe";
    Check(agent(cursor.c_str()) && MatchCandidate(cursor,kGuiSubsystem)==agent(cursor.c_str()),"a GUI Cursor.exe is accepted");
    Check(!MatchCandidate(cursor,3) && !MatchCandidate(cursor,0),"console programs and non-images are rejected");
    for(const auto* file:{L"unins000.exe",L"uninst.exe",L"Uninstall DeepSeek Harness.exe",L"UNINSTALL.EXE",L"Update.exe",L"inno_updater.exe"})
        Check(IsUninstallerOrUpdater(file),"uninstallers and updaters are recognized");
    for(const auto* file:{L"Cursor.exe",L"Code.exe",L"updater-helper.exe",L"Unison.exe"})
        Check(!IsUninstallerOrUpdater(file),"products are not uninstallers");
    for(const auto* path:{L"C:\\P\\cursor\\_\\Cursor.exe",L"C:\\P\\cursor\\_bak\\Cursor.exe",L"C:\\P\\AnthropicClaude\\app-0.9.3\\claude.exe"}) {
        Check(HasStagingSegment(path),"updater staging folders are recognized");
        Check(!MatchCandidate(path,kGuiSubsystem),"a staged copy is never a candidate");
    }
    Check(!HasStagingSegment(cursor) && !HasStagingSegment(L"C:\\Apps\\app-store\\_x\\x.exe") && !HasStagingSegment(L"C:\\Apps\\_bak.exe") &&
          !HasStagingSegment(L"Cursor.exe"),"ordinary folders and file names are not staging");
    for(const auto* path:{L"C:\\Users\\_\\AppData\\Local\\Programs\\cursor\\Cursor.exe",L"D:\\app-2025\\Kiro\\Kiro.exe",L"D:\\_bak\\Trae CN\\Trae CN.exe"})
        Check(!HasStagingSegment(path) && MatchCandidate(path,kGuiSubsystem),"a staging-like folder above the install folder never hides an install");
    for(const auto* path:{L"C:\\P\\Grok Bot\\Uninstall Grok Bot.exe",L"C:\\P\\cursor\\unins000.exe",L"C:\\P\\cursor\\tools\\inno_updater.exe",
                          L"C:\\P\\AnthropicClaude\\Update.exe"})
        Check(!MatchCandidate(path,kGuiSubsystem),"uninstallers and updaters are never candidates");

    // DisplayIcon is "path[,index]", optionally quoted; only executables count.
    Check(ExeFromDisplayIcon(L"C:\\P\\Grok Bot\\Grok Bot.exe,0")==L"C:\\P\\Grok Bot\\Grok Bot.exe","DisplayIcon index removed");
    Check(ExeFromDisplayIcon(L" \"C:\\P\\cursor\\Cursor.exe\",0 ")==L"C:\\P\\cursor\\Cursor.exe","quoted DisplayIcon");
    Check(ExeFromDisplayIcon(L"C:\\P\\A.EXE, -101")==L"C:\\P\\A.EXE","negative resource index");
    Check(ExeFromDisplayIcon(L"C:\\P\\ClaudeScience\\claude-science.exe")==L"C:\\P\\ClaudeScience\\claude-science.exe","DisplayIcon without index");
    Check(ExeFromDisplayIcon(L"C:\\Vendor, Inc\\app.exe")==L"C:\\Vendor, Inc\\app.exe","a comma inside the path is kept");
    Check(ExeFromDisplayIcon(L"C:\\P\\ZCode\\uninstallerIcon.ico").empty() && ExeFromDisplayIcon(L"C:\\P\\app.ico,0").empty() &&
          ExeFromDisplayIcon(L"").empty() && ExeFromDisplayIcon(L"\"C:\\P\\app.exe").empty(),"icons and malformed values give no exe");

    // Shortcut byte filter: catalog exe names in ANSI or UTF-16LE, any ASCII case.
    using namespace std::string_view_literals;
    Check(MayNameRuleExe("\x01\x02 C:\\P\\cursor\\CURSOR.EXE\0"sv),"ANSI exe name in any case");
    Check(MayNameRuleExe("x\0C\0o\0d\0e\0.\0E\0x\0E\0\0\0"sv) && MayNameRuleExe("\0G\0r\0o\0k\0 \0B\0o\0t\0.\0e\0x\0e\0"sv),
          "UTF-16LE exe names at either byte parity");
    Check(!MayNameRuleExe("C:\\Windows\\notepad.exe n\0o\0t\0e\0p\0a\0d\0.\0e\0x\0e\0"sv) && !MayNameRuleExe(""sv) &&
          !MayNameRuleExe("Cursor.lnk Cursor.ex"sv),"links that cannot name a catalog exe are skipped");

    // Catalog shape: product names that LocalReplacement migrates to, and unambiguous rules.
    Check(kEntries[0].name==L"ChatGPT / Codex" && kEntries[1].name==L"Claude Desktop","legacy website migration names are stable");
    bool coreFirst=true,seenOptional=false;
    for(const auto& entry:kEntries){if(!entry.core)seenOptional=true;else if(seenOptional)coreFirst=false;}
    Check(coreFirst,"core products precede discover-only ones");
    std::set<std::wstring> layouts;
    for(const auto& rule:kRules) {
        Check(EndsWith(rule.exe,L".exe") && !rule.folder.empty() && !IsUninstallerOrUpdater(rule.exe),"every rule names a product exe and folder");
        Check(layouts.insert(Lower(rule.exe)+L"|"+Lower(rule.folder)).second,"no two rules claim one layout");
        Check(rule.ide==(rule.probe==Probe::None && rule.agent==Agent::JetBrains),"only IDE rules skip folder probes");
        if(!rule.ide) {
            const auto path=L"C:\\Root\\"+std::wstring(rule.folder)+L"\\"+std::wstring(rule.exe);
            Check(MatchRule(path)==&rule,"every rule matches its own layout");
        }
    }
    for(size_t index=0;index<std::size(kEntries);++index) {
        bool hasRule=false;
        for(const auto& rule:kRules)hasRule=hasRule || static_cast<size_t>(rule.agent)==index;
        Check(kEntries[index].core || hasRule,"a discover-only product can be discovered");
    }
}

std::wstring Variable(const wchar_t* name) {
    std::array<wchar_t,1024> value{};
    const DWORD length=GetEnvironmentVariableW(name,value.data(),static_cast<DWORD>(value.size()));
    return length<value.size()?std::wstring(value.data(),length):std::wstring();
}

// App activation starts the reel without SESSIONNAME; the reel takes it from the session, as
// Explorer does, so the programs it starts see it. Only this test process's environment changes.
void SessionEnvironmentChecks() {
    DWORD session=0;
    Check(ProcessIdToSessionId(GetCurrentProcessId(),&session)!=0,"the test knows its session");
    std::array<wchar_t,1024> expected{};
    DWORD bytes=static_cast<DWORD>(expected.size()*sizeof(wchar_t));
    const auto key=L"Volatile Environment\\"+std::to_wstring(session);
    const bool named=RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"SESSIONNAME",RRF_RT_REG_SZ,nullptr,expected.data(),&bytes)==ERROR_SUCCESS;
    const auto original=Variable(L"SESSIONNAME");
    const bool hadOriginal=GetEnvironmentVariableW(L"SESSIONNAME",nullptr,0)!=0;
    Check(SetEnvironmentVariableW(L"SESSIONNAME",nullptr)!=0,"SESSIONNAME removed as activation leaves it out");
    auto set=reel::InheritSessionEnvironment();
    const bool reported=std::find(set.begin(),set.end(),L"SESSIONNAME")!=set.end();
    if(named)Check(reported && Variable(L"SESSIONNAME")==expected.data(),"a missing SESSIONNAME is the session's own");
    else Check(!reported && GetEnvironmentVariableW(L"SESSIONNAME",nullptr,0)==0,"without the session's key nothing is invented");
    // A console session keeps CLIENTNAME empty in its key; like Explorer, the reel leaves it undefined.
    std::array<wchar_t,1024> client{};
    bytes=static_cast<DWORD>(client.size()*sizeof(wchar_t));
    if(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"CLIENTNAME",RRF_RT_REG_SZ,nullptr,client.data(),&bytes)==ERROR_SUCCESS && !client[0] &&
       GetEnvironmentVariableW(L"CLIENTNAME",nullptr,0)==0)
        Check(std::find(set.begin(),set.end(),L"CLIENTNAME")==set.end() && GetEnvironmentVariableW(L"CLIENTNAME",nullptr,0)==0,
            "an empty CLIENTNAME stays undefined");
    Check(SetEnvironmentVariableW(L"SESSIONNAME",L"Kept")!=0,"a set SESSIONNAME");
    set=reel::InheritSessionEnvironment();
    Check(Variable(L"SESSIONNAME")==L"Kept" && std::find(set.begin(),set.end(),L"SESSIONNAME")==set.end(),"a SESSIONNAME already set is kept");
    SetEnvironmentVariableW(L"SESSIONNAME",hadOriginal?original.c_str():nullptr);
}
}

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        Fixture fixture;
        std::array<wchar_t,32768> windows{};
        Check(GetWindowsDirectoryW(windows.data(),static_cast<UINT>(windows.size()))>0,"Windows path exists");
        const auto notepad=std::wstring(windows.data())+L"\\System32\\notepad.exe";
        Check(GetFileAttributesW(notepad.c_str())!=INVALID_FILE_ATTRIBUTES,"native Notepad test destination exists");
        const auto native=fixture.Link(L"native.lnk",notepad);
        auto profile=Target(native);profile.arguments=L"--preserved";
        Check(reel::IsVerifiedLocalApplication(profile),"native executable shortcut accepted");
        Check(profile.target==native && profile.arguments==L"--preserved","verification does not rewrite launch profile");
        const auto second=fixture.Link(L"second.lnk",native);
        const auto third=fixture.Link(L"third.lnk",second);
        Check(reel::IsVerifiedLocalApplication(Target(second)),"native shortcut chain accepted");
        // ShellLink may flatten chains while saving. Test the actual resulting
        // native destination rather than assuming an unresolved on-disk chain.
        Check(reel::IsVerifiedLocalApplication(Target(third)),"ShellLink-saved native chain remains usable");
        const auto website=fixture.Link(L"website.lnk",L"https://example.com/");
        Check(!reel::IsVerifiedLocalApplication(Target(website)),"URL destination shortcut rejected");
        const auto missing=fixture.Link(L"missing-target.lnk",fixture.Path(L"missing.exe"));
        Check(!reel::IsVerifiedLocalApplication(Target(missing)),"missing shortcut destination rejected");
        Check(!reel::IsVerifiedLocalApplication(Target(fixture.Path(L"missing.lnk"))),"missing shortcut file rejected");
        Check(!reel::IsVerifiedLocalApplication(Target(L"https://example.com/")),"direct web target rejected");
        Check(reel::IsVerifiedLocalApplication(Target(L"notepad.exe")),"non-shortcut native launch policy retained");
        Check(reel::IsVerifiedLocalApplication(Target(L"ms-settings:colors")),"Windows settings activation retained");
        Check(reel::IsVerifiedLocalApplication({L"Package",reel::TargetKind::Aumid,L"PackageFamily!App",L"",L""}),"packaged activation policy retained");

        // An absolute executable must exist as a file; a bare name is found at launch.
        Check(reel::IsVerifiedLocalApplication(Target(notepad)),"existing absolute executable accepted");
        Check(!reel::IsVerifiedLocalApplication(Target(fixture.Path(L"missing.exe"))),"missing absolute executable rejected");
        Check(!reel::IsVerifiedLocalApplication(Target(fixture.Folder(L"folder.exe"))),"a folder named like an executable is rejected");
        // A network executable is never touched: a slow or disconnected share must not stall the reel.
        Check(reel::IsVerifiedLocalApplication(Target(L"\\\\copilot-reel-no-such-host\\share\\agent.exe")),"a UNC executable counts as present without a lookup");
        Check(reel::IsVerifiedLocalApplication(Target(L"\\\\.\\UNC\\copilot-reel-no-such-host\\share\\agent.exe")),
            "a UNC executable behind the device prefix counts as present without a lookup");
        for(const auto* path:{L"\\\\server\\share\\a.exe",L"//server/share/a.exe",L"\\\\?\\UNC\\server\\share\\a.exe",
                              L"\\\\.\\unc\\server\\share\\a.exe"})
            Check(reel::IsNetworkPath(path),"UNC paths are network paths, with or without a device prefix");
        for(const auto* path:{L"C:\\Windows\\notepad.exe",L"\\\\?\\C:\\Windows\\notepad.exe",L"\\\\.\\C:\\Windows\\notepad.exe",
                              L"notepad.exe",L"\\\\?\\Volume{0}\\a.exe",L"\\\\.\\UNCX\\a.exe",L""})
            Check(!reel::IsNetworkPath(path),"local drives, bare names and volume paths are not network paths");
        // A shortcut's own arguments decide whether a running window may stand in for it.
        const auto profiled=fixture.Link(L"profiled.lnk",notepad,L"--profile-directory=\"Work\"");
        Check(reel::ShortcutArguments(profiled)==L"--profile-directory=\"Work\"" && reel::ShortcutArguments(native).empty(),
            "shortcut arguments are read without resolving the link");
        // ShellLink may flatten a chain while saving; when it keeps it, the inner link's arguments count.
        const auto outer=fixture.Link(L"outer.lnk",profiled);
        if(reel::LaunchIdentity(reel::ShortcutTarget(outer)).ends_with(L".lnk"))
            Check(reel::ShortcutArguments(outer)==L"--profile-directory=\"Work\"","arguments of a linked shortcut are found along the chain");
        Check(reel::ShortcutArguments(fixture.Path(L"missing.lnk")).empty(),"an unreadable shortcut has no arguments");
        Check(reel::IsVerifiedLocalApplication(Target(L"absent-tool.exe")),"bare names keep the search-path policy");

        // One identity per launched application, independent of arguments and links.
        const auto file=L"file:"+reel::LaunchIdentity(std::filesystem::path(notepad).lexically_normal().wstring());
        Check(reel::InstalledIdentity(Target(notepad))==file,"absolute executable identity is its lowercase full path");
        Check(reel::InstalledIdentity(profile)==file && reel::InstalledIdentity(Target(second))==file &&
              reel::InstalledIdentity(Target(third))==file,"shortcuts to one executable share its identity");
        auto upper=Target(reel::LaunchIdentity(notepad));upper.target[0]=static_cast<wchar_t>(std::towupper(upper.target[0]));
        upper.arguments=L"--other";upper.workingDirectory=L"C:\\";
        Check(reel::InstalledIdentity(upper)==file,"case, arguments and working directory are ignored");
        Check(profile.target==native && profile.arguments==L"--preserved","identity does not rewrite launch profile");
        const reel::AppTarget package{L"Package",reel::TargetKind::Aumid,L"Vendor.Agent_abc123!App",L"",L""};
        const reel::AppTarget shouting{L"Other",reel::TargetKind::Aumid,L"VENDOR.AGENT_ABC123!APP",L"",L""};
        Check(reel::InstalledIdentity(package)==L"aumid:vendor.agent_abc123!app","package identity is its lowercase AUMID");
        Check(reel::InstalledIdentity(shouting)==reel::InstalledIdentity(package),"AUMID identity ignores case");
        Check(reel::InstalledIdentity(Target(L"shell:AppsFolder\\Vendor.Agent_abc123!App"))==reel::InstalledIdentity(package),
              "shell:AppsFolder activation equals the AUMID");
        Check(reel::InstalledIdentity(Target(L"Notepad.EXE"))==L"file:notepad.exe","a bare name is its own identity");
        Check(reel::InstalledIdentity(Target(L"MS-Settings:Colors"))==L"uri:ms-settings:colors","Settings URI identity");
        for(const auto& unverified:{Target(website),Target(missing),Target(fixture.Path(L"missing.lnk")),Target(L"https://example.com/"),
                                    Target(fixture.Path(L"missing.exe")),Target(L""),
                                    reel::AppTarget{L"Broken",reel::TargetKind::Aumid,L"NoApplicationId",L"",L""}})
            Check(reel::InstalledIdentity(unverified).empty(),"unverified targets have no identity");
        Check(reel::LaunchIdentity(reel::ShortcutTarget(native))==reel::LaunchIdentity(notepad),
              "a shortcut's raw destination is read without Resolve");
        Check(reel::ShortcutTarget(fixture.Path(L"missing.lnk")).empty(),"an unreadable shortcut has no destination");

        // Discovery loads only links whose bytes name a catalog executable, as ShellLink writes them.
        auto bytes=[](const std::wstring& path) {
            std::ifstream file(path,std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        };
        const auto cursorExe=fixture.Path(L"cursor\\Cursor.exe");
        const auto cursorLink=fixture.Link(L"cursor.lnk",cursorExe);
        Check(reel::catalog::MayNameRuleExe(bytes(cursorLink)),"a saved link to a catalog executable passes the byte filter");
        Check(!reel::catalog::MayNameRuleExe(bytes(native)),"a saved link to another executable is not loaded");
        Check(reel::catalog::MatchRule(reel::ShortcutTarget(cursorLink))!=nullptr,"the loaded link names its catalog rule");

        CatalogChecks();
        SessionEnvironmentChecks();
        std::cout<<"Local launch tests passed: "<<checks<<" checks; no destination was executed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;
    } catch(const winrt::hresult_error& error) {
        std::wcerr<<L"FAIL: "<<error.message().c_str()<<L'\n';return 1;
    }
}
