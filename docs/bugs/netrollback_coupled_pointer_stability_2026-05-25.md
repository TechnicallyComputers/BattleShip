# Rollback: coupled pointer scrub + baseline gate completeness — 2026-05-25

**Status:** SHIPPED  
**Scope:** `netrollbacksnapshot.c`, `netrollback.c`

## Problem

Fighter `status_vars` / `passive_vars` are `memcpy`'d into the rollback blob. Coupled `GObj *` fields (guard shield effect, capture Yoshi effect, Fox reflector, Yoshi egg, Link/Kirby boomerang, Samus charge, Ness PK Thunder, Pikachu thunder) could carry **stale addresses** when the parallel id field was zero or when apply ran before effects/weapons existed. That risks use-after-free reads during item apply or status procs.

Separately, resim **baseline gate open** only required figh/world/item/rng (+ anim when FSM demands) even though baseline v2 wire carries weapon/map/camera/effect — peers could open the replay gate with divergent weapon/map state.

## Fix

1. **`syNetRbSnapClearCoupledGObjPointersInStatusPassive`** — unconditional NULL of all known coupled effect/weapon slots on capture scrub, apply scrub, and blob storage (ids on `SYNetRbSnapFighterBlob` remain authoritative).

2. **`syNetRbSnapResolveCoupledEffectGobj` + rebind** — always assign effect pointers from ids (NULL when id 0 or live struct missing).

3. **Apply order** — call `syNetRbSnapRebindFighterCoupledGObjs(slot, FALSE)` after weapon apply (finalize still refreshes geometry when needed).

4. **Resim baseline gate** — slot and wire digests must match weapon, map, camera, and effect (effect only when peer v2 tail was received: `sSYNetRollbackLastPeerOutcomeEffectValid`).

## Code pointers

| Area | Symbol |
|------|--------|
| Scrub | `syNetRbSnapClearCoupledGObjPointersInStatusPassive` |
| Effect rebind | `syNetRbSnapRebindFighterEffectGobjs` |
| Weapon rebind | `syNetRbSnapRebindFighterCoupledGObjs` |
| Baseline gate | `syNetRollbackTryOpenResimBaselineGateFromPeerDigest` |
