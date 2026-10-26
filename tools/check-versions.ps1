# The version is typed in four files: CMakeLists.txt, packaging\app.rc (the executable's resources,
# which About, the quick menu and diagnostics read in a development build), packaging\app.manifest
# and packaging\AppxManifest.xml (the package, which Windows and the updater read). Throws, naming
# each copy, when they differ; tools\test.ps1 and tools\package.ps1 run it.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Read-Text([string]$Path) { [IO.File]::ReadAllText((Join-Path $root $Path)) }
# "0.3.12", "0,3,12,0" and "0.3.12.0" are one version.
function Format-Version([string]$Text) {
    $parts = @($Text -split '[.,]' | ForEach-Object { [int]$_.Trim() })
    while ($parts.Count -lt 4) { $parts += 0 }
    $parts[0..3] -join '.'
}
$copies = [ordered]@{}
$patterns = @(
    @('CMakeLists.txt', 'project\(CopilotReel VERSION ([0-9.]+)', 'CMakeLists.txt project'),
    @('packaging/app.rc', '(?m)^\s*FILEVERSION\s+([0-9 ,]+?)\s*$', 'app.rc fixed FILEVERSION'),
    @('packaging/app.rc', '(?m)^\s*PRODUCTVERSION\s+([0-9 ,]+?)\s*$', 'app.rc fixed PRODUCTVERSION'),
    @('packaging/app.rc', 'VALUE "FileVersion", "([0-9.]+)', 'app.rc FileVersion string'),
    @('packaging/app.rc', 'VALUE "ProductVersion", "([0-9.]+)', 'app.rc ProductVersion string'),
    @('packaging/app.manifest', '<assemblyIdentity version="([0-9.]+)"', 'app.manifest assemblyIdentity'),
    @('packaging/AppxManifest.xml', '<Identity [^>]*Version="([0-9.]+)"', 'AppxManifest.xml Identity'))
foreach ($pattern in $patterns) {
    $match = [regex]::Match((Read-Text $pattern[0]), $pattern[1])
    if (-not $match.Success) { throw "No version found for $($pattern[2]) in $($pattern[0])." }
    $copies[$pattern[2]] = Format-Version $match.Groups[1].Value
}
$versions = @($copies.Values | Sort-Object -Unique)
if ($versions.Count -ne 1) {
    $copies.GetEnumerator() | ForEach-Object { Write-Output ("{0}: {1}" -f $_.Key, $_.Value) }
    throw "The version copies differ: $($versions -join ', ')."
}
Write-Output ("Version {0} in all {1} copies." -f $versions[0], $copies.Count)
