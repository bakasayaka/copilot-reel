#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace reel {
// Interaction test seam. An unpackaged process started with COPILOT_REEL_TEST_INPUT=<run id>
// and an absolute COPILOT_REEL_DATA_DIR becomes a test instance: it reads its cursor and its
// pointer, click and key input only from test packets, and owns a ".Test.<run id>" namespace.
// It never moves the real pointer, sends input or takes focus. The installed app has package
// identity, so the seam is always off there whatever the environment holds.
struct TestInputGate {
    bool present=false, active=false;   // present: the run id variable is set
    std::wstring runId, reason;         // reason: why a present gate is off; "packaged" in the installed app
    POINT pointer{};                    // initial synthetic cursor, physical screen pixels
};

// The same absolute drive or UNC rule as COPILOT_REEL_DATA_DIR itself.
inline bool IsAbsoluteTestDirectory(std::wstring_view path) {
    const auto separator=[](wchar_t c) {return c==L'\\' || c==L'/';};
    if(path.size()>=3 && ((path[0]>=L'A' && path[0]<=L'Z') || (path[0]>=L'a' && path[0]<=L'z')) &&
        path[1]==L':' && separator(path[2]))return true;
    if(path.size()<5 || path[0]!=L'\\' || path[1]!=L'\\' || path[2]==L'.' || path[2]==L'?')return false;
    const auto share=path.find_first_of(L"\\/",2);
    return share!=std::wstring_view::npos && share>2 && share+1<path.size() && !separator(path[share+1]);
}

// "x,y" in physical screen pixels; negative coordinates address monitors left of or above the primary.
inline std::optional<POINT> ParseTestPointer(std::wstring_view text) {
    const auto number=[](std::wstring_view part) -> std::optional<LONG> {
        const bool negative=!part.empty() && part.front()==L'-';
        if(negative)part.remove_prefix(1);
        if(part.empty() || part.size()>10)return std::nullopt;
        int64_t value=0;
        for(const wchar_t c:part) {
            if(c<L'0' || c>L'9')return std::nullopt;
            value=value*10+(c-L'0');
        }
        if(value>0x7fffffff)return std::nullopt;
        return static_cast<LONG>(negative?-value:value);
    };
    const auto comma=text.find(L',');
    if(comma==text.npos)return std::nullopt;
    const auto x=number(text.substr(0,comma)),y=number(text.substr(comma+1));
    if(!x || !y)return std::nullopt;
    return POINT{*x,*y};
}

// Pure, so the truth table is unit-tested. An empty pointer leaves {0,0}; CurrentTestInput then
// uses the center of the primary work area. A packaged process ignores every other value.
inline TestInputGate ParseTestInputGate(std::wstring_view runId,std::wstring_view pointer,std::wstring_view dataDir,bool packaged) {
    TestInputGate gate;
    if(runId.empty())return gate;
    gate.present=true;
    if(packaged) {gate.reason=L"packaged";return gate;}
    const bool validId=runId.size()>=8 && runId.size()<=40 && runId.find_first_not_of(
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-")==runId.npos;
    if(!validId) {gate.reason=L"COPILOT_REEL_TEST_INPUT must be 8-40 characters of A-Z, a-z, 0-9 or -";return gate;}
    if(!IsAbsoluteTestDirectory(dataDir)) {gate.reason=L"COPILOT_REEL_DATA_DIR must be an absolute drive or UNC directory";return gate;}
    if(!pointer.empty()) {
        const auto point=ParseTestPointer(pointer);
        if(!point) {gate.reason=L"COPILOT_REEL_TEST_POINTER must be x,y in physical screen pixels";return gate;}
        gate.pointer=*point;
    }
    gate.runId=runId;gate.active=true;
    return gate;
}

// Pointer sets the synthetic cursor and runs the pointer path; Click and RightClick hit-test
// like WM_NCHITTEST and then click or go back; Key runs the key path with a virtual-key code.
// Press and Release are the two halves of a click, so Pointer moves between them drag: Press
// hit-tests like Click, and a Release during a drag reaches the reel anywhere, as captured.
enum class TestAction : unsigned { Pointer, Click, RightClick, Key, Press, Release, Count };
// Sent as WM_COPYDATA with dwData kTestIpcMagic ('RTST'); CommandPacket keeps its own 'REEL'.
struct TestPacket { unsigned version=1; TestAction action=TestAction::Pointer; uint64_t sequence=0; POINT screen{}; unsigned vk=0; unsigned reserved=0; };
static_assert(sizeof(TestPacket)==32 && offsetof(TestPacket,sequence)==8 && offsetof(TestPacket,screen)==16 && offsetof(TestPacket,vk)==24,
    "tests/reel_seam.py mirrors this layout");
inline constexpr ULONG_PTR kTestIpcMagic=0x52545354;

// Inactive, foreign, truncated, oversized, future-version, unknown-action and out-of-range
// packets are rejected, exactly like any unknown WM_COPYDATA. reserved must stay zero.
inline std::optional<TestPacket> DecodeTestPacket(ULONG_PTR tag,DWORD size,const void* data,bool active) {
    if(!active || tag!=kTestIpcMagic || size!=sizeof(TestPacket) || !data)return std::nullopt;
    TestPacket packet;std::memcpy(&packet,data,sizeof(packet));
    if(packet.version!=1 || static_cast<unsigned>(packet.action)>=static_cast<unsigned>(TestAction::Count) ||
        packet.vk>0xFE || packet.reserved)return std::nullopt;
    return packet;
}

// Parsed once from COPILOT_REEL_TEST_INPUT, COPILOT_REEL_TEST_POINTER, COPILOT_REEL_DATA_DIR
// and the process's package identity.
const TestInputGate& CurrentTestInput();
// The synthetic cursor while the seam is active, otherwise GetCursorPos. UI thread only.
POINT CursorPosition();
// Moves only the synthetic cursor; does nothing when the seam is inactive.
void SetSyntheticCursor(POINT screen);
// ".Test.<run id>" for the instance mutex and ready event when active, otherwise empty.
std::wstring TestInstanceSuffix();
// COPILOT_REEL_TEST_TRAY=hidden gives an active test instance a hidden notification-area icon
// without the GUID, so the seam can check the tray paths (tests/run-reel-seam.py, group tray).
// Nothing shows: the icon is hidden and a notice on it changes only its tooltip.
bool TestTrayIcon();
}
