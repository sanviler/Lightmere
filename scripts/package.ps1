# Makes the portable Windows ZIP after a build.
# The release workflow passes the tag version (for example v0.8.0).
# For local builds, the version is read from CMakeLists.txt.
param(
    [string]$BuildDir = "build/Release",
    [string]$OutDir = "dist",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionNumber = (Select-String -Path CMakeLists.txt -Pattern 'project\(Lightmere VERSION ([0-9.]+)').Matches[0].Groups[1].Value
    $Version = "v$versionNumber"
}
elseif (-not $Version.StartsWith("v")) {
    $Version = "v$Version"
}

$exe = Join-Path $BuildDir "lightmere.exe"
if (-not (Test-Path $exe)) {
    throw "$exe not found: build the Release configuration first"
}

$stage = Join-Path $OutDir "Lightmere"
if (Test-Path $stage) {
    Remove-Item -Recurse -Force $stage
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe, README.md, LICENSE, CHANGELOG.md -Destination $stage

$zip = Join-Path $OutDir "Lightmere-$Version-Windows-x64.zip"
if (Test-Path $zip) {
    Remove-Item -Force $zip
}
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"
