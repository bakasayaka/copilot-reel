#include "settings.h"
#include "localization.h"

#include <shlobj.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace reel {
namespace {
constexpr std::size_t kMaxApps = 8, kMaxParked = 16;  // as in radial_menu.h
constexpr DWORD kMaxConfigBytes = 512 * 1024;

bool IsAbsoluteDataDirectory(std::wstring_view path) {
    const auto separator = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    if (path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && separator(path[2])) return true;
    // Accept conventional UNC server/share paths; device namespaces are not data directories.
    if (path.size() < 5 || path[0] != L'\\' || path[1] != L'\\' || path[2] == L'.' || path[2] == L'?') return false;
    const auto share = path.find_first_of(L"\\/", 2);
    return share != std::wstring_view::npos && share > 2 && share + 1 < path.size() && !separator(path[share + 1]);
}

std::wstring DataDirectoryOverride() {
    constexpr wchar_t name[] = L"COPILOT_REEL_DATA_DIR";
    for (int retry = 0; retry < 3; ++retry) {
        SetLastError(ERROR_SUCCESS);
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0) {
            const DWORD code = GetLastError();
            if (code == ERROR_SUCCESS || code == ERROR_ENVVAR_NOT_FOUND) return {};
            throw std::runtime_error("Cannot read COPILOT_REEL_DATA_DIR");
        }
        std::wstring directory(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(name, directory.data(), required);
        if (copied >= required) continue; // An environment update can race with the length query.
        if (copied == 0) continue;
        directory.resize(copied);
        if (!IsAbsoluteDataDirectory(directory))
            throw std::runtime_error("COPILOT_REEL_DATA_DIR must be an absolute drive or UNC directory");
        return directory;
    }
    throw std::runtime_error("COPILOT_REEL_DATA_DIR changed while being read");
}

std::wstring WinError(DWORD code) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = length && message ? std::wstring(message, length) : L"Windows error " + std::to_wstring(code);
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

struct FileHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~FileHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    void Close() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; } }
};

bool Blank(std::wstring_view text) {
    return text.empty() || std::all_of(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
}

bool ValidText(const std::wstring& value, std::size_t limit) {
    return value.size() <= limit && value.find_first_of(std::wstring(L"\0\r\n", 3)) == std::wstring::npos;
}

// One file format for the reel (apps.ini, 1-8 entries) and for the apps taken out of it
// (parked.ini, up to kMaxParked entries, most recent first).
// The byte limit lets every entry use its full field limits.
struct FileFormat { const wchar_t* header; const wchar_t* comment; std::size_t minimum, maximum; DWORD maxBytes; };
constexpr FileFormat kReelFormat{L"CopilotReel", L"Copilot Reel configuration", 1, kMaxApps, kMaxConfigBytes};
constexpr FileFormat kParkedFormat{L"CopilotReelParked", L"Copilot Reel apps taken out of the reel; Add offers them first",
    0, kMaxParked, 4 * kMaxConfigBytes};
// Validates, then replaces the file atomically through a flushed temporary file.
bool WriteList(const std::wstring& path, const FileFormat& format, const std::vector<AppTarget>& targets, std::wstring& error);

bool Validate(const std::vector<AppTarget>& targets, std::wstring& error, const FileFormat& format = kReelFormat) {
    if (targets.size() < format.minimum || targets.size() > format.maximum) {
        error = &format == &kReelFormat ? Tr(L"The reel requires between 1 and 8 applications.", L"轮盘需要 1 至 8 个应用。", L"リールには 1 ～ 8 個のアプリが必要です。") :
            Tr(L"At most 16 apps can wait outside the reel.", L"轮盘外最多保留 16 个应用。", L"リールの外に置けるアプリは最大 16 個です。");
        return false;
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& app = targets[i];
        if (Blank(app.name) || Blank(app.target) || !ValidText(app.name, 128) ||
            !ValidText(app.target, 8192) || !ValidText(app.arguments, 8192) ||
            !ValidText(app.workingDirectory, 8192) ||
            (app.kind != TargetKind::Shell && app.kind != TargetKind::Aumid)) {
            error = std::wstring(Tr(L"Invalid application ", L"无效的应用 ", L"無効なアプリ ")) + std::to_wstring(i + 1) +
                Tr(L": name and target are required. Names support up to 128 characters; other fields support 8192. Line breaks are not allowed.",
                   L"：名称和目标不能为空。名称最多 128 字符，其他字段最多 8192 字符，不能包含换行。",
                   L": 名前と対象は必須です。名前は 128 文字まで、その他の項目は 8192 文字までで、改行は使えません。");
            return false;
        }
    }
    return true;
}

std::vector<AppTarget> Defaults() {
    return {
        {L"Notepad", TargetKind::Shell, L"notepad.exe", L"", L""},
        {L"Windows Settings", TargetKind::Shell, L"ms-settings:", L"", L""}
    };
}

std::wstring Encode(std::wstring_view text) {
    std::wstring encoded;
    encoded.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'\\') encoded += L"\\\\";
        else if (c == L'\t') encoded += L"\\t";
        else encoded += c;
    }
    return encoded;
}

