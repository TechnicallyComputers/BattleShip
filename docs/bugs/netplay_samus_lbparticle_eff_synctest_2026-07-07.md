# Netplay: Samus LBParticle VFX fails synctest verify (DefaultProcUpdate eff)

**Date:** 2026-07-07  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `1624461209`  
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

```
[FAIL] tick 5909: diverged=eff  [synctest probe]
[FAIL] tick 6749: diverged=eff  [synctest probe]
[FAIL] tick 6389: diverged=wpn  (weapon-repair ok logged, still SYNCTEST_FAIL)
```

Presentation: Samus Up+A flame particles and some bomb explosion sparkles missing after rollback/resim.

## Root cause

### eff @5909 / @6749

`eff_fold_diag` at capture: `count=1`, `gobj_id=1011`, `respawn=0`, `parent_id=1000` (Samus fighter). Verify ejected the shell as `verify_non_canonical` (`hidden=0 excluded=0`) → live eff hash empty vs slot.

These are mid-play **LBParticle script shells** driven by `efManagerDefaultProcUpdate` (Samus flame LR from motion scripts, throw/sparkle class VFX). They have `respawn=NONE` and never round-trip through effect blobs (`effect_count=0`), but `syNetRbSnapLiveEffectExcludedFromRollbackHash` only excluded `anim_frame <= 0` shells — mid-play particles with `anim_frame > 0` were folded into the rollback hash and then ejected on verify with no mint path.

Same failure class as Link spin blade FX and ImpactWave cosmetic exclusion.

### wpn @6389

Samus `SpecialNEnd` charge-shot release: `syNetRollbackTryRecoverWeaponHashDrift` repaired the weapon partition (`weapon-repair ok`, hashes matched), but `syNetRollbackVerifyLoadedSlot` had no success path when repair fixed the **only** mismatch during synctest probe (non-resim context — `resim-sim-core-ok` is FALSE).

## Fix

| File | Change |
|------|--------|
| `port/net/sys/netrollbacksnapshot.c` | `syNetRbSnapLiveEffectIsDefaultProcParticleCosmetic()` — `efManagerDefaultProcUpdate` + `respawn=NONE`; hide + exclude from rollback fold (ImpactWave pattern) |
| `port/net/sys/netrollback.c` | `syNetRollbackVerifyLoadHashMatchesSlot()`; after weapon/effect repair, return TRUE when all partitions match (verify + resim load paths) |

Cosmetic exclusion keeps mid-play flame/sparkle shells live through verify (not ejected), restoring presentation for Up+A flames and similar VFX during rollback.

## Re-soak pass criteria

Session class `1624461209`: synctest passes through ticks 5909, 6389, 6749+. Samus Up+A flames and bomb explode sparkles visible on both peers after resim.
