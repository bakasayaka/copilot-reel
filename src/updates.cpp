#include "updates.h"
#include "localization.h"
#include "settings.h"
#include "test_input.h"

#include <appmodel.h>
#include <appxpackaging.h>
#include <bcrypt.h>
#include <shlwapi.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace reel {
namespace {
using namespace std::chrono_literals;
namespace wf = winrt::Windows::Foundation;
namespace wh = winrt::Windows::Web::Http;

constexpr wchar_t kFeed[] = L"https://api.github.com/repos/bakasayaka/copilot-reel/releases?per_page=10";
constexpr wchar_t kPackageName[] = L"CopilotReel.Desktop";
constexpr wchar_t kPublisher[] = L"CN=CopilotReel Development";
// A package is about 1.4 MB; anything far larger is not ours.
constexpr unsigned long long kMaxPackage = 64ull << 20;

#if defined(_M_ARM64)
constexpr wchar_t kBuiltArchitecture[] = L"arm64";
#else
constexpr wchar_t kBuiltArchitecture[] = L"x64";
#endif

std::wstring Lower(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}

std::wstring Hex(const BYTE* bytes, size_t count, bool upper) {
    static constexpr wchar_t lower[] = L"0123456789abcdef", higher[] = L"0123456789ABCDEF";
    const wchar_t* digits = upper ? higher : lower;
    std::wstring result;
    for (size_t i = 0; i < count; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}

std::wstring Environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (!length) return {};
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return value;
}

long long UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::wstring Message(const winrt::hresult_error& error) {
    wchar_t code[16]{};
    swprintf_s(code, L"0x%08X", static_cast<unsigned>(error.code().value));
    return std::wstring(error.message()) + L" (" + code + L")";
}

// One request with a deadline that ends once the headers arrive. The client takes the user's proxy
// settings and skips the HTTP cache. A failure reports its own error, not a timeout.
wh::HttpResponseMessage Get(const std::wstring& url, const std::wstring& version, const wchar_t* accept,
    std::chrono::seconds deadline) {
    wh::Filters::HttpBaseProtocolFilter filter;
    filter.CacheControl().ReadBehavior(wh::Filters::HttpCacheReadBehavior::NoCache);
    filter.CacheControl().WriteBehavior(wh::Filters::HttpCacheWriteBehavior::NoCache);
    wh::HttpClient client(filter);
    auto headers = client.DefaultRequestHeaders();
    headers.UserAgent().TryParseAdd(L"CopilotReel/" + version);
    headers.Accept().TryParseAdd(accept);
    headers.TryAppendWithoutValidation(L"X-GitHub-Api-Version", L"2022-11-28");
    auto request = client.GetAsync(wf::Uri(url), wh::HttpCompletionOption::ResponseHeadersRead);
    if (request.wait_for(deadline) == wf::AsyncStatus::Started) {
        request.Cancel();
        throw std::runtime_error("the request timed out");
    }
    auto response = request.GetResults();
    if (!response.IsSuccessStatusCode()) {
        const int status = static_cast<int>(response.StatusCode());
        throw std::runtime_error("the server answered HTTP " + std::to_string(status) +
            (status == 403 || status == 429 ? " (GitHub's rate limit for this address; the next check retries)" : ""));
    }
    return response;
}

template <class Operation> auto Finish(Operation&& operation, std::chrono::seconds deadline) {
    if (operation.wait_for(deadline) == wf::AsyncStatus::Started) {
        operation.Cancel();
        throw std::runtime_error("the transfer timed out");
    }
    return operation.GetResults();
}

// Why Windows refuses a package signed with a certificate it does not trust, in words a user can act on.
std::wstring TrustAdvice(HRESULT code) {
    if (code == CERT_E_UNTRUSTEDROOT || code == CERT_E_CHAINING || code == static_cast<HRESULT>(TRUST_E_NOSIGNATURE))
        return L"; this computer does not trust the package's certificate: import the CopilotReel-Development.cer of that release";
    return {};
}

std::wstring FileUri(const std::wstring& path) {
    std::wstring uri = L"file:///";
    for (const wchar_t c : path) {
        if (c == L'\\') uri += L'/';
        else if (std::iswalnum(c) || wcschr(L"-._~/:", c)) uri += c;
        else {
            // Percent-encode the character's UTF-8 bytes.
            char utf8[8]{};
            const int count = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr, nullptr);
            for (int i = 0; i < count; ++i) {
                wchar_t escaped[4]{};
                swprintf_s(escaped, L"%%%02X", static_cast<unsigned char>(utf8[i]));
                uri += escaped;
            }
        }
    }
    return uri;
}

