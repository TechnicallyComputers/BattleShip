# Builds BattleShip as a self-contained Windows release zip.
#
# Usage:
#   pwsh scripts/package-windows.ps1              # offline (SSB64_NETMENU=OFF, no curl)
#   pwsh scripts/package-windows.ps1 -Netplay     # netplay (SSB64_NETMENU=ON, vcpkg curl)
#
# Offline and netplay use separate build dirs (build-bundle-win-* vs build-bundle-win-netplay-*).
# Do not pass -DSSB64_NETMENU=* on the command line; this script sets OFF|ON explicitly.
#
# Output:
#   Default:  dist\BattleShip-windows.zip
#   Netplay:  dist\BattleShip-Netplay-windows.zip  (JP: BattleShip-JP-Netplay-windows.zip)
#
# Layout produced (extracted):
#   BattleShip\
#     BattleShip.exe             — main executable
#     torch.exe                  — sidecar for first-run extraction
#     f3d.o2r                    — Fast3D shader archive (ROM-independent)
#     config.yml                 — Torch extraction config
#     yamls\us\*.yml             — Torch extraction recipes
#     gamecontrollerdb.txt       — SDL controller mappings
#     SDL2.dll                   — runtime dependency (vcpkg-bundled)
#     <other vcpkg DLLs>         — picked up by Get-ChildItem from build dir
#
# Portable: drop the extracted folder anywhere and run BattleShip.exe.
# Save data and config (ssb64_save.bin, BattleShip.cfg.json, logs/) land
# next to the .exe in the extraction directory — move the folder, the
# saves move with it. BattleShip.o2r is NOT bundled; the first-run wizard
# extracts it from the user's ROM into the same directory as BattleShip.exe.
#
# We intentionally do NOT pass -DNON_PORTABLE=ON. NON_PORTABLE bakes
# CMAKE_INSTALL_PREFIX into libultraship's install_config.h at configure
# time, and CMake resolves any relative prefix against the configure cwd —
# so on the GitHub Actions runner that bakes the runner workspace path
# (e.g. "D:/a/BattleShip/BattleShip/BattleShip"), which v0.7.2 shipped
# and which crashed user machines whose D: drive returned ERROR_NOT_READY
# when libultraship probed it. Building portable side-steps the entire
# class of bug: the runtime resolves resource paths via GetModuleFileNameW
# (LUS Context::GetAppBundlePath, _WIN32 branch) and saves via the same
# mechanism, so the only paths the binary ever touches are exe-relative.

param(
    [switch]$Netplay
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
# ROM version: us (default) or jp. The JP build is a SEPARATE
# application — own .exe name, zip, app-data dir — so a user can keep
# both and they never touch each other's ROM/o2r/saves. $AppName
# mirrors CMake SSB64_APP_NAME / OUTPUT_NAME. US keeps the historical
# "BattleShip" identity so existing links / the in-app updater are
# unaffected.
$Ver = if ($env:SSB64_VERSION) { $env:SSB64_VERSION } else { "us" }
if ($Ver -ne "us" -and $Ver -ne "jp") { Write-Error "SSB64_VERSION must be us|jp"; exit 1 }
$AppName = if ($Ver -eq "jp") { "BattleShip-JP" } else { "BattleShip" }
if ($Netplay) {
    $BuildDir = Join-Path $Root "build-bundle-win-netplay-$Ver"
    $StageLabel = if ($Ver -eq "jp") { "BattleShip-JP-Netplay" } else { "BattleShip-Netplay" }
    $ZipPath = Join-Path $Root "dist\$StageLabel-windows.zip"
} else {
    $BuildDir = Join-Path $Root "build-bundle-win-$Ver"
    $StageLabel = $AppName
    $ZipPath = Join-Path $Root "dist\$AppName-windows.zip"
}
$DistDir = Join-Path $Root "dist"
$StageDir = Join-Path $DistDir $StageLabel
$Jobs = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 4 }

