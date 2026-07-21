# Launch SIGSEGV in `portFixupSprite` (`fault_addr=0x73c0`)

**Date:** 2026-07-19  
**Status:** FIXED (local tree restore + packaging / first_run / generator guards)  
**Platforms:** Linux AppImage / AppDir (repro on locally packaged Netplay build)

## Symptom

Immediate crash on launch during scene 27 (`mnStartupStartScene`):

```
SSB64: !!!! CRASH SIGSEGV fault_addr=0x73c0
... portFixupSprite+0x104 ...
GFX STALE-DL DIAG badCmd host=0x73c0   # red herring from crash handler
```

`0x73c0` is exactly `llN64LogoSprite`. The N64 logo path is:

```c
sprite = lbRelocGetFileData(Sprite*,
    lbRelocGetExternHeapFile(llN64LogoFileID, heap),
    llN64LogoSprite);
```

When the file base is `NULL`, `GetFileData` yields `NULL + 0x73c0` → `portFixupSprite` dereferences `0x73c0`.

## Root cause (two stacked failures)

1. **Working tree deleted `yamls/us/reloc_*.yml`** (still tracked in git; `git status` showed `D`).  
   - `scripts/package-linux.sh` copied only `audio.yml` + `particles.yml` into the AppDir.  
   - Recipe hash at configure time also omitted reloc yaml content → binary wanted `95ccbf57…` vs prior `86e4d929…`.  
   - first_run re-extracted with the incomplete yaml set → **audio/particles-only** `BattleShip.o2r` (~27 files).

2. **`GenerateRelocArtifacts` / `generate_reloc_table.py` ran against that empty yaml dir** and rewrote `port/resource/RelocFileTable.us.cpp` as **2132× `NULL`**.  
   - `portLoadRelocResource` hit `gRelocFileTable[id] == NULL` and returned `nullptr` **without** the stale-o2r `_exit` path (that path only runs after a non-NULL path fails `LoadResource`).  
   - Even after a full o2r was restored, the AppDir binary still crashed until RelocFileTable was restored and `ssb64` relinked.

## Fix

| Layer | Change |
|-------|--------|
| Tree | `git checkout -- yamls/us/` + restore `RelocFileTable.us.cpp`; rebuild `ssb64`; refresh AppDir binary + full o2r |
| `scripts/package-linux.sh` / `package-macos.sh` | Prefer `$BUILD_DIR/yamls/$VER`; **fail** if `< 15` `reloc_*.yml` |
| `port/first_run.cpp` | Reject / re-extract archives with no `reloc_*` zip entries before stamping `.recipe` |
| `tools/generate_reloc_table.py` | Exit non-zero if yaml parse yields 0 or `< 90%` of `FILE_COUNT` entries |
| `port/bridge/lbreloc_bridge.cpp` | NULL `gRelocFileTable` entry → actionable message + `_exit(0)` (same class as stale o2r) |

## Verify

Smoke launch of `dist/BattleShip-Netplay.AppDir` after restore: past `mnStartupStartScene`, frames advance (no `fault_addr=0x73c0`).

## Operator notes

- If launch dies on `0x73c0` again: check `strings BattleShip \| rg N64Logo` (must hit) and `unzip -l BattleShip.o2r \| rg reloc_ \| wc -l` (should be ~2000+).  
- Do not delete `yamls/us/reloc_*.yml` in a dirty tree before packaging; regenerate via `tools/generate_yamls.py` if needed, then commit.
