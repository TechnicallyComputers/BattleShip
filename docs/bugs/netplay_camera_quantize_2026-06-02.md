# Netplay — battle camera cross-ISA quantize (2026-06-02)

**Date:** 2026-06-02  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Cross-ISA soak with PK Thunder hold quantize: sim `cam=` hashes matched during hold/jibaku, but rollback load logged `LOAD_HASH_DRIFT presentational-only` with `snap_cam ≠ live_cam` (e.g. `0x4FAA41B3` vs `0x29D47287` @ tick 360). Camera follow during PK Thunder (`is_camera_follow` on head weapon) uses unquantized pan/FOV integrators in `gmcamera.c` while weapon translate was already grid-rounded.

## Root cause

`syNetSyncHashGMCamera()` folds `GMCamera` scalars only; snapshot stored raw struct with no quantize on capture/apply. Cross-ISA ULP drift in `target_dist`, `fovy`, zoom/follow params, and incremental pan math diverged ring slot camera hash from live forward sim. `CObj` eye/at/up were not normalized at the sim boundary.

## Fix

| Layer | Change |
|-------|--------|
| **Quantize** | `syNetplayQuantizeGMCameraState` — all `GMCamera` f32 fields + pause eye floats |
| **Live sim** | `syNetplayCanonicalizeGMCameraSimState` — struct + battle `CObj` eye/at/up/fovy; called from `syNetplayCanonicalizeActiveFightersForNetplay` |
| **Rollback** | Quantize camera blob on capture; quantize live struct, run `gmCameraRunFuncCamera`, canonicalize again on apply |

## Verification

1. Cross-ISA resim load: no `LOAD_HASH_DRIFT` on `cam` when only presentational drift remained (or snap/live cam match after load).
2. PK Thunder hold: camera framing tracks head without post-resim focus snap.
3. Bisect: `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` should restore cam drift at load boundaries.

## Related

- [`netplay_ness_pkthunder_hold_quantize_2026-06-02.md`](netplay_ness_pkthunder_hold_quantize_2026-06-02.md)
- [`netrollback_camera_restore_resim_2026-05-25.md`](netrollback_camera_restore_resim_2026-05-25.md)
- [`netplay_cross_isa_determinism_2026-05-27.md`](netplay_cross_isa_determinism_2026-05-27.md)
