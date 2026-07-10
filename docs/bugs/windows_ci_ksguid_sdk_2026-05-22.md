# Windows CI: ksguid.lib / SDK 10.0.26100.0

**Date:** 2026-05-22  
**Symptom:** `ci-append-windows-sdk-lib.ps1` fails with `ksguid.lib not found at ...\10.0.26100.0\um\x64\ksguid.lib`, or link fails with `LNK1181: cannot open input file 'ksguid.lib'`.

## Root cause

- `ksguid.lib` is a **Windows SDK** import library (WASAPI 5.1 / `KSDATAFORMAT_SUBTYPE_PCM` in libultraship), not a repo file.
- GitHub `windows-2022` images often activate SDK **10.0.26100.0** via `ilammy/msvc-dev-cmd`. That tree may include `um\x64` on `LIB` but **omit** `ksguid.lib`.
- Older SDK folders on the same runner (e.g. **10.0.22621.0**) usually still contain `ksguid.lib`.

## Fix (in repo)

1. **`scripts/ci-refresh-vcvars-windows-sdk.ps1`** — pick an SDK version that has `ksguid.lib`, set `WindowsSDKVersion`, re-run `vcvars64`, export to `GITHUB_ENV`.
2. **`scripts/ci-append-windows-sdk-lib.ps1`** + **`ci-windows-sdk-lib-common.ps1`** — scan all `10.*` SDK versions; optional VS install of SDK 22621/19041; set `SSB64_KSGUID_LIB`.
3. **`cmake/WindowsSdkUmLib.cmake`** — embed full path to `ksguid.lib` in Ninja at configure time.
4. **Release tags** must point at a commit that includes the scripts above. Tag `v0.9.4-beta` (commit `48a6550`) predates this fix.

## CI success markers

```
ci-refresh-vcvars-windows-sdk.ps1
Selected Windows SDK: 10.0.22621.0 (ksguid at ...)
ci-append-windows-sdk-lib.ps1 (multi-SDK scan + install fallback)
ksguid.lib: C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\ksguid.lib
```

## Old script (do not use)

If the log shows `throw "ksguid.lib not found at $ksguid"` at line 23, the job is on an **old** `ci-append-windows-sdk-lib.ps1` (single-version check only). Re-tag or re-run workflow on `main` after merging these scripts.
