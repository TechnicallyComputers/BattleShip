# Netplay grab hold geometry — stale grabber joint / split Y projection

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (phase 3 forward-tick refresh — soak pending)

## Symptoms

2P soak (Ness grab Donkey Kong): during standard grab hold (`CatchWait` / `CaptureWait`, ticks ~500–508), victim appears upside-down with feet anchored and upper body clipped into the floor. Large characters (DK) exaggerate the pose error. Reproduced with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`; clean with synctest off.

## Root cause

Victim pose is assembled from two independent paths:

1. **X/Z + rotation** — `ftCommonCapturePulledRotateScale` reads grabber `joint_itemheavy_id` world matrix (`func_ovl0_800C9A38` / `FTParts->mtx_translate`).
2. **Y** — `ftCommonCaptureWaitProcMap` / `ftCommonCapturePulledProcMap` project onto grabber floor line, not from the hand joint.

After rollback snapshot apply, geometry refresh ran in **core apply before** presentation sync + joint anim reapply (fixed in phase 2). Offline and delay-sync looked fine; rollback-only forward sim still entered a bad coupled attractor (Ness→DK hold: victim Y floor-snapped while hand rotation inverted) because **`syNetRbSnapRefreshGrabCouplingGeometry()` ran only on load finalize**, not after each live `gcRunAll` tick. Per-tick snapshot save then persisted the wrong pose as authoritative `figh` state on both peers.

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapshotRefreshGrabCouplingGeometry()` — invalidate grabber transform, rerun capture pulled physics + wait/pulled/thrown ProcMap for each victim with `capture_gobj` | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Call refresh in **`syNetRbSnapshotFinalizeLoadFromSlot` after joint anim reapply** (not in core apply) | `netrollbacksnapshot.c` |
| **Phase 3:** `syNetRbSnapshotRefreshGrabCouplingGeometry()` after each rollback sim tick when grab coupling active (`syNetRollbackAfterBattleUpdate`, before snapshot save; includes resim replay via `BattleSimOnly`) | `netrollback.c`, `netrollbacksnapshot.c` |
| Pointer-only rebind in `syNetRbSnapRebindFighterGrabCoupling()` | `netrollbacksnapshot.c` |
| Invalidate grabber transform before hand-matrix read in `ftCommonCapturePulledRotateScale` | [`decomp/src/ft/ftcommon/ftcommoncapturepulled.c`](decomp/src/ft/ftcommon/ftcommoncapturepulled.c) |
| PORT NULL guards on stale `capture_gobj` / `capture_fp` in capture wait/pulled ProcMap | `ftcommoncapturewait.c`, `ftcommoncapturepulled.c` |

DK cargo (victim `Shouldered` + grabber `ThrowF*`) uses the same thrown ProcMap/Physics path — covered by the thrown branch in geometry refresh. See [`netplay_grab_synctest_roundtrip_2026-05-22.md`](netplay_grab_synctest_roundtrip_2026-05-22.md).

Aerial Y-from-hand attach was **not** changed — needs N64 verification first.

## Soak pass criteria

- Ness (or Fox) standard grab on DK / Mario on flat ground and mild slope: victim upright in hold, no ground clip
- Rollback during grab hold: geometry stable after resim; no inverted victim
- Synctest on: `SYNCTEST_OK` through hold + throw probes ([`netplay_grab_synctest_roundtrip_2026-05-22.md`](netplay_grab_synctest_roundtrip_2026-05-22.md))
- Yoshi / Kirby capture paths unchanged
- Existing throw release NULL guards still hold ([`netplay_grab_synctest_throw_segv_2026-05-20.md`](netplay_grab_synctest_throw_segv_2026-05-20.md))

## Upstream handoff

Consolidated patch list and recommended vanilla fix: [`../grab_coupling_geometry_handoff_2026-05-23.md`](../grab_coupling_geometry_handoff_2026-05-23.md)
