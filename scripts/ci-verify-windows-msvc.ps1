# Post-vcvars / post-SDK-append sanity check for Windows release CI.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot/ci-windows-sdk-lib-common.ps1"

Get-Command cl -ErrorAction Stop | Out-String | Write-Host
Get-Command link -ErrorAction Stop | Out-String | Write-Host

$sdk = Get-WindowsSdkKsguidLib
if (-not $sdk) {
    throw "ksguid.lib not found after ci-append-windows-sdk-lib.ps1"
}
Write-Host "ksguid.lib: $($sdk.Ksguid)"

if ($env:SSB64_KSGUID_LIB -and (Test-Path -LiteralPath $env:SSB64_KSGUID_LIB)) {
    Write-Host "SSB64_KSGUID_LIB (GITHUB_ENV): $env:SSB64_KSGUID_LIB"
}
