#include <unknwn.h>
#include "artwork.h"
#include "iconography.h"
#include "official_icons.h"
#include "surface_paint.h"
#include "vi_tokens.h"
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
unsigned checks=0;
void Check(bool value,const char* description) {
    ++checks;if(!value){std::cerr<<"FAIL: "<<description<<'\n';std::exit(1);}
}
reel::AppTarget Target(const wchar_t* target,const wchar_t* name=L"Renamed entry",
                       reel::TargetKind kind=reel::TargetKind::Shell) {
    return {name,kind,target,L"",L""};
}

struct Raster { uint64_t hash=0; unsigned visible=0,outsideTile=0,inCorners=0; float centerX=0,centerY=0; };

// Headless WARP device context, so the production DrawArtwork path (per-DPI prescale,
// tile clip, Segoe Fluent Icons) is exercised without a window.
class Canvas {
public:
    Canvas() {
        winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,d3d.put(),nullptr,nullptr));
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),nullptr,factory.put_void()));
        winrt::check_hresult(factory->CreateDevice(d3d.as<IDXGIDevice>().get(),device.put()));
    }
    // Draws one item into a 64 DIP square with its 40 DIP tile at (12,12)-(52,52).
    Raster Draw(const reel::AppTarget& app,float dpi,bool dark=true) {
        winrt::com_ptr<ID2D1DeviceContext> context;
        winrt::check_hresult(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,context.put()));
        const UINT edge=static_cast<UINT>(64*dpi/96);
        const auto format=D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED);
        winrt::com_ptr<ID2D1Bitmap1> target,readable;
        winrt::check_hresult(context->CreateBitmap(D2D1::SizeU(edge,edge),nullptr,0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format,dpi,dpi),target.put()));
        winrt::check_hresult(context->CreateBitmap(D2D1::SizeU(edge,edge),nullptr,0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format,dpi,dpi),readable.put()));
        context->SetTarget(target.get());context->SetDpi(dpi,dpi);
        context->BeginDraw();context->Clear(D2D1::ColorF(0,0.0f));
        Check(reel::DrawArtwork(context.get(),app,D2D1::RectF(12,12,52,52),
            reel::ArtworkStyle{dpi,dark,false,D2D1::ColorF(1,1,1,1)}),"artwork draws for every non-language item");
        winrt::check_hresult(context->EndDraw());
        winrt::check_hresult(readable->CopyFromBitmap(nullptr,target.get(),nullptr));
        D2D1_MAPPED_RECT pixels{};winrt::check_hresult(readable->Map(D2D1_MAP_OPTIONS_READ,&pixels));
        Raster result;result.hash=14695981039346656037ull;
        const float scale=dpi/96,tileLeft=12*scale,tileRight=52*scale,radius=reel::vi::kTileRadius*scale;
        double sumX=0,sumY=0;
        for(UINT y=0;y<edge;++y)for(UINT x=0;x<edge;++x) {
            const BYTE* pixel=pixels.bits+y*pixels.pitch+x*4;
            for(unsigned channel=0;channel<4;++channel){result.hash^=pixel[channel];result.hash*=1099511628211ull;}
            if(pixel[3]<8)continue;
            ++result.visible;sumX+=x+.5;sumY+=y+.5;
            const float px=x+.5f,py=y+.5f;
            if(px<tileLeft || px>tileRight || py<tileLeft || py>tileRight)++result.outsideTile;
            // Beyond the r10 corner arc of the tile shape.
            const float cx=std::clamp(px,tileLeft+radius,tileRight-radius),cy=std::clamp(py,tileLeft+radius,tileRight-radius);
            if((px-cx)*(px-cx)+(py-cy)*(py-cy)>(radius+1)*(radius+1))++result.inCorners;
        }
        winrt::check_hresult(readable->Unmap());
        if(result.visible){result.centerX=static_cast<float>(sumX/result.visible/scale);result.centerY=static_cast<float>(sumY/result.visible/scale);}
        return result;
    }
    // Paints `draw` as the renderer paints a composition surface of `width` x `height` pixels kept
    // at `offset` in an atlas, and returns that region's pixels. `local` is reel::PaintLocal; without
    // it the content is drawn in place, its offset a DIP translation, as the renderer did through 0.3.11.
    template<class Draw>
    std::vector<BYTE> Surface(POINT offset,UINT width,UINT height,float dpi,bool local,Draw&& draw) {
        winrt::com_ptr<ID2D1DeviceContext> context;
        winrt::check_hresult(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,context.put()));
        const auto format=D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED);
        const auto atlasSize=D2D1::SizeU(offset.x+width+3,offset.y+height+3);
        winrt::com_ptr<ID2D1Bitmap1> atlas,readable;
        winrt::check_hresult(context->CreateBitmap(atlasSize,nullptr,0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format),atlas.put()));
        winrt::check_hresult(context->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format),readable.put()));
        context->SetTarget(atlas.get());
        context->BeginDraw();
        context->Clear(D2D1::ColorF(0,0.f));
        if(local)reel::PaintLocal(context.get(),offset,width,height,dpi,draw);
        else {
            context->SetDpi(dpi,dpi);
            context->SetTransform(D2D1::Matrix3x2F::Translation(offset.x*96.f/dpi,offset.y*96.f/dpi));
            draw(context.get());
        }
        winrt::check_hresult(context->EndDraw());
        const D2D1_POINT_2U origin{0,0};
        const D2D1_RECT_U region{static_cast<UINT32>(offset.x),static_cast<UINT32>(offset.y),offset.x+width,offset.y+height};
        winrt::check_hresult(readable->CopyFromBitmap(&origin,atlas.get(),&region));
        D2D1_MAPPED_RECT pixels{};winrt::check_hresult(readable->Map(D2D1_MAP_OPTIONS_READ,&pixels));
        std::vector<BYTE> result;
        for(UINT y=0;y<height;++y)result.insert(result.end(),pixels.bits+y*pixels.pitch,pixels.bits+y*pixels.pitch+width*4);
        winrt::check_hresult(readable->Unmap());
        return result;
    }
