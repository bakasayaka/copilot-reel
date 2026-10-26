#include "core.h"
#include "radial_menu.h"
#include "local_launch.h"
#include <iostream>
#include <limits>
#include <cstdlib>

void Check(bool condition, const char* label) {
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
int main() {
    using namespace reel;
    Check(HitTest(0,0,6)==-1, "center cancels");
    Check(HitTest(44,0,6)==-1, "dead zone boundary cancels");
    for (unsigned count=1; count<=12; ++count) {
        for (unsigned i=0; i<count; ++i) {
            const double angle=-kPi/2 + i*2*kPi/count;
            Check(HitTest(112*std::cos(angle),112*std::sin(angle),count)==static_cast<int>(i), "sector centers match shared layout");
            Check(HitTest(10000*std::cos(angle),10000*std::sin(angle),count)==static_cast<int>(i), "outside ring preserves direction");
        }
    }
    Check(HitTest(10,100,0)==-1, "empty targets");
    Check(HitTest(10,100,13)==-1, "unsupported sector count rejected");
    Check(HitTest(std::numeric_limits<double>::quiet_NaN(),100,6)==-1, "invalid pointer rejected");
    Check(HitTest(100,std::numeric_limits<double>::infinity(),12)==-1, "infinite pointer rejected");
    for(unsigned count=2;count<=12;++count) {
        const double step=2*kPi/count;
        for(unsigned i=0;i<count;++i) {
            const double boundary=-kPi/2+(i+.5)*step;
            Check(HitTest(112*std::cos(boundary-1e-7),112*std::sin(boundary-1e-7),count)==static_cast<int>(i),"sector boundary preserves earlier side");
            Check(HitTest(112*std::cos(boundary+1e-7),112*std::sin(boundary+1e-7),count)==static_cast<int>((i+1)%count),"sector boundary advances to next side");
        }
    }
    for(const WorkArea work : {WorkArea{0,0,1920,1040},WorkArea{-2560,-200,0,1240}}) {
        for(int size : {384,576,768}) {
            const int xs[]={work.left,work.right-1,(work.left+work.right)/2};
            const int ys[]={work.top,work.bottom-1,(work.top+work.bottom)/2};
            for(int x:xs) for(int y:ys) {
                const auto placement=PlaceWheel(x,y,work,size);
                Check(placement.fits,"normal footprint fits");
                Check(placement.left>=work.left && placement.top>=work.top && placement.left+size<=work.right && placement.top+size<=work.bottom,"full circle fits monitor work area");
            }
        }
    }
    Check(!PlaceWheel(0,0,{0,0,300,300},384).fits,"undersized display never clips silently");
    const auto interior=PlaceWheel(600,400,{0,0,1920,1040},384);
    Check(interior.centerX==600 && interior.centerY==400,"interior stays pointer-centered");
    for(const WorkArea work : {WorkArea{0,0,1920,1040},WorkArea{-3840,-2160,0,0},WorkArea{1920,-1080,5760,2160}}) {
        for(int dpi : {96,120,144,168,192,288,384}) {
            const int size=384*dpi/96;
            const int xs[]={work.left,work.left+1,(work.left+work.right)/2,work.right-2,work.right-1};
            const int ys[]={work.top,work.top+1,(work.top+work.bottom)/2,work.bottom-2,work.bottom-1};
            for(int x:xs)for(int y:ys) {
                const auto placement=PlaceWheel(x,y,work,size);
                const bool shouldFit=work.right-work.left>=size && work.bottom-work.top>=size;
                Check(placement.fits==shouldFit,"orbit fit respects the complete physical footprint at every DPI");
                if(!shouldFit)continue;
                Check(placement.size==size && placement.left>=work.left && placement.top>=work.top &&
                    placement.left+size<=work.right && placement.top+size<=work.bottom,
                    "orbit and shadow stay inside all monitor edges and corners");
                Check(placement.centerX==placement.left+size/2.0 && placement.centerY==placement.top+size/2.0,
                    "orbit hit-test origin equals its rendered center");
                const int expectedLeft=std::clamp(x-size/2,work.left,work.right-size);
                const int expectedTop=std::clamp(y-size/2,work.top,work.bottom-size);
                Check(placement.left==expectedLeft && placement.top==expectedTop,
                    "placement uses only the minimum translation required by the work area");
                constexpr int offsetX=-5000,offsetY=777;
                const auto translated=PlaceWheel(x+offsetX,y+offsetY,
                    {work.left+offsetX,work.top+offsetY,work.right+offsetX,work.bottom+offsetY},size);
                Check(translated.left==placement.left+offsetX && translated.top==placement.top+offsetY,
                    "orbit placement is invariant under monitor-origin translation");
            }
        }
    }
    for(int dpi : {96,120,144,192,288,384}) {
        const int size=384*dpi/96;
        const auto placement=PlaceWheel(0,0,{-5000,-5000,5000,5000},size);
        Check(placement.centerX==0 && placement.centerY==0,
            "unconstrained wheel opens directly on the pointer at every DPI");
    }
    Check(!PlaceWheel(0,0,{0,0,1920,1080},0).fits,"orbit rejects an empty canvas");
    const auto corner=PlaceWheel(-3840,-2160,{-3840,-2160,0,0},768);
    Check(corner.left==-3840 && corner.top==-2160,"a corner shifts the full canvas minimally");
    const auto smallShift=PlaceWheel(180,400,{0,0,1920,1080},384);
    Check(smallShift.centerX==192 && smallShift.centerY==400,"a small boundary correction moves the canvas only that far");
    const auto oddCanvas=PlaceWheel(600,400,{0,0,1920,1080},383);
    Check(std::abs(oddCanvas.centerX-600)<=.5 && std::abs(oddCanvas.centerY-400)<=.5,
        "odd pixel dimensions remain pointer-centered within half a pixel");
    GestureState g;
    Check(!g.Release(), "orphan stop cannot launch");
    Check(g.Start(), "start arms");
    Check(!g.Start(), "duplicate start is idempotent");
    Check(!g.Release(), "center release cancels");
    g.Start(); g.Select(2,6);
    Check(g.Release()==2, "valid release commits");
    Check(!g.Release(), "duplicate stop never commits twice");
    g.Start(); const auto old=g.Generation(); g.Select(2,6); g.Cancel();
    Check(g.Generation()!=old && !g.Active() && !g.Release(), "cancel invalidates pending work");
    g.Start(); g.Select(7,6);
    Check(!g.Release(), "stale config index cannot launch");
    // A pointer the edge clamp leaves outside the plate (192 DIP right of the center at the
    // reel's 168 DIP plate radius) selects by direction without entering the center first.
    g.Start();
    Check(g.ObservePointer(192,0,6) && g.Selected()==HitTest(192,0,6) && g.Release()==HitTest(192,0,6),
        "a pointer off the plate selects by direction at once");
    g.Start();
    Check(g.ObservePointer(0,-112,6) && g.Release()==0,"selection uses the visual center");
    g.Start();g.ObservePointer(112,0,6);g.ObservePointer(0,0,6);
    Check(!g.Release(),"returning to center cancels");
    HoldOrdering ordering;
    Check(ordering.Start(100),"first Down is accepted");
    Check(!ordering.Start(100) && !ordering.Start(99),"duplicate and stale Down are rejected");
    Check(!ordering.Stop(99),"older Up cannot close a newer hold");
    Check(ordering.Stop(101),"matching later Up closes the hold");
    Check(!ordering.Start(100) && !ordering.Start(101),"Up tombstone rejects an equal or earlier Down");
    Check(ordering.Start(102) && !ordering.Stop(101) && ordering.Stop(103),"next hold is independent of stale prior Up");
    HoldOrdering cold;
    Check(cold.Stop(200),"orphan cold Up records a tombstone");
    Check(!cold.Start(199) && !cold.Start(200),"cold Up arriving first suppresses a delayed older Down");
    Check(cold.Start(201) && cold.Stop(202),"later deliberate hold remains usable after a cold tombstone");
    HoldOrdering once;
    GestureState onceGesture;
    int launches=0;
    if(once.Start(500))onceGesture.Start();
    onceGesture.Select(11,12);
    if(once.Stop(501) && onceGesture.Release())++launches;
    if(once.Stop(501) && onceGesture.Release())++launches;
    Check(launches==1,"duplicate release packets cannot launch an agent twice");

    SetLanguage(Language::English);
    std::vector<AppTarget> presets;
    for(int i=0;i<13;++i)presets.push_back({L"Preset "+std::to_wstring(i),TargetKind::Shell,L"",L"",L""});
    for(size_t count=1;count<=8;++count) {
        std::vector<AppTarget> apps;
        for(size_t i=0;i<count;++i)apps.push_back({L"App "+std::to_wstring(i),TargetKind::Shell,
            L"C:\\Apps\\App"+std::to_wstring(i)+L".exe",L"",L""});
        const auto main=BuildMenu(MenuPage::Main,apps,presets);
        Check(main.size()==count+1 && main.size()<=9,"main wheel has exactly one combined plus entry");
        for(size_t i=0;i<count;++i) {
            Check(main[i].action==MenuAction::Launch && main[i].index==static_cast<int>(i) &&
                main[i].visual.target==apps[i].target,"launch sectors preserve target identity and index");
        }
        Check(main[count].action==MenuAction::Settings && main[count].visual.target==L"reel:add",
            "the single plus icon opens the combined hub without launching an app");
        const auto hub=BuildMenu(MenuPage::Settings,apps,presets);
        Check(hub.size()==7u && hub[5].action==MenuAction::Presets,"hub keeps its Add slot at every app count, so it never reflows");
        Check(hub[5].enabled==(count<kMaxApps),"hub Add is enabled only while another app can be saved");
        for(size_t i : {0u,1u,2u,3u,4u,6u})Check(hub[i].enabled,"hub commands other than Add are always enabled");
        const auto manage=BuildMenu(MenuPage::Manage,apps,presets);
        Check(manage.size()==count+1,"manage wheel keeps its Add slot at the saved-app limit");
        for(size_t i=0;i<count;++i)
            Check(manage[i].action==MenuAction::Entry && manage[i].index==static_cast<int>(i) && manage[i].enabled,
                "manage opens the selected entry submenu");
        Check(manage.back().action==MenuAction::Presets && manage.back().enabled==(count<kMaxApps),
            "Add opens the preset wheel and is disabled at the limit");
        for(const auto& entry:main)Check(entry.enabled,"main wheel entries are always enabled");
        for(size_t i=0;i<count;++i) {
            const auto entry=BuildMenu(MenuPage::Entry,apps,presets,0,static_cast<int>(i));
            Check(entry.size()==(count>1?4u:3u) && entry[0].action==MenuAction::Done &&
                entry[1].action==MenuAction::Edit && entry[2].action==MenuAction::SetPreferred,
                "entry wheel shows the app itself, then edit and preferred actions");
            Check(entry[0].visual.target==apps[i].target && entry[0].visual.name==apps[i].name &&
                entry[0].visual.kind==apps[i].kind,"the entry's first slot is the app with its own identity");
            for(const auto& command:entry)Check(command.index==static_cast<int>(i) && command.action!=MenuAction::Launch,
                "entry submenu preserves its app index and cannot launch");
            if(count>1)Check(entry[3].action==MenuAction::Remove,"remove is available only while another app remains");
            Check(!AdjustFor(MenuPage::Entry).control,"an app's actions page has no reorder chevrons");
        }
        // Any app moves to any place; every other app keeps its relative order and all its fields.
        for(int from=0;from<static_cast<int>(count);++from)for(int to=0;to<static_cast<int>(count);++to) {
            auto moved=apps;
            const bool ok=MoveEntry(moved,from,to);
            Check(ok==(from!=to),"a move needs a different place inside the reel");
            Check(moved.size()==count && moved[to].target==apps[from].target,"the moved app lands at its new place");
            for(int position=0;position<static_cast<int>(count);++position)
                Check(moved[MovedIndex(position,from,to)].target==apps[position].target &&
                    moved[MovedIndex(position,from,to)].name==apps[position].name,"MovedIndex follows every entry");
            MoveEntry(moved,to,from);
            for(size_t k=0;k<count;++k)Check(moved[k].target==apps[k].target,"moving back restores the order");
        }
        Check(!MoveEntry(apps,-1,0) && !MoveEntry(apps,0,static_cast<int>(count)) && !MoveEntry(apps,static_cast<int>(count),0),
            "only existing entries move, and only inside the reel");
        // A drag lands in the app slot nearest the pointer's angle; Add's slot is never a target.
        const size_t slots=count+1;
        for(size_t slot=0;slot<slots;++slot) {
            const double angle=-kPi/2+slot*2*kPi/slots;
            const int landed=DropSlot(112*std::cos(angle),112*std::sin(angle),slots,count);
            if(slot<count)Check(landed==static_cast<int>(slot),"a drag over an app slot lands there");
            else Check(landed==0 || landed==static_cast<int>(count)-1,"a drag over Add lands at the nearer end of the reel");
            const double nearLast=angle-kPi/slots*.5,nearFirst=angle+kPi/slots*.5;
            if(slot==count) {
                Check(DropSlot(std::cos(nearLast),std::sin(nearLast),slots,count)==static_cast<int>(count)-1,
                    "the earlier half of Add lands at the end");
                Check(DropSlot(std::cos(nearFirst),std::sin(nearFirst),slots,count)==0,
                    "the later half of Add lands at the start");
            }
        }
    }
    Check(DropSlot(1,1,0,0)==-1 && DropSlot(1,1,2,3)==-1 && DropSlot(std::numeric_limits<double>::quiet_NaN(),1,4,3)==-1,
        "a drop needs apps, room for them and a real pointer");
    const std::vector<AppTarget> noApps;
    const auto settings=BuildMenu(MenuPage::Settings,noApps,presets);
    Check(settings.size()==7 && settings[0].action==MenuAction::Language &&
        settings[1].action==MenuAction::Appearance && settings[2].action==MenuAction::Size && settings[3].action==MenuAction::TrayIcon &&
        settings[4].action==MenuAction::Manage && settings[5].action==MenuAction::Presets && settings[6].action==MenuAction::About,
        "the combined hub contains language, appearance, reel size, the tray icon, management, Add and About; the preferred agent is set from an app's actions");
    // The Tray icon tile shows the saved choice in its icon and keeps its name.
    Check(settings[3].visual.target==L"reel:tray-icon:color","a color tray icon shows the reel's color mark");
    SetTrayMonochrome(true);
    const auto monochromeHub=BuildMenu(MenuPage::Settings,noApps,presets);
    SetTrayMonochrome(false);
    Check(monochromeHub[3].visual.target==L"reel:tray-icon:monochrome" && monochromeHub[3].visual.name==settings[3].visual.name &&
        monochromeHub[3].action==MenuAction::TrayIcon,"a monochrome tray icon shows the mark alone under the same name");
    for(const auto& command:settings)Check(command.visual.target.starts_with(L"reel:") && command.action!=MenuAction::Launch,
        "settings metadata cannot be mistaken for an app-launch action");
    Check(settings[6].badge==TileBadge::None,"About carries no dot while no update waits");

    // About: the reel's mark named by the version, GitHub named by the author, the two switches with their
    // state in the icon, and one tile that checks for an update or installs the one found.
    {
        AboutState about{L"0.3.8",true,false,UpdateStage::Idle,L""};
        auto page=BuildMenu(MenuPage::About,noApps,presets,0,-1,0,about);
        Check(page.size()==5 && page[0].action==MenuAction::AboutInfo && page[1].action==MenuAction::GitHub &&
            page[2].action==MenuAction::AutoCheck && page[3].action==MenuAction::AutoInstall && page[4].action==MenuAction::CheckUpdate,
            "About lists the version, GitHub, both switches and the update tile in order");
        Check(page[0].visual.name==L"0.3.8" && page[0].visual.target==L"reel:author" &&
            page[1].visual.name==L"baka_sayaka" && page[1].visual.target==L"reel:github",
            "the reel's mark is named by the version and GitHub's by the author");
        Check(page[2].visual.target==L"reel:auto-check:on" && page[3].visual.target==L"reel:auto-install:off" &&
            page[2].badge==TileBadge::None && page[3].badge==TileBadge::None,"a switch shows its state in its icon, without a badge");
        for(const auto& entry:page)Check(entry.enabled && entry.visual.target.starts_with(L"reel:"),"every About tile is internal");
        about.stage=UpdateStage::Available;about.offered=L"0.3.9";
        page=BuildMenu(MenuPage::About,noApps,presets,0,-1,0,about);
        Check(page[4].action==MenuAction::InstallUpdate && page[4].badge==TileBadge::Dot && page[4].visual.name==L"Install 0.3.9",
            "a found update turns the last tile into its install, with the dot");
        about.autoCheck=false;about.autoInstall=true;
        page=BuildMenu(MenuPage::About,noApps,presets,0,-1,0,about);
        Check(page[2].visual.target==L"reel:auto-check:off" && page[3].visual.target==L"reel:auto-install:on","the switches follow their state");
        Check(BuildMenu(MenuPage::Settings,noApps,presets,0,-1,0,about)[6].badge==TileBadge::Dot,"the hub's About tile carries the dot");
        about.stage=UpdateStage::Failed;
        Check(UpdateWaiting(about) && BuildMenu(MenuPage::About,noApps,presets,0,-1,0,about)[4].action==MenuAction::InstallUpdate,
            "a failed install keeps the update waiting");
        about.offered.clear();
        page=BuildMenu(MenuPage::About,noApps,presets,0,-1,0,about);
        Check(!UpdateWaiting(about) && page[4].action==MenuAction::CheckUpdate && page[4].badge==TileBadge::None &&
            page[4].visual.target==L"reel:check-failed","a failed check shows it and offers to check again");
        about.stage=UpdateStage::Installing;about.offered=L"0.3.9";
        Check(!UpdateWaiting(about),"an install in progress waits for nothing");
        about.stage=UpdateStage::Unavailable;about.offered.clear();
        page=BuildMenu(MenuPage::About,noApps,presets,0,-1,0,about);
        Check(page[0].enabled && page[1].enabled && !page[2].enabled && !page[3].enabled && !page[4].enabled,
            "a development build shows About but cannot update");
    }
    for(size_t offset : {0u,5u,10u}) {
        const auto page=BuildMenu(MenuPage::Presets,noApps,presets,offset);
        const size_t shown=std::min(size_t{5},presets.size()-offset);
        Check(page.size()==shown+1+(offset>0?1:0)+(offset+5<presets.size()?1:0) && page.size()<=8,
            "preset page budgets room for custom and navigation sectors");
        for(size_t i=0;i<shown;++i)Check(page[i].action==MenuAction::Preset &&
            page[i].index==static_cast<int>(offset+i) && page[i].visual.target.empty(),
            "preset page preserves absolute indices and missing desktop targets");
        Check(page[shown].action==MenuAction::Custom,"custom executable selection is always available");
        size_t navigation=shown+1;
        if(offset>0)Check(page[navigation++].action==MenuAction::Previous,"later preset pages provide a previous route");
        if(offset+5<presets.size())Check(page[navigation].action==MenuAction::Next,"nonfinal preset pages provide a next route");
    }
    Check(BuildMenu(MenuPage::Presets,noApps,noApps).size()==1,"empty preset catalog still permits a custom app");

    const auto hubAdjust=AdjustFor(MenuPage::Settings);
    Check(hubAdjust.control && hubAdjust.earlier && hubAdjust.later,"the hub language tile always has both chevrons");
    for(auto page:{MenuPage::Main,MenuPage::Manage,MenuPage::Presets,MenuPage::Entry})
        Check(!AdjustFor(page).control,"only the hub has an adjustable slot");
    Check(static_cast<int>(MenuPage::Entry)==5,"logged page numbers keep their meaning");

    // Identities: AUMIDs compare case-insensitively, shortcuts resolve to their executable.
    const auto identity=[](const AppTarget& app)->std::wstring {
        auto key=LaunchIdentity(app.target);
        if(key.empty() || key.starts_with(L"broken"))return {};
        if(key==L"c:\\links\\cursor a.lnk" || key==L"c:\\links\\cursor b.lnk")return L"file:c:\\apps\\cursor.exe";
        if(key==L"c:\\links\\science.lnk")return L"file:c:\\apps\\science.exe";
        return (app.kind==TargetKind::Aumid?L"aumid:":L"file:")+key;
    };
    const std::vector<AppTarget> saved={{L"Work",TargetKind::Aumid,L"OpenAI.Codex_2p2nqsd0c76g0!APP",L"",L""},
        {L"Science",TargetKind::Shell,L"C:\\Apps\\Science.exe",L"",L""}};
    const std::vector<AppTarget> catalog={{L"ChatGPT / Codex",TargetKind::Aumid,L"OpenAI.Codex_2p2nqsd0c76g0!App",L"",L""},
        {L"Claude Science",TargetKind::Shell,L"C:\\Links\\Science.lnk",L"",L""},
        {L"Antigravity",TargetKind::Shell,L"",L"",L""},
        {L"ZCode",TargetKind::Shell,L"C:\\Apps\\ZCode.exe",L"",L""},
        {L"Cursor A",TargetKind::Shell,L"C:\\Links\\Cursor A.lnk",L"",L""},
        {L"Cursor B",TargetKind::Shell,L"C:\\Links\\Cursor B.lnk",L"",L""},
        {L"JetBrains IDE",TargetKind::Shell,L"",L"",L""},
        {L"Broken",TargetKind::Shell,L"broken.lnk",L"",L""}};
    size_t installed=0,restorable=0;
    const auto add=AddCatalog(saved,{},catalog,identity,&restorable,&installed);
    std::vector<std::wstring> addNames;for(const auto& item:add)addNames.push_back(item.name);
    Check(addNames==std::vector<std::wstring>{L"ZCode",L"Cursor A",L"Antigravity",L"JetBrains IDE",L"Broken"} && installed==2 && restorable==0,
        "Add lists installed agents not in the reel first, once each, then entries that still need a target");
    Check(saved.size()==2 && saved[0].target==L"OpenAI.Codex_2p2nqsd0c76g0!APP","the saved reel is not changed by the Add catalog");
    const auto addPage=BuildMenu(MenuPage::Presets,saved,add);
    Check(addPage.size()==6 && addPage[0].index==0 && addPage[5].action==MenuAction::Custom,"the Add page indexes the ordered catalog");

    // Apps taken out of the reel come first on Add with their own settings, most recent first.
    const AppTarget myZCode{L"My ZCode",TargetKind::Shell,L"C:\\Apps\\ZCode.exe",L"--profile work",L"D:\\Work"};
    const AppTarget site{L"Docs",TargetKind::Shell,L"https://example.com/docs",L"",L""};
    const AppTarget science{L"Science",TargetKind::Shell,L"C:\\Apps\\Science.exe",L"",L""};
    std::vector<AppTarget> parked;
    Park(parked,site);Park(parked,myZCode);Park(parked,science);Park(parked,myZCode);
    Check(parked.size()==3 && SameApp(parked[0],myZCode) && SameApp(parked[1],science) && SameApp(parked[2],site),
        "parking an app again moves it to the front once");
    const auto withParked=AddCatalog(saved,parked,catalog,identity,&restorable,&installed);
    std::vector<std::wstring> withNames;for(const auto& item:withParked)withNames.push_back(item.name);
    Check(withNames==std::vector<std::wstring>{L"My ZCode",L"Docs",L"Cursor A",L"Antigravity",L"JetBrains IDE",L"Broken"} &&
        restorable==2 && installed==1,
        "parked apps lead, the one still in the reel is hidden, and the catalog agent they offer is listed once");
    Check(withParked[0].arguments==L"--profile work" && withParked[0].workingDirectory==L"D:\\Work",
        "a parked app keeps every field");
    const AppTarget scienceLab{L"Science lab",TargetKind::Shell,L"C:\\Apps\\Science.exe",L"--lab",L""};
    size_t profileCount=0;
    const auto profiles=AddCatalog(saved,{scienceLab},catalog,identity,&profileCount);
    Check(!profiles.empty() && SameApp(profiles[0],scienceLab) && profileCount==1,
        "a parked profile of an app still in the reel with other arguments is still offered");
    const auto restorePage=BuildMenu(MenuPage::Presets,saved,withParked,0,-1,restorable);
    Check(restorePage[0].action==MenuAction::Restore && restorePage[1].action==MenuAction::Restore &&
        restorePage[2].action==MenuAction::Preset,"parked apps are put back as saved; catalog entries stay presets");
    const auto laterPage=BuildMenu(MenuPage::Presets,saved,withParked,5,-1,restorable);
    Check(laterPage[0].action==MenuAction::Preset && laterPage[0].index==5,"paging keeps the parked boundary absolute");
    std::vector<AppTarget> many;
    for(int i=0;i<20;++i)Park(many,{L"App",TargetKind::Shell,L"C:\\A"+std::to_wstring(i)+L".exe",L"",L""});
    Check(many.size()==kMaxParked && many.front().target==L"C:\\A19.exe" && many.back().target==L"C:\\A4.exe",
        "the parked list keeps the most recent apps");

    const auto keyHub=[](UINT key,int selected,bool shift=false){return RouteKey(key,shift,true,selected);};
    for(int selected:{-1,0,3})Check(keyHub('A',selected)==KeyRoute::Adjust && keyHub('D',selected)==KeyRoute::Adjust,
        "A and D always adjust an adjustable slot 0");
    Check(keyHub(VK_LEFT,0)==KeyRoute::Adjust && keyHub(VK_RIGHT,0)==KeyRoute::Adjust,"arrows adjust the selected slot 0");
    Check(keyHub(VK_LEFT,2)==KeyRoute::Previous && keyHub(VK_RIGHT,-1)==KeyRoute::Next,"arrows move around the ring elsewhere");
    for(bool adjustable:{false,true}) {
        Check(RouteKey(VK_ESCAPE,false,adjustable,1)==KeyRoute::Back && RouteKey(VK_BACK,false,adjustable,-1)==KeyRoute::Back,
            "Esc and Backspace go back on every page");
        Check(RouteKey(VK_RETURN,false,adjustable,1)==KeyRoute::Confirm && RouteKey(VK_SPACE,false,adjustable,-1)==KeyRoute::Confirm,
            "Return and Space confirm on every page");
        Check(RouteKey(VK_TAB,false,adjustable,0)==KeyRoute::Next && RouteKey(VK_TAB,true,adjustable,0)==KeyRoute::Previous,
            "Tab and Shift+Tab move around the ring");
    }
    Check(RouteKey('A',false,false,0)==KeyRoute::None && RouteKey(VK_LEFT,false,false,0)==KeyRoute::Previous,
        "pages without an adjustable slot never adjust");
    Check(RouteKey('A',false,false,2,true)==KeyRoute::Adjust && RouteKey('D',false,false,-1,true)==KeyRoute::Adjust &&
        RouteKey(VK_DELETE,false,false,2,true)==KeyRoute::Remove,"on Manage A and D move the selected app and Delete takes it out");
    Check(RouteKey(VK_LEFT,false,false,0,true)==KeyRoute::Previous && RouteKey(VK_RIGHT,false,false,0,true)==KeyRoute::Next,
        "on Manage the arrows still move around the ring");
    Check(RouteKey(VK_DELETE,false,true,0)==KeyRoute::None && RouteKey(VK_DELETE,false,false,0)==KeyRoute::None,
        "Delete does nothing outside Manage");
    Check(AdjustDirection('A')==-1 && AdjustDirection(VK_LEFT)==-1 && AdjustDirection('D')==1 && AdjustDirection(VK_RIGHT)==1,
        "A and Left move earlier, D and Right move later");
    std::vector<AppTarget> full;
    for(int i=0;i<8;++i)full.push_back({L"App",TargetKind::Shell,L"C:\\A"+std::to_wstring(i)+L".exe",L"",L""});
    const auto fullHub=BuildMenu(MenuPage::Settings,full,presets),fullManage=BuildMenu(MenuPage::Manage,full,presets);
    Check(!CanConfirm(fullHub,5) && !CanConfirm(fullManage,8) && CanConfirm(fullHub,0) && CanConfirm(fullHub,6) && CanConfirm(fullManage,7),
        "a disabled Add cannot be confirmed; enabled items can");
    Check(!CanConfirm(fullHub,-1) && !CanConfirm(fullHub,7),"nothing selected or a stale index confirms nothing");

    // Copilot events in every reel state.
    const ReelContext closed{},open{true,false},blocked{false,true},openBlocked{true,true};
    {
        ReelInput input;
        Check(input.Tap(10,closed)==CopilotRoute::LaunchPreferred,"a tap with the reel closed opens the preferred agent");
        Check(input.HoldStop(20,closed)==CopilotRoute::Ignore,"an orphan release does nothing");
        Check(input.HoldStart(19,closed)==CopilotRoute::Ignore,"its tombstone rejects an older delayed hold");
        Check(input.HoldStart(100,closed)==CopilotRoute::Open,"a hold opens the reel");
        input.Opened(false,100);
        Check(input.Held() && !input.Persistent(),"the opening hold is held, not persistent");
        Check(input.HoldStart(100,open)==CopilotRoute::Ignore && input.HoldStart(150,open)==CopilotRoute::Ignore,
            "a duplicate hold never resets the opening hold");
        Check(input.Tap(150,open)==CopilotRoute::Ignore,"a tap during a hold is ignored");
        Check(input.HoldStop(101,open)==CopilotRoute::Release,"the release of the opening hold resolves it");
        Check(input.HoldStop(101,open)==CopilotRoute::Ignore,"a duplicate release does nothing");
        input.PageShown(102);
        Check(input.Persistent() && !input.Held(),"drilling in leaves a persistent menu");
        Check(input.HoldStop(101,open)==CopilotRoute::Ignore,"a late release never also chooses a child");
        Check(input.Tap(103,open)==CopilotRoute::Confirm,"a tap in a persistent menu confirms");
        Check(input.Tap(102,open)==CopilotRoute::Ignore && input.Tap(90,open)==CopilotRoute::Ignore,
            "a tap older than the page is stale");
        Check(input.HoldStart(104,open)==CopilotRoute::Resume && input.Held() && input.Persistent(),
            "a new hold in a persistent menu resumes direction selection");
        Check(input.Tap(104,open)==CopilotRoute::Ignore && input.HoldStart(104,open)==CopilotRoute::Ignore,
            "a tap or duplicate hold during the resumed hold is ignored");
        Check(input.HoldStop(105,open)==CopilotRoute::Confirm,"the resumed hold's release confirms");
        Check(input.HoldStop(105,open)==CopilotRoute::Ignore && input.HoldStart(104,open)==CopilotRoute::Ignore,
            "duplicate and delayed packets of that hold do nothing");
        Check(input.HoldStart(106,open)==CopilotRoute::Resume,"the next hold resumes again");
        input.EndHold();
        Check(input.HoldStop(107,open)==CopilotRoute::Ignore,"a click, key or adjustment consumes the resumed hold");
        Check(input.HoldStart(108,open)==CopilotRoute::Resume,"and the next hold resumes");
        input.PageShown(109);
        Check(input.HoldStop(110,open)==CopilotRoute::Ignore,"a page change consumes the resumed hold");
        Check(input.Tap(111,openBlocked)==CopilotRoute::Ignore && input.HoldStart(112,openBlocked)==CopilotRoute::Ignore &&
            input.HoldStop(113,openBlocked)==CopilotRoute::Ignore,"nothing reaches an open reel behind an editor or picker");
        input.Closed();
        Check(input.Tap(120,blocked)==CopilotRoute::Ignore && input.HoldStart(121,blocked)==CopilotRoute::Ignore,
            "a dialog blocks the preferred tap and the opening hold");
        Check(input.Tap(122,closed)==CopilotRoute::LaunchPreferred && input.HoldStart(123,closed)==CopilotRoute::Open,
            "after closing, tap and hold have their closed meaning again");
        Check(input.HoldStop(124,closed)==CopilotRoute::Ignore,"an opening that was never placed cannot be released");
    }
    {
        ReelInput preview;
        preview.Opened(true,200);
        Check(preview.HoldStop(201,open)==CopilotRoute::Ignore,"a preview has no hold to release");
        Check(preview.Tap(201,open)==CopilotRoute::Confirm,"a tap confirms in a preview");
        Check(preview.Tap(200,open)==CopilotRoute::Ignore,"a tap from before the preview is stale");
    }

    const auto shell=[](const wchar_t* target,const wchar_t* name=L"Saved display name",
                        const wchar_t* arguments=L"",const wchar_t* directory=L"") {
        return AppTarget{name,TargetKind::Shell,target,arguments,directory};
    };
    Check(IsLocalApplication(shell(L"notepad.exe")),"local executable alias is eligible for short tap");
    Check(IsLocalApplication(shell(L"C:\\Apps\\Agent.EXE")),"absolute executable path is eligible case-insensitively");
    Check(IsLocalApplication(shell(L"C:\\Apps\\Agent.lnk")),"local application shortcut is eligible for short tap");
    Check(IsLocalApplication(shell(L"ms-settings:colors")),"Windows Settings remains a local application route");
    Check(IsLocalApplication(shell(L"shell:AppsFolder\\Vendor.Agent_family!App")),"packaged AppsFolder activation remains local");
    Check(IsLocalApplication({L"Native agent",TargetKind::Aumid,L"Vendor.Agent_family!App",L"",L""}),
        "packaged app activation is eligible for short tap");
    Check(!IsLocalApplication({L"Invalid package",TargetKind::Aumid,L"",L"",L""}) &&
        !IsLocalApplication({L"Invalid package",TargetKind::Aumid,L"Vendor.Agent_family",L"",L""}),
        "empty and incomplete packaged application identities are rejected");
    Check(!IsLocalApplication({L"Invalid package",TargetKind::Aumid,L"https://example.invalid/Agent!App",L"",L""}),
        "a URL cannot masquerade as a packaged application identity");
    for(const auto target:{L"https://chatgpt.com",L"https://example.invalid/agent.exe",L"http://localhost:3000",
                           L"https://example.invalid/agent.lnk",L"agent://open",L"agent:fake.exe",L"notepad.exe?query=1",L"notepad.exe\n"})
        Check(!IsLocalApplication(shell(target)),"website, protocol, and malformed targets cannot be dispatched by short tap");
    const std::vector<AppTarget> nativePresets={
        {L"ChatGPT / Codex",TargetKind::Aumid,L"OpenAI.Codex_family!App",L"",L""},
        {L"Claude Desktop",TargetKind::Shell,L"C:\\Apps\\Claude.lnk",L"",L""}
    };
    for(const auto url:{L"https://chatgpt.com",L"https://chatgpt.com/",L"HTTPS://CHAT.OPENAI.COM/"}) {
        const auto replacement=LocalReplacement(shell(url,L"My renamed ChatGPT"),nativePresets);
        Check(replacement && replacement->name==L"My renamed ChatGPT" && replacement->kind==TargetKind::Aumid &&
            replacement->target==nativePresets[0].target,"exact legacy OpenAI websites migrate to installed identity while preserving their display name");
    }
    const auto claudeReplacement=LocalReplacement(shell(L"https://claude.ai/",L"Research assistant"),nativePresets);
    Check(claudeReplacement && claudeReplacement->name==L"Research assistant" &&
        claudeReplacement->target==nativePresets[1].target,"exact legacy Claude website migrates to the discovered shortcut");
    for(const auto target:{L"https://chatgpt.com/chat/123",L"https://chatgpt.com?mode=1",L"https://chatgpt.com.evil.invalid",
                           L"http://chatgpt.com",L"chatgpt://open",L"C:\\Users\\Someone\\chatgpt.url",L"https://example.invalid"})
        Check(!LocalReplacement(shell(target),nativePresets),"unrelated URLs, paths, and protocols are never silently migrated");
    Check(!LocalReplacement(shell(L"https://chatgpt.com",L"Saved",L"--profile=test"),nativePresets),
        "legacy targets with arguments are not silently repurposed");
    Check(!LocalReplacement(shell(L"https://claude.ai",L"Saved",L"",L"C:\\Work"),nativePresets),
        "legacy targets with a working directory are not silently repurposed");
    Check(!LocalReplacement(shell(L"https://chatgpt.com"),{}),"a missing installed application never becomes a guessed target");
    const std::vector<AppTarget> unresolvedPresets={{L"ChatGPT / Codex",TargetKind::Shell,L"",L"",L""},
        {L"Claude Desktop",TargetKind::Shell,L"https://claude.ai",L"",L""}};
    Check(!LocalReplacement(shell(L"https://chatgpt.com"),unresolvedPresets) &&
        !LocalReplacement(shell(L"https://claude.ai"),unresolvedPresets),"unresolved or website presets cannot substitute for a native installation");
    const auto localProfile=shell(L"C:\\Apps\\Agent.exe",L"Existing native agent",L"--profile=test",L"C:\\Work");
    const auto unchanged=LocalReplacement(localProfile,nativePresets);
    Check(unchanged && unchanged->name==localProfile.name && unchanged->target==localProfile.target &&
        unchanged->arguments==localProfile.arguments && unchanged->workingDirectory==localProfile.workingDirectory,
        "existing local launch profiles remain unchanged");

    SetLanguage(Language::SimplifiedChinese);
    const auto translatedSettings=BuildMenu(MenuPage::Settings,noApps,presets);
    Check(translatedSettings[0].visual.name!=settings[0].visual.name &&
        translatedSettings[0].action==settings[0].action && translatedSettings[0].visual.target==settings[0].visual.target,
        "language changes labels without changing action identity");
    SetLanguage(Language::Japanese);
    const auto japaneseSettings=BuildMenu(MenuPage::Settings,noApps,presets);
    Check(japaneseSettings[0].visual.name!=settings[0].visual.name && japaneseSettings[0].visual.name!=translatedSettings[0].visual.name &&
        japaneseSettings[0].action==settings[0].action && japaneseSettings[0].visual.target==settings[0].visual.target,
        "Japanese labels keep the same action identity");
    SetLanguage(Language::English);
    Check(ParseUri(L"copilot-reel://?state=Down")==Command::HoldStart, "protocol down");
    Check(ParseUri(L"copilot-reel://?state=Up")==Command::HoldStop, "protocol up");
    Check(ParseUri(L"copilot-reel://?state=Tap")==Command::Tap, "protocol tap");
    Check(ParseUri(L"copilot-reel://?state=Up&target=evil.exe")==Command::Invalid, "URI cannot inject target");
    Check(ParseUri(L"copilot-reel://?state=Tap&inputTimestamp=123456789")==Command::Tap &&
          ParseUri(L"copilot-reel://?state=Down&inputTimestamp=1")==Command::HoldStart &&
          ParseUri(L"copilot-reel://?state=Up&inputTimestamp=42")==Command::HoldStop, "the Shell's input timestamp is accepted");
    for(const auto* uri:{L"copilot-reel://?state=Tap&inputTimestamp=",L"copilot-reel://?state=Tap&inputTimestamp=12x",
                         L"copilot-reel://?state=Tap&inputTimestamp=1&target=evil.exe",L"copilot-reel://?state=Tap&",
                         L"copilot-reel://?state=Tapx&inputTimestamp=1",L"copilot-reel://?state=Tap&inputTimestamp=123456789012345678901"})
        Check(ParseUri(uri)==Command::Invalid, "anything beyond a numeric input timestamp is rejected");
    Check(ParseUri(L"https://example.com/?state=Down")==Command::Invalid, "foreign URI rejected");
    std::cout << "Core tests passed: 1-12 sectors, pointer-centered placement at 96-384 DPI and negative monitor origins, cold gesture ordering, exactly-once release, radial routes, preset pagination, protocol validation, and native short-tap policy.\n";
}
