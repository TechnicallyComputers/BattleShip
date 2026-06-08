# Netplay — camera load-hash drift: CObj restore (2026-06-07)

**Date:** 2026-06-07  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Synctest / rollback load at tick 509 logged `LOAD_HASH_DRIFT presentational-only` with `snap_cam=0xAD821785` vs `live_cam=0x43486FFA` while all sim partitions matched. Context: Falcon `GuardOff` after shield release; cross-ISA soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`.

## Root cause

June 2026 quantize fixed cross-ISA ULP in forward sim (`syNetplayQuantizeGMCameraState` + canonicalize at tick end). Load path still diverged:

1. **`SYNetRbSnapCameraBlob` stored `GMCamera` only** — battle `CObj` eye/at/up/fovy were not snapshotted.
2. **`syNetRbSnapApplyCamera` always ran `gmCameraRunFuncCamera`** after restore (May 2026 resim fix for stale projection). That executes a full camera integration tick using the **live CObj from the post-load forward-sim instant** (e.g. tick 510 during synctest probe of 509), mutating hashed struct fields (`target_dist`, `fovy`, …) away from the ring slot digest.

Quantize was present; the gap was **missing CObj partition + spurious integration on load**.

## Fix

| Layer | Change |
|-------|--------|
| **Blob** | `cobj_valid`, `cobj_eye/at/up`, `cobj_fovy` in `SYNetRbSnapCameraBlob`; capture quantizes CObj scalars |
| **Apply** | Restore CObj from blob; skip `gmCameraRunFuncCamera` when `cobj_valid` (legacy slots fall back to one integration step) |
| **Verify** | `syNetplayCanonicalizeGMCameraSimState()` before load-hash compare (parity with save post-tick) |
| **Diag** | `SSB64_NETPLAY_CAMERA_LOAD_DIAG=1` → `camera_apply_diag` lines (hashes, struct scalars, CObj vectors, `used_run`) |

## Verification

1. Re-soak synctest session that previously drifted @509: expect `snap_cam == live_cam` after load (no presentational-only cam drift).
2. Resim / FC reanchor: camera framing still correct (CObj restored, next forward tick runs normal `gcRunAll` camera proc).
3. Optional: `SSB64_NETPLAY_CAMERA_LOAD_DIAG=1` — `used_run=0`, `hash_after == slot_cam` on ring loads.

## Related

- [`netplay_camera_quantize_2026-06-02.md`](netplay_camera_quantize_2026-06-02.md)
- [`netrollback_camera_restore_resim_2026-05-25.md`](netrollback_camera_restore_resim_2026-05-25.md)
