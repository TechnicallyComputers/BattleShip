# Netplay: stale `EFStruct.proc_update` mis-prunes hit VFX as orphan shield

**Date:** 2026-06-05  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

- After Mario shield spam, first fireball hit on Ness (tick ~503): weapon vanishes correctly but hit/damage VFX missing or thin.
- Log: first `guard_shield_prune reason=no_fighter effect_gobj_id=1011` exactly on hit tick; `DOUBLE-EJECT` cluster follows.
- `no_fighter_rebind=0` — rebind path never fires (`is_shield=0` on both fighters).
- No `guard_shield_prune` during shield spam itself (ticks 416–463).

## Root cause

Two layers:

1. **Pool metadata.** `efManagerGetNextStructAlloc` cleared `fighter_gobj` but not `proc_update`. Desc-based effects (shields) set `proc_update = efManagerShieldProcUpdate` in `efManagerMakeEffect`. Particle hit VFX (`efManagerDamageNormalLightMakeEffect`, etc.) reuse pool slots and attach `efManagerDefaultProcUpdate` directly without touching `proc_update`. After shield spam, reused slots still looked like shields to reconcile.

2. **Shield identity check.** `syNetRbSnapLiveEffectIsShield` compared only `ep->proc_update == efManagerShieldProcUpdate`, so damage VFX wrappers were misclassified and ejected same tick via `no_fighter` prune (`fighter_gobj == NULL` is correct for particle effects).

Real decoupled guard bubbles (the target of `no_fighter` prune) still run `efManagerShieldProcUpdate` as their live GObj process.

## Fix

| Location | Change |
|----------|--------|
| `decomp/src/ef/efmanager.c` | Clear `ep->proc_update = NULL` on pool alloc (`GetNextStructAlloc`) and return (`SetPrevStructAlloc`). |
| `port/net/sys/netrollbacksnapshot.c` | `syNetRbSnapLiveEffectIsShield` also requires the effect GObj to have a running func process matching `ep->proc_update`. |

Related: sentinel unlink + `no_fighter_rebind` in `netplay_guard_shield_zombie_eject_uaf_2026-06-05.md` (orthogonal; handles true orphan bubbles and DOUBLE-EJECT zombies).

## Verification

- Re-run Mario shield spam → fireball vs Ness soak.
- Expect **zero** `guard_shield_prune reason=no_fighter` on hit tick 503.
- Hit tick `sim_state_tick` `eff` hash should retain non-sentinel value through reconcile.
- Shield spam defer/linger logs unchanged; true orphan shield rebind still works when `is_shield=1`.
