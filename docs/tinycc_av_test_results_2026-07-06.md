# TinyCC Antivirus Flagging - Windows Test Results

**Date:** 2026-07-06
**Machine:** DESKTOP, Windows 10 Home, 64-bit, OS HAL `10.0.26100.1`
**Branch:** `claude/tinycc-antivirus-flagging-6hi2j3`
**Superproject base commit:** `569bc61c96b68c3d2eb61978114972c0acc65974` plus local implementation changes
**libultraship base commit:** `ec22b3e242469257d8e050314359e874707715fc` plus local implementation changes
**Package under test:** default US Windows package, `dist\BattleShip-windows.zip`, with scripting/modding enabled by default

## Defender Configuration

`Update-MpSignature` completed with signatures already current:

- `AMEngineVersion`: `1.1.26050.11`
- `AntivirusSignatureVersion`: `1.453.456.0`
- `AntivirusSignatureLastUpdated`: `7/6/2026 3:37:26 AM`
- `RealTimeProtectionEnabled`: `True`
- `IoavProtectionEnabled`: `True`
- `BehaviorMonitorEnabled`: `True`
- `MAPSReporting`: blank in PowerShell output
- `SubmitSamplesConsent`: blank in PowerShell output

`MpCmdRun.exe -ValidateMapsConnection` reported internet connectivity and a last successful MAPS connection at `7/6/2026 3:19:44 PM`, but returned `0x80070005` from the non-elevated shell. Treat cloud-validation as partial.

## Build Under Test

Debug build with scripting enabled:

```powershell
cmake -S . -B build\x64 -A x64 -DDISABLE_SCRIPTING=OFF
cmake --build build\x64 --target ssb64 --config Debug --parallel 4
```

Result: pass.

Default Windows package:

```powershell
Remove-Item Env:SSB64_ENABLE_SCRIPTING -ErrorAction SilentlyContinue
Remove-Item Env:SSB64_VERSION -ErrorAction SilentlyContinue
.\scripts\package-windows.ps1
```

Result: pass. Produced `dist\BattleShip-windows.zip` at 9,656,330 bytes. The package script reported `Includes TCC scripting support for C mods.`

Artifact SHA-256:

```text
B2C1550FEB4BF004E0010BDD6C85BB294EFDAD359561BD1E126C38B20E44619A
```

## Artifact Checks

Default package `.tcc/lib` contents:

```text
.tcc/lib/BattleShip.def  2552383 bytes
.tcc/lib/libtcc1.a          4438 bytes
```

`Get-ChildItem -Recurse .tcc | Where Extension -in '.dll','.exe'` returned no files for `dist\BattleShip\.tcc`.

The same check against the zip entry list returned no `.dll` or `.exe` entries under `.tcc`.

`dumpbin /DEPENDENTS dist\BattleShip\BattleShip.exe` showed no `tcc.dll` dependency. Reported DLL imports were:

```text
SHELL32.dll
KERNEL32.dll
USER32.dll
GDI32.dll
WINMM.dll
IMM32.dll
ole32.dll
OLEAUT32.dll
VERSION.dll
SETUPAPI.dll
OPENGL32.dll
bcrypt.dll
ADVAPI32.dll
PSAPI.DLL
COMDLG32.dll
dbghelp.dll
HID.DLL
D3DCOMPILER_47.dll
dwmapi.dll
```

## Positive Control

Downloaded latest public release `v1.4.1-hotfix` asset `BattleShip-windows.zip` from GitHub Releases and attempted extraction under `C:\avtest\old-modding`.

Result: positive control passed. Defender real-time protection blocked/quarantined the old public zip during extraction.

Detection details:

```text
ThreatName: Trojan:Win32/Suschil!rfn
ThreatID: 2147927547
Severity: Severe
Category: Trojan
Path: file:_C:\avtest\old-modding\BattleShip-windows.zip
Detection Source: Real-Time Protection
Action: Quarantine
Action Status: No additional actions required
Security intelligence Version: AV/AS/NIS 1.453.456.0
Engine Version: AM/NIS 1.1.26050.11
```

## Static Scans

Commands run:

```powershell
Start-MpScan -ScanType CustomScan -ScanPath dist\BattleShip-windows.zip
Start-MpScan -ScanType CustomScan -ScanPath dist\BattleShip
Get-MpThreatDetection
Get-WinEvent -LogName 'Microsoft-Windows-Windows Defender/Operational'
```

Result: pass. No Defender detections or operational detection/remediation events for `dist\BattleShip-windows.zip` or `dist\BattleShip`.

The only Defender detection present in history was the expected positive-control detection for `C:\avtest\old-modding\BattleShip-windows.zip`.

## MotW Extraction And Scan

Stamped the new default package zip with MotW:

```powershell
Set-Content -Path C:\avtest\motw\BattleShip-windows.zip -Stream Zone.Identifier `
    -Value "[ZoneTransfer]`nZoneId=3"
