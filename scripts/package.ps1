# Makes the portable Windows zip after a build:
#   cmake --preset windows-vcpkg
#   cmake --build --preset release
#   ./scripts/package.ps1
# Result: dist/lightmere-<version>-windows-x64.zip with lightmere.exe, README.md, LICENSE and CHANGELOG.md.
# The zip contains no game files: the user copies the four ADF images next to lightmere.exe.
param(
    [string]$BuildDir = "build/Release",
    [string]$OutDir = "dist"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(Lightmere VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$exe = Join-Path $BuildDir "lightmere.exe"
if (-not (Test-Path $exe)) { throw "$exe not found: build the Release configuration first" }
$stage = Join-Path $OutDir "lightmere"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe, README.md, LICENSE, CHANGELOG.md -Destination $stage
$zip = Join-Path $OutDir "lightmere-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"