private:
    winrt::com_ptr<ID3D11Device> d3d;
    winrt::com_ptr<ID2D1Factory1> factory;
    winrt::com_ptr<ID2D1Device> device;
};
}

int main() {
    using namespace reel;winrt::init_apartment(winrt::apartment_type::single_threaded);
    // System destinations are recognized by target, independently of localized names.
    Check(IdentifySystemIcon(Target(L"ms-settings:",L"Windows 设置"))==SystemIconKind::Settings,"localized Settings target");
    Check(IdentifySystemIcon(Target(L"MS-SETTINGS:colors"))==SystemIconKind::Settings,"Settings URI page and case");
    Check(IdentifySystemIcon(Target(L"C:\\Windows\\ImmersiveControlPanel\\SystemSettings.exe"))==SystemIconKind::Settings,"Settings executable path");
    Check(IdentifySystemIcon(Target(L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel",L"Renamed",TargetKind::Aumid))==SystemIconKind::Settings,"Settings package identifier");
    Check(IdentifySystemIcon(Target(L"notepad.exe",L"记事本"))==SystemIconKind::Notepad,"localized Notepad target");
    Check(IdentifySystemIcon(Target(L" \"C:\\Windows\\System32\\NOTEPAD.EXE\" "))==SystemIconKind::Notepad,"quoted absolute Notepad path");
    Check(IdentifySystemIcon(Target(L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App",L"Notes",TargetKind::Aumid))==SystemIconKind::Notepad,"Notepad package identifier");
    Check(IdentifySystemIcon(Target(L"shell:AppsFolder\\Microsoft.WindowsNotepad_8wekyb3d8bbwe!App"))==SystemIconKind::Notepad,"Shell app-folder activation");
    for(const auto& app:{Target(L"custom.exe",L"Windows Settings"),Target(L"https://example.com/notepad.exe"),
        Target(L"https://example.com/?target=ms-settings:"),Target(L"notepad.exe --fake"),
        Target(L"my-notepad.exe"),Target(L"notepad.exe",L"Name",TargetKind::Aumid)})
        Check(IdentifySystemIcon(app)==SystemIconKind::None,"non-system target cannot acquire a system identity");

    // Vendor identity comes from the target, never from the display name or a profile folder.
    Check(IdentifyBrand(Target(L"OpenAI.Codex_2p2nqsd0c76g0!App",L"Work",TargetKind::Aumid))==L"chatgpt","ChatGPT package family");
    Check(IdentifyBrand(Target(L"Claude_pzs8sxrjxfjjc!Claude",L"Assistant",TargetKind::Aumid))==L"claude","Claude package family");
    Check(IdentifyBrand(Target(L"https://gemini.google.com/app"))==L"gemini","Gemini URL host");
    Check(IdentifyBrand(Target(L"https://gemini.google.com.evil.example/"))==L"","look-alike host is not the vendor");
    Check(IdentifyBrand(Target(L"C:\\Program Files\\Microsoft VS Code\\Code.exe"))==L"vscode","VS Code by install folder");
    Check(IdentifyBrand(Target(L"C:\\Tools\\Code.exe"))==L"","generic Code.exe outside its install folder");
    Check(IdentifyBrand(Target(L"C:\\Users\\x\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe"))==L"vscode","per-user VS Code");
    for(const auto* path:{L"C:\\Users\\x\\AppData\\Local\\Programs\\Microsoft VS Code Insiders\\Code - Insiders.exe",
                          L"C:\\Program Files\\Microsoft VS Code Insiders\\Code.exe"})
        Check(IdentifyBrand(Target(path))==L"","VS Code Insiders keeps its own Shell icon");
    Check(IdentifyBrand(Target(L"C:\\Program Files\\JetBrains\\PyCharm\\bin\\pycharm64.exe"))==L"pycharm","JetBrains product executable");
    Check(IdentifyBrand(Target(L"C:\\Users\\Claude\\tool.exe"))==L"","a profile named Claude never selects a vendor");
    Check(IdentifyBrand(Target(L"C:\\Users\\Claude\\AppData\\Local\\Tool\\tool.exe"))==L"","vendor words in profile paths are ignored");
    Check(IdentifyBrand(Target(L"custom.exe",L"Gemini"))==L"","display names cannot claim vendor artwork");
    Check(IdentifyBrand(Target(L"",L"Claude for Science"))==L"claude","an unresolved preset uses its catalog name");
    for(const auto* path:{L"C:\\Users\\x\\AppData\\Local\\Programs\\ClaudeScience\\claude-science.exe",
                          L"C:\\Users\\x\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Claude for Science.lnk"})
        Check(IdentifyBrand(Target(path))==L"","an installed Claude Science keeps its own Shell icon");

    PreloadOfficialIconAssets();
    for(std::size_t i=0;i<vi::kBrandCount;++i)
        Check(OfficialArtworkBytes(vi::kBrands[i].key)!=nullptr,"every brand in the token table has packaged artwork");
    const auto claude=Target(L"Claude_pzs8sxrjxfjjc!Claude",L"Claude",TargetKind::Aumid);
    const auto gemini=Target(L"https://gemini.google.com",L"Gemini");
    const auto perplexity=Target(L"https://www.perplexity.ai",L"Perplexity");
    const auto settings=Target(L"ms-settings:");
    const auto notepad=Target(L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App",L"Notepad",TargetKind::Aumid);
    const auto unknown=Target(L"C:\\nowhere\\tool.exe",L"Gemini");
    const std::vector<AppTarget> items{claude,gemini,perplexity,settings,notepad,unknown};
    Check(ClassifyArtwork(Target(L"reel:add"))==ArtworkClass::Command,"internal commands are command glyphs");
    PrepareArtwork(items,96);
    Check(ClassifyArtwork(claude)==ArtworkClass::Plated && ClassifyArtwork(perplexity)==ArtworkClass::Plated,
        "artwork with its own plate is plated");
    Check(ClassifyArtwork(gemini)==ArtworkClass::Glyph,"open marks sit on the neutral tile");
    Check(ClassifyArtwork(unknown)==ArtworkClass::Generic,"unknown targets fall back to the generic glyph");
    // This test executable carries no icon, so Windows would show its stock application icon:
    // a custom app without an icon, and a shortcut to one, keep the generic glyph instead.
    std::wstring self(MAX_PATH,L'\0');
    self.resize(GetModuleFileNameW(nullptr,self.data(),static_cast<DWORD>(self.size())));
    std::wstring shortcut(MAX_PATH,L'\0');
    shortcut.resize(GetTempPathW(static_cast<DWORD>(shortcut.size()),shortcut.data()));
    shortcut+=L"copilot-reel-no-icon-"+std::to_wstring(GetCurrentProcessId())+L".lnk";
    {
        winrt::com_ptr<IShellLinkW> link;
        Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put()))) &&
              SUCCEEDED(link->SetPath(self.c_str())) && SUCCEEDED(link.as<IPersistFile>()->Save(shortcut.c_str(),TRUE)),
            "the fixture writes a shortcut to the test executable");
    }
    const auto noIcon=Target(self.c_str()),noIconLink=Target(shortcut.c_str()),withIcon=Target(L"C:\\Windows\\System32\\cmd.exe");
    PrepareArtwork({noIcon,noIconLink,withIcon},96);
    Check(ClassifyArtwork(noIcon)==ArtworkClass::Generic && ArtworkIdentity(noIcon,false)==L"generic",
        "an executable without an icon of its own takes the generic glyph, not Windows' stock icon");
    Check(ClassifyArtwork(noIconLink)==ArtworkClass::Generic,"a shortcut to an executable without an icon takes the generic glyph");
    Check(ClassifyArtwork(withIcon)!=ArtworkClass::Generic,"an executable with its own icon keeps its Shell icon");
    DeleteFileW(shortcut.c_str());

    Canvas canvas;
    for(const float dpi:{96.f,192.f}) {
        PrepareArtwork(items,dpi);
        const auto plated=canvas.Draw(claude,dpi),open=canvas.Draw(gemini,dpi),other=canvas.Draw(perplexity,dpi);
        Check(plated.visible>800*dpi/96*dpi/96,"plated artwork fills its tile");
        Check(plated.outsideTile==0 && plated.inCorners==0,"plated artwork is clipped to the r10 tile shape");
        Check(open.visible>80 && open.outsideTile==0,"glyph marks stay inside the tile");
        Check(std::abs(open.centerX-32)<1.5f && std::abs(open.centerY-32)<1.5f,"glyph marks are centered in the tile");
        Check(plated.hash!=open.hash && open.hash!=other.hash,"vendor marks remain distinct");
        const auto system=canvas.Draw(settings,dpi),note=canvas.Draw(notepad,dpi),generic=canvas.Draw(unknown,dpi);
        Check(generic.visible>40 && generic.outsideTile==0,"generic glyph renders inside the tile");
        PrepareArtwork({noIcon},dpi);
        Check(canvas.Draw(noIcon,dpi).hash==generic.hash,"an executable without an icon draws exactly the generic glyph");
        Check(system.hash!=generic.hash && note.hash!=generic.hash && system.hash!=note.hash,
            "Windows Settings and Notepad use their own Shell icons");
        for(const auto* command:{L"reel:add",L"reel:manage",L"reel:preferred",L"reel:appearance",L"reel:edit",
                                 L"reel:remove",L"reel:previous",L"reel:next"}) {
            const auto glyph=canvas.Draw(Target(command),dpi);
            Check(glyph.visible>20 && glyph.outsideTile==0,"command glyph renders inside the tile");
            Check(std::abs(glyph.centerX-32)<2.5f && std::abs(glyph.centerY-32)<2.5f,"command glyph is centered on its ink");
        }
        Check(canvas.Draw(Target(L"reel:appearance"),dpi,false).hash!=canvas.Draw(Target(L"reel:appearance"),dpi,true).hash,
            "appearance shows the sun in light and the moon in dark");
        // The hub's Tray icon shows the tray icon it chose: the reel's plated color mark, or the mark
        // alone, centered at the size of a command glyph.
        const auto trayColor=Target(L"reel:tray-icon:color"),trayMono=Target(L"reel:tray-icon:monochrome");
        PrepareArtwork({trayColor,trayMono},dpi);
        const auto color=canvas.Draw(trayColor,dpi),mono=canvas.Draw(trayMono,dpi);
        Check(ClassifyArtwork(trayColor)==ArtworkClass::Plated && color.visible>800*dpi/96*dpi/96 && color.inCorners==0,
            "a color tray icon fills its tile with the reel's own mark");
        Check(color.hash==canvas.Draw(Target(L"reel:author"),dpi).hash,"it is the mark About shows");
        Check(ClassifyArtwork(trayMono)==ArtworkClass::Command && mono.visible>20 && mono.outsideTile==0 &&
            std::abs(mono.centerX-32)<2.5f && std::abs(mono.centerY-32)<2.5f,"a monochrome tray icon is the mark alone, centered");
        Check(mono.hash!=color.hash && mono.hash!=canvas.Draw(Target(L"reel:about"),dpi).hash,"the two tray icons and About stay distinct");
    }
    // Catalog entries that still need a target all have an empty one; their catalog name picks the
    // mark, so they must never share artwork (0.3.5 drew Antigravity's for JetBrains IDE and Junie).
    const std::vector<AppTarget> pending{Target(L"",L"Antigravity"),Target(L"",L"JetBrains IDE"),Target(L"",L"Junie"),
                                         Target(L"",L"Claude Science"),Target(L"",L"Unknown Agent")};
    PrepareArtwork(pending,96);
    Check(ArtworkIdentity(pending[0],false)==L"source|antigravity" && ArtworkIdentity(pending[1],false)==L"source|jetbrains" &&
          ArtworkIdentity(pending[2],false)==L"source|junie" && ArtworkIdentity(pending[3],false)==L"source|claude" &&
          ArtworkIdentity(pending[4],false)==L"generic","an entry without a target takes its catalog mark");
    for(size_t i=0;i<pending.size();++i)for(size_t k=i+1;k<pending.size();++k) {
        Check(ArtworkIdentity(pending[i],false)!=ArtworkIdentity(pending[k],false),"entries without a target keep distinct artwork identities");
        Check(canvas.Draw(pending[i],96).hash!=canvas.Draw(pending[k],96).hash,"entries without a target draw distinct marks");
    }
    Check(ArtworkIdentity(Target(L"https://chatgpt.com",L"Work"),false)!=ArtworkIdentity(Target(L"https://chatgpt.com",L"Work"),true),
        "the identity follows the plate's light or dark variant");
    Check(ArtworkIdentity(Target(L"reel:add"),false)==L"command|reel:add","commands are identified by their command");
    // A target on a share is never looked up: an offline server must not stall a page.
    const auto networkTool=Target(L"\\\\copilot-reel-no-such-host\\share\\tool.exe");
    const auto networkCode=Target(L"\\\\copilot-reel-no-such-host\\share\\Microsoft VS Code\\Code.exe");
    const auto before=GetTickCount64();
    PrepareArtwork({networkTool,networkCode,Target(L"\\\\copilot-reel-no-such-host\\share\\agent.lnk")},96);
    Check(GetTickCount64()-before<1000,"preparing network targets does not wait on the share");
    Check(ArtworkIdentity(networkTool,false)==L"generic" && ArtworkIdentity(networkCode,false)==L"source|vscode",
        "a network target keeps its vendor mark or the generic glyph");
    Glyph glyph{};
    Check(!CommandGlyph(Target(L"reel:language"),true,glyph),"the language monogram is drawn by the renderer");

    // A surface's pixels never depend on where the atlas keeps it. The content steps a raster mark,
    // a font glyph and a name through sixteen positions an eighth of a device pixel apart, so some
    // land exactly half a pixel off the grid, at the 154 and 77 DPI of a 0.8 reel on 200% and 100%.
    winrt::com_ptr<IDWriteFactory> write;
    winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.put())));
    winrt::com_ptr<IDWriteTextFormat> format;
    winrt::check_hresult(write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,13,L"en-us",format.put()));
    const auto github=Target(L"reel:github"),generic=Target(L"C:\\Tools\\no-icon-tool.exe");
    PrepareArtwork({github,generic},154);PrepareArtwork({github,generic},77);
    unsigned placed=0,shifted=0;
    for(const float dpi:{154.f,77.f}) {
        const float pixel=96/dpi;
        const UINT width=static_cast<UINT>(std::ceil(16*48*dpi/96)),height=static_cast<UINT>(std::ceil(96*dpi/96));
        const auto content=[&](ID2D1DeviceContext* context) {
            context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(context->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),brush.put()));
            for(int i=0;i<16;++i) {
                const float x=4+i*48+i*pixel/8;
                DrawArtwork(context,github,D2D1::RectF(x,4,x+40,44),ArtworkStyle{dpi,true,false,D2D1::ColorF(1,1,1,1)});
                DrawArtwork(context,generic,D2D1::RectF(x+.5f*pixel,48,x+40+.5f*pixel,88),ArtworkStyle{dpi,true,false,D2D1::ColorF(1,1,1,1)});
                context->DrawText(L"Claude",6,format.get(),D2D1::RectF(x,88-pixel*i/8,x+48,104),brush.get());
            }
        };
        const auto reference=canvas.Surface({0,0},width,height,dpi,true,content);
        const auto inPlace=canvas.Surface({0,0},width,height,dpi,false,content);
        Check(reference==inPlace,"at the atlas origin both ways draw the same pixels");
        for(const POINT offset:{POINT{1,0},POINT{0,1},POINT{3,5},POINT{517,33},POINT{1999,7},POINT{2731,101},POINT{3001,2},POINT{4093,17}}) {
            Check(canvas.Surface(offset,width,height,dpi,true,content)==reference,"a surface's pixels do not depend on its place in the atlas");
            ++placed;
            if(canvas.Surface(offset,width,height,dpi,false,content)!=reference)++shifted;
        }
    }
    ReleaseArtwork(nullptr);
    std::cout<<"Iconography tests passed: "<<checks<<" checks; target identity, artwork classes, tile clipping and command glyphs; "
             <<"surfaces identical at "<<placed<<" atlas offsets (drawn in place, "<<shifted<<" of them would differ).\n";
}
