# Netplay intro Wait-phase camera freeze (Yoshi's Story)

**Date:** 2026-06-11  
**Status:** Fix shipped (soak pending)  
**Map:** Yoshi's Story (`gkind=5`)

## Symptom

After intro resim position fix (Phase 3), fighter poses track correctly on rollback but the battle camera still stops following during the intro countdown — especially on the AppearL fighter.

## Log evidence (soak1-linux / soak1-android)

- Intro epoch 1: load `@229`, mismatch `@230`, resim to `@232` — structural rollback healthy; fighter positions at load match sim.
- `cam=0x4076A21D` flat from tick ~88 through GO on **both** peers (GMCameraStruct hash — not a cross-peer desync).
- Status trail: Kirby P0 hits `nFTCommonStatusWait` (10) at tick 196; Yoshi P1 at tick 211. Load `@229` has **both** fighters in Wait.
- `IntroLoadFidelityScope` only covers Appear statuses + `nFTCommonStatusEntry` (5) — **not** Wait.
- Phase 3 `syNetRbSnapshotRefreshLiveIntroPresentationAfterInterface()` was gated on `IntroLoadFidelityScope`, so it **stopped running** for ticks 211–386 while countdown continued.
- Rollback load still calls `syNetRbSnapApplyCamera()` with `cobj_valid`, pinning CObj eye/at from the ring slot.

## Root cause

Two coupled issues:

1. **Scope gap:** Most of the visible intro countdown runs in `nFTCommonStatusWait` after Appear/Entry exit. Live camera integrate was tied to `IntroLoadFidelityScope`, so presentation frames after tick ~211 never ran an extra `gmCameraRunFuncCamera` pass.
2. **CObj re-pin:** Load apply restores frozen CObj from snapshot; without a follow-up integrate during Wait, the viewport stays at the load-tick eye/at even while fighter anim advances.

Flat `cam` hash during Wait is expected (GMCameraStruct params stabilize); the bug is **visual CObj**, not sim hash divergence.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

- Gate intro **camera** presentation on `game_status == nSCBattleGameStatusWait` (`syNetRbSnapIntroCountdownWaitActive`), not `IntroLoadFidelityScope`.
- Keep figatree refresh scoped to Appear/Entry only.
- After `syNetRbSnapApplyCamera` CObj restore during intro Wait, run one live `gmCameraRunFuncCamera` integrate.
- `syNetRbSnapshotResyncLiveFightersFromSlotForSim`: drop slot camera re-pin; integrate live camera instead.
- Defer-wait / replay-gate / deferred refresh paths: always integrate camera during Wait; figatree re-pin only when Appear/Entry scope is active.

## Verify

Soak Yoshi's Story intro with `SSB64_NETPLAY_CAMERA_LOAD_DIAG=1` optional. Camera should track AppearL through countdown and survive epoch-1 resim `@229`.
