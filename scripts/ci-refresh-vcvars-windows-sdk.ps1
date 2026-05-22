# Re-run vcvars64 with a Windows SDK version that contains ksguid.lib, then export env to GITHUB_ENV.
# ilammy/msvc-dev-cmd often leaves WindowsSDKVersion at 10.0.26100.0 where um\x64 exists but ksguid.lib does not.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot/ci-windows-sdk-lib-common.ps1"

function Select-WindowsSdkVersionWithKsguid {
    $kitRoots = @(
        "C:\Program Files (x86)\Windows Kits\10\Lib",
        "C:\Program Files\Windows Kits\10\Lib"
    )
    $preferred = @(
        "10.0.22621.0",
        "10.0.22000.0",
        "10.0.20348.0",
        "10.0.19041.0"
    )
    foreach ($verName in $preferred) {
        foreach ($kits in $kitRoots) {
            $ks = Join-Path $kits "$verName\um\x64\ksguid.lib"
            if (Test-Path -LiteralPath $ks) {
                return @{ Version = $verName; Ksguid = $ks }
            }
        }
    }
    foreach ($kits in $kitRoots) {
        if (-not (Test-Path -LiteralPath $kits)) { continue }
        $versions = Get-ChildItem -LiteralPath $kits -Directory |
            Where-Object { $_.Name -match "^10\." } |
            Sort-Object Name -Descending
        foreach ($ver in $versions) {
            $ks = Join-Path $ver.FullName "um\x64\ksguid.lib"
            if (Test-Path -LiteralPath $ks) {
                return @{ Version = $ver.Name; Ksguid = $ks }
            }
        }
    }
    return $null
}

function Export-VcVarsToGithubEnv {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "Visual Studio C++ tools not found" }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found" }
    Write-Host "   Running vcvars64.bat (WindowsSDKVersion=$env:WindowsSDKVersion)"
    cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^(?<key>[^=]+)=(?<val>.*)$') {
            $key = $Matches.key
            $val = $Matches.val
            if ($val -match '[\r\n%]') { return }
            Add-Content -LiteralPath $env:GITHUB_ENV -Value "$key=$val"
        }
    }
}

Write-Host "ci-refresh-vcvars-windows-sdk.ps1"

$pick = Select-WindowsSdkVersionWithKsguid
if (-not $pick) {
    Write-Host "SDK diagnostics:"
    Write-SdkDiag "C:\Program Files (x86)\Windows Kits\10\Lib"
    Write-SdkDiag "C:\Program Files\Windows Kits\10\Lib"
    $sdk = Ensure-WindowsSdkKsguidLib
    $pick = @{ Version = $sdk.SdkVersion; Ksguid = $sdk.Ksguid }
} else {
    Write-Host "Selected Windows SDK: $($pick.Version) (ksguid at $($pick.Ksguid))"
}

$env:WindowsSDKVersion = "$($pick.Version)\"
Export-VcVarsToGithubEnv
Write-Host "Refreshed GITHUB_ENV from vcvars64 (WindowsSDKVersion=$($env:WindowsSDKVersion))"
