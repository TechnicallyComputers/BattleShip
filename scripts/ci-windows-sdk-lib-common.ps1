# Shared helpers for Windows CI: locate ksguid.lib (WASAPI 5.1 GUIDs in libultraship).
$ErrorActionPreference = "Stop"

function Write-SdkDiag {
    param([string]$LibRoot)
    if (-not (Test-Path -LiteralPath $LibRoot)) {
        Write-Host "  (missing) $LibRoot"
        return
    }
    $vers = Get-ChildItem -LiteralPath $LibRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "^10\." } |
        Sort-Object Name -Descending
    Write-Host "  $LibRoot"
    foreach ($v in $vers) {
        $um = Join-Path $v.FullName "um\x64"
        $ks = Join-Path $um "ksguid.lib"
        $flag = if (Test-Path -LiteralPath $ks) { "OK" } else { "no ksguid" }
        Write-Host "    $($v.Name)\um\x64 -> $flag"
    }
}

function Find-KsguidOnLibEnv {
    if (-not $env:LIB) { return $null }
    foreach ($dir in ($env:LIB -split ';')) {
        if (-not $dir) { continue }
        $candidate = Join-Path $dir "ksguid.lib"
        if (Test-Path -LiteralPath $candidate) {
            return @{ Um = $dir; Ksguid = $candidate; SdkVersion = "LIB" }
        }
    }
    return $null
}

function Get-WindowsSdkKsguidLib {
    $hit = Find-KsguidOnLibEnv
    if ($hit) { return $hit }

    $kitRoots = @(
        "C:\Program Files (x86)\Windows Kits\10\Lib",
        "C:\Program Files\Windows Kits\10\Lib"
    )
    $legacy = @(
        "C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib\x64\ksguid.lib"
    )
    foreach ($legacyPath in $legacy) {
        if (Test-Path -LiteralPath $legacyPath) {
            $um = Split-Path -Parent $legacyPath
            return @{ Um = $um; Ksguid = $legacyPath; SdkVersion = "legacy" }
        }
    }

    foreach ($kits in $kitRoots) {
        if (-not (Test-Path -LiteralPath $kits)) { continue }
        $versions = Get-ChildItem -LiteralPath $kits -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match "^10\." } |
            Sort-Object Name -Descending
        foreach ($ver in $versions) {
            $um = Join-Path $ver.FullName "um\x64"
            $ksguid = Join-Path $um "ksguid.lib"
            if (Test-Path -LiteralPath $ksguid) {
                return @{ Um = $um; Ksguid = $ksguid; SdkVersion = $ver.Name }
            }
        }
        $any = Get-ChildItem -LiteralPath $kits -Recurse -Filter "ksguid.lib" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($any) {
            $um = Split-Path -Parent $any.FullName
            return @{ Um = $um; Ksguid = $any.FullName; SdkVersion = "recursive" }
        }
    }
    return $null
}

function Install-Windows10SdkComponent {
    param([string]$ComponentId)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $setup = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\setup.exe"
    if (-not (Test-Path -LiteralPath $vswhere) -or -not (Test-Path -LiteralPath $setup)) {
        Write-Host "   vswhere/setup not found; skip SDK install ($ComponentId)"
        return $false
    }
    $installPath = & $vswhere -latest -property installationPath
    if (-not $installPath) { return $false }
    Write-Host "   Installing VS component: $ComponentId"
    $proc = Start-Process -FilePath $setup -ArgumentList @(
        "modify",
        "--installPath", $installPath,
        "--add", $ComponentId,
        "--quiet", "--norestart", "--wait"
    ) -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        Write-Host "   VS modify exit code: $($proc.ExitCode)"
        return $false
    }
    return $true
}

function Ensure-WindowsSdkKsguidLib {
    $sdk = Get-WindowsSdkKsguidLib
    if ($sdk) { return $sdk }

    Write-Host "ksguid.lib not found; attempting Windows 10 SDK component install..."
    foreach ($component in @(
        "Microsoft.VisualStudio.Component.Windows10SDK.22621",
        "Microsoft.VisualStudio.Component.Windows10SDK.19041"
    )) {
        if (Install-Windows10SdkComponent $component) {
            $sdk = Get-WindowsSdkKsguidLib
            if ($sdk) { return $sdk }
        }
    }

    Write-Host "SDK diagnostics:"
    Write-SdkDiag "C:\Program Files (x86)\Windows Kits\10\Lib"
    Write-SdkDiag "C:\Program Files\Windows Kits\10\Lib"
    throw @"
ksguid.lib not found on this runner.
vcvars may list um\x64 for 10.0.26100.0 but that SDK drop often omits ksguid.lib.
Push scripts/ci-append-windows-sdk-lib.ps1 (multi-version scan) and/or install SDK 22621 on the runner.
"@
}