std::wstring ArchitectureName(APPX_PACKAGE_ARCHITECTURE architecture) {
    switch (architecture) {
    case APPX_PACKAGE_ARCHITECTURE_X64: return L"x64";
    case APPX_PACKAGE_ARCHITECTURE_ARM64: return L"arm64";
    case APPX_PACKAGE_ARCHITECTURE_X86: return L"x86";
    case APPX_PACKAGE_ARCHITECTURE_NEUTRAL: return L"neutral";
    default: return L"other";
    }
}

std::wstring StageName(UpdateStage stage) {
    switch (stage) {
    case UpdateStage::Checking: return L"checking";
    case UpdateStage::Current: return L"current";
    case UpdateStage::Available: return L"available";
    case UpdateStage::Installing: return L"installing";
    case UpdateStage::Failed: return L"failed";
    case UpdateStage::Unavailable: return L"unavailable";
    default: return L"idle";
    }
}
}

std::optional<PackageVersion> ParseVersion(std::wstring_view text) {
    if (!text.empty() && (text.front() == L'v' || text.front() == L'V')) text.remove_prefix(1);
    if (const auto dash = text.find(L'-'); dash != text.npos) text = text.substr(0, dash);
    unsigned parts[4]{};
    size_t count = 0;
    while (true) {
        if (count == 4 || text.empty() || !std::iswdigit(text.front())) return std::nullopt;
        unsigned value = 0;
        while (!text.empty() && std::iswdigit(text.front())) {
            value = value * 10 + (text.front() - L'0');
            if (value > 65535) return std::nullopt;
            text.remove_prefix(1);
        }
        parts[count++] = value;
        if (text.empty()) break;
        if (text.front() != L'.') return std::nullopt;
        text.remove_prefix(1);
    }
    if (count < 2) return std::nullopt;
    return PackageVersion{parts[0], parts[1], parts[2], parts[3]};
}

std::wstring VersionText(const PackageVersion& version) {
    auto text = std::to_wstring(version.major) + L"." + std::to_wstring(version.minor) + L"." + std::to_wstring(version.build);
    if (version.revision) text += L"." + std::to_wstring(version.revision);
    return text;
}

std::optional<UpdateOffer> NewestUpdate(std::wstring_view json, const PackageVersion& current,
    std::wstring_view architecture, bool loopback) {
    namespace wj = winrt::Windows::Data::Json;
    const auto text = [](const wj::JsonObject& object, const wchar_t* key) -> std::wstring {
        if (!object.HasKey(key)) return {};
        const auto value = object.GetNamedValue(key);
        return value.ValueType() == wj::JsonValueType::String ? std::wstring(value.GetString()) : std::wstring();
    };
    std::optional<UpdateOffer> best;
    for (const auto& entry : wj::JsonArray::Parse(winrt::hstring(json))) {
        if (entry.ValueType() != wj::JsonValueType::Object) continue;
        const auto release = entry.GetObject();
        if (release.HasKey(L"draft") && release.GetNamedValue(L"draft").ValueType() == wj::JsonValueType::Boolean &&
            release.GetNamedBoolean(L"draft")) continue;
        const auto tag = text(release, L"tag_name");
        const auto version = ParseVersion(tag);
        if (!version || *version <= current || (best && *version <= best->version)) continue;
        if (!release.HasKey(L"assets") || release.GetNamedValue(L"assets").ValueType() != wj::JsonValueType::Array) continue;
        for (const auto& item : release.GetNamedArray(L"assets")) {
            if (item.ValueType() != wj::JsonValueType::Object) continue;
            const auto asset = item.GetObject();
            const auto name = text(asset, L"name");
            // CopilotReel_<version>_<architecture>.msix, with the release's own version.
            const std::wstring prefix = L"CopilotReel_", suffix = std::wstring(L"_") + std::wstring(architecture) + L".msix";
            if (name.size() <= prefix.size() + suffix.size() || !name.starts_with(prefix) || !name.ends_with(suffix)) continue;
            // Only letters, digits, dots, dashes and underscores: an asset name is shown and logged.
            if (std::any_of(name.begin(), name.end(), [](wchar_t c) { return !(std::iswalnum(c) || c == L'.' || c == L'-' || c == L'_') || c > 127; }))
                continue;
            const auto named = ParseVersion(std::wstring_view(name).substr(prefix.size(), name.size() - prefix.size() - suffix.size()));
            if (!named || *named != *version) continue;
            const auto url = text(asset, L"browser_download_url");
            if (!url.starts_with(L"https://") && !(loopback && url.starts_with(L"http://127.0.0.1:"))) continue;
            UpdateOffer offer{*version, tag, name, url};
            if (const auto digest = Lower(text(asset, L"digest")); digest.starts_with(L"sha256:") && digest.size() == 7 + 64)
                offer.sha256 = digest.substr(7);
            if (asset.HasKey(L"size") && asset.GetNamedValue(L"size").ValueType() == wj::JsonValueType::Number)
                offer.size = static_cast<unsigned long long>((std::max)(0.0, asset.GetNamedNumber(L"size")));
            best = offer;
            break;
        }
    }
    return best;
}