function Write-Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Reset-StaleCmakeCache {
    param([string]$Dir, [bool]$WantNetmenu)
    $cache = Join-Path $Dir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache)) { return }
    $raw = Get-Content -LiteralPath $cache -Raw
    $hasNetmenu = $raw -match 'SSB64_NETMENU:BOOL=ON'
    if ($WantNetmenu -and -not $hasNetmenu) {
        Write-Host "   Clearing CMake cache (reconfigure with SSB64_NETMENU=ON)"
        Remove-Item -LiteralPath $cache -Force
        Remove-Item -LiteralPath (Join-Path $Dir "CMakeFiles") -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (-not $WantNetmenu -and $hasNetmenu) {
        Write-Host "   Clearing CMake cache (offline build had SSB64_NETMENU=ON)"
        Remove-Item -LiteralPath $cache -Force
        Remove-Item -LiteralPath (Join-Path $Dir "CMakeFiles") -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-OfflineWindowsConfigured {
    param([string]$Dir)
    $cache = Join-Path $Dir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache)) { return }
    $raw = Get-Content -LiteralPath $cache -Raw
    if ($raw -match 'SSB64_NETMENU:BOOL=ON') {
        Fail "Offline Windows build has SSB64_NETMENU=ON in CMakeCache — should be OFF (no curl/mm_matchmaking)"
    }
    $ninja = Join-Path $Dir "build.ninja"
    if (Test-Path -LiteralPath $ninja) {
        if (Select-String -LiteralPath $ninja -Pattern 'mm_matchmaking\.c' -Quiet) {
            Fail "Offline build compiles mm_matchmaking.c — SSB64_NETMENU must be OFF"
        }
    }
    Write-Host "   Offline: SSB64_NETMENU=OFF (no matchmaking/curl)"
}

function Test-NetplayCurlConfigured {
    param([string]$Dir)
    $curlHeader = Get-ChildItem -Path (Join-Path $Dir "libultraship\vcpkg\installed") -Recurse -Filter "curl.h" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\include\\curl\\curl\.h$' } |
        Select-Object -First 1
    if (-not $curlHeader) {
        Fail "vcpkg curl not installed (no include/curl/curl.h under $Dir\libultraship\vcpkg\installed). Check LUS_VCPKG_EXTRA_PACKAGES and vcpkg install log."
    }
    Write-Host "   vcpkg curl.h: $($curlHeader.FullName)"
    $cache = Join-Path $Dir "CMakeCache.txt"
    if (Test-Path -LiteralPath $cache) {
        if ((Get-Content -LiteralPath $cache -Raw) -notmatch 'SSB64_NETMENU:BOOL=ON') {
            Fail "CMakeCache has SSB64_NETMENU=OFF; expected ON for netplay package"
        }
        if ((Get-Content -LiteralPath $cache -Raw) -notmatch 'SSB64_VCPKG_CURL_PREFIX') {
            Fail "CMakeCache missing SSB64_VCPKG_CURL_PREFIX — push latest CMakeLists.txt / cmake/Ssb64NetmenuDeps.cmake"
        }
    }
}

function Import-VcVars64IfNeeded {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        Fail "MSVC not found (vswhere missing). Run from a Developer Command Prompt or install VS Build Tools."
    }
    $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        Fail "Visual Studio C++ tools not installed."
    }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) {
        Fail "vcvars64.bat not found under $vsPath"
    }
    Write-Host "   Importing MSVC environment from vcvars64.bat"
    cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^(?<key>[^=]+)=(?<val>.*)$') {
            Set-Item -Path "Env:$($Matches.key)" -Value $Matches.val
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Fail "vcvars64 did not expose cl.exe on PATH"
    }
}

