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

## Yamabuki tower gate lifecycle

Saffron's tower door is a layered hazard, not just a yakumono anim:

| Layer | Runtime owner | Snapshot/hash owner | Notes |
| ----- | ------------- | ------------------- | ----- |
| Collision wall | yakumono id 3 (`mpCollisionSetYakumonoPosID`) | map yakumono blob + `map=`/`mph=` | Closed at x 960, open at x 1600. Any live `gate_pos` drift shows up as map-kin drift. |
| Stage gate state | `gGRCommonStruct.yamabuki` | `SYNetRbSnapGroundYamabuki` + `syNetSyncFoldYamabukiGateRollbackWorld` | Owns `gate_status`, `gate_noentry`, `monster_wait`, `gate_wait`, `monster_id_prev`, and `gate_pos`. |
| Door mesh | `gate_gobj` DObj tree | slot-local Yamabuki gate DObj blob + `anim=` | The visible panel is the first child DObj, not the root. Completed open anim wraps to frame 0 unless held. |
| Ground monster item | item link | item blob/hash | Spawn timing and Hitokage flame cadence can affect `item=`, `eff=`, and `figh=` independently of the door mesh. |

Important lifecycle details for rollback work:

- `Wait + gate_pos.x >= 1280` is already collision-open even before the Pokemon exists; presentation must hold the child DObj at frame 9 after the open anim completes.
- On spawn, PORT reuses `gate_wait` while `gate_status=Open` as the minimum post-spawn egress timer. This avoids immediately moving yakumono id 3 back to x 960 when the newly spawned monster's initial `monster.x - coll_width` is still below the closed threshold.
- After the egress timer expires, `grYamabukiGateUpdateOpen` still holds yakumono id 3 at x 1600 while the quantized tracked edge (`monster.x - coll_width`) is below x 960. Only once that edge reaches the doorway does it derive `gate_pos.x`, clamp to [960, 1600], and possibly close behind the monster.
- Rollback restore must run the Yamabuki gate repair after item apply so the live ground-monster GObj can be resolved before collision and presentation are finalized.
- Because `gate_wait` is already in the ground blob and world hash, reusing it as the Open-state minimum egress timer adds no new rollback serialization surface; the extended hold is derived from restored item pose and collision width.

## Verification

Re-run Zebes crouch/S-spam soak with `SSB64_NETPLAY_RESIM_TICK_TRACE=1`: expect `mph` match on `resim_tick t=405+` after epoch-0 load, and `map_yaku post-load` with `live_n == stored_n` on both peers.
