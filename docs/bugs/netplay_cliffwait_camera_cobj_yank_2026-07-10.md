# Netplay: CliffWait end-match camera loses fighters (CObj yank)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `43656188`  
**Match:** Mario vs Luigi, Dream Land — determinism PASS through `VS_SESSION_END` @9274  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

At match end the camera jumped away from both fighters for ~2 seconds (stopped framing the
right-ledge CliffWait), then slowly recovered. Both peers identical — not a cross-ISA desync.

## Log evidence (soak2-linux / soak2-android)

| Tick | Observation |
|------|-------------|
| @9029 | `cobj_at≈(1593, 310)` — sane, fighters on right platform |
| @9080→9087 | Luigi CliffCatch→**CliffWait** `topn≈(2405, -336)`; Mario Wait `≈(2100, 0)` |
| @9149 | **`cobj_eye≈(-3184, 3526)`, `cobj_at≈(-235, 1184)`** — look-at mid/left-high, not on fighters |
| @9150 emergency | live still wild: `cobj_at≈(3, 1058)` (one pan step toward fighters) |
| @9269 | recovered: `cobj_at≈(2192, -14)` |
| @9274 | `VS_SESSION_END`; Luigi still CliffWait for 187 ticks (fall_wait 480/1080 — vanilla-legal hang) |

`SYNCTEST_OK` @9029/9149/9269; `state_diverge=0`. `camera_mode=0` both fighters. `used_run=0` on
apply diags (load-hash verify skips `gmCameraRunFuncCamera`).

Interest math from those TopNs cannot produce `at.x≈-235` / `at.y≈1184`. `syNetSyncHashGMCamera()`
folds struct scalars only — **not CObj eye/at** — so peers stay green while the viewport is wrong.

Hidden/excluded live effects still ran during the window (`effect_count=0` in ring saves; verify
ejected `hidden_cosmetic` shells with `anim_frame=250` / `16` at the 9149 probe). Quakes are
intentionally excluded from the rollback effect fold (`netplay_quake_cosmetic_rollback_exclusion`)
but still call `gmCameraSetVelAt` on forward sim. A shell with stage-scale DObj translate yields
thousands of units of `vel_at` per frame; pan (~5–10%/tick) then takes seconds to recover.

## Root cause

1. **Presentation-only camera impulse** (`vel_at` / CObj) can be yanked hard by a bad quake DObj
   amplitude while gameplay hashes stay matched.
2. **Snapshot apply** restored pending `vel_at` from the blob (one-frame impulse, not hashed), so
   load/synctest could re-inject a yank on the next integrate.
3. Synctest probe apply uses `used_run=0` (correct for load-hash fidelity); that pins whatever wild
   CObj was already in the ring, amplifying the visible pop at probe boundaries.

Not a classic `FTStatusVars` / `camera_mode` union stomp (modes stayed Default). CliffWait hang
itself is separate from the CliffSlow scrub bug (already fixed); auto-fall had not expired.

## Fix

1. **`decomp/src/ef/efmanager.c`** — clamp quake `gmCameraSetVelAt` impulse magnitude to 250 when
   DObj-derived amplitude is absurd; log `quake_camera_impulse_clamped`.
2. **`port/net/sys/netrollbacksnapshot.c`** — zero `vel_at` after camera blob apply; optional
   `SSB64_NETPLAY_CAMERA_INTEREST_DIAG=1` logs when `cobj_at` diverges from
   `gmCameraUpdateInterests` by >500.
3. **`port/net/sys/netrollback.c`** — after synctest emergency restore, one
   `gmCameraRunFuncCamera` + canonicalize so presentation starts pan recovery immediately.

## Test plan

- [ ] Re-soak Mario/Luigi Dream Land (or any long CliffWait) with synctest on; confirm no multi-second
      camera loss near session end; grep for `quake_camera_impulse_clamped` / `camera_interest_div`.
- [ ] Optional: `SSB64_NETPLAY_CAMERA_INTEREST_DIAG=1` + `SSB64_NETPLAY_CAMERA_LOAD_DIAG=1`.
- [ ] Offline control: quake shake still visible but not stage-teleporting.