# automate-vcpkg.cmake runs git pull when ${BuildDir}/libultraship/vcpkg exists.
# A partial/failed prior configure leaves a non-git directory and configure dies with
# "fatal: not a git repository" before README.md exists.
function Remove-InvalidVcpkgTree([string]$Dir) {
    if (-not (Test-Path -LiteralPath $Dir)) { return }
    $gitDir = Join-Path $Dir ".git"
    $readme = Join-Path $Dir "README.md"
    if ((-not (Test-Path -LiteralPath $gitDir)) -or (-not (Test-Path -LiteralPath $readme))) {
        Write-Host "   Removing invalid vcpkg cache: $Dir"
        Remove-Item -LiteralPath $Dir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Copy-CaBundle($DestDir) {
    New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    $CommittedCa = Join-Path $Root "port\net\cacert.pem"
    if (Test-Path -LiteralPath $CommittedCa) {
        Copy-Item -LiteralPath $CommittedCa (Join-Path $DestDir "cacert.pem")
        Write-Host "   CA bundle: $CommittedCa (committed)"
        return
    }
    $Candidates = @(
        (Join-Path $BuildDir "vcpkg_installed\x64-windows-static\share\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "vcpkg_installed\x64-windows\share\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "libultraship\vcpkg_installed\x64-windows-static\share\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "libultraship\vcpkg_installed\x64-windows\share\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "libultraship\vcpkg\installed\x64-windows-static\share\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "libultraship\vcpkg\installed\x64-windows\share\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "libultraship\vcpkg\installed\x64-windows-static\tools\curl\curl-ca-bundle.crt"),
        (Join-Path $BuildDir "libultraship\vcpkg\installed\x64-windows-static\tools\curl\cacert.pem")
    )
    foreach ($c in $Candidates) {
        if (Test-Path -LiteralPath $c) {
            Copy-Item -LiteralPath $c (Join-Path $DestDir "cacert.pem")
            Write-Host "   CA bundle: $c"
            return
        }
    }
    foreach ($root in @(
        (Join-Path $BuildDir "libultraship\vcpkg\installed"),
        (Join-Path $BuildDir "libultraship\vcpkg_installed"),
        (Join-Path $BuildDir "vcpkg_installed")
    )) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $found = Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -in @('curl-ca-bundle.crt', 'cacert.pem', 'ca-bundle.crt') } |
            Select-Object -First 1
        if ($found) {
            Copy-Item -LiteralPath $found.FullName (Join-Path $DestDir "cacert.pem")
            Write-Host "   CA bundle: $($found.FullName)"
            return
        }
    }
    if ($env:GITHUB_ACTIONS -eq 'true') {
        $MozillaCaUrl = "https://curl.se/ca/cacert.pem"
        try {
            Invoke-WebRequest -Uri $MozillaCaUrl -OutFile (Join-Path $DestDir "cacert.pem") -ErrorAction Stop
            Write-Host "   CA bundle: downloaded from $MozillaCaUrl (no committed/vcpkg bundle)"
            return
        } catch {
            Fail "Could not find CA certificate bundle and download failed: $_"
        }
    }
    Fail "Could not find a CA certificate bundle for HTTPS matchmaking (see port/net/cacert.pem)"
}

# ── 0. Run codegen scripts that don't need the ROM ──
# Encoded credit files are gitignored (input text is in decomp/src/credits/),
# so a fresh checkout (CI or otherwise) must run the encoder before
# cmake builds scstaffroll.c. ROM-independent — same step CMake's
# GenerateCreditsAssets target runs.
Write-Step "Encoding credits text"
Push-Location (Join-Path $Root "decomp/src/credits")
foreach ($f in @("staff.credits.us.txt", "titles.credits.us.txt")) {
    & python "$Root/tools/creditsTextConverter.py" $f | Out-Null
    if ($LASTEXITCODE -ne 0) { Pop-Location; Fail "credits encode failed: $f" }
}
foreach ($f in @("info.credits.us.txt", "companies.credits.us.txt")) {
    & python "$Root/tools/creditsTextConverter.py" -paragraphFont $f | Out-Null
    if ($LASTEXITCODE -ne 0) { Pop-Location; Fail "credits encode failed: $f" }
}
Pop-Location

# ── 1. Configure + build (Release, portable) ──
# Ninja + x64 MSVC. GHA sets vcvars via ilammy/msvc-dev-cmd; local builds need a Developer Command Prompt.
$CmakeArgs = @(
    "-DCMAKE_BUILD_TYPE=Release",
    "-DSSB64_VERSION=$Ver",
    "-GNinja",
    "-DCMAKE_VS_PLATFORM_NAME=x64",
    "-DCMAKE_C_COMPILER=cl",
    "-DCMAKE_CXX_COMPILER=cl"
)
if ($Netplay) {
    $CmakeArgs += "-DSSB64_NETMENU=ON"
} else {
    # Force OFF — stale CMakeCache from a netplay configure must not pull in mm_matchmaking/curl.
    $CmakeArgs += "-DSSB64_NETMENU=OFF"
}
Write-Step "Configuring release build (portable$(if ($Netplay) { ', SSB64_NETMENU=ON' }))"
Import-VcVars64IfNeeded
if ($env:GITHUB_ACTIONS -eq 'true' -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host "   CI: removing prior build tree for clean MSVC configure"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
# Use libultraship's local vcpkg tree (not a stale runner-wide VCPKG_ROOT).
Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
Remove-InvalidVcpkgTree (Join-Path $BuildDir "libultraship\vcpkg")
Reset-StaleCmakeCache $BuildDir $Netplay.IsPresent
# No NON_PORTABLE, no CMAKE_INSTALL_PREFIX. LUS resolves the bundle path
# via GetModuleFileNameW at runtime, and the port's port_save.cpp +
# Ship::Context::GetAppDirectoryPath() route saves/config to the cwd
# (= BattleShip.exe's directory when launched normally). See the file
# header for the v0.7.2 crash this avoids.
if ($Netplay) {
    & cmake -B $BuildDir $Root @CmakeArgs
} else {
    & cmake -B $BuildDir $Root @CmakeArgs | Out-Null
}
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }

if ($Netplay) {
    Test-NetplayCurlConfigured $BuildDir
} else {
    Test-OfflineWindowsConfigured $BuildDir
}

Write-Step "Building BattleShip + torch"
cmake --build $BuildDir --config Release -j $Jobs
if ($LASTEXITCODE -ne 0) { Fail "build failed" }

# ── 2. Build f3d.o2r (zip of LUS shaders, ROM-independent) ──
Write-Step "Packaging Fast3D shader archive"
$F3DO2R = Join-Path $BuildDir "f3d.o2r"
if (Test-Path $F3DO2R) { Remove-Item $F3DO2R -Force }
$ShaderSrc = Join-Path $Root "libultraship\src\fast"
Push-Location $ShaderSrc
Compress-Archive -Path "shaders" -DestinationPath $F3DO2R -CompressionLevel Optimal
Pop-Location
if (-not (Test-Path $F3DO2R)) { Fail "f3d.o2r was not created" }

# ── 3. Locate built artifacts ──
# CMake OUTPUT_NAME == SSB64_APP_NAME == $AppName, so the exe is
# BattleShip.exe (US) or BattleShip-JP.exe (JP).
$GameExe = Join-Path $BuildDir "Release\$AppName.exe"
if (-not (Test-Path $GameExe)) {
    # Fall back to non-multi-config layout (Ninja).
    $GameExe = Join-Path $BuildDir "$AppName.exe"
}
$TorchExe = $null
foreach ($cand in @(
    "TorchExternal\src\TorchExternal-build\Release\torch.exe",
    "TorchExternal\src\TorchExternal-build\torch.exe",
    "torch-install\bin\torch.exe"
)) {
    $p = Join-Path $BuildDir $cand
    if (Test-Path $p) { $TorchExe = $p; break }
}
if (-not (Test-Path $GameExe))   { Fail "$AppName.exe not found at $GameExe" }
if (-not $TorchExe)              { Fail "torch.exe not found in $BuildDir" }

# ── 4. Stage the release tree ──
Write-Step "Staging $StageDir"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Path $StageDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $StageDir "yamls\$Ver") | Out-Null

Copy-Item $GameExe        (Join-Path $StageDir "$AppName.exe")
Copy-Item $TorchExe      (Join-Path $StageDir "torch.exe")
Copy-Item $F3DO2R        (Join-Path $StageDir "f3d.o2r")
Copy-Item (Join-Path $Root "gamecontrollerdb.txt") $StageDir
Copy-Item (Join-Path $Root "config.yml") $StageDir
Copy-Item (Join-Path $Root "yamls\$Ver\*.yml") (Join-Path $StageDir "yamls\$Ver")
# Standalone .ico for shortcut/installer use — the icon is also embedded
# directly in BattleShip.exe via port/ssb64.rc, so Explorer picks it up
# without this file. Keep it bundled for future installer work.
# Region-aware: JP picks assets\icon-jp.ico, US keeps assets\icon.ico.
# (Note: ssb64.rc still embeds assets\icon.ico unconditionally, so the
# .exe's own Explorer-icon is US-art for both regions until that .rc
# is taught to pick per-region — leave for follow-up if needed.)
$IcoSrc = if ($Ver -eq "jp") { Join-Path $Root "assets\icon-jp.ico" }
          else                { Join-Path $Root "assets\icon.ico"   }
Copy-Item $IcoSrc (Join-Path $StageDir "$AppName.ico")