bool Decode(std::wstring_view text, std::wstring& value) {
    value.clear();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\\') { value += text[i]; continue; }
        if (++i == text.size()) return false;
        if (text[i] == L'\\') value += L'\\';
        else if (text[i] == L't') value += L'\t';
        else return false;
    }
    return true;
}

bool Parse(std::wstring_view text, std::vector<AppTarget>& targets, const FileFormat& format = kReelFormat) {
    using Fields = std::map<std::wstring, std::wstring>;
    std::map<std::wstring, Fields> sections;
    std::wstring section;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto end = text.find(L'\n', offset);
        auto line = text.substr(offset, end == std::wstring_view::npos ? text.size() - offset : end - offset);
        offset = end == std::wstring_view::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
        if (line.empty() || line.front() == L';' || line.front() == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = line.substr(1, line.size() - 2);
            if (section.empty() || !sections.emplace(section, Fields{}).second) return false;
            continue;
        }
        const auto equal = line.find(L'=');
        if (section.empty() || equal == std::wstring_view::npos || equal == 0) return false;
        std::wstring value;
        if (!Decode(line.substr(equal + 1), value) ||
            !sections.at(section).emplace(std::wstring(line.substr(0, equal)), std::move(value)).second) return false;
    }
    const auto header = sections.find(format.header);
    if (header == sections.end() || header->second.size() != 2) return false;
    const auto version = header->second.find(L"Version");
    const auto countValue = header->second.find(L"Count");
    if (version == header->second.end() || version->second != L"1" || countValue == header->second.end()) return false;
    const auto& digits = countValue->second;
    if (digits.empty() || digits.size() > 2 || (digits.size() > 1 && digits[0] == L'0') ||
        !std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return false;
    const auto count = static_cast<std::size_t>(std::stoul(digits));
    if (count < format.minimum || count > format.maximum || sections.size() != count + 1) return false;
    targets.clear();
    for (std::size_t i = 0; i < count; ++i) {
        const auto found = sections.find(L"App" + std::to_wstring(i));
        if (found == sections.end() || found->second.size() != 5) return false;
        const auto& fields = found->second;
        for (const auto* key : {L"Name", L"Kind", L"Target", L"Arguments", L"WorkingDirectory"})
            if (!fields.contains(key)) return false;
        AppTarget app;
        app.name = fields.at(L"Name");
        const auto& kind = fields.at(L"Kind");
        if (kind == L"shell") app.kind = TargetKind::Shell;
        else if (kind == L"aumid") app.kind = TargetKind::Aumid;
        else return false;
        app.target = fields.at(L"Target");
        app.arguments = fields.at(L"Arguments");
        app.workingDirectory = fields.at(L"WorkingDirectory");
        targets.push_back(std::move(app));
    }
    std::wstring error;
    return Validate(targets, error, format);
}

} // namespace

std::wstring ConfigPath() {
    if (auto directory = DataDirectoryOverride(); !directory.empty()) {
        if (directory.back() != L'\\' && directory.back() != L'/') directory += L'\\';
        return directory + L"apps.ini";
    }
    // In the package, %LOCALAPPDATA% is redirected to its LocalCache\Local folder, which can sit on
    // another volume (Windows can make LocalCache a junction to <drive>:\WpSystem\<user SID>\...). A
    // rename through the redirection then fails with ERROR_NOT_SAME_DEVICE, so the reel uses that
    // folder itself: the same files, where a temporary file and its replacement always share a volume.
    // Outside a package the flag changes nothing.
    PWSTR directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE | KF_FLAG_RETURN_FILTER_REDIRECTION_TARGET,
        nullptr, &directory))) return {};
    std::wstring path;
    try { path = directory; } catch (...) { CoTaskMemFree(directory); throw; }
    CoTaskMemFree(directory);
    return path + L"\\CopilotReel\\apps.ini";
}