std::wstring FileSha256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::wstring result;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return result;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<BYTE> buffer(1 << 16);
        DWORD read = 0;
        bool ok = true;
        while ((ok = ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != FALSE) && read)
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) { ok = false; break; }
        BYTE digest[32]{};
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) result = Hex(digest, sizeof(digest), false);
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

PackageCheck CheckPackage(const std::wstring& path) {
    PackageCheck check;
    // The signature, with Windows' own trust decision: the certificate must be trusted here.
    WINTRUST_FILE_INFO file{sizeof(file)};
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{sizeof(data)};
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG trust = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    if (trust == ERROR_SUCCESS) {
        if (const auto* provider = WTHelperProvDataFromStateData(data.hWVTStateData))
            if (const auto* signer = WTHelperGetProvSignerFromChain(const_cast<CRYPT_PROVIDER_DATA*>(provider), 0, FALSE, 0);
                signer && signer->csCertChain && signer->pasCertChain[0].pCert) {
                BYTE thumbprint[20]{};
                DWORD size = sizeof(thumbprint);
                if (CertGetCertificateContextProperty(signer->pasCertChain[0].pCert, CERT_SHA1_HASH_PROP_ID, thumbprint, &size))
                    check.signer = Hex(thumbprint, size, true);
            }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    if (trust != ERROR_SUCCESS) {
        wchar_t code[16]{};
        swprintf_s(code, L"0x%08X", static_cast<unsigned>(trust));
        check.error = std::wstring(L"the signature is not trusted (") + code + L")" + TrustAdvice(trust);
        return check;
    }
    if (std::find(std::begin(kTrustedSigners), std::end(kTrustedSigners), check.signer) == std::end(kTrustedSigners)) {
        check.error = L"the signer " + (check.signer.empty() ? std::wstring(L"is unknown") : check.signer + L" is not the reel's");
        return check;
    }
    // The identity from the package's own manifest.
    try {
        winrt::com_ptr<IStream> stream;
        winrt::check_hresult(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, stream.put()));
        const auto factory = winrt::create_instance<IAppxFactory>(__uuidof(AppxFactory));
        winrt::com_ptr<IAppxPackageReader> reader;
        winrt::check_hresult(factory->CreatePackageReader(stream.get(), reader.put()));
        winrt::com_ptr<IAppxManifestReader> manifest;
        winrt::check_hresult(reader->GetManifest(manifest.put()));
        winrt::com_ptr<IAppxManifestPackageId> id;
        winrt::check_hresult(manifest->GetPackageId(id.put()));
        LPWSTR name = nullptr, publisher = nullptr;
        UINT64 version = 0;
        APPX_PACKAGE_ARCHITECTURE architecture{};
        winrt::check_hresult(id->GetName(&name));
        check.identity.name = name;
        CoTaskMemFree(name);
        winrt::check_hresult(id->GetPublisher(&publisher));
        check.identity.publisher = publisher;
        CoTaskMemFree(publisher);
        winrt::check_hresult(id->GetVersion(&version));
        winrt::check_hresult(id->GetArchitecture(&architecture));
        check.identity.version = {static_cast<unsigned>(version >> 48 & 0xFFFF), static_cast<unsigned>(version >> 32 & 0xFFFF),
            static_cast<unsigned>(version >> 16 & 0xFFFF), static_cast<unsigned>(version & 0xFFFF)};
        check.identity.architecture = ArchitectureName(architecture);
    } catch (const winrt::hresult_error& error) {
        check.error = L"the package cannot be read: " + Message(error);
    }
    return check;
}

