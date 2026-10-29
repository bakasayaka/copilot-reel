// Compile this test as a separate executable, without separately compiling
// settings.cpp. The include keeps parser details private to production while
// making the anonymous-namespace helpers available in this translation unit.
// No test calls LoadTargets, SaveTargets, LoadParked, SaveParked, or any window API,
// and only the packaged check computes ConfigPath, without opening it: the real user's
// configuration is never read or written. The writer is tested only in a private
// temporary directory, or in the package's own folder beside the reel's.
#include "../src/settings.cpp"

#include <appmodel.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned int checks = 0;

void Require(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}

bool ReadBack(const std::wstring& path, const reel::FileFormat& format, std::vector<reel::AppTarget>& apps) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::wstring content(4096, L'\0');
    DWORD read = 0;
    ReadFile(file, content.data(), static_cast<DWORD>(content.size() * sizeof(wchar_t)), &read, nullptr);
    CloseHandle(file);
    content.resize(read / sizeof(wchar_t));
    return !content.empty() && content.front() == L'\xfeff' && reel::Parse(std::wstring_view(content).substr(1), apps, format);
}

std::wstring Replace(std::wstring source, std::wstring_view before, std::wstring_view after) {
    const auto position = source.find(before);
    if (position == std::wstring::npos) throw std::runtime_error("invalid test fixture replacement");
    source.replace(position, before.size(), after);
    return source;
}

const std::wstring kFixture = L"; Copilot Reel configuration\n[CopilotReel]\nVersion=1\nCount=3\n\n[App0]\nName=\u4e2d\u6587 Agent \u5de5\u4f5c\u53f0\nKind=shell\nTarget=C:\\\\Program Files\\\\\u4e2d\u6587\u5e94\u7528\\\\agent.exe\nArguments=--profile=\"\u5de5\u4f5c \u7a7a\u95f4\" --mode=a=b\nWorkingDirectory=C:\\\\\u7528\u6237\\\\\u793a\u4f8b \u6587\u4ef6\u5939\n\n[App1]\nName=Web query\nKind=shell\nTarget=https://example.test/chat?q=a=b&agent=\u4e2d\u6587%20\u52a9\u624b#main\nArguments=\nWorkingDirectory=\n\n[App2]\nName=Packaged app\nKind=aumid\nTarget=Example.Agent_123456789abcd!App\nArguments=conversation=123\nWorkingDirectory=\n";

void TestFixture() {
    std::vector<reel::AppTarget> apps;
    Require(reel::Parse(kFixture, apps), "valid Unicode configuration should parse");
    Require(apps.size() == 3, "entry count and order should be preserved");
    Require(apps[0].name == L"\u4e2d\u6587 Agent \u5de5\u4f5c\u53f0", "Chinese name and internal spaces must survive");
    Require(apps[0].target == L"C:\\Program Files\\\u4e2d\u6587\u5e94\u7528\\agent.exe", "Windows backslashes and path spaces must survive");
    Require(apps[0].arguments == L"--profile=\"\u5de5\u4f5c \u7a7a\u95f4\" --mode=a=b", "quotes and equals in arguments must survive");
    Require(apps[0].workingDirectory == L"C:\\\u7528\u6237\\\u793a\u4f8b \u6587\u4ef6\u5939", "working directory must survive");
    Require(apps[1].target == L"https://example.test/chat?q=a=b&agent=\u4e2d\u6587%20\u52a9\u624b#main", "URI query equals, ampersands, percent escapes, and fragment must survive");
    Require(apps[1].arguments.empty() && apps[1].workingDirectory.empty(), "optional empty values must stay empty");
    Require(apps[2].kind == reel::TargetKind::Aumid && apps[2].target == L"Example.Agent_123456789abcd!App", "AUMID must preserve package identity and entrypoint");

    std::wstring crlf;
    for (wchar_t c : kFixture) {
        if (c == L'\n') crlf += L'\r';
        crlf += c;
    }
    Require(reel::Parse(crlf, apps), "Windows CRLF lines should parse");
    Require(reel::Parse(kFixture.substr(0, kFixture.size() - 1), apps), "final newline is optional");
}

