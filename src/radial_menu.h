#pragma once
#include "app_types.h"
#include "localization.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace reel {
// The values are logged and read by the seam tests; 4 was the Preferred agent page.
enum class MenuPage { Main, Manage, Presets, Settings, Entry=5, About=6 };
enum class MenuAction { Launch, Manage, Settings, Edit, Presets, Preset, Restore, Custom,
    Language, SetPreferred, Appearance, Entry, Remove, Previous, Next, Done, Size,
    About, AboutInfo, GitHub, AutoCheck, AutoInstall, CheckUpdate, InstallUpdate, TrayIcon };
// Disabled entries keep their slot and name but can never be chosen.
struct MenuEntry { AppTarget visual; MenuAction action; int index=-1; bool enabled=true; TileBadge badge=TileBadge::None; };
inline constexpr wchar_t kAuthor[]=L"baka_sayaka";
inline constexpr wchar_t kRepositoryUrl[]=L"https://github.com/bakasayaka/copilot-reel";
// What the About page shows: the running version, the two update switches and where an update
// stands. `offered` is the version an update would install, or empty.
struct AboutState {
    std::wstring version;
    bool autoCheck=true,autoInstall=false;
    UpdateStage stage=UpdateStage::Idle;
    std::wstring offered;
};
// An update waits to be installed: its tile and the hub's About tile carry the dot.
inline bool UpdateWaiting(const AboutState& about) {
    return !about.offered.empty() && (about.stage==UpdateStage::Available || about.stage==UpdateStage::Failed);
}
inline constexpr size_t kMaxApps=8;
// Apps taken out of the reel keep their settings as Add candidates; the oldest leave first.
inline constexpr size_t kMaxParked=16;

inline bool CanConfirm(const std::vector<MenuEntry>& menu,int index) {
    return index>=0 && static_cast<size_t>(index)<menu.size() && menu[index].enabled;
}

// Slot 0 of the hub (the language) is adjusted with the flanking chevrons or A/D.
struct SlotAdjust { bool control=false,earlier=false,later=false; };
inline SlotAdjust AdjustFor(MenuPage page) {
    if(page==MenuPage::Settings)return {true,true,true};
    return {};
}

// Moves one app to another place in the reel and keeps every field of every entry.
inline bool MoveEntry(std::vector<AppTarget>& apps,int from,int to) {
    const int count=static_cast<int>(apps.size());
    if(from<0 || from>=count || to<0 || to>=count || from==to)return false;
    if(from<to)std::rotate(apps.begin()+from,apps.begin()+from+1,apps.begin()+to+1);
    else std::rotate(apps.begin()+to,apps.begin()+from,apps.begin()+from+1);
    return true;
}
// Where an entry at `position` lands after MoveEntry(apps,from,to).
inline int MovedIndex(int position,int from,int to) {
    if(position==from)return to;
    if(from<to && position>from && position<=to)return position-1;
    if(to<from && position>=to && position<from)return position+1;
    return position;
}

// The Manage page places a dragged app in the app slot whose angle is nearest the pointer.
// The page has `slots` evenly spaced slots from 12 o'clock clockwise; the first `apps` of
// them hold apps and Add follows, so a pointer over Add picks the nearer end of the reel.
inline int DropSlot(double dx,double dy,size_t slots,size_t apps) {
    if(!apps || slots<apps || !std::isfinite(dx) || !std::isfinite(dy))return -1;
    constexpr double pi=3.14159265358979323846;
    double angle=std::atan2(dy,dx)+pi/2;
    if(angle<0)angle+=2*pi;
    const double position=angle*static_cast<double>(slots)/(2*pi);
    int best=0;double nearest=1e9;
    for(size_t i=0;i<apps;++i) {
        double distance=std::fmod(std::abs(position-static_cast<double>(i)),static_cast<double>(slots));
        distance=std::min(distance,static_cast<double>(slots)-distance);
        if(distance<nearest-1e-9) {nearest=distance;best=static_cast<int>(i);}
    }
    return best;
}

// Two entries are the same app when every saved field matches.
inline bool SameApp(const AppTarget& a,const AppTarget& b) {
    return a.name==b.name && a.kind==b.kind && a.target==b.target && a.arguments==b.arguments &&
        a.workingDirectory==b.workingDirectory;
}
// An app taken out of the reel goes first among the parked candidates, once.
inline void Park(std::vector<AppTarget>& parked,const AppTarget& app) {
    std::erase_if(parked,[&](const AppTarget& other){return SameApp(other,app);});
    parked.insert(parked.begin(),app);
    if(parked.size()>kMaxParked)parked.resize(kMaxParked);
}

