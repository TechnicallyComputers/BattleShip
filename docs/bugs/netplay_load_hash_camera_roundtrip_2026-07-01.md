# Netplay — load-hash camera round-trip (presentational-only drift)

**Date:** 2026-07-01  
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Sector Z soak2 (`session=55707289`, ~2086 ticks): paired `LOAD_HASH_DRIFT` on **cam only** at ticks 389, 480, 519, 815, … on both peers. Sim-core hashes matched; engine soft-continued as presentational-only.

```
camera_apply_diag: hash_before=slot_cam, hash_after≠slot_cam, used_run=1
LOAD_HASH_DRIFT snap_cam=0x987AE3B2 live_cam=0xE5A154C3 (figh/world/map/rng/anim match)
```

## Root cause

June 2026 gameplay resim camera fix runs `gmCameraRunFuncCamera` inside `syNetRbSnapApplyCamera` when rollback semantics are active. That integrate step mutates **GMCamera struct fields** folded into `syNetSyncHashGMCamera()` (`target_dist`, `pzoom_dist`, …). Load-hash verify compares live hash immediately after apply, so the extra integrate breaks save→apply round-trip even though CObj is excluded from the hash fold.

## Fix

- Skip `gmCameraRunFuncCamera` in `ApplyCamera` when `syNetRollbackLoadHashVerifyEnabled()` (default on).
- Integrate during forward resim via `RefreshIntroPresentationAfterForwardResimTick` (each gameplay replay tick) and existing resim-complete / intro paths — presentation unchanged, verify oracle clean.

## Verify

Re-soak with `SSB64_NETPLAY_SNAPSHOT_CAMERA_LOAD_DIAG=1`: expect zero cam-only `LOAD_HASH_DRIFT` lines; camera still tracks through deck resim.