void TestEscaping() {
    const std::wstring original = L"  \u4e2d\u6587 \\server\\\u5171\u4eab\\agent.lnk\t--key=a=b&x=1  ";
    const auto encoded = reel::Encode(original);
    Require(encoded == L"  \u4e2d\u6587 \\\\server\\\\\u5171\u4eab\\\\agent.lnk\\t--key=a=b&x=1  ", "encoding must escape backslashes and tabs without trimming values");
    std::wstring decoded;
    Require(reel::Decode(encoded, decoded) && decoded == original, "escaped text should round-trip exactly");
    Require(reel::Decode(L"", decoded) && decoded.empty(), "empty optional field should round-trip");
    Require(!reel::Decode(L"dangling\\", decoded), "dangling escape must be rejected");
    Require(!reel::Decode(L"unsupported\\q", decoded), "unknown escape must be rejected instead of changing a path");
}

void TestCorruptConfigurations() {
    std::vector<reel::AppTarget> apps;
    const std::vector<std::pair<std::wstring, const char*>> invalid{
        {L"", "empty file should fail"},
        {Replace(kFixture, L"Version=1", L"Version=2"), "unsupported version should fail"},
        {Replace(kFixture, L"Count=3", L"Count=0"), "zero entries should fail"},
        {Replace(kFixture, L"Count=3", L"Count=9"), "more than eight entries should fail"},
        {Replace(kFixture, L"Count=3", L"Count=2"), "declared count mismatch should fail"},
        {Replace(kFixture, L"Count=3", L"Count=3\nCount=3"), "duplicate header key should fail"},
        {kFixture + L"\n[App0]\n", "duplicate section should fail"},
        {Replace(kFixture, L"[App1]", L"[App7]"), "noncontiguous indexes should fail"},
        {Replace(kFixture, L"Name=Web query", L"Name=Web query\nName=Second"), "duplicate application key should fail"},
        {Replace(kFixture, L"Kind=aumid", L"Kind=unknown"), "unknown launch kind should fail"},
        {Replace(kFixture, L"Name=Web query", L"Name= \t "), "whitespace-only name should fail"},
        {Replace(kFixture, L"Target=Example.Agent_123456789abcd!App", L"Target="), "empty launch target should fail"},
        {Replace(kFixture, L"Arguments=conversation=123\n", L""), "missing optional-valued field should fail"},
        {Replace(kFixture, L"Arguments=conversation=123", L"Arguments=bad\\q"), "invalid escape should fail"},
        {kFixture + L"\n[Unknown]\nKey=value\n", "unknown section should fail"},
        {L"Name=orphan\n" + kFixture, "key outside a section should fail"}
    };
    for (const auto& [fixture, description] : invalid) Require(!reel::Parse(fixture, apps), description);
}

