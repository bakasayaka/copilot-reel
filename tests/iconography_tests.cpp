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
#include <array>
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

struct Raster { uint64_t hash=0; unsigned visible=0,outsideTile=0,inCorners=0; float centerX=0,centerY=0,inkWidth=0,inkHeight=0;
                UINT edge=0; std::vector<BYTE> alpha; };

// Solid runs (alpha 128 or more) from the ink's center to the right edge, on the ink's middle row:
// one for the outer orbit alone, two when the inner ring runs inside it.
unsigned RunsRightOfCenter(const Raster& raster) {
    UINT left=raster.edge,top=raster.edge,right=0,bottom=0;
    for(UINT y=0;y<raster.edge;++y)for(UINT x=0;x<raster.edge;++x)if(raster.alpha[y*raster.edge+x]>=8) {
        left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
    }
    unsigned runs=0;bool inside=false;
    for(UINT x=(left+right)/2,y=(top+bottom)/2;x<raster.edge;++x) {
        const bool solid=raster.alpha[y*raster.edge+x]>=128;
        runs+=solid && !inside;inside=solid;
    }
    return runs;
}

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
    Raster Draw(const reel::AppTarget& app,float dpi,bool dark=true,std::optional<bool> appDark={}) {
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
            reel::ArtworkStyle{dpi,dark,false,D2D1::ColorF(1,1,1,1),appDark}),"artwork draws for every non-language item");
        winrt::check_hresult(context->EndDraw());
        winrt::check_hresult(readable->CopyFromBitmap(nullptr,target.get(),nullptr));
        D2D1_MAPPED_RECT pixels{};winrt::check_hresult(readable->Map(D2D1_MAP_OPTIONS_READ,&pixels));
        Raster result;result.hash=14695981039346656037ull;
        result.edge=edge;result.alpha.resize(static_cast<size_t>(edge)*edge);
        const float scale=dpi/96,tileLeft=12*scale,tileRight=52*scale,radius=reel::vi::kTileRadius*scale;
        double sumX=0,sumY=0;
        UINT left=edge,top=edge,right=0,bottom=0;
        for(UINT y=0;y<edge;++y)for(UINT x=0;x<edge;++x) {
            const BYTE* pixel=pixels.bits+y*pixels.pitch+x*4;
            result.alpha[static_cast<size_t>(y)*edge+x]=pixel[3];
            for(unsigned channel=0;channel<4;++channel){result.hash^=pixel[channel];result.hash*=1099511628211ull;}
            if(pixel[3]<8)continue;
            ++result.visible;sumX+=x+.5;sumY+=y+.5;
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
            const float px=x+.5f,py=y+.5f;
            if(px<tileLeft || px>tileRight || py<tileLeft || py>tileRight)++result.outsideTile;
            // Beyond the r10 corner arc of the tile shape.
            const float cx=std::clamp(px,tileLeft+radius,tileRight-radius),cy=std::clamp(py,tileLeft+radius,tileRight-radius);
            if((px-cx)*(px-cx)+(py-cy)*(py-cy)>(radius+1)*(radius+1))++result.inCorners;
        }
        winrt::check_hresult(readable->Unmap());
        if(result.visible){result.centerX=static_cast<float>(sumX/result.visible/scale);result.centerY=static_cast<float>(sumY/result.visible/scale);
            result.inkWidth=(right-left+1)/scale;result.inkHeight=(bottom-top+1)/scale;}
        return result;
    }
    // Paints `draw` as the renderer paints a composition surface of `width` x `height` pixels kept
    // at `offset` in an atlas, and returns that region's pixels. `local` is reel::PaintLocal; without
    // it the content is drawn in place, its offset a DIP translation, as the renderer did through 0.3.11.
    template<class Draw>
    std::vector<BYTE> Surface(POINT offset,UINT width,UINT height,float dpi,bool local,Draw&& draw,reel::PaintScratch* scratch=nullptr) {
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
        if(local)reel::PaintLocal(context.get(),offset,width,height,dpi,draw,scratch);
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
    // A1: a plate is a tile. A filled rounded square passes; a window with square corners, which fills
    // a rounded rectangle just as well, and a wide plate do not.
    auto plate=[](UINT width,UINT height,float radius) {
        std::vector<BYTE> pixels(256*256*4,0);
        const float left=(256-width)/2.f,top=(256-height)/2.f;
        for(UINT y=0;y<256;++y)for(UINT x=0;x<256;++x) {
            const float px=x+.5f,py=y+.5f;
            if(px<left || px>left+width || py<top || py>top+height)continue;
            const float cx=std::clamp(px,left+radius,left+width-radius),cy=std::clamp(py,top+radius,top+height-radius);
            if((px-cx)*(px-cx)+(py-cy)*(py-cy)>radius*radius)continue;
            BYTE* p=&pixels[(static_cast<size_t>(y)*256+x)*4];p[0]=p[1]=p[2]=200;p[3]=255;
        }
        return pixels;
    };
    Check(LooksPlated(plate(224,224,48).data(),256,256),"a filled rounded square is a plate");
    Check(!LooksPlated(plate(224,224,4).data(),256,256),"a window with nearly square corners is no plate");
    Check(!LooksPlated(plate(240,176,40).data(),256,256),"a wide rounded rectangle is no plate");
    // A2: a Shell icon draws from its own frame for the target, not from the 256 px one shrunk.
    PrepareArtwork({withIcon},96);
    const UINT frame96=ShellFrameFor(withIcon,96);
    PrepareArtwork({withIcon},192);
    const UINT frame192=ShellFrameFor(withIcon,192);
    Check(frame96>0 && frame96<256 && frame192>frame96,"Windows' own small icon frames draw at 100% and larger ones at 200%");
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
        // R10: a contrast theme can put a dark plate under light apps; the glyph follows the app mode.
        Check(canvas.Draw(Target(L"reel:appearance"),dpi,true,false).hash==canvas.Draw(Target(L"reel:appearance"),dpi,false).hash &&
              canvas.Draw(Target(L"reel:appearance"),dpi,false,true).hash==canvas.Draw(Target(L"reel:appearance"),dpi,true).hash,
            "sun or moon follows the app mode, not the plate");
        // The hub's Tray icon shows the tray icon it chose: the reel's plated color mark, or the mark
        // alone, centered and as wide as the color mark's rings, so only the color changes.
        const auto trayColor=Target(L"reel:tray-icon:color"),trayMono=Target(L"reel:tray-icon:monochrome");
        PrepareArtwork({trayColor,trayMono},dpi);
        const auto color=canvas.Draw(trayColor,dpi),mono=canvas.Draw(trayMono,dpi);
        Check(ClassifyArtwork(trayColor)==ArtworkClass::Plated && color.visible>800*dpi/96*dpi/96 && color.inCorners==0,
            "a color tray icon fills its tile with the reel's own mark");
        Check(color.hash==canvas.Draw(Target(L"reel:author"),dpi).hash,"it is the mark About shows");
        Check(ClassifyArtwork(trayMono)==ArtworkClass::Command && mono.visible>20 && mono.outsideTile==0 &&
            std::abs(mono.centerX-32)<2.5f && std::abs(mono.centerY-32)<2.5f,"a monochrome tray icon is the mark alone, centered");
        const float markExtent=vi::kTile*vi::kOrbitMarkExtent/vi::kOrbitMarkCanvas;
        Check(std::abs(mono.inkWidth-markExtent)<=96/dpi && std::abs(mono.inkHeight-markExtent)<=96/dpi,
            "the mark alone is as wide as the color mark's rings");
        Check(mono.hash!=color.hash && mono.hash!=canvas.Draw(Target(L"reel:about"),dpi).hash,"the two tray icons and About stay distinct");
    }
    // At the 70% step on a 100% display (54 DPI) the mark keeps its width, with a one-pixel stroke.
    const auto smallest=canvas.Draw(Target(L"reel:tray-icon:monochrome"),54);
    Check(std::abs(smallest.inkWidth-vi::kTile*vi::kOrbitMarkExtent/vi::kOrbitMarkCanvas)<=96/54.f && smallest.outsideTile==0,
        "the mark alone keeps its width at the smallest step");
    // P2, one cutout rule for every badge: a check or dot on a tile cuts its ring out of the tile, so the
    // plate or the acrylic shows through; on a selected tile the ring takes the seat fill inside the seat
    // and stays clear outside it (it no longer spills a seat-colored crescent onto the plate). The badge
    // center is on the pixel grid, so the ring, the disc and the check share it.
    for(const float dpi:{96.f,144.f,192.f})for(const auto badge:{TileBadge::Check,TileBadge::Dot})for(const bool selected:{false,true}) {
        const float h=32,scale=dpi/96;
        const UINT edge=static_cast<UINT>(64*scale);
        const auto shape=MeasureBadge(badge,{h,h},dpi);
        Check(std::abs(shape.center.x*scale-std::round(shape.center.x*scale))<1e-3f &&
              std::abs(shape.center.y*scale-std::round(shape.center.y*scale))<1e-3f,"the badge center is on the pixel grid");
        Check(shape.ring>shape.disc && shape.disc>=(badge==TileBadge::Check?vi::kBadgeRadius:vi::kBadgeDotRadius),"the ring surrounds the disc");
        const D2D1_COLOR_F tileColor{1,1,1,1},disc{0,.4f,.8f,1},onDisc{1,1,1,1},seatFill{.8f,.9f,1,1};
        const auto seat=D2D1::RoundedRect(D2D1::RectF(h-vi::kSeat/2,h-vi::kSeat/2,h+vi::kSeat/2,h+vi::kSeat/2),vi::kSeatRadius,vi::kSeatRadius);
        const auto pixels=canvas.Surface({0,0},edge,edge,dpi,true,[&](ID2D1DeviceContext* context) {
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(context->CreateSolidColorBrush(tileColor,brush.put()));
            context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(h-vi::kTile/2,h-vi::kTile/2,h+vi::kTile/2,h+vi::kTile/2),
                vi::kTileRadius,vi::kTileRadius),brush.get());
            DrawBadge(context,badge,shape,{disc,onDisc,seatFill},selected?&seat:nullptr,dpi);
        });
        auto pixel=[&](float x,float y) {
            const size_t i=(static_cast<size_t>(y*scale)*edge+static_cast<size_t>(x*scale))*4;
            return std::array<BYTE,4>{pixels[i],pixels[i+1],pixels[i+2],pixels[i+3]};
        };
        // Halfway through the ring, toward the tile's center (inside the tile and the seat) and away from
        // it (outside the tile and, at the seat's corner, outside the seat).
        const float mid=(shape.disc+shape.ring)/2,d=mid/std::sqrt(2.f);
        const auto inward=pixel(shape.center.x-d,shape.center.y+d),outward=pixel(shape.center.x+d,shape.center.y-d);
        const auto discCenter=pixel(shape.center.x,shape.center.y+shape.disc/2);
        Check(discCenter[3]==255 && std::abs(discCenter[2]-0)<=2 && std::abs(discCenter[1]-102)<=2 && std::abs(discCenter[0]-204)<=2,
            "the disc is the accent");
        Check(outward[3]<=40,"outside the seat the ring is clear: no crescent on the plate");
        if(dpi<144)continue;  // below, the 1.5 DIP ring is one or two pixels and its middle is all edge
        // Premultiplied: a pixel the ring's antialiased edge grazes keeps the seat fill's color at less alpha.
        auto straight=[&](int channel){return inward[3]?inward[channel]*255/inward[3]:0;};
        if(selected)
            Check(inward[3]>=200 && std::abs(straight(2)-204)<=12 && std::abs(straight(1)-230)<=12 && std::abs(straight(0)-255)<=12,
                "inside the seat a selected badge's ring is the seat fill");
        else
            Check(inward[3]<=40,"the ring is cut out of the tile, so the plate or the acrylic shows through");
    }
    // Every reel size at 100%, 125% and 150% display scaling, at the reel's effective DPI: the stroke is
    // 1/14 of the mark and the inner ring needs two pixels of it, so the 70% and 85% steps at 100% and
    // the 70% step at 125% show the outer orbit and the dot; every other one shows both rings.
    for(const float monitor:{96.f,120.f,144.f})for(const float step:vi::kReelSizeSteps) {
        const float dpi=static_cast<float>(std::lround(monitor*vi::kReelScale*step));
        const auto mark=canvas.Draw(Target(L"reel:tray-icon:monochrome"),dpi);
        const bool outerOnly=(monitor==96 && step<1) || (monitor==120 && step<.8f);
        Check(RunsRightOfCenter(mark)==(outerOnly?1u:2u),
            outerOnly?"the hub's small monochrome mark is the outer orbit and the dot":"the hub's monochrome mark has both rings");
        Check(std::abs(mark.inkWidth-vi::kTile*vi::kOrbitMarkExtent/vi::kOrbitMarkCanvas)<=96/dpi && mark.outsideTile==0,
            "the hub's monochrome mark keeps the color mark's width at every size");
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
    // A local shortcut to a share, or with its icon on one, is left alone the same way.
    std::wstring networkLink(MAX_PATH,L'\0'),iconLink;
    networkLink.resize(GetTempPathW(static_cast<DWORD>(networkLink.size()),networkLink.data()));
    iconLink=networkLink+L"copilot-reel-network-icon-"+std::to_wstring(GetCurrentProcessId())+L".lnk";
    networkLink+=L"copilot-reel-network-"+std::to_wstring(GetCurrentProcessId())+L".lnk";
    {
        winrt::com_ptr<IShellLinkW> link,icon;
        Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put()))) &&
              SUCCEEDED(link->SetPath(L"\\\\copilot-reel-no-such-host\\share\\tool.exe")) &&
              SUCCEEDED(link.as<IPersistFile>()->Save(networkLink.c_str(),TRUE)) &&
              SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(icon.put()))) &&
              SUCCEEDED(icon->SetPath(L"C:\\Windows\\System32\\cmd.exe")) &&
              SUCCEEDED(icon->SetIconLocation(L"\\\\copilot-reel-no-such-host\\share\\tool.ico",0)) &&
              SUCCEEDED(icon.as<IPersistFile>()->Save(iconLink.c_str(),TRUE)),
            "the fixture writes local shortcuts to a share");
    }
    const auto viaLink=Target(networkLink.c_str()),viaIcon=Target(iconLink.c_str());
    const auto linked=GetTickCount64();
    PrepareArtwork({viaLink,viaIcon},96);
    Check(GetTickCount64()-linked<1000,"preparing a local shortcut to a share does not wait on the share");
    Check(ArtworkIdentity(viaLink,false)==L"generic" && ArtworkIdentity(viaIcon,false)==L"generic",
        "a local shortcut to a share keeps the generic glyph");
    DeleteFileW(networkLink.c_str());DeleteFileW(iconLink.c_str());
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
        // The renderer keeps the bitmap it drew into while sizes repeat; reused, it draws the same pixels.
        reel::PaintScratch scratch;
        Check(canvas.Surface({0,0},width,height,dpi,true,content,&scratch)==reference && scratch.bitmap,"a scratch bitmap draws the same pixels");
        const auto* kept=scratch.bitmap.get();
        Check(canvas.Surface({517,33},width,height,dpi,true,content,&scratch)==reference && scratch.bitmap.get()==kept,
            "the scratch bitmap is reused for a surface of the same size");
    }
    ReleaseArtwork(nullptr);
    std::cout<<"Iconography tests passed: "<<checks<<" checks; target identity, artwork classes, tile clipping and command glyphs; "
             <<"surfaces identical at "<<placed<<" atlas offsets (drawn in place, "<<shifted<<" of them would differ).\n";
}