# Bundle the ESC menu fonts. Menu.cpp::FindMenuAssetPath walks up from
# RealAppBundlePath() and from current_path(); placing the TTFs at
# <staging>\assets\custom\fonts\ next to the .exe matches the first
# iteration of the walker rooted at the .exe's directory. Without this
# the menu falls back to ImGui's default font silently.
#
# OFL 1.1 §1 requires the license text to accompany each redistributed
# font file, so the *-OFL.txt files ship alongside the .ttf they govern.
$FontsDir = Join-Path $StageDir "assets\custom\fonts"
New-Item -ItemType Directory -Path $FontsDir -Force | Out-Null
Copy-Item (Join-Path $Root "assets\custom\fonts\Montserrat-Regular.ttf")  $FontsDir
Copy-Item (Join-Path $Root "assets\custom\fonts\Montserrat-OFL.txt")      $FontsDir
Copy-Item (Join-Path $Root "assets\custom\fonts\Inconsolata-Regular.ttf") $FontsDir
Copy-Item (Join-Path $Root "assets\custom\fonts\Inconsolata-OFL.txt")     $FontsDir

if ($Netplay) {
    $NetAssets = $null
    foreach ($cand in @(
        (Join-Path $BuildDir "port\net\assets"),
        (Join-Path $Root "port\net\assets")
    )) {
        if (Test-Path $cand) { $NetAssets = $cand; break }
    }
    if ($NetAssets) {
        $NetDest = Join-Path $StageDir "port\net\assets"
        New-Item -ItemType Directory -Path $NetDest -Force | Out-Null
        Copy-Item -Path (Join-Path $NetAssets "*") -Destination $NetDest -Recurse -Force
    } else {
        Write-Host "WARN: netmenu build but port\net\assets not found — VS menu PNGs may be missing" -ForegroundColor Yellow
    }
    Copy-CaBundle (Join-Path $StageDir "ssl")
}

# Project LICENSE + verbatim upstream LICENSE files for the submodules
# whose compiled code is in this distribution. MIT requires the upstream
# copyright + permission notice to ride along with redistributed copies.
# .txt suffix follows Windows convention so users can double-click open.
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $StageDir "LICENSE.txt")
$LicensesDir = Join-Path $StageDir "licenses"
New-Item -ItemType Directory -Path $LicensesDir -Force | Out-Null
$LusLicense = Join-Path $Root "libultraship\LICENSE"
$TorchLicense = Join-Path $Root "torch\LICENSE"
if (-not (Test-Path $LusLicense))   { Fail "libultraship\LICENSE not found - submodules not initialized?" }
if (-not (Test-Path $TorchLicense)) { Fail "torch\LICENSE not found - submodules not initialized?" }
Copy-Item $LusLicense   (Join-Path $LicensesDir "libultraship-LICENSE.txt")
Copy-Item $TorchLicense (Join-Path $LicensesDir "torch-LICENSE.txt")
@'
This directory contains license texts for third-party components whose
compiled code is included in this BattleShip distribution:

  - libultraship-LICENSE.txt  (MIT, Copyright (c) 2022 kenix3)
  - torch-LICENSE.txt         (MIT, Copyright (c) 2023 Lywx)

Bundled font licenses (SIL Open Font License 1.1) live alongside the
font files at assets\custom\fonts\.

The BattleShip project's own MIT license is in ..\LICENSE.txt.

Additional libraries dynamically linked at runtime (SDL2, GLEW, libzip,
nlohmann_json, tinyxml2, spdlog, fmt, hidapi-via-libultraship) are
distributed under their respective upstream licenses (zlib, modified
BSD, BSD-3-Clause, MIT). Refer to those upstream packages for full
license texts.
'@ | Set-Content -Path (Join-Path $LicensesDir "README.txt") -Encoding UTF8

# Bundle DLLs that landed next to BattleShip.exe (vcpkg drops SDL2.dll, etc.).
$ExeBuildDir = Split-Path $GameExe -Parent
Get-ChildItem -Path $ExeBuildDir -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName $StageDir
}

# ── 5. Zip ──
Write-Step "Compressing $ZipPath"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal
if (-not (Test-Path $ZipPath)) { Fail "zip was not created" }

$ZipKB = [int]((Get-Item $ZipPath).Length / 1024)
Write-Host "`n✓ Release zip ready: $ZipPath ($ZipKB KB)" -ForegroundColor Green
Write-Host "   Variant: $(if ($Netplay) { 'netmenu/netplay' } else { 'offline' })"
Write-Host "   Portable: extract anywhere; save data lives next to BattleShip.exe."
Write-Host "   First launch will prompt for your ROM via the ImGui wizard."
if ($Netplay) {
    Write-Host "   Netplay: automatch uses HTTPS matchmaking (vcpkg curl + ssl\cacert.pem)."
}
