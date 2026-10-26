#pragma once
#include <cmath>
#include <algorithm>
#include <optional>
#include <string_view>
#include <cstdint>

namespace reel {
constexpr double kPi = 3.14159265358979323846;

struct WorkArea { int left, top, right, bottom; };
struct WheelPlacement {
    int left = 0, top = 0, size = 0;
    double centerX = 0, centerY = 0;
    bool fits = false;
};

// Use the complete drawing footprint, including shadow padding. All positions
// are physical screen pixels; negative coordinates support secondary monitors.
// The pointer is the origin unless the complete footprint crosses a work-area
// boundary. In that case use only the minimum translation needed to contain it.
// Wherever the clamp leaves the pointer, on the plate or off it, selection goes by
// direction from the plate center as soon as the pointer moves; there is no center entry.
inline WheelPlacement PlaceWheel(int pointerX, int pointerY, WorkArea work, int size) {
    WheelPlacement result;
    if(size <= 0 || work.right-work.left < size || work.bottom-work.top < size) return result;
    result.left=std::clamp(pointerX-size/2,work.left,work.right-size);
    result.top=std::clamp(pointerY-size/2,work.top,work.bottom-size);
    result.size=size;
    result.centerX=result.left+size/2.0;
    result.centerY=result.top+size/2.0;
    result.fits=true;
    return result;
}

// URI activations can arrive out of process-creation order. An Up tombstone
// rejects a delayed Down from the same older physical gesture.
class HoldOrdering {
public:
    bool Start(uint64_t stamp) {
        if(stamp<=lastStop_ || stamp<=lastStart_) return false;
        lastStart_=stamp;return true;
    }
    bool Stop(uint64_t stamp) {
        lastStop_=std::max(lastStop_,stamp);
        return stamp>=lastStart_;
    }
private:
    uint64_t lastStart_=0,lastStop_=0;
};

// One meaning per Copilot event in every state. Closed: a tap opens the preferred agent
// and a hold opens the reel. The opening hold selects by direction and its release
// resolves once. A persistent menu (entered by drilling in, or a preview) confirms
// with a tap or with the release of a new hold; it never launches the preferred agent
// and never re-places the plate. Nothing reaches the reel behind an editor or picker.
enum class CopilotRoute { Ignore, LaunchPreferred, Open, Resume, Release, Confirm };
struct ReelContext { bool open=false, blocked=false; };
class ReelInput {
public:
    void Opened(bool persistent,uint64_t stamp) {persistent_=persistent;held_=!persistent;pageAt_=stamp;}
    // A page change always ends the hold, so its late release cannot also choose a child.
    void PageShown(uint64_t stamp) {persistent_=true;held_=false;pageAt_=std::max(pageAt_,stamp);}
    void Closed() {persistent_=held_=false;}
    void EndHold() {held_=false;}
    CopilotRoute Tap(uint64_t stamp,ReelContext reel) const {
        if(reel.blocked)return CopilotRoute::Ignore;
        if(!reel.open)return CopilotRoute::LaunchPreferred;
        // No physical tap happens during a hold, and a tap older than the page on screen is stale.
        if(held_ || !persistent_ || stamp<=pageAt_)return CopilotRoute::Ignore;
        return CopilotRoute::Confirm;
    }
    CopilotRoute HoldStart(uint64_t stamp,ReelContext reel) {
        if(reel.blocked || held_ || (reel.open && !persistent_) || !ordering_.Start(stamp))return CopilotRoute::Ignore;
        if(!reel.open)return CopilotRoute::Open;
        held_=true;return CopilotRoute::Resume;
    }
    CopilotRoute HoldStop(uint64_t stamp,ReelContext reel) {
        const bool ordered=ordering_.Stop(stamp);  // the tombstone is recorded in every state
        if(!held_ || !ordered || reel.blocked || !reel.open)return CopilotRoute::Ignore;
        held_=false;
        return persistent_?CopilotRoute::Confirm:CopilotRoute::Release;
    }
    bool Persistent() const {return persistent_;}
    bool Held() const {return held_;}
private:
    HoldOrdering ordering_;
    bool persistent_=false,held_=false;
    uint64_t pageAt_=0;
};

inline int HitTest(double dx, double dy, unsigned count, double deadZone = 44.0) {
    if (count == 0 || count > 12 || !std::isfinite(dx) || !std::isfinite(dy) ||
        dx * dx + dy * dy <= deadZone * deadZone) return -1;
    double angle = std::atan2(dy, dx) + kPi / 2;
    if (angle < 0) angle += 2 * kPi;
    const double step = 2 * kPi / count;
    return static_cast<int>(std::floor((angle + step / 2) / step)) % count;
}

class GestureState {
public:
    bool Start() {
        if (active_) return false;
        active_ = true; selected_ = -1; ++generation_; return true;
    }
    bool Select(int index, unsigned count) {
        if (!active_) return false;
        const int valid = index >= 0 && static_cast<unsigned>(index) < count ? index : -1;
        if (valid == selected_) return false;
        selected_ = valid; return true;
    }
    bool ObservePointer(double dx,double dy,unsigned count,double deadZone=44.0) {
        if(!active_ || !std::isfinite(dx) || !std::isfinite(dy)) return false;
        return Select(HitTest(dx,dy,count,deadZone),count);
    }
    std::optional<int> Release() {
        if (!active_) return {};
        const int result = selected_;
        Cancel();
        return result < 0 ? std::optional<int>{} : std::optional<int>{result};
    }
    void Cancel() { active_ = false; selected_ = -1; ++generation_; }
    bool Active() const { return active_; }
    int Selected() const { return selected_; }
    unsigned Generation() const { return generation_; }
private:
    bool active_ = false;
    int selected_ = -1;
    unsigned generation_ = 0;
};

// KeyHold is a hold whose release no message reports (the Shell's argument-less activation
// while the key is down): the resident polls the key instead.
enum class Command : unsigned { Background, Settings, Preview, HoldStart, HoldStop, Cancel, Quit, Tap, Diagnostics, SmokeTest, KeyHold,
    UpdateCheck, UpdateInstall, Invalid };

inline Command ParseUri(std::wstring_view uri) {
    // This private activation protocol intentionally accepts no launch targets. Behind a Shell
    // feature flag the Shell appends "&inputTimestamp=<ms>" to every state; nothing else is accepted.
    constexpr std::wstring_view prefix = L"copilot-reel://?state=", stamp = L"&inputTimestamp=";
    if (!uri.starts_with(prefix)) return Command::Invalid;
    uri.remove_prefix(prefix.size());
    if (const auto extra = uri.find(L'&'); extra != std::wstring_view::npos) {
        if (!uri.substr(extra).starts_with(stamp)) return Command::Invalid;
        const auto digits = uri.substr(extra + stamp.size());
        if (digits.empty() || digits.size() > 20 ||
            !std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
            return Command::Invalid;
        uri = uri.substr(0, extra);
    }
    if (uri == L"Down") return Command::HoldStart;
    if (uri == L"Up") return Command::HoldStop;
    if (uri == L"Tap") return Command::Tap;
    return Command::Invalid;
}
}
