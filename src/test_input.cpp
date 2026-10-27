#include "test_input.h"
#include <appmodel.h>

namespace reel {
namespace {
std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD size=GetEnvironmentVariableW(name,nullptr,0);
    if(!size)return {};
    std::wstring value(size,L'\0');
    const DWORD copied=GetEnvironmentVariableW(name,value.data(),size);
    value.resize(copied<size?copied:0);
    return value;
}
bool Packaged() {
    UINT32 length=0;
    return GetCurrentPackageFullName(&length,nullptr)!=APPMODEL_ERROR_NO_PACKAGE;
}
POINT PrimaryWorkCenter() {
    MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info))return {};
    return {info.rcWork.left+(info.rcWork.right-info.rcWork.left)/2,info.rcWork.top+(info.rcWork.bottom-info.rcWork.top)/2};
}
POINT& SyntheticCursor() {
    static POINT point=CurrentTestInput().pointer;
    return point;
}
}
const TestInputGate& CurrentTestInput() {
    static const TestInputGate value=[] {
        const auto runId=EnvironmentValue(L"COPILOT_REEL_TEST_INPUT");
        // The production path reads one variable and nothing else.
        if(runId.empty())return TestInputGate{};
        const auto pointer=EnvironmentValue(L"COPILOT_REEL_TEST_POINTER");
        auto gate=ParseTestInputGate(runId,pointer,EnvironmentValue(L"COPILOT_REEL_DATA_DIR"),Packaged());
        if(gate.active && pointer.empty())gate.pointer=PrimaryWorkCenter();
        return gate;
    }();
    return value;
}
POINT CursorPosition() {
    if(CurrentTestInput().active)return SyntheticCursor();
    POINT point{};GetCursorPos(&point);return point;
}
void SetSyntheticCursor(POINT screen) {
    if(CurrentTestInput().active)SyntheticCursor()=screen;
}
std::wstring TestInstanceSuffix() {
    return CurrentTestInput().active?L".Test."+CurrentTestInput().runId:std::wstring();
}
bool TestTrayIcon() {
    static const bool value=CurrentTestInput().active && EnvironmentValue(L"COPILOT_REEL_TEST_TRAY")==L"hidden";
    return value;
}
}