void TestLimits() {
    const reel::AppTarget valid{L"Agent", reel::TargetKind::Shell, L"https://example.test", L"", L""};
    std::wstring error;
    Require(!reel::Validate({}, error) && !error.empty(), "empty application list should return an error");
    Require(reel::Validate({valid}, error), "one valid application is supported");
    Require(reel::Validate(std::vector<reel::AppTarget>(8, valid), error), "eight applications are supported");
    Require(!reel::Validate(std::vector<reel::AppTarget>(9, valid), error), "nine applications must be rejected");

    auto app = valid;
    app.name = std::wstring(128, L'\u540d');
    Require(reel::Validate({app}, error), "128 UTF-16 name units are supported");
    app.name += L'\u79f0';
    Require(!reel::Validate({app}, error), "overlong name must be rejected");
    app = valid;
    app.target = std::wstring(8192, L'x');
    Require(reel::Validate({app}, error), "8192-unit target is supported");
    app.target += L'x';
    Require(!reel::Validate({app}, error), "overlong target must be rejected");
    app = valid;
    app.arguments = std::wstring(8193, L'x');
    Require(!reel::Validate({app}, error), "overlong arguments must be rejected");
    app = valid;
    app.workingDirectory = std::wstring(8193, L'x');
    Require(!reel::Validate({app}, error), "overlong working directory must be rejected");

    for (wchar_t forbidden : {L'\0', L'\r', L'\n'}) {
        app = valid;
        app.target += forbidden;
        app.target += L"hidden suffix";
        Require(!reel::Validate({app}, error), "embedded NUL or newline must not truncate or inject configuration content");
    }
    app = valid;
    app.kind = static_cast<reel::TargetKind>(100);
    Require(!reel::Validate({app}, error), "invalid enum value must be rejected");

    auto defaults = reel::Defaults();
    Require(defaults.size() == 2 && reel::Validate(defaults, error), "fallback defaults must be valid and bounded");
    Require(defaults[0].target == L"notepad.exe" && defaults[1].target == L"ms-settings:", "fallback defaults remain local; the controller adds discovered installed agents");
}
std::wstring ParkedFixture(std::size_t count) {
    std::wstring text = L"[CopilotReelParked]\nVersion=1\nCount=" + std::to_wstring(count) + L"\n";
    for (std::size_t i = 0; i < count; ++i)
        text += L"[App" + std::to_wstring(i) + L"]\nName=Agent " + std::to_wstring(i) +
            L"\nKind=shell\nTarget=C:\\\\Apps\\\\agent" + std::to_wstring(i) + L".exe\nArguments=--profile work\nWorkingDirectory=D:\\\\Work\n";
    return text;
}

