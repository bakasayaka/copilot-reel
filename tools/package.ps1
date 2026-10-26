param(
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [ValidateSet('x64','arm64')][string]$Architecture='x64',
    [string]$CertificateThumbprint
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$binary=if($Architecture -eq 'arm64') {
    Join-Path $projectRoot "build\arm64\$Configuration\CopilotReel.exe"
} else {
    Join-Path $projectRoot "build\$Configuration\CopilotReel.exe"
}
if(-not (Test-Path -LiteralPath $binary)) {throw 'Build the application with tools/build.ps1 first.'}
# One version in the sources, and the executable built from them carries it.
& (Join-Path $PSScriptRoot 'check-versions.ps1')
$sdkBin=Join-Path $PSScriptRoot 'local\sdk\c\bin\10.0.26100.0\x64'
$makeappx=Join-Path $sdkBin 'makeappx.exe'
$signtool=Join-Path $sdkBin 'signtool.exe'
$timestampServer='http://timestamp.digicert.com'
if(-not (Test-Path -LiteralPath $makeappx)) {throw 'Windows SDK packaging tool not found.'}
# Keep the ARM64 layout out of artifacts\package so it cannot replace a loose x64 registration directory.
$stageName=if($Architecture -eq 'arm64') {'package-arm64'} else {'package'}
$stage=Join-Path $projectRoot "artifacts\$stageName"
# Recreate only this exact staging directory, so removed assets cannot survive
# from an earlier package. Resolve and check the destination before deletion.
$stage=[System.IO.Path]::GetFullPath($stage)
$expectedStage=[System.IO.Path]::GetFullPath((Join-Path $projectRoot "artifacts\$stageName"))
if($stage -ne $expectedStage -or (Split-Path -Parent $stage) -ne [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts'))) {
    throw 'The package staging directory is outside the expected workspace location.'
}
if(Test-Path -LiteralPath $stage) {Remove-Item -LiteralPath $stage -Recurse -Force}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $stage 'CopilotReel.exe')
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\AppxManifest.xml') -Destination (Join-Path $stage 'AppxManifest.xml')
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\assets') -Destination $stage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\brands') -Destination (Join-Path $stage 'assets') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\fonts') -Destination (Join-Path $stage 'assets') -Recurse -Force
$manifestPath=Join-Path $stage 'AppxManifest.xml'
$manifestText=[System.IO.File]::ReadAllText($manifestPath)
if(-not $manifestText.Contains('ProcessorArchitecture="x64"')) {throw 'AppxManifest.xml has no x64 architecture token to set for this package.'}
$patched=$manifestText.Replace('ProcessorArchitecture="x64"', "ProcessorArchitecture=`"$Architecture`"")
$utf8=New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($manifestPath, $patched, $utf8)
$stream=[System.IO.File]::OpenRead($binary)
try {
    $header=New-Object byte[] 64
    if($stream.Read($header, 0, 64) -ne 64) {throw 'Executable header is truncated.'}
    $peOffset=[BitConverter]::ToInt32($header, 60)
    [void]$stream.Seek($peOffset, [System.IO.SeekOrigin]::Begin)
    $pe=New-Object byte[] 6
    if($stream.Read($pe, 0, 6) -ne 6 -or [System.Text.Encoding]::ASCII.GetString($pe, 0, 2) -ne 'PE') {throw 'Executable is not a PE image.'}
    $machine=[BitConverter]::ToUInt16($pe, 4)
} finally { $stream.Dispose() }
$expectedMachine=if($Architecture -eq 'arm64') {[uint16]0xAA64} else {[uint16]0x8664}
if($machine -ne $expectedMachine) {throw ("{0} is machine 0x{1:X4}; refusing to pack it as {2}." -f $binary, $machine, $Architecture)}
[xml]$manifest=Get-Content -LiteralPath $manifestPath
$version=$manifest.Package.Identity.Version
$fileVersion=[System.Diagnostics.FileVersionInfo]::GetVersionInfo($binary)
if(('{0}.{1}.{2}.{3}' -f $fileVersion.FileMajorPart,$fileVersion.FileMinorPart,$fileVersion.FileBuildPart,$fileVersion.FilePrivatePart) -ne $version) {
    throw "$binary is not version $version; build it again."
}
if($manifest.Package.Identity.ProcessorArchitecture -ne $Architecture) {throw 'Staged manifest architecture does not match the requested package.'}
$package=Join-Path $projectRoot "artifacts\CopilotReel_${version}_${Architecture}.msix"
& $makeappx pack /d $stage /p $package /o
if($LASTEXITCODE -ne 0) {throw 'MSIX validation/packing failed.'}
if($CertificateThumbprint) {
    # Without a timestamp the package stops installing once the certificate expires.
    & $signtool sign /fd SHA256 /tr $timestampServer /td SHA256 /sha1 $CertificateThumbprint $package
    if($LASTEXITCODE -ne 0) {throw 'MSIX signing failed. Certificate publisher must match the manifest Identity.'}
    & $signtool verify /pa $package
    if($LASTEXITCODE -ne 0) {throw 'Package signature chain verification failed.'}
} else {
    Write-Output 'Package is unsigned. Windows Copilot picker eligibility requires a signed, installed provider.'
}
Write-Output "Package: $package"
if($Architecture -eq 'arm64') {
    Write-Output "ARM64 layout is for package inspection only. Do not register it on an x64 host: $stage\AppxManifest.xml"
} else {
    Write-Output "Layout for development registration: $stage\AppxManifest.xml"
}