std::vector<AppTarget> LoadTargets(std::wstring& error) {
    error.clear();
    try {
        const auto path = ConfigPath();
        if (path.empty()) { error = Tr(L"Cannot locate the user configuration directory.", L"无法定位用户配置目录。", L"利用者の構成フォルダーが見つかりません。"); return Defaults(); }
        FileHandle file;
        file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                auto defaults = Defaults();
                SaveTargets(defaults, error);
                return defaults;
            }
            error = std::wstring(Tr(L"Cannot read configuration: ", L"无法读取配置：", L"構成を読み取れません: ")) + WinError(code);
            return Defaults();
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 2 || size.QuadPart > kMaxConfigBytes || (size.QuadPart % 2) != 0) {
            error = Tr(L"Invalid configuration length. The original file is retained; default entries are in use.", L"配置文件长度无效。原文件已保留，暂时使用默认入口。", L"構成の長さが無効です。元のファイルは保持され、既定の項目を使用しています。");
            return Defaults();
        }
        std::wstring content(static_cast<std::size_t>(size.QuadPart / 2), L'\0');
        DWORD read = 0;
        if (!ReadFile(file.handle, content.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) || read != size.QuadPart) {
            error = Tr(L"Configuration read failed. The original file is retained; default entries are in use.", L"配置文件读取失败。原文件已保留，暂时使用默认入口。", L"構成を読み取れませんでした。元のファイルは保持され、既定の項目を使用しています。");
            return Defaults();
        }
        std::vector<AppTarget> targets;
        if (content.front() != L'\xfeff' || !Parse(std::wstring_view(content).substr(1), targets)) {
            error = std::wstring(Tr(L"Invalid configuration format. Expected UTF-16 LE with a BOM, Version=1, and 1–8 complete entries. The original file is retained; default entries are in use.\r\n\r\n",
                L"配置文件格式无效。需要带 BOM 的 UTF-16 LE、Version=1 和 1–8 个完整入口。原文件已保留，暂时使用默认入口。\r\n\r\n",
                L"構成の形式が無効です。BOM 付き UTF-16 LE、Version=1、1 ～ 8 個の完全な項目が必要です。元のファイルは保持され、既定の項目を使用しています。\r\n\r\n")) + path;
            return Defaults();
        }
        return targets;
    } catch (...) {
        error = Tr(L"An error occurred while reading configuration. The original file is retained; default entries are in use.", L"读取配置时发生错误。原文件已保留，暂时使用默认入口。", L"構成の読み取り中に問題が発生しました。元のファイルは保持され、既定の項目を使用しています。");
        return Defaults();
    }
}

std::wstring ParkedPath() {
    const auto path = ConfigPath();
    return path.empty() ? path : path.substr(0, path.find_last_of(L"\\/") + 1) + L"parked.ini";
}

std::vector<AppTarget> LoadParked(std::wstring& error) {
    error.clear();
    std::vector<AppTarget> parked;
    try {
        const auto path = ParkedPath();
        if (path.empty()) { error = L"Cannot locate the user configuration directory."; return {}; }
        FileHandle file;
        file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) error = L"Cannot read parked.ini: " + WinError(code);
            return {};
        }
        LARGE_INTEGER size{};
        std::wstring content;
        DWORD read = 0;
        if (GetFileSizeEx(file.handle, &size) && size.QuadPart >= 2 && size.QuadPart <= kParkedFormat.maxBytes && size.QuadPart % 2 == 0) {
            content.assign(static_cast<std::size_t>(size.QuadPart / 2), L'\0');
            if (!ReadFile(file.handle, content.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) || read != size.QuadPart) content.clear();
        }
        // A damaged list never blocks the reel: it is left in place and Add offers no parked apps.
        if (content.empty() || content.front() != L'\xfeff' || !Parse(std::wstring_view(content).substr(1), parked, kParkedFormat)) {
            error = L"parked.ini is not a valid parked app list; it is left unchanged and ignored.";
            return {};
        }
        return parked;
    } catch (...) {
        error = L"An error occurred while reading parked.ini.";
        return {};
    }
}

bool SaveParked(const std::vector<AppTarget>& parked, std::wstring& error) {
    return WriteList(ParkedPath(), kParkedFormat, parked, error);
}

bool SaveTargets(const std::vector<AppTarget>& targets, std::wstring& error) {
    return WriteList(ConfigPath(), kReelFormat, targets, error);
}

