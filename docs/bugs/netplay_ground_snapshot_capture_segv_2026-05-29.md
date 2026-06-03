# Netplay ground snapshot capture SIGSEGV (Saffron / automatch tick 0)

**Date:** 2026-05-29  
**Status:** **FIX SHIPPED (soak pending)**  
**Platform:** Android aarch64 (also affects any PORT build using rollback ring save)

## Symptom

Android client hard-exited (signal 11) during first VS ticks after automatch connect on Saffron City (`stage=7`). Logcat stack:

```
syNetRbSnapCaptureGround
  ← syNetRbSnapFillSlotFromLive
    ← syNetRbSnapshotSaveMarked
      ← syNetRollbackSavePostTick
        ← syNetRollbackAfterBattleUpdate
          ← scVSBattleFuncUpdate
```

Linux host continued alone, then hit `strict remote MISS stall abort` waiting for remote wire input — fallout from peer death, not root cause.

## Root cause

Two related issues:

1. **`syNetRbSnapCaptureGround` dereferenced cached `*_gobj` pointers** (`src->monster_gobj->id`, etc.) without validating the GObj is still linked. Stale non-NULL pointers in `gGRCommonStruct` (a **union** of per-stage var blobs) fault when read.

2. **Automatch taskman scene loads bypass `scManagerRunLoop`'s union zero.** Staging → VS uses `syTaskmanLoadScene` without the `memset(&gGRCommonStruct, …)` that was added for the Zebes stale-DL family in `scManagerRunLoop`. Rematch / staging paths could leave prior-stage GObj* cache values in the Yamabuki view of the union. **`grYamabukiInitGroundVars` did not clear `monster_gobj`** before first tower spawn (gate_gobj is recreated; monster is not).

## Fix

| Layer | Change |
|-------|--------|
| Capture hardening | `syNetRbSnapCaptureGround` uses existing `syNetRbSnapGobjId()` (live link-list validation) for all stage cached GObj IDs — same class as `netrollback_snapshot_guards_2026-05-16.md`. |
| Taskman lifecycle | `syTaskmanLoadScene`: `memset(&gGRCommonStruct, 0, …)` before `func_start`. After `gcEjectAll()` on task break in `syTaskmanCommonTaskUpdate` / `Draw`. |
| Yamabuki init | `grYamabukiInitGroundVars`: `monster_gobj = NULL` at entry. |

## Verification

- Rebuild `ssb64` / Android netplay debug APK.
- Soak: automatch on Saffron (Android client + Linux host), confirm no SIGSEGV at tick 0–10 and on rematch without app restart.

## Related

- [`zebes_acid_effect_stale_dl_link_holder_2026-05-23.md`](zebes_acid_effect_stale_dl_link_holder_2026-05-23.md) — `gGRCommonStruct` union + scManager zero  
- [`netrollback_snapshot_guards_2026-05-16.md`](netrollback_snapshot_guards_2026-05-16.md) — `syNetRbSnapGobjId`  
- [`netplay_capsule_orphan_hold_yamabuki_double_spawn_2026-05-19.md`](netplay_capsule_orphan_hold_yamabuki_double_spawn_2026-05-19.md) — Yamabuki monster lifecycle