```

Extracted it using Windows Shell COM (`Shell.Application`) so MotW propagated to contents. Verified `C:\avtest\motw\extracted\BattleShip.exe` had a `Zone.Identifier` alternate data stream.

Scanned `C:\avtest\motw`.

Result: pass. No Defender detections or operational events for the MotW-stamped zip/extracted tree. No `.dll` or `.exe` appeared under `C:\avtest\motw\extracted\.tcc`.

SmartScreen UI reputation was not independently classified in automation. The test confirms Defender did not detect or quarantine the MotW-stamped package.

## Runtime And Hot Reload

Staged local ROM-derived `BattleShip.o2r` into `C:\avtest\motw\extracted` and staged all three workspace source mods:

- `Demo Mod`
- `HookTest`
- `Player Tint`

Launched MotW-extracted `BattleShip.exe` from `C:\avtest\motw\extracted`. The process remained running after 45 seconds.

Fresh `%APPDATA%\BattleShip\ssb64.log` evidence:

```text
SSB64: ScriptLoader initialized (codeVersion=1)
SSB64: mounted mod archive -> C:/avtest/motw/extracted/mods/Demo Mod
SSB64: mounted mod archive -> C:/avtest/motw/extracted/mods/HookTest
SSB64: mounted mod archive -> C:/avtest/motw/extracted/mods/Player Tint
[mods] hooked osSpTaskStartGo @ ... (trampoline=..., chain=1, owner=HookTest)
[HookTest] hook installed OK
SSB64: TCC scripted mods compiled + loaded
[HookTest] osSpTaskStartGo detour hit 2220 times
```

Then the in-game `Mods` -> `Hot Reload` button was pressed manually. Fresh log evidence:

```text
[PlayerTint] exit OK
[HookTest] exit OK (detour saw 1507 frames)
[mods] uninstalled ... (owner=HookTest, chain emptied, chain=0)
[DemoMod] exit OK (saw 1526 frames)
[mods] hooked osSpTaskStartGo @ ... (trampoline=..., chain=1, owner=HookTest)
[HookTest] hook installed OK
[HookTest] osSpTaskStartGo detour hit 1080 times
```

Result: pass. Mods compiled, initialized, installed a native hook, ran for thousands of frames, unloaded cleanly on hot reload, recompiled/reinitialized, and the hook continued firing after reload.

Defender produced no detection/remediation events during runtime or hot reload.

## VirusTotal And WDSI

Not completed from this machine.

VirusTotal: no `vt`/VirusTotal CLI was installed and no VirusTotal API key environment variable was present. VirusTotal API v3 upload requires an API key in the `x-apikey` header.

WDSI: Microsoft’s file submission page is the correct path for Defender false-positive review (`https://www.microsoft.com/en-us/wdsi/filesubmission` / `https://aka.ms/wdsi`), but submission requires interactive/account-backed portal work. Prepared values for submission:

- File: `dist\BattleShip-windows.zip`
- SHA-256: `B2C1550FEB4BF004E0010BDD6C85BB294EFDAD359561BD1E126C38B20E44619A`
- Old detection being remediated: `Trojan:Win32/Suschil!rfn`, ThreatID `2147927547`
- Summary: false positive caused by prior package shipping TinyCC DLL/temp-DLL mod loading; new package static-links TinyCC and relocates mods in memory, with no `tcc.dll`/`tcc.exe` under `.tcc`.

## Protocol Matrix

| Protocol Step | Result | Notes |
|---------------|--------|-------|
| 4.0 positive control | Pass | Latest public `BattleShip-windows.zip` (`v1.4.1-hotfix`) detected/quarantined as `Trojan:Win32/Suschil!rfn`. |
| 4.1 static scan | Pass | New default `dist\BattleShip-windows.zip` and extracted `dist\BattleShip` produced no Defender detections. |
| 4.2 MotW + SmartScreen | Partial pass | MotW stamped and Shell-extracted package produced no Defender detections; SmartScreen reputation UI not independently classified. |
| 4.3 runtime behavior | Pass | MotW-extracted package loaded all three mods, HookTest detoured engine code, hot reload unloaded/reloaded hooks cleanly, and Defender stayed quiet. |
| 4.4 baseline standard package | N/A | There is no split package; the default Windows package includes modding support. |
| 4.5 VirusTotal | Blocked | No local CLI/API key configured. |
| 4.6 WDSI submission | Blocked | Requires interactive/account-backed Microsoft submission. Prepared file hash and detection details above. |

## Conclusion

The local Windows AV protocol supports the fix for the default `BattleShip-windows.zip`: the current public release reproduces the Defender false positive, while the new package removes shipped TinyCC PE artifacts, has no `tcc.dll` import, passes Defender static/MotW scans, and passes a runtime + hot-reload behavior test with source mods and native hooks.

Remaining release tasks are external submission/reputation tasks: VirusTotal scan with an API key or browser upload, and Microsoft WDSI false-positive submission through the account-backed portal.