// The Add page: apps taken out of the reel, most recent first and with their own settings;
// then installed agents that are not in the reel yet, in catalog order; then catalog entries
// that still need a target. `identity` names the application a target launches and is empty
// when the target is not a verified local application. A parked app is a launch profile: it is
// skipped only while the same profile (target, arguments and folder) is back in the reel, and
// a catalog agent a parked app already offers is listed once.
inline std::vector<AppTarget> AddCatalog(const std::vector<AppTarget>& apps,const std::vector<AppTarget>& parked,
    const std::vector<AppTarget>& presets,const std::function<std::wstring(const AppTarget&)>& identity,
    size_t* parkedCount=nullptr,size_t* installedCount=nullptr) {
    std::set<std::wstring> taken;
    for(const auto& app:apps)if(auto key=identity(app);!key.empty())taken.insert(std::move(key));
    std::vector<AppTarget> restorable,installed,pending;
    for(const auto& app:parked) {
        const bool inReel=std::any_of(apps.begin(),apps.end(),[&](const AppTarget& other) {
            return other.kind==app.kind && other.target==app.target && other.arguments==app.arguments &&
                other.workingDirectory==app.workingDirectory;
        });
        if(inReel)continue;
        if(auto key=identity(app);!key.empty())taken.insert(std::move(key));
        restorable.push_back(app);
    }
    for(const auto& preset:presets) {
        auto key=identity(preset);
        if(key.empty())pending.push_back(preset);
        else if(taken.insert(std::move(key)).second)installed.push_back(preset);
    }
    if(parkedCount)*parkedCount=restorable.size();
    if(installedCount)*installedCount=installed.size();
    restorable.insert(restorable.end(),installed.begin(),installed.end());
    restorable.insert(restorable.end(),pending.begin(),pending.end());
    return restorable;
}

// Esc and Backspace go back, Return and Space confirm, Tab moves around the ring. On a page
// with an adjustable slot 0, A and D always adjust it; Left and Right adjust it while it is
// selected and otherwise move around the ring like on every other page. On a reorderable
// page (Manage) A and D move the selected app and Delete takes it out of the reel.
enum class KeyRoute { None, Back, Confirm, Adjust, Next, Previous, Remove };
inline KeyRoute RouteKey(UINT key,bool shift,bool adjustable,int selected,bool reorderable=false) {
    if(key==VK_ESCAPE || key==VK_BACK)return KeyRoute::Back;
    if(key==VK_RETURN || key==VK_SPACE)return KeyRoute::Confirm;
    if((adjustable || reorderable) && (key=='A' || key=='D'))return KeyRoute::Adjust;
    if(reorderable && key==VK_DELETE)return KeyRoute::Remove;
    if(key==VK_LEFT || key==VK_RIGHT)
        return adjustable && selected==0?KeyRoute::Adjust:(key==VK_LEFT?KeyRoute::Previous:KeyRoute::Next);
    if(key==VK_TAB)return shift?KeyRoute::Previous:KeyRoute::Next;
    return KeyRoute::None;
}
inline int AdjustDirection(UINT key) {return key=='A' || key==VK_LEFT?-1:1;}

