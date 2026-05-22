# Appends Windows Kits um\x64 to LIB for subsequent GHA steps (GITHUB_ENV).
# msvc-dev-cmd often omits um\x64 from LIB; link then cannot resolve ksguid.lib unless
# CMake embeds a full path (WindowsSdkUmLib.cmake). This keeps find_library working at configure.
$ErrorActionPreference = "Stop"

$kits = "C:\Program Files (x86)\Windows Kits\10\Lib"
if (-not (Test-Path -LiteralPath $kits)) {
    throw "Windows SDK not found at $kits"
}

$ver = Get-ChildItem -LiteralPath $kits -Directory |
    Where-Object { $_.Name -match "^10\." } |
    Sort-Object Name -Descending |
    Select-Object -First 1

if (-not $ver) {
    throw "No Windows SDK 10.x lib folder under $kits"
}

$um = Join-Path $ver.FullName "um\x64"
$ksguid = Join-Path $um "ksguid.lib"
if (-not (Test-Path -LiteralPath $ksguid)) {
    throw "ksguid.lib not found at $ksguid"
}

$lib = if ($env:LIB) { "$um;$env:LIB" } else { $um }
Add-Content -LiteralPath $env:GITHUB_ENV -Value "LIB=$lib"
Write-Host "Windows SDK um\x64 on LIB: $um"
Write-Host "ksguid.lib: $ksguid"
