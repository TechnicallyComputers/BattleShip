# Link down-air hit detect cross-ISA quantize (2026-05-30)

**Date:** 2026-05-30  
**Status:** Phase 3 (non-mutating hit tests) — cross-ISA soak pending  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `decomp/src/ft/ftmain.c`

## Symptoms

Cross-ISA netplay with `SSB64_NETPLAY_SIM_F32_QUANTIZE` on (default during VS): Link **down aerial** (`AttackAirLw`) should bounce off an opponent via `ftCommonAttackAirLwProcHit` but often **tunnels through** the fighter and lands on the stage.

Not seen offline (quantize off) or on same-ISA rollback-only sessions.

## Root cause

Link’s bounce is hit-driven (`proc_hit`), not map collision. The coarse gate is `gmCollisionCheckFighterInFighterRange`, which compares:

- Attacker `FTAttackColl.pos_curr` / `pos_prev` from `gmCollisionGetFighterPartsWorldPosition` (matrix multiply in `gmcollision.c`)
- Victim root `DObj` translate (quantized after `gcPlayDObjAnimJoint`)

Cross-ISA fixes quantized joints and snapshot `MPCollData`, but **attack coll world positions were left on raw ISA-specific floats**. At stage scale the 1/65536 grid cannot merge 1-ULP matrix drift; the boolean range test can fail for one frame → no `proc_hit` → `ftCommonAttackAirProcMap` lands on the floor.

## Fix

1. **`syNetplayQuantizeFTAttackColl`** — quantize `pos_curr` (and `pos_prev` when interpolating) after each `gmCollisionGetFighterPartsWorldPosition` in `ftMainProcPhysicsMap`.
2. **`syNetplayCanonicalizeFighterHitDetectPose`** — quantize victim root translate + all active attack colls on one fighter.
3. **`syNetplayCanonicalizeFighterHitDetectPose(this_gobj)`** at the start of `ftMainSearchHitFighter` (victim root + active colls).
4. **`syNetplayQuantizeFTAttackColl(other_attack_coll)`** immediately before each `gmCollisionCheckFighterInFighterRange` call.

**2026-05-30 SEGV follow-up:** Removed scene-wide `syNetplayCanonicalizeHitDetectSceneOnce` (iterated all fighters during hit search). Added NULL `attack_coll->joint` / `joint->parent_gobj` guards before `gmCollisionGetFighterPartsWorldPosition` (`ftmain.c`) and early return in `gmCollisionGetFighterPartsWorldPosition` — NULL `parent_gobj` caused `ftGetStruct(NULL)` (`fault_addr=0x0`) during Link dair contact when hitboxes were refreshed mid-status.

**2026-05-30 phase 2 (superseded):** Scene-wide in-place canonicalize + `size`/`vec_scale` quantize — caused early Zebes acid; see [netplay_hit_detect_nonmutating_quantize_2026-05-30.md](netplay_hit_detect_nonmutating_quantize_2026-05-30.md).

**2026-05-30 phase 3 (non-mutating):**

- Removed `syNetplayCanonicalizeHitDetectSceneOnce`; fighter–fighter tests use scratch grid copies in `gmcollision.c` only.
- Zebes acid: quantize fighter Y, acid surface Y, and `acid_level_curr`/`step` (live + snapshot).
- Attack coll persist: `pos_curr`/`pos_prev` only (physics + snapshot); no `size`/`vec_scale` snap.

## Two distinct failure modes — do not conflate

Soak triage must separate these; they live in different code and have different fixes:

1. **Missed hit detect** — no damage, no hitstun on the victim. Coarse AABB (`gmCollisionCheckFighterInFighterRange`) or damage-coll test fails for a frame. Addressed by phases 1–3 (scratch quantize of attack coll world pose vs victim root).
2. **Hit without bounce** — victim *does* take damage / enters hitstun, but Link keeps falling and lands on the stage instead of rebounding. This is **not** a collision miss. Link's bounce is `proc_hit` driven (`ftCommonAttackAirLwProcHit`): `ftMainProcParams` only rebounds `vel_air.y` when `attack_damage != 0` **and** `fp->proc_hit != NULL`.

### Hit-without-bounce root cause (2026-05-30)

`proc_hit` is a function pointer, installed on dair entry (`ftCommonAttackAirCheckInterrupt`), and is **not** part of the fighter snapshot blob. Every rollback/synctest load runs `syNetRbSnapshotRebindAllFighters` → `syNetRbSnapRebindFighterStatusProcs`, which calls `ftMainRebindStatusProcs` (restores only `proc_update`/`proc_physics`/`proc_map` from the status table) and then explicitly sets `fp->proc_hit = NULL`. If Link is mid-dair (`status=213`) across a rebind, `proc_hit` is dropped: the next contact still sets `attack_damage` via search-hit, but `ftMainProcParams` has no handler to call → no `vel_air.y` rebound → visible pass-through. Fits a *single* pass-through in a long soak while damage otherwise syncs.

**Fix:** `syNetRbSnapRebindFighterStatusProcs` now reinstalls `fp->proc_hit = ftCommonAttackAirLwProcHit` when `status_id == nFTCommonStatusAttackAirLw` and `fkind` is Link/NLink — mirroring how Thunder Jolt / fireball throw statuses reinstall `proc_accessory` after a rebind.

**Diagnostics:** `ring_save_player` (under `SSB64_NETPLAY_SNAPSHOT_RING_SAVE_DIAG=1`) now also logs `vel_y_live` / `vel_y_blob` (raw f32 bits), `atk_dmg` (live `attack_damage`), `proc_hit` (`0`=NULL, `1`=`ftCommonAttackAirLwProcHit`, `2`=other), and `rehit` (live `rehit_timer`). The pass-through frame is where Link is `status=213` with `atk_dmg>0` and `proc_hit=0` (or `vel_y_live` not rebounded to ≈`40.0F`). `fighter_field_diff` already carries `vel_air_y` on load drift.

## Verification

- Cross-ISA (Linux x86_64 ↔ Android arm64): Link down+A through opponent → bounce (`vel_air.y` rebound), not ground landing from same attack.
- Bisect: `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` on both peers should restore vanilla tunneling risk / pre-fix behavior.
- Same-ISA netplay: no regression (quantize is symmetric).

## Related

- [netplay_cross_isa_determinism_2026-05-27.md](netplay_cross_isa_determinism_2026-05-27.md) — grid + anim hooks
- [netplay_thrown_item_world_pose_fma_2026-05-30.md](netplay_thrown_item_world_pose_fma_2026-05-30.md) — `gmcollision.c` FMA matcher
