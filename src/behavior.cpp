#include "behavior.h"
#include "settings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace reel {
namespace {
constexpr std::wstring_view kPrefix = L"[Behavior]\r\nVersion=1\r\nPreferredTarget=";
std::atomic<unsigned long long> nextTemporary{0};

std::wstring BehaviorPath() {
    const auto config = ConfigPath();
    const auto separator = config.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};
    return config.substr(0, separator + 1) + L"behavior.ini";
}

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    void Close() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
};

bool IsKey(std::wstring_view key) {
    if (key.size() != 19 || key.substr(0, 3) != L"v1-") return false;
    for (const auto character : key.substr(3)) {
        if (!((character >= L'0' && character <= L'9') ||
            (character >= L'a' && character <= L'f'))) return false;
    }
    return true;
}

std::wstring ReadPreferredKey() {
    const auto path = BehaviorPath();
    if (path.empty()) return {};
    File file{CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.handle == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 2 ||
        size.QuadPart > 4096 || size.QuadPart % sizeof(wchar_t) != 0) return {};
    std::wstring text(static_cast<size_t>(size.QuadPart) / sizeof(wchar_t), L'\0');
    DWORD copied = 0;
    if (!ReadFile(file.handle, text.data(), static_cast<DWORD>(size.QuadPart), &copied, nullptr) ||
        copied != size.QuadPart || text.front() != L'\xfeff') return {};
    std::wstring_view content(text);
    content.remove_prefix(1);
    if (!content.starts_with(kPrefix) || !content.ends_with(L"\r\n")) return {};
    content.remove_prefix(kPrefix.size());
    content.remove_suffix(2);
    return IsKey(content) ? std::wstring(content) : std::wstring{};
}
}

std::wstring StableTargetKey(const AppTarget& target) {
    // FNV-1a is an identity fingerprint, not a credential or security boundary.
    // Hash explicit UTF-16LE code units and length-delimited fields so the key
    // is deterministic across releases and preserves distinct launch profiles.
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const auto byte = [&hash](std::uint8_t value) {
        hash ^= value;
        hash *= UINT64_C(1099511628211);
    };
    const auto field = [&byte](std::wstring_view value) {
        const auto length = static_cast<std::uint64_t>(value.size());
        for (int shift = 0; shift < 64; shift += 8)
            byte(static_cast<std::uint8_t>(length >> shift));
        for (const auto character : value) {
            const auto code = static_cast<std::uint16_t>(character);
            byte(static_cast<std::uint8_t>(code));
            byte(static_cast<std::uint8_t>(code >> 8));
        }
    };
    field(target.kind == TargetKind::Aumid ? L"aumid" : L"shell");
    field(target.target);
    field(target.arguments);
    field(target.workingDirectory);
    std::wstring key = L"v1-";
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        key += digits[(hash >> shift) & 0xf];
    return key;
}

int PreferredTargetIndex(const std::vector<AppTarget>& targets) {
    if (targets.empty()) return -1;
    try {
        const auto key = ReadPreferredKey();
        if (!key.empty()) {
            for (size_t index = 0; index < targets.size(); ++index) {
                if (StableTargetKey(targets[index]) == key) return static_cast<int>(index);
            }
        }
    } catch (...) { }
    return 0;
}

bool SavePreferredTarget(const AppTarget& target) {
    std::wstring temporary;
    bool owned = false;
    File file;
    try {
        if (target.target.empty() || (target.kind != TargetKind::Shell && target.kind != TargetKind::Aumid)) return false;
        const auto path = BehaviorPath();
        if (path.empty()) return false;
        const auto directory = path.substr(0, path.find_last_of(L"\\/"));
        std::error_code created;
        std::filesystem::create_directories(directory, created);
        if (created) return false;
        const std::wstring content = std::wstring(L"\xfeff") + std::wstring(kPrefix) + StableTargetKey(target) + L"\r\n";
        temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(nextTemporary.fetch_add(1)) + L".tmp";
        file.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.handle == INVALID_HANDLE_VALUE) return false;
        owned = true;
        const DWORD bytes = static_cast<DWORD>(content.size() * sizeof(wchar_t));
        DWORD written = 0;
        const bool saved = WriteFile(file.handle, content.data(), bytes, &written, nullptr) &&
            written == bytes && FlushFileBuffers(file.handle);
        file.Close();
        if (!saved || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            return false;
        }
        owned = false;
        return true;
    } catch (...) {
        file.Close();
        if (owned) DeleteFileW(temporary.c_str());
        return false;
    }
}
}
