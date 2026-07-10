# Appends the directory containing ksguid.lib to LIB (GITHUB_ENV) for Windows release CI.
# v3: scan all SDK versions; optional VS SDK component install; respects existing LIB from vcvars.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot/ci-windows-sdk-lib-common.ps1"

Write-Host "ci-append-windows-sdk-lib.ps1 (multi-SDK scan + install fallback)"

$sdk = Ensure-WindowsSdkKsguidLib
$lib = if ($env:LIB) { "$($sdk.Um);$env:LIB" } else { $sdk.Um }
Add-Content -LiteralPath $env:GITHUB_ENV -Value "LIB=$lib"
Add-Content -LiteralPath $env:GITHUB_ENV -Value "SSB64_KSGUID_LIB=$($sdk.Ksguid)"
Write-Host "Windows SDK version: $($sdk.SdkVersion)"
Write-Host "Windows SDK um\x64 on LIB: $($sdk.Um)"
Write-Host "ksguid.lib: $($sdk.Ksguid)"
