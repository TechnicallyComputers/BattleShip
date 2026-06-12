# Netplay DamageFly resim airborne laydown — 2026-06-12

**Status:** FIX SHIPPED (re-soak pending)

## Symptom

soak1 Mario vs Link @520 (Linux + Android): forced resim loads tick 519 with Link in
`DamageFlyHi` (airborne knockback). Visually he appears to snap into `DownBounceU` /
`DownWaitU` laydown pose while still floating above the stage during fast resim replay.

Re-soak after airborne fix hit a **different entry path**: knockdown commits at tick ~502
(`DamageFlyN` → `DownBounceU`) before load @519, so Link is already `DownBounceU` +
`ga=ground` at rollback. Laydown presentation can still be wrong when MPColl floor bind is
stale even though sim digests match.

## Root cause

Snapshot load restores `MPCollData.pos_prev` / `pos_diff` from the ring capture while
TopN translate is already at the live knockback position. On the first resim ProcMap
tick, `mpCommonCheckFighterDamageCollision` integrates from stale `pos_prev` and can
set `MAP_FLAG_FLOOR` in the damage overlay even though `floor_line_id` is unset — vanilla
`ftCommonDamageAirCommonProcMap` then calls `ftCommonDownBounceSetStatus` (laydown) while
Y is still above the visible floor.

Sim digests still match cross-peer; this is a load-time collision re-anchor gap, not desync.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

**Airborne knockback (original path)**

- `syNetRbSnapStatusInAirborneDamageKnockbackScope` — `DamageFlyHi`..`DamageFlyRoll` and
  `DamageFall` while `ga == air`.
- `syNetRbSnapRefreshAirborneDamageKnockbackCollAfterLoad` — rebind MPColl to TopN, reset
  `pos_prev`/`pos_diff` from live translate, clear damage `coll_mask_*`, run
  `mpCommonCheckFighterDamageCollision`; call `mpCommonSetFighterGround` only when floor
  contact is confirmed.

**Grounded knockdown (re-soak sibling path)**

- `syNetRbSnapStatusInGroundedDownTechFloorScope` — `DownBounceD`..`DownWaitU` while
  `ga == ground`.
- `syNetRbSnapRefreshGroundedDownTechCollAfterLoad` — MPColl re-anchor only when
  `pos_prev`/`pos_diff` are stale (no `mpCommonCheckFighterOnFloor` on load: that probe
  mutates translate/coll away from the ring blob and caused linux/android `figh` split by
  tick 520 → SIGSEGV @600 in soak1).

**Shared**

- `syNetRbSnapRefreshKnockdownCollFromSlot` — runs both repairs on load/resync hooks:
  `PrepareLoadedSlotForVerify`, `ResyncLiveFightersFromSlotForSim`.
- Extend `syNetRbSnapStatusInGameplayResimAnimFragileScope` to include airborne knockback
  statuses for PreSim figatree repair through landing.

## Re-soak pass criteria

Mario vs Link soak1 @520, both peers:

**Airborne path** (if knockback still airborne at load @519):

- Load @519 leaves Link in `DamageFlyHi` with plausible tumble (not instant laydown in air).
- First landing frame transitions to `DownBounceU` with Y near floor.

**Grounded path** (knockdown committed before load, as in post-fix re-soak):

- Load @519: `DownBounceU`/`DownWaitU` with `ga=ground`, Y near stage floor (~1398 on Dream Land).
- Link progresses through down-tech (not frozen `DownWaitU` through tick 600); recovers to Wait.

**Both paths:**

- `resim complete` unchanged; no new `LOAD_HASH_DRIFT` / `BATTLE_SIM_HOLD`.