std::wstring IdentityMismatch(const PackageIdentity& candidate, const PackageIdentity& running, const PackageVersion& offered) {
    if (candidate.name != running.name) return L"the package is " + candidate.name + L", not " + running.name;
    if (candidate.publisher != running.publisher) return L"the publisher is " + candidate.publisher + L", not " + running.publisher;
    if (candidate.architecture != running.architecture) return L"the package is for " + candidate.architecture + L", not " + running.architecture;
    if (candidate.version != offered) return L"the package is " + VersionText(candidate.version) + L", not the offered " + VersionText(offered);
    if (candidate.version <= running.version) return L"the package is not newer than " + VersionText(running.version);
    return {};
}

struct Updater::Shared {
    HWND window = nullptr;
    UINT message = 0;
    mutable std::mutex lock;
    UpdateSnapshot snapshot;
    bool busy = false, verified = false;
    std::optional<PackageIdentity> running;  // none: nothing to update
    std::wstring feed;
    bool dryRun = false;                      // a test instance stops after verifying
    // Worker side: where packages are downloaded, resolved on first use, and whether the folder was
    // cleared of an earlier update's package.
    std::wstring downloads;
    bool cleared = false;
    // A verified package waiting for the reel to be idle before Windows installs it.
    std::wstring prepared;

    void Changed() { PostMessageW(window, message, 0, 0); }
    // The package manager reads the file outside this package's AppData redirection, so a package
    // downloads to its real LocalCache folder.
    const std::wstring& Downloads() {
        if (downloads.empty()) {
            if (dryRun) {
                const auto config = ConfigPath();
                downloads = config.substr(0, config.find_last_of(L"\\/")) + L"\\updates";
            } else {
                downloads = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalCacheFolder().Path()) + L"\\Updates";
            }
        }
        if (!cleared) {
            // A package left from an earlier update has done its work.
            std::error_code ignored;
            std::filesystem::remove_all(downloads, ignored);
            cleared = true;
        }
        return downloads;
    }
    std::optional<UpdateOffer> FetchOffer() {
        const auto response = Get(feed, VersionText(running->version), L"application/vnd.github+json", 30s);
        return NewestUpdate(Finish(response.Content().ReadAsStringAsync(), 30s), running->version, running->architecture, dryRun);
    }
    // Runs `work` on a worker thread with its own apartment; false when no thread could start.
    template <class Work> static bool Run(const std::shared_ptr<Shared>& shared, Work work) {
        try {
            std::thread([shared, work] {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                work(*shared);
                shared->Changed();
                winrt::uninit_apartment();
            }).detach();
            return true;
        } catch (...) {
            std::lock_guard guard(shared->lock);
            shared->busy = false;
            shared->snapshot.stage = UpdateStage::Failed;
            shared->snapshot.error = L"no worker thread";
            return false;
        }
    }
};

namespace {
std::wstring Failure() {
    try { throw; }
    catch (const winrt::hresult_error& failure) { return Message(failure) + TrustAdvice(failure.code()); }
    catch (const std::exception& failure) { return winrt::to_hstring(failure.what()).c_str(); }
    catch (...) { return L"the update failed"; }
}
}

