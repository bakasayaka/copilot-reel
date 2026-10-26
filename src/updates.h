#pragma once
#include "app_types.h"

#include <compare>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// In-app updates from the GitHub releases of bakasayaka/copilot-reel. A check asks the releases API
// (prereleases included, since every release so far is one) for the newest release whose package
// for this architecture is newer than the running one. An install downloads that package, checks
// its SHA-256 against GitHub's digest, its signature against the development certificate and its
// identity against this package, then hands it to the Windows package manager, which closes the
// reel, updates it and starts it again.
namespace reel {
struct PackageVersion {
    unsigned major = 0, minor = 0, build = 0, revision = 0;
    auto operator<=>(const PackageVersion&) const = default;
};
// "0.3.8", "0.3.8.0", "v0.3.8-beta": two to four dot-separated numbers below 65536, an optional
// leading v and an optional "-suffix".
std::optional<PackageVersion> ParseVersion(std::wstring_view text);
// How versions are shown: "0.3.8" for 0.3.8.0, "0.3.8.1" otherwise.
std::wstring VersionText(const PackageVersion& version);
// The version in the executable's own resources (packaging/app.rc), as VersionText shows it, or
// empty. The package carries the same number (tools/check-versions.ps1).
std::wstring ExecutableVersion();

struct UpdateOffer {
    PackageVersion version;
    std::wstring tag, asset, url;
    std::wstring sha256;              // lowercase hex from GitHub's asset digest; empty when it has none
    unsigned long long size = 0;
};
// The newest release that is not a draft, is newer than `current` and has the asset
// CopilotReel_<version>_<architecture>.msix (ASCII letters, digits, '.', '-', '_' only), where
// <version> is the release's own version,
// downloaded over HTTPS (or plain HTTP from 127.0.0.1 when `loopback`, for a test feed).
// `json` is the body of GET /repos/{owner}/{repo}/releases. Throws on malformed JSON.
std::optional<UpdateOffer> NewestUpdate(std::wstring_view json, const PackageVersion& current,
    std::wstring_view architecture, bool loopback = false);

struct PackageIdentity {
    std::wstring name, publisher, architecture;  // architecture: "x64" or "arm64"
    PackageVersion version;
};
// The signer and identity of a downloaded package. `error` is empty when `signer` is trusted and
// the identity could be read.
struct PackageCheck { std::wstring error, signer; PackageIdentity identity; };
// Lowercase hex SHA-256 of a file; empty when it cannot be read.
std::wstring FileSha256(const std::wstring& path);
// The signature must verify with Windows trust and come from one of the trusted thumbprints.
PackageCheck CheckPackage(const std::wstring& path);
// Why `candidate` may not replace `running`, or empty when it may: the same name, publisher
// and architecture, and a higher version than the running package and exactly `offered`.
std::wstring IdentityMismatch(const PackageIdentity& candidate, const PackageIdentity& running,
    const PackageVersion& offered);
// SHA-1 thumbprints, upper-case hex, of certificates whose packages may update the reel.
// A replacement certificate is added here one release before packages are signed with it.
inline constexpr std::wstring_view kTrustedSigners[] = {L"061ED5990CEBC4F2D7C252ABE3EFE7EB99D6239F"};

struct UpdateSnapshot {
    UpdateStage stage = UpdateStage::Idle;
    std::optional<UpdateOffer> offer;
    std::wstring error;     // the last failure, for the log and diagnostics
    long long checkedAt = 0; // Unix seconds of the last successful check, or 0
};

// Runs checks and installs on worker threads, one at a time, and posts `message` to `window`
// after every change. Snapshot is safe from any thread. Without a package (a development build)
// it reports Unavailable, unless a test instance points it at a feed of its own, where an
// install stops after verifying the package.
class Updater {
public:
    Updater(HWND window, UINT message);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    UpdateSnapshot Snapshot() const;
    // The running version, as the About page shows it.
    std::wstring CurrentVersion() const;
    // False while a check or an install runs, or without a package. Install asks GitHub again,
    // downloads and verifies the newest package and then waits, Prepared, until the reel is idle.
    bool Check();
    bool Install();
    // A verified package waits; Commit hands it to Windows, which closes and restarts the reel.
    bool Prepared() const;
    bool Commit();
    std::wstring Diagnostics() const;
private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};
}
