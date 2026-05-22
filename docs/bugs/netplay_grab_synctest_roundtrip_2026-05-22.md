# Netplay grab/throw SYNCTEST round-trip (finalize order + throw_desc)

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

2P soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: standard grab hold (Ness→DK) showed upside-down victim geometry and peer phase drift; same scenario clean with synctest off. Throw probe ticks logged `LOAD_HASH_DRIFT anim` (`SYNCTEST_FAIL` class).

## Root cause

Same class as [`netrollback_weapon_load_finalize_order_2026-05-20.md`](netrollback_weapon_load_finalize_order_2026-05-20.md):

1. **`syNetRbSnapRefreshGrabCouplingGeometry()` ran in core apply** (inside `syNetRbSnapRebindFighterGrabCoupling`) **before** `SyncFighterPresentation()` + `ReapplyFighterJointAnimFromSlot()`. Hand-joint reads used stale transforms; presentation then clobbered joint AObj without re-deriving victim pose.
2. **`throw_desc` not snapshotted** — motion `SetThrow` runtime pointer lost on load; throw-phase verify/release could drift after probe.

Prior `SYNCTEST_SKIP reason=grab_coupling` deferred probes but did not fix round-trip; skipping synctest during grab is not the desired end state.

## Fix

| Change | Location |
|--------|----------|
| Move `syNetRbSnapRefreshGrabCouplingGeometry()` to finalize **after** presentation + joint anim reapply | `syNetRbSnapshotFinalizeLoadFromSlot()` in [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Pointer-only `syNetRbSnapRebindFighterGrabCoupling()` in core apply / post-verify rebind | `netrollbacksnapshot.c` |
| Snapshot/restore `throw_desc_ptr` (`uintptr_t`, intern-buffer motion data — same session lifetime as `joint_anim_joint_event32`) | `SYNetRbSnapFighterBlob` capture/apply |
| Remove synctest grab-coupling skip — probes run through hold/throw when round-trip is correct | [`port/net/sys/netrollback.c`](port/net/sys/netrollback.c) |

## DK cargo (back carry)

DK forward throw → `ftCommonCaptureShoulderedSetStatus` → victim `nFTCommonStatusShouldered` with `capture_gobj` → DK `ThrowFWait`/`ThrowFWalk*` with `catch_gobj`. Victim pose uses `ftCommonThrownProcPhysics` → `ftCommonCapturePulledRotateScale` (same hand-joint + floor projection as standard hold). **Same finalize refresh path** — no separate DK cargo hook required.

## Soak pass criteria

- Ness→DK standard grab through 120-tick synctest windows: `SYNCTEST_OK`, upright hold, clean throw
- DK grab → forward throw → cargo walk/jump: victim on back through synctest probe
- No `LOAD_HASH_DRIFT anim` on throw release probe; no upside-down victim with synctest on
- Fox grab throw path unchanged (prior NULL guards on `throw_desc` remain)

## Related

- [`netplay_grab_geometry_stale_joint_2026-05-22.md`](netplay_grab_geometry_stale_joint_2026-05-22.md) — Phase 1 geometry refresh (now wired into correct finalize phase)
- [`netplay_grab_synctest_throw_segv_2026-05-20.md`](netplay_grab_synctest_throw_segv_2026-05-20.md) — coupling rebind + throw release guards (skip superseded)
