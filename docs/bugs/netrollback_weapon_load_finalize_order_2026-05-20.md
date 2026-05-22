# Weapon load finalize order (rollback synctest drift)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (2026-05-20 phase 1 + 2026-05-20 phase 2 joint-anim round-trip)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Yoshi **SpecialHi** egg charge at synctest probe tick (~749–779): `LOAD_HASH_DRIFT` with **weapon + anim** partition mismatch (`wpn` / `anim` slot vs live). Egg self-hit and resim storm followed.

## Root cause (phase 1)

`synNetRbSnapRebindFighterCoupledGObjs()` ran inside core apply (`syNetRbSnapApplySlotToLive`) **before** `syNetRbSnapshotSyncFighterPresentation()`. Yoshi egg vector refresh calls `gmCollisionGetFighterPartsWorldPosition()`, which needs figatree/part transforms from presentation sync.

## Root cause (phase 2)

Even with presentation-before-coupling order, **`ftMainRefreshFigatreeVisual` → `lbCommonAddFighterPartsFigatree` → `gcAddDObjAnimJoint`** rebuilds figatree anim joints and **clobbers** per-joint AObj chains restored from the snapshot blob. Verify then fails `hash_animation` (and `hash_weapon` when `ftYoshiSpecialHiUpdateEggVectors` runs against the clobbered skeleton).

Pre-verify finalize also called geometry-dependent coupled refresh (`UpdateEggVectors`, Samus charge shot position) that overwrote weapon DObj transforms already restored from the weapon blob.

## Fix

Split load into phases:

1. **Core apply** — fighters, map, world, items, weapons, grab/item-hold coupling, camera.
2. **`syNetRbSnapshotFinalizeLoad(tick)` (pre-verify)** — `SyncFighterPresentation()` → **`ReapplyFighterJointAnimFromSlot()`** → coupled **pointer** rebind only (`refresh_coupled_weapon_geometry=FALSE`) → `RefreshWeaponHitPositions()`.
3. **Verify** — `syNetRollbackVerifyLoadedSlot()`.
4. **Post-verify** — `syNetRbSnapshotRebindAllFighters()`; forward sim status physics restores coupled weapon geometry on the next tick.
5. **Emergency restore** — core apply → finalize with `refresh_coupled_weapon_geometry=TRUE` → proc rebind.

Call finalize **before** verify on:

- `syNetRollbackLoadPostTick`
- Synctest emergency probe
- Resim anchor probe (then status proc rebind for forward sim)

## Verification

- Synctest at Yoshi egg charge ticks: expect `SYNCTEST_OK`, no `wpn` / `anim` drift line.
- Soak: Samus charge, Ness PK Thunder, Pikachu Thunder through rollback load + resim.
- Second SpecialHi after resim: no egg self-hit.