namespace {
bool WriteList(const std::wstring& path, const FileFormat& format, const std::vector<AppTarget>& targets, std::wstring& error) {
    error.clear();
    std::wstring temporary;
    bool temporaryOwned = false;
    try {
        if (!Validate(targets, error, format)) return false;
        if (path.empty()) { error = Tr(L"Cannot locate the user configuration directory.", L"无法定位用户配置目录。", L"利用者の構成フォルダーが見つかりません。"); return false; }
        // COPILOT_REEL_DATA_DIR may end in '/', which ConfigPath keeps.
        // A new package has no LocalCache\Local yet, so every missing level is created.
        const auto directory = path.substr(0, path.find_last_of(L"\\/"));
        std::error_code created;
        std::filesystem::create_directories(directory, created);
        if (created) {
            error = std::wstring(Tr(L"Cannot create the configuration directory: ", L"无法创建配置目录：", L"構成フォルダーを作成できません: ")) +
                WinError(static_cast<DWORD>(created.value()));
            return false;
        }
        std::wstring content = std::wstring(L"\xfeff; ") + format.comment + L". UTF-16 LE; backslashes are escaped.\r\n[" +
            format.header + L"]\r\nVersion=1\r\nCount=" + std::to_wstring(targets.size()) + L"\r\n";
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto& app = targets[i];
            content += L"\r\n[App" + std::to_wstring(i) + L"]\r\nName=" + Encode(app.name) +
                L"\r\nKind=" + (app.kind == TargetKind::Shell ? std::wstring(L"shell") : std::wstring(L"aumid")) +
                L"\r\nTarget=" + Encode(app.target) + L"\r\nArguments=" + Encode(app.arguments) +
                L"\r\nWorkingDirectory=" + Encode(app.workingDirectory) + L"\r\n";
        }
        if (content.size() * sizeof(wchar_t) > format.maxBytes) {
            error = &format == &kReelFormat ?
                Tr(L"The configuration exceeds 512 KiB. Shorten paths or arguments.", L"配置总大小超过 512 KiB，请缩短参数或路径。", L"構成が 512 KiB を超えています。ファイルの場所や引数を短くしてください。") :
                Tr(L"The apps kept outside the reel exceed 2 MiB. Put some back or shorten their paths or arguments.",
                   L"轮盘外保留的应用超过 2 MiB。请放回部分应用，或缩短其路径或参数。",
                   L"リールの外に置いたアプリが 2 MiB を超えています。いくつか戻すか、ファイルの場所や引数を短くしてください。");
            return false;
        }
        FileHandle file;
        for (unsigned int attempt = 0; attempt < 16; ++attempt) {
            temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetCurrentThreadId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
            file.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file.handle != INVALID_HANDLE_VALUE) { temporaryOwned = true; break; }
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
        }
        if (file.handle == INVALID_HANDLE_VALUE) {
            error = std::wstring(Tr(L"Cannot create a temporary configuration file: ", L"无法创建临时配置文件：", L"一時的な構成ファイルを作成できません: ")) + WinError(GetLastError());
            temporary.clear(); // Never delete a file that this call did not create.
            return false;
        }
        const DWORD bytes = static_cast<DWORD>(content.size() * sizeof(wchar_t));
        DWORD written = 0;
        const bool writtenOk = WriteFile(file.handle, content.data(), bytes, &written, nullptr) && written == bytes;
        const bool flushed = writtenOk && FlushFileBuffers(file.handle);
        const DWORD code = GetLastError();
        file.Close();
        if (!flushed) {
            error = std::wstring(Tr(L"Configuration write failed; the original file is unchanged: ", L"配置写入失败，原文件保持不变：", L"構成を書き込めませんでした。元のファイルは変更されていません: ")) + WinError(code);
            DeleteFileW(temporary.c_str());
            return false;
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = std::wstring(Tr(L"Cannot replace the configuration; the original file is unchanged: ", L"无法替换配置文件，原文件保持不变：", L"構成を置き換えられません。元のファイルは変更されていません: ")) + WinError(GetLastError());
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    } catch (...) {
        if (temporaryOwned && !temporary.empty()) DeleteFileW(temporary.c_str());
        error = Tr(L"An error occurred while saving configuration; the original file is unchanged.", L"保存配置时发生错误，原文件保持不变。", L"構成の保存中に問題が発生しました。元のファイルは変更されていません。");
        return false;
    }
}
} // namespace
} // namespace reel
