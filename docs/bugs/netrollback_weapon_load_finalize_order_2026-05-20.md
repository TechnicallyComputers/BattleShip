# Weapon load finalize order (rollback synctest drift)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Yoshi **SpecialHi** egg charge at synctest probe tick (~779): `LOAD_HASH_DRIFT` with **weapon partition only** (`wpn` slot vs live mismatch). Fighter/world/rng/cam/anim hashes matched. Egg self-hit and resim storm followed (~870+).

## Root cause

`synNetRbSnapRebindFighterCoupledGObjs()` ran inside core apply (`syNetRbSnapApplySlotToLive`) **before** `syNetRbSnapshotSyncFighterPresentation()`. Yoshi egg vector refresh calls `gmCollisionGetFighterPartsWorldPosition()`, which needs figatree/part transforms from presentation sync. Wrong egg attach position → weapon DObj drift at verify → gameplay desync on next sim steps.

Synctest, `LoadPostTick`, emergency restore, and resim anchor probe all verified **before** presentation + coupled rebind on some paths.

## Fix

Split load into two phases:

1. **Core apply** — fighters, map, world, items, weapons, grab/item-hold coupling, camera (no geometry-dependent coupled rebind).
2. **`syNetRbSnapshotFinalizeLoad(tick)`** — `SyncFighterPresentation()` → `RebindFighterCoupledGObjs()` → `RefreshWeaponHitPositions()`.

Call finalize **before** `syNetRollbackVerifyLoadedSlot()` on:

- `syNetRollbackLoadPostTick`
- Synctest emergency probe
- Resim anchor probe (then status proc rebind for forward sim)

Status proc rebind (`syNetRbSnapshotRebindAllFighters`) remains **after** successful verify (proc pointers are not hashed).

Emergency restore: core apply → finalize → proc rebind.

## Verification

- Synctest at Yoshi egg charge ticks: expect `SYNCTEST_OK`, no `wpn` drift line.
- Soak: Samus charge, Ness PK Thunder, Pikachu Thunder through rollback load + resim.
