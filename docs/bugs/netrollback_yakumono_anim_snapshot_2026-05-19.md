# NetRollback — yakumono DObj anim snapshot for map-kin resim

**Date:** 2026-05-19  
**Status:** Fix shipped (soak verification pending)

## Symptom

During rollback resim with matched inputs, `figh` / `world` / `rng` stayed aligned while `mph` (`syNetSyncHashMapCollisionKinematics`) diverged from the first replay tick (e.g. load 404, mismatch 405 on Zebes crouch soak). Post-load `mph` matched cross-peer; split appeared after one `mpCollisionPlayYakumonoAnim` step.

## Root cause

The map snapshot partition stored yakumono **kinematic outputs** (`translate`, `gMPCollisionSpeeds`, `user_data.s`, `gMPCollisionUpdateTic`) but not the **animation runtime** that produces them on the next tick:

- `anim_wait` / `anim_frame` / `anim_speed`
- AObj chain scalars (first 6 nodes, same as fighter `SYNetRbSnapDObjAnimBlob`)
- `anim_joint.event32` stream cursor (`AObjAnimAdvance` is `(script)++`)
- `dobj->flags` (Show/Hidden transitions in `PlayYakumonoAnim`)

After load, hashed kinematics matched saved slot hashes locally, but live yakumono anim state still reflected forward-sim history, so peers diverged on the first autonomous anim step.

## Fix

Extend `SYNetRbSnapYakuBlob` with `SYNetRbSnapDObjAnimBlob anim`, `flags`, and `anim_joint_event32`. Capture/apply via `syNetRbSnapCaptureYakuDObj` / `syNetRbSnapApplyYakuDObj` (anim + cursor + flags, then kinematics). Post-load log: `map_yaku post-load tick=… live_n=… stored_n=… mph=0x…`.

## Notes

- Yakumono count remains stage-static (`mpCollisionAllocYakumono`); index `i` is the identity key.
- `aobj->interpolate` (TraI) is not snapshotted; Zebes platforms typically use TraX/Y/Z. Add per-node `interpolate` if a stage needs TraI.
- Fighter joints had the same `event32` gap; see [`netrollback_fighter_joint_anim_event32_2026-05-19.md`](netrollback_fighter_joint_anim_event32_2026-05-19.md).

## Verification

Re-run Zebes crouch/S-spam soak with `SSB64_NETPLAY_RESIM_TICK_TRACE=1`: expect `mph` match on `resim_tick t=405+` after epoch-0 load, and `map_yaku post-load` with `live_n == stored_n` on both peers.
