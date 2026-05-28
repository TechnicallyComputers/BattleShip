# Fox Special-Lw reflector snapshot load drift — 2026-05-27

**Status:** FIX SHIPPED (soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`, `port/net/sys/netsync.c`

## Symptom

Fox vs Fox automatch (Linux host + Android client): `eff` hash fork at tick **134** (reflector spawn, `status=0xE0` / 224, `motion=199`). `figh_light` matched through ~359 while **`figh_full` diverged silently** ticks 241–359. **FC@360** failed on snap@359 full fighter tokens; inputs agreed. Reanchor **LOAD_HASH_DRIFT @240**: scalar fighter fields matched blob, but live `eff` / `anim` / `cam` did not round-trip (`eff` slot `0x9404D2B2` vs live `0x811C9DC5`).

## Root cause

1. **Load path:** Reflector effect GObj missing after apply while `fox_speciallw_effect_gobj_id` on the fighter blob remained valid — coupled rebind left `status_vars.fox.speciallw.effect_gobj` NULL and `hash_effect` / full fighter hash drifted.
2. **Finalize order:** `ReapplyFighterJointAnimFromSlot` ran before Fox reflector effect pointers were restored, so effect `proc_update` could clobber fighter motion/anim state during verify.
3. **Verify timing:** `syNetRollbackVerifyLoadedSlot` ran before `syNetRbSnapshotFinalizeLoadCoupling`, reading pre-coupling live `eff` when reflector was still absent.
4. **Effect hash fold:** `syNetSyncHashActiveEffectsForRollback` did not fold Fox reflector `effect_vars.reflector.index/status`, so cross-peer `eff` could diverge without tripping the same partition early.

Cosmetic RNG in `efmanager.c` already routes through `syUtilsRand*Cosmetic` (`syNetRollbackIsResimulating` gate in `utils.c`); not the primary Fox reflector class.

## Fix

1. **`syNetRbSnapEnsureFoxReflectorEffectsFromSlot`** — Before effect reconcile on load: Fox in SpecialLw / AirLw scope + valid `fox_speciallw_effect_gobj_id` + missing GObj → `syNetRbSnapTryRespawnEffectFromBlob` (snapshot blob or synthetic FOX_REFLECTOR row).
2. **Finalize** — When any Fox is in reflector scope, **`syNetRbSnapRebindFighterEffectGobjs` before `ReapplyFighterJointAnimFromSlot`**.
3. **`syNetRollbackLoadPostTick`** — Run **`FinalizeLoadCoupling` before verify** (then post-verify joint reapply unchanged).
4. **`syNetSyncHashActiveEffectsForRollback`** — Fold reflector `index` + `status` when `proc_update == efManagerFoxReflectorProcUpdate`.

## Fox Special-Lw bisect soak (manual)

Host Linux + client Android, Fox vs Fox, same stage/seed as failing session. Enable Phase 0 bundle from [`netplay_environment_variables.md`](../netplay_environment_variables.md) (`SNAPSHOT_RING_SAVE_DIAG`, `VALIDATION_DUAL_HASH`, `FRAME_COMMIT_DIAG=2`, `SNAPSHOT_FIGHTER_FIELD_DIFF`, `SNAPSHOT_EFFECT_DIAG`, `ROLLBACK_VERIFY_EFFECT_HASH=1`, `RESIM_ANCHOR_PROBE`, fighter slot hash window 120–370, `SIM_STATE_TICK_INTERVAL=1`).

| Stage | Goal | Pass criteria |
|-------|------|----------------|
| **A** | Baseline with diagnostics | Record first diverging partition at ticks **134**, **240**, **254**, **360** (`eff`, `anim`, `figh_full`, `cam`) |
| **B** | Ring integrity | At tick **359**, each peer: `ring_figh_full == live_figh_full` in `ring_save_diag` |
| **C** | Load isolation | `ROLLBACK_SYNCTEST=1` (tick ≥120 only); `RESIM_ANCHOR_PROBE` after load@240; `VERIFY_EFFECT_HASH` fails pre-fix / passes post-fix |
| **D** | A/B toggles | One at a time: `AOBJ_CHAIN_REBUILD=0`, `SNAPSHOT_FIGHTER_CLEANUP=force` vs default |
| **E** | Post-fix | FC@120/240/360 agree; no `LOAD_HASH_DRIFT`; deliberate mid-reflector rollback recovers load@240 |

**Record template (Stage A):**

```
tick=134  partition=eff   host=… client=…
tick=240  partition=…     (last good FC)
tick=254  partition=figh_light transition
tick=360  partition=figh_full  FC fail snap@359
```

## Verification

- Debug build; hold Fox Special-Lw from ~tick 130 through FC@360.
- Expect `effect_respawn kind=FOX_REFLECTOR result=ok` on load when GObj was missing.
- `LOAD_HASH_DRIFT` at tick 240 should not fire with `ROLLBACK_VERIFY_EFFECT_HASH=1` after fix.

## Code pointers

| Area | Symbol |
|------|--------|
| Reflector respawn | `syNetRbSnapEnsureFoxReflectorEffectsFromSlot` |
| Effect rebind | `syNetRbSnapRebindFighterEffectGobjs` |
| Load verify order | `syNetRollbackLoadPostTick` |
| Effect hash | `syNetSyncHashActiveEffectsForRollback` |
