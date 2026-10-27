[CmdletBinding()]
param([switch]$VerifyOnly)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Needed by Windows PowerShell 5.1 on machines with older .NET defaults.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$manifestPath = Join-Path $PSScriptRoot 'toolchain-manifest.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$localRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'local'))
$downloadRoot = Join-Path $localRoot 'downloads'
$targets = @($manifest.targets)
if ($manifest.schemaVersion -ne 1 -or $manifest.architecture -ne 'x64' -or $targets -notcontains 'x64' -or $targets -notcontains 'arm64') {
    throw 'Unsupported toolchain manifest. Expected an x64 host with x64 and arm64 targets.'
}
if (-not $VerifyOnly) { New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null }

function Get-ContainedPath([string]$Root, [string]$Relative) {
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
    $candidate = [IO.Path]::GetFullPath([IO.Path]::Combine($prefix, $Relative.Replace('/', '\')))
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        $Relative.Contains(':') -or [IO.Path]::IsPathRooted($Relative)) {
        throw "Path escapes its toolchain directory: $Relative"
    }
    return $candidate
}

function Test-ArchiveHash([string]$Path, [string]$Hash) {
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and
        ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Hash)
}

foreach ($package in $manifest.packages) {
    $uri = [Uri]$package.url
    if ($uri.Scheme -ne 'https' -or $uri.Host -notin @('download.visualstudio.microsoft.com', 'api.nuget.org')) {
        throw "Unexpected package origin: $($package.url)"
    }
    if ($package.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Missing SHA-256 for $($package.id)" }
    $archivePath = Get-ContainedPath $downloadRoot $package.file
    if (-not (Test-ArchiveHash $archivePath $package.sha256)) {
        if ($VerifyOnly) { throw "Missing or modified archive: $archivePath" }
        Write-Host "Downloading $($package.id) $($package.version)"
        $partialPath = $archivePath + '.partial'
        Invoke-WebRequest -UseBasicParsing -Uri $uri.AbsoluteUri -OutFile $partialPath
        if (-not (Test-ArchiveHash $partialPath $package.sha256)) {
            throw "SHA-256 mismatch; package has not been extracted: $partialPath"
        }
        Move-Item -LiteralPath $partialPath -Destination $archivePath -Force
    }
    Write-Host "Verified $($package.id)"
    if ($VerifyOnly) { continue }
    $destination = Get-ContainedPath $localRoot $package.destination
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        foreach ($entry in $archive.Entries) {
            if (-not $entry.Name) { continue }
            if ($package.stripPrefix -and -not $entry.FullName.StartsWith($package.stripPrefix, [StringComparison]::Ordinal)) { continue }
            $relative = $entry.FullName.Substring($package.stripPrefix.Length)
            $target = Get-ContainedPath $destination $relative
            # Re-running bootstrap must not replace identical DLLs used by an active compiler.
            if ((Test-Path -LiteralPath $target -PathType Leaf) -and (Get-Item -LiteralPath $target).Length -eq $entry.Length) {
                $stream = $entry.Open()
                $hasher = [Security.Cryptography.SHA256]::Create()
                try { $entryHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
                finally { $stream.Dispose(); $hasher.Dispose() }
                if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $entryHash) { continue }
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally { $archive.Dispose() }
}

$compilerRoot = Join-Path $localRoot $manifest.msvcDirectory
$sdkRoot = Join-Path $localRoot 'sdk\c'
$required = @(
    (Join-Path $compilerRoot 'bin\Hostx64\x64\cl.exe'),
    (Join-Path $compilerRoot 'bin\Hostx64\x64\link.exe'),
    (Join-Path $compilerRoot 'bin\Hostx64\arm64\cl.exe'),
    (Join-Path $compilerRoot 'bin\Hostx64\arm64\link.exe'),
    (Join-Path $compilerRoot 'include\vector'),
    (Join-Path $compilerRoot 'lib\x64\oldnames.lib'),
    (Join-Path $compilerRoot 'lib\arm64\oldnames.lib'),
    (Join-Path $compilerRoot 'lib\arm64\libcmt.lib'),
    (Join-Path $sdkRoot "Include\$($manifest.sdkVersion)\cppwinrt\winrt\Windows.UI.Composition.h"),
    (Join-Path $sdkRoot "bin\$($manifest.sdkVersion)\x64\rc.exe"),
    (Join-Path $sdkRoot "bin\$($manifest.sdkVersion)\x64\makeappx.exe"),
    (Join-Path $sdkRoot "bin\$($manifest.sdkVersion)\x64\signtool.exe"),
    (Join-Path $sdkRoot "bin\$($manifest.sdkVersion)\x64\makepri.exe"),
    (Join-Path $localRoot 'sdk-x64\c\um\x64\kernel32.lib'),
    (Join-Path $localRoot 'sdk-x64\c\ucrt\x64\libucrt.lib'),
    (Join-Path $localRoot 'sdk-arm64\c\um\arm64\kernel32.lib'),
    (Join-Path $localRoot 'sdk-arm64\c\um\arm64\d2d1.lib'),
    (Join-Path $localRoot 'sdk-arm64\c\um\arm64\dwrite.lib'),
    (Join-Path $localRoot 'sdk-arm64\c\um\arm64\WindowsApp.lib'),
    (Join-Path $localRoot 'sdk-arm64\c\ucrt\arm64\libucrt.lib')
)
foreach ($path in $required) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Toolchain is incomplete: $path" } }
Write-Host "Toolchain ready: MSVC 14.44 host x64, targets x64 and ARM64; Windows SDK $($manifest.sdkVersion)."
Write-Host 'No machine PATH, registry, certificate store, or installed applications were changed.'