// parked.ini shares the reel's format under its own header: 0-16 entries, never mistaken for apps.ini.
void TestParked() {
    std::vector<reel::AppTarget> apps;
    Require(reel::Parse(ParkedFixture(0), apps, reel::kParkedFormat) && apps.empty(), "an empty parked list is valid");
    Require(reel::Parse(ParkedFixture(16), apps, reel::kParkedFormat) && apps.size() == 16 &&
        apps[15].target == L"C:\\Apps\\agent15.exe" && apps[3].arguments == L"--profile work" && apps[3].workingDirectory == L"D:\\Work",
        "sixteen parked apps keep order and every field");
    Require(!reel::Parse(ParkedFixture(17), apps, reel::kParkedFormat), "more than sixteen parked apps should fail");
    Require(!reel::Parse(Replace(ParkedFixture(1), L"Count=1", L"Count=01"), apps, reel::kParkedFormat), "a padded count should fail");
    Require(!reel::Parse(Replace(ParkedFixture(1), L"Count=1", L"Count=+1"), apps, reel::kParkedFormat), "a signed count should fail");
    Require(!reel::Parse(kFixture, apps, reel::kParkedFormat), "apps.ini is not a parked list");
    Require(!reel::Parse(ParkedFixture(2), apps), "a parked list is not apps.ini");
    Require(!reel::Parse(Replace(kFixture, L"Count=3", L"Count=10"), apps), "a two-digit reel count above eight should fail");

    std::wstring error;
    const reel::AppTarget valid{L"Agent", reel::TargetKind::Shell, L"C:\\Apps\\agent.exe", L"", L""};
    Require(reel::Validate({}, error, reel::kParkedFormat), "no parked apps is valid");
    Require(!reel::Validate(std::vector<reel::AppTarget>(17, valid), error, reel::kParkedFormat) && !error.empty(),
        "seventeen parked apps must be rejected");

    // A round trip through the atomic writer in a private temporary directory.
    wchar_t temp[MAX_PATH + 1]{};
    Require(GetTempPathW(MAX_PATH, temp) > 0, "temporary directory");
    const std::wstring directory = std::wstring(temp) + L"copilot-reel-settings-test-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring path = directory + L"\\missing\\parked.ini";
    std::vector<reel::AppTarget> parked{{L"\u4e2d\u6587 Agent", reel::TargetKind::Shell, L"C:\\Apps\\a\tb.exe", L"--x=1", L"C:\\W"},
        {L"Store", reel::TargetKind::Aumid, L"Example.Agent_123!App", L"", L""}};
    // The writer creates every missing level, as a new package's LocalCache\Local is.
    Require(reel::WriteList(path, reel::kParkedFormat, parked, error), "parked apps should save into a missing directory");
    Require(ReadBack(path, reel::kParkedFormat, apps) && apps.size() == 2 &&
        apps[0].name == parked[0].name && apps[0].target == parked[0].target && apps[1].kind == reel::TargetKind::Aumid,
        "a saved parked list reads back unchanged");
    Require(reel::WriteList(path, reel::kParkedFormat, {}, error), "an empty parked list saves");
    Require(!reel::WriteList(path, reel::kReelFormat, {}, error), "the reel never saves empty");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

std::string Utf8(std::wstring_view text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// Run by tools/test.ps1 inside the installed package (Invoke-CommandInDesktopPackage), where Windows
// redirects %LOCALAPPDATA%. ConfigPath is computed but never opened: the writer saves only in a folder
// of its own beside the reel's, then reads the file back through the redirected path.
std::string PackagedCheck() {
    UINT32 length = 0;
    Require(GetCurrentPackageFamilyName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER, "the check runs inside a package");
    std::wstring family(length, L'\0');
    Require(GetCurrentPackageFamilyName(&length, family.data()) == ERROR_SUCCESS, "the package family name");
    family.resize(length - 1);
    const auto config = reel::ConfigPath();
    constexpr std::wstring_view leaf = L"\\CopilotReel\\apps.ini";
    const std::wstring expected = L"\\Packages\\" + family + L"\\LocalCache\\Local" + std::wstring(leaf);
    Require(config.size() > expected.size() && CompareStringOrdinal(config.c_str() + config.size() - expected.size(),
        static_cast<int>(expected.size()), expected.c_str(), static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL,
        "ConfigPath names the package's own LocalCache\\Local folder, not the redirected %LOCALAPPDATA%");

    PWSTR redirectedRoot = nullptr;
    Require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &redirectedRoot)), "%LOCALAPPDATA%");
    const std::wstring redirected(redirectedRoot);
    CoTaskMemFree(redirectedRoot);
    const auto folder = L"\\CopilotReel-packaged-test-" + std::to_wstring(GetCurrentProcessId());
    const auto real = config.substr(0, config.size() - leaf.size()) + folder;
    const auto path = real + L"\\nested\\apps.ini";
    std::vector<reel::AppTarget> apps{{L"Agent", reel::TargetKind::Shell, L"C:\\Apps\\agent.exe", L"", L""}}, read;
    std::wstring error;
    Require(reel::WriteList(path, reel::kReelFormat, apps, error), "a new file saves in the package's folder");
    apps.push_back({L"Store", reel::TargetKind::Aumid, L"Example.Agent_123!App", L"", L""});
    Require(reel::WriteList(path, reel::kReelFormat, apps, error), "an existing file is replaced in the package's folder");
    Require(ReadBack(redirected + folder + L"\\nested\\apps.ini", reel::kReelFormat, read) && read.size() == 2 &&
        read[1].target == apps[1].target, "the redirected %LOCALAPPDATA% shows the replaced file");
    std::error_code ignored;
    std::filesystem::remove_all(real, ignored);
    return Utf8(L"ConfigPath=" + config);
}

// The report is written whole once the check ends, so the runner never reads half of it.
int WriteReport(const std::wstring& report, const std::string& text) {
    const auto partial = report + L".partial";
    {
        std::ofstream file(std::filesystem::path(partial), std::ios::binary | std::ios::trunc);
        file << text;
        if (!file.flush()) return 1;
    }
    return MoveFileExW(partial.c_str(), report.c_str(), MOVEFILE_REPLACE_EXISTING) ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--packaged") {
        std::string text;
        try {
            const auto detail = PackagedCheck();
            text = "Packaged settings check passed: " + std::to_string(checks) + " checks; " + detail + "\n";
        } catch (const std::exception& error) {
            text = "Packaged settings check failed after " + std::to_string(checks) + " checks: " + error.what() + "\n";
        }
        return WriteReport(argv[2], text);
    }
    try {
        TestFixture();
        TestEscaping();
        TestCorruptConfigurations();
        TestLimits();
        TestParked();
        std::cout << "Settings tests passed: " << checks << " checks; no user files accessed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Settings test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
