# Gameplay resim: battle camera frozen after rollback (CObj pin)

**Date:** 2026-07-01  
**Scope:** `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED — soak pending (Sector Z cross-ISA FC resim)

## Symptoms

After FC resim with an intro-era load anchor (Sector Z soak: load @360 → replay to @481), fighters track correctly post-resim but the **battle camera stays fixed** — viewport no longer follows Fox/Pikachu on the Great Fox deck. Same on both peers (presentation bug, not desync).

## Root cause

June 2026 `cobj_valid` restore pins battle `CObj` eye/at from the ring slot for load-hash fidelity, but `syNetSyncHashGMCamera()` hashes **GMCamera struct only** — not CObj. After load:

1. **`syNetRbSnapApplyCamera`** restored CObj and only re-ran `gmCameraRunFuncCamera` during **intro Wait**, not gameplay rollback loads.
2. **`syNetRbSnapshotRefreshIntroPresentationAfterResimComplete`** gameplay path rebuilt part transforms only — no CObj repair at resim end.
3. Intro-span resim (load tick still in countdown, target tick in live GO) left the viewport at **load-tick intro framing** while sim replayed 120 ticks of deck combat.

Log signature: long flat `cam=0x4FAA41B3` (ring slot hash) while `figh`/`anim` move; `camera_apply_diag` shows `cobj_valid=1`, `hash_after ≠ slot_cam`.

## Fix

| Site | Change |
|------|--------|
| `syNetRbSnapApplyCamera` | After `cobj_valid` restore, integrate when intro Wait **or** rollback semantics active **and** load-hash verify disabled |
| `syNetRbSnapshotRefreshIntroPresentationAfterForwardResimTick` | Gameplay resim: integrate each forward replay tick (camera hash verify skips integrate on apply) |
| `syNetRbSnapshotRefreshIntroPresentationAfterResimComplete` | Gameplay: CObj-only restore from **target_tick** slot + one integrate |
| `syNetRbSnapshotResyncLiveFightersFromSlotForSim` | Camera integrate on gameplay rollback resync (same gate as intro) |

## Test plan

1. Sector Z soak with FC resim load @360: camera follows fighters through and after resim (no frozen intro framing on deck).
2. Intro countdown resim (Yoshi's Story / early anchor): camera still tracks through Appear → GO (intro Wait integrate unchanged).
3. Optional `SSB64_NETPLAY_CAMERA_LOAD_DIAG=1`: post-resim `cobj_eye/at` move with fighters, not stuck at load-tick values.

## Related

- [netplay_camera_load_cobj_restore_2026-06-07.md](netplay_camera_load_cobj_restore_2026-06-07.md)
- [netplay_intro_wait_camera_scope_2026-06-11.md](netplay_intro_wait_camera_scope_2026-06-11.md)
- [netrollback_camera_restore_resim_2026-05-25.md](netrollback_camera_restore_resim_2026-05-25.md)
