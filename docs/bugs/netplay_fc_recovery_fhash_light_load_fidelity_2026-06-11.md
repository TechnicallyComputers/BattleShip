# Netplay: FC recovery deadlock — fhash_light load fidelity (2026-06-11)

## Symptom

Yoshi's Story soak after intro resim @230: match runs until FC validation **480**. Inputs agree; ring tokens diverge (`figh` + `world`). Deferred FC recovery loads ring@360 → resim to 481, but load verify fails:

```
LOAD_HASH_DRIFT tick=360 figh=0x0D5FD086/0x592E3386 (world/item match)
guard_shield_load_drift: per-slot fhash_light MATCH (63917D39 / 923200AB)
resim-sim-core-reject reason=figh fc_recovery
RESIM_LOAD_FIDELITY_RETRY failed=360..355
```

Sim freezes at 481 (`publish=0 sup=1`, `allow_battle_sim=0`) until `VS_SESSION_END`.

## Root cause

1. **Strict aggregate `figh` gate** — `syNetRollbackLoadHashDriftIsResimSimCoreOk` and an early `LOAD_HASH_DRIFT fc_recovery figh reject` path required `live_f == slot_figh` during FC recovery load. After blob apply, aggregate fighter hash often drifts while **per-slot `fhash_light`** (ring recipe) still matches (joint/anim presentation finalize).

2. **Battle sim hold after failed recovery** — `BeginResim` arms `BATTLE_SIM_HOLD` on load fail; deferred FC path cleared `fc_recovery` but did not clear the hold → permanent stall.

Latent **item cosmetic tolerance** at FC 240/360 allowed cross-peer item drift after first resim; real `world`/`figh` divergence by 480 is separate follow-up.

## Fix

1. **`syNetRollbackFcRecoveryFighDriftOk`** — when `fc_recovery` active and aggregate figh mismatches, accept load if `HashFightersLightFromLive() == GetSlotHashFighterLight(tick)`.

2. **Remove early `fc_recovery figh reject`** in `syNetRollbackLoadPostTick` (redundant with sim-core-ok path).

3. **`syNetRollbackClearBattleSimHoldAfterLoadFail()`** when deferred FC `BeginResim` fails.

## Validation

Re-run Kirby/Yoshi Yoshi's Story soak: expect FC recovery resim @361→481 to complete (or walk back cleanly) without sim=481 freeze; no `BATTLE_SIM_HOLD` after failed recovery.
