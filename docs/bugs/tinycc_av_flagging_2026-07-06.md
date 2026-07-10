# TinyCC AV Flagging

**Date:** 2026-07-06
**Status:** Fixed locally; external VT/WDSI submission pending
**Area:** Default Windows package, TinyCC scripting runtime

## Symptom

The default Windows package could trip Microsoft Defender false-positive detections. The high-risk artifacts and behaviors were concentrated in the scripting runtime:

- `tcc.dll` shipped next to `BattleShip.exe`.
- Source mods were compiled to a temporary DLL on disk and immediately loaded with the platform dynamic loader.

That combination matched two common AV heuristics: known TinyCC binary signatures and "process writes unsigned DLL then loads it" behavior.

## Root Cause

The mod loader used TinyCC in DLL output mode because that matched the original `LibraryLoader` abstraction. On Windows, that required both a shipped `tcc.dll` runtime and DLL startup objects under `.tcc/lib`.

The package script copied the whole `.tcc` runtime into the Windows zip when scripting was enabled. Even when `tcc.exe` was only a build tool, `tcc.dll` remained a shipped executable payload and an import of `BattleShip.exe`.

## Fix

The scripting path now uses TinyCC memory output instead of temporary DLL output:

- `libtcc` is built as a static library and linked into `BattleShip.exe`.
- `.tcc` runtime staging keeps headers, `libtcc1.a`, and `BattleShip.def` only.
- Windows DLL startup objects (`dllcrt1.o`, `dllmain.o`) are no longer built into the shipped TinyCC runtime archive.
- `BattleShip.def` is treated as an export-name list. The loader resolves those exports against the running process and registers them with TinyCC before relocation.
- Windows `__imp_` import-pointer aliases are registered so existing `dllimport`-style mod headers continue to work.
- Source mods use `TCC_OUTPUT_MEMORY`, `tcc_relocate`, and `tcc_get_symbol`; unload frees the `TCCState` instead of unloading a temp DLL.
- The Windows package script fails if any `.dll` or `.exe` appears under the staged `.tcc` tree.
- The default US `BattleShip-windows.zip` now includes scripting/modding support; there is no split `-modding` artifact in the release flow.

One Windows-specific adjustment was also required: the source mod compile options dropped `-g`. This TinyCC memory relocation path attempted to resolve a debug sidecar object such as `bt-log.o` when debug info was enabled.

## Verification

Local Windows preflight on 2026-07-06:

- `cmake -S . -B build\x64 -A x64 -DDISABLE_SCRIPTING=OFF`
- `cmake --build build\x64 --target ssb64 --config Debug --parallel 4`
- `.\scripts\package-windows.ps1` produced default `dist\BattleShip-windows.zip` with scripting enabled.
- Debug `.tcc/lib` contained only `BattleShip.def` and `libtcc1.a`.
- Packaged `.tcc/lib` contained only `BattleShip.def` and `libtcc1.a`.
- No `.dll` or `.exe` appeared under `.tcc` in either the staged package or zip entry list.
- `dumpbin /DEPENDENTS dist\BattleShip\BattleShip.exe` showed no `tcc.dll`.
- Latest public release `v1.4.1-hotfix` `BattleShip-windows.zip` reproduced the Defender false positive: `Trojan:Win32/Suschil!rfn`, ThreatID `2147927547`.
- New `dist\BattleShip-windows.zip`, extracted staging folder, and MotW-stamped Shell extraction produced no Defender detections.
- `template`, `hooktest`, and `playertint` workspace mods compiled and initialized from the MotW extraction.
- `hooktest` installed a native detour and fired for thousands of frames.
- Manual `Mods` -> `Hot Reload` unloaded all three mods, uninstalled the hook, recompiled/reinitialized the mods, and the hook continued firing.

External release reputation/submission steps remain pending: VirusTotal scan and Microsoft WDSI false-positive submission.

Detailed local results: `docs/tinycc_av_test_results_2026-07-06.md`.