Updater::Updater(HWND window, UINT message) : shared_(std::make_shared<Shared>()) {
    auto& s = *shared_;
    s.window = window;
    s.message = message;
    s.snapshot.checkedAt = CurrentUpdatePreferences().lastCheck;
    // The package identity from the loader, which costs nothing at a cold start.
    UINT32 size = 0;
    if (GetCurrentPackageId(&size, nullptr) == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<BYTE> buffer(size);
        if (GetCurrentPackageId(&size, buffer.data()) == ERROR_SUCCESS) {
            const auto* id = reinterpret_cast<const PACKAGE_ID*>(buffer.data());
            const auto architecture = id->processorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? L"arm64" :
                id->processorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"x64" : L"other";
            s.running = PackageIdentity{id->name, id->publisher, architecture,
                {id->version.Major, id->version.Minor, id->version.Build, id->version.Revision}};
            s.feed = kFeed;
        }
    } else if (const auto feed = Environment(L"COPILOT_REEL_TEST_UPDATE_FEED"); CurrentTestInput().active && !feed.empty()) {
        // Only a seam test instance may name its own feed and version; it never installs.
        const auto version = ParseVersion(Environment(L"COPILOT_REEL_TEST_VERSION"));
        s.running = PackageIdentity{kPackageName, kPublisher, kBuiltArchitecture, version.value_or(PackageVersion{})};
        s.feed = feed;
        s.dryRun = true;
    }
    if (!s.running) s.snapshot.stage = UpdateStage::Unavailable;
}

Updater::~Updater() = default;

UpdateSnapshot Updater::Snapshot() const {
    std::lock_guard guard(shared_->lock);
    return shared_->snapshot;
}

bool Updater::Prepared() const {
    std::lock_guard guard(shared_->lock);
    return !shared_->prepared.empty();
}

std::wstring ExecutableVersion() {
    wchar_t module[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return {};
    DWORD ignored = 0;
    std::vector<BYTE> info(GetFileVersionInfoSizeW(module, &ignored));
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT size = 0;
    if (info.empty() || !GetFileVersionInfoW(module, 0, static_cast<DWORD>(info.size()), info.data()) ||
        !VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&fixed), &size) || !fixed) return {};
    return VersionText({HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
        HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS)});
}

std::wstring Updater::CurrentVersion() const {
    if (shared_->running) return VersionText(shared_->running->version);
    // A development build names the version in its own resources.
    return ExecutableVersion();
}

bool Updater::Check() {
    {
        std::lock_guard guard(shared_->lock);
        if (!shared_->running || shared_->busy) return false;
        shared_->busy = true;
        shared_->snapshot.stage = UpdateStage::Checking;
    }
    shared_->Changed();
    return Shared::Run(shared_, [](Shared& s) {
        std::optional<UpdateOffer> offer;
        std::wstring error;
        try { s.Downloads(); offer = s.FetchOffer(); } catch (...) { error = Failure(); }
        std::lock_guard guard(s.lock);
        auto& snapshot = s.snapshot;
        snapshot.error = error;
        if (error.empty()) {
            snapshot.offer = offer;
            snapshot.checkedAt = UnixNow();
            snapshot.stage = offer ? UpdateStage::Available : UpdateStage::Current;
            s.verified = false;
        } else {
            // A failed check keeps an update it found before.
            snapshot.stage = snapshot.offer ? UpdateStage::Available : UpdateStage::Failed;
        }
        s.busy = false;
    });
}