// Action metadata is separate from launch targets, so internal commands never
// reach ShellExecute and app entries retain their official icon identity.
// On the Add page the first `restorable` catalog entries are parked apps.
inline std::vector<MenuEntry> BuildMenu(MenuPage page,const std::vector<AppTarget>& apps,
    const std::vector<AppTarget>& presets,size_t offset=0,int editing=-1,size_t restorable=0,
    const AboutState& about=AboutState{}) {
    std::vector<MenuEntry> result;
    auto action=[&](std::wstring name,const wchar_t* glyph,MenuAction kind,int index=-1,bool enabled=true,
        TileBadge badge=TileBadge::None) {
        result.push_back({{std::move(name),TargetKind::Shell,glyph,L"",L""},kind,index,enabled,badge});
    };
    auto entries=[&](MenuAction kind) {
        for(size_t i=0;i<apps.size();++i) result.push_back({apps[i],kind,static_cast<int>(i)});
    };
    // At capacity Add stays in place, disabled, so the orbit never reflows.
    const bool canAdd=apps.size()<kMaxApps;
    switch(page) {
    case MenuPage::Main:
        entries(MenuAction::Launch);
        action(Tr(L"Apps and settings",L"应用与设置",L"アプリと設定"),L"reel:add",MenuAction::Settings);break;
    case MenuPage::Manage:
        entries(MenuAction::Entry);
        action(Tr(L"Add app",L"添加应用",L"アプリを追加"),L"reel:add",MenuAction::Presets,-1,canAdd);break;
    case MenuPage::Presets:
        for(size_t i=offset;i<std::min(offset+5,presets.size());++i)
            result.push_back({presets[i],i<restorable?MenuAction::Restore:MenuAction::Preset,static_cast<int>(i)});
        action(Tr(L"Custom app",L"自定义应用",L"手動で追加"),L"reel:add",MenuAction::Custom);
        if(offset>0)action(Tr(L"Previous",L"上一页",L"前へ"),L"reel:previous",MenuAction::Previous);
        if(offset+5<presets.size())action(Tr(L"Next",L"下一页",L"次へ"),L"reel:next",MenuAction::Next);break;
    case MenuPage::Settings:
        action(Tr(L"Language",L"语言",L"言語"),L"reel:language",MenuAction::Language);
        action(Tr(L"Appearance",L"外观",L"外観"),L"reel:appearance",MenuAction::Appearance);
        // Shows its step in the tile; each confirm moves to the next and wraps.
        action(Tr(L"Reel size",L"轮盘大小",L"リールの大きさ"),L"reel:size",MenuAction::Size);
        // Shows the notification-area icon it chose: the reel's color mark or the mark alone.
        action(Tr(L"Tray icon",L"托盘图标",L"トレイ アイコン"),
            CurrentTrayMonochrome()?L"reel:tray-icon:monochrome":L"reel:tray-icon:color",MenuAction::TrayIcon);
        action(Tr(L"Manage apps",L"管理应用",L"アプリの管理"),L"reel:manage",MenuAction::Manage);
        action(Tr(L"Add app",L"添加应用",L"アプリを追加"),L"reel:add",MenuAction::Presets,-1,canAdd);
        action(Tr(L"About",L"关于",L"バージョン情報"),L"reel:about",MenuAction::About,-1,true,UpdateWaiting(about)?TileBadge::Dot:TileBadge::None);break;
    case MenuPage::About: {
        // The reel's own mark comes first, named by the version, then GitHub's mark, named by the
        // author, two switches that show their state in the icon and the update tile, which checks or
        // installs the update it found. Every name fits the center's one line at every size step
        // (tests/typography_tests.cpp), so the switches say Auto-check and Auto-install, a failure
        // names only what failed (choosing it tries again, as Up to date checks again), and the
        // Japanese update tile says 更新, as it does while installing and after a failure.
        const bool updatable=about.stage!=UpdateStage::Unavailable;
        const auto state=[](const wchar_t* target,bool on){return std::wstring(target)+(on?L":on":L":off");};
        result.push_back({{about.version,TargetKind::Shell,L"reel:author",L"",L""},MenuAction::AboutInfo});
        result.push_back({{kAuthor,TargetKind::Shell,L"reel:github",L"",L""},MenuAction::GitHub});
        result.push_back({{Tr(L"Auto-check",L"自动检查更新",L"更新を自動確認"),TargetKind::Shell,state(L"reel:auto-check",about.autoCheck),L"",L""},
            MenuAction::AutoCheck,-1,updatable});
        result.push_back({{Tr(L"Auto-install",L"自动安装更新",L"更新を自動適用"),TargetKind::Shell,state(L"reel:auto-install",about.autoInstall),L"",L""},
            MenuAction::AutoInstall,-1,updatable});
        const auto& version=about.offered;
        switch(about.stage) {
        case UpdateStage::Unavailable:
            action(Tr(L"Installed app only",L"安装版才能更新",L"要インストール"),L"reel:check-update",MenuAction::CheckUpdate,-1,false);break;
        case UpdateStage::Checking:
            action(Tr(L"Checking…",L"正在检查…",L"確認中…"),L"reel:check-update",MenuAction::CheckUpdate);break;
        case UpdateStage::Current:
            action(Tr(L"Up to date",L"已是最新版本",L"最新版です"),L"reel:check-update",MenuAction::CheckUpdate);break;
        case UpdateStage::Available:
            action(Tr(L"Install ",L"安装 ",L"更新 ")+version,L"reel:install-update",MenuAction::InstallUpdate,-1,true,TileBadge::Dot);break;
        case UpdateStage::Installing:
            action(Tr(L"Installing ",L"正在安装 ",L"更新中 ")+version+L"…",L"reel:install-update",MenuAction::InstallUpdate);break;
        case UpdateStage::Failed:
            if(version.empty())action(Tr(L"Check failed",L"检查失败",L"確認失敗"),L"reel:check-failed",MenuAction::CheckUpdate);
            else action(Tr(L"Install failed",L"安装失败",L"更新失敗"),L"reel:install-update",MenuAction::InstallUpdate,-1,true,TileBadge::Dot);
            break;
        default:
            action(Tr(L"Check for updates",L"检查更新",L"更新を確認"),L"reel:check-update",MenuAction::CheckUpdate);break;
        }
        break;
    }
    case MenuPage::Entry:
        // The app itself comes first and carries its preferred badge; choosing it is done.
        // Its place in the reel changes by dragging it on the Manage page.
        if(editing>=0 && static_cast<size_t>(editing)<apps.size())result.push_back({apps[editing],MenuAction::Done,editing});
        action(Tr(L"Edit app",L"编辑应用",L"アプリを編集"),L"reel:edit",MenuAction::Edit,editing);
        action(Tr(L"Set as preferred",L"设为首选",L"既定に設定"),L"reel:preferred",MenuAction::SetPreferred,editing);
        if(apps.size()>1)action(Tr(L"Remove from reel",L"从轮盘移除",L"リールから外す"),L"reel:remove",MenuAction::Remove,editing);break;
    }
    return result;
}
}