bool Updater::Install() {
    {
        std::lock_guard guard(shared_->lock);
        if (!shared_->running || shared_->busy || !shared_->snapshot.offer) return false;
        shared_->busy = true;
        shared_->snapshot.stage = UpdateStage::Installing;
        shared_->snapshot.error.clear();
    }
    shared_->Changed();
    return Shared::Run(shared_, [](Shared& s) {
        std::wstring error;
        bool verified = false, prepared = false;
        std::optional<UpdateOffer> offer;
        try {
            // GitHub is asked again: a release can have been replaced or re-signed since the last check.
            offer = s.FetchOffer();
            {
                std::lock_guard guard(s.lock);
                s.snapshot.offer = offer;
                s.snapshot.checkedAt = UnixNow();
            }
            if (offer) {
                // A fixed name: the asset's own name never becomes a path.
                const auto path = s.Downloads() + L"\\CopilotReel-update.msix", partial = path + L".partial";
                std::filesystem::create_directories(s.downloads);
                if (offer->size > kMaxPackage) throw std::runtime_error("the package is too large");
                const auto response = Get(offer->url, VersionText(s.running->version), L"application/octet-stream", 60s);
                if (const auto length = response.Content().Headers().ContentLength(); length && length.Value() > kMaxPackage)
                    throw std::runtime_error("the package is too large");
                const auto buffer = Finish(response.Content().ReadAsBufferAsync(), 300s);
                if (buffer.Length() > kMaxPackage || (offer->size && buffer.Length() != offer->size))
                    throw std::runtime_error("the download has the wrong size");
                HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("the download cannot be saved");
                DWORD written = 0;
                const bool saved = WriteFile(file, buffer.data(), buffer.Length(), &written, nullptr) && written == buffer.Length();
                CloseHandle(file);
                if (!saved || !MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
                    throw std::runtime_error("the download cannot be saved");
                if (!offer->sha256.empty() && FileSha256(path) != offer->sha256)
                    throw std::runtime_error("the download does not match GitHub's SHA-256");
                const auto check = CheckPackage(path);
                if (!check.error.empty()) throw std::runtime_error(winrt::to_string(check.error));
                if (const auto mismatch = IdentityMismatch(check.identity, *s.running, offer->version); !mismatch.empty())
                    throw std::runtime_error(winrt::to_string(mismatch));
                verified = true;
                if (!s.dryRun) {
                    // Windows restarts only a process that has run for 60 seconds, so a fresh one waits
                    // before the package is handed over.
                    FILETIME created{}, exited{}, kernel{}, user{}, now{};
                    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
                    GetSystemTimeAsFileTime(&now);
                    const auto ticks = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
                    const auto uptime = std::chrono::milliseconds((ticks(now) - ticks(created)) / 10000);
                    if (uptime < 70s) std::this_thread::sleep_for(70s - uptime);
                    std::lock_guard guard(s.lock);
                    s.prepared = path;
                    prepared = true;
                }
            }
        } catch (...) { error = Failure(); }
        std::lock_guard guard(s.lock);
        s.verified = verified;
        s.snapshot.error = error;
        // A prepared package waits for Commit; a test instance stops with the package verified.
        s.snapshot.stage = !error.empty() ? UpdateStage::Failed : prepared ? UpdateStage::Installing :
            offer ? UpdateStage::Available : UpdateStage::Current;
        s.busy = prepared;
    });
}

bool Updater::Commit() {
    std::wstring path;
    {
        std::lock_guard guard(shared_->lock);
        if (shared_->prepared.empty()) return false;
        path = std::exchange(shared_->prepared, std::wstring());
    }
    return Shared::Run(shared_, [path](Shared& s) {
        std::wstring error;
        try {
            // Nothing may change the verified file while Windows reads it.
            const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("the verified package is gone");
            struct Held { HANDLE file; ~Held() { CloseHandle(file); UnregisterApplicationRestart(); } } held{file};
            // The package manager closes every process of this package, updates it and, since the reel
            // registered for restart, starts it again. The call returns only when the update did not happen.
            RegisterApplicationRestart(L"--background", RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT);
            winrt::Windows::Management::Deployment::PackageManager manager;
            const auto result = manager.AddPackageAsync(wf::Uri(FileUri(path)), nullptr,
                winrt::Windows::Management::Deployment::DeploymentOptions::ForceApplicationShutdown).get();
            error = FAILED(result.ExtendedErrorCode()) ?
                std::wstring(result.ErrorText()) + L" (" + Message(winrt::hresult_error(result.ExtendedErrorCode())) + L")" +
                    TrustAdvice(result.ExtendedErrorCode()) :
                L"Windows accepted the package but did not update the running reel";
        } catch (...) { error = Failure(); }
        std::lock_guard guard(s.lock);
        s.snapshot.error = error;
        s.snapshot.stage = UpdateStage::Failed;
        s.busy = false;
    });
}

std::wstring Updater::Diagnostics() const {
    std::lock_guard guard(shared_->lock);
    const auto& s = shared_->snapshot;
    std::wstring text = L"updateStage=" + StageName(s.stage) +
        L"\r\nupdateOffer=" + (s.offer ? VersionText(s.offer->version) + L"|" + s.offer->asset : std::wstring()) +
        L"\r\nupdateVerified=" + (shared_->verified ? L"1" : L"0") +
        L"\r\nupdatePrepared=" + (shared_->prepared.empty() ? L"0" : L"1") +
        L"\r\nupdateError=" + s.error +
        L"\r\nupdateCheckedAt=" + std::to_wstring(s.checkedAt) +
        L"\r\nupdateRunning=" + (shared_->running ? VersionText(shared_->running->version) + L"|" + shared_->running->architecture : std::wstring()) +
        L"\r\nupdateFeed=" + shared_->feed;
    return text;
}
}
