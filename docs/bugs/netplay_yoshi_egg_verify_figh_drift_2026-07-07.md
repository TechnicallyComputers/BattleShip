# Netplay: Yoshi egg-lay synctest figh drift + premature hatch (verify finalize)

**Date:** 2026-07-07  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `1653978063`  
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

```
[FAIL] tick 629: diverged=figh  [synctest probe]
P0 Samus: status=178 (YoshiEgg), P1 Yoshi: status=157 (EscapeB)
fighter_field_diff: P0 light_ok=0 full_ok=0 anim_ok=1
eff= matched (YoshiEggEscape respawn=11 on gobj_id=1011)
```

Presentation: Yoshi neutral-B egg capture sometimes hatches prematurely after rollback/resim.

## Root cause

`PrepareLoadedSlotForVerify` runs `RefreshYoshiEggLayPresentationFromSlot` + effect reconcile, but `syNetRollbackVerifyLoadedSlot` then calls `FinalizeVerifyEffectState` (slot effect enforce, `RebindFighterEffectGobjs`, optional joint reapply) **before** the figh hash compare.

soak2 @1653978063 tick 629: `slot_effect_enforce_id_collision` ejected a stale `gobj_id=1011` shell (`anim_frame=12`) while Samus remained in `YoshiEgg`. Effect rebind + non-fragile joint reapply drifted P0 `fhash_light` fields (`breakout_wait`, `motion_vars.flag0`, `captureyoshi.effect_gobj` id) even though joint anim hash matched.

Drifted `breakout_wait` / escape flags on the victim blob explain premature hatch on the next forward-sim tick (`ftCommonYoshiEggProcUpdate` escape path).

## Fix

| File | Change |
|------|--------|
| `port/net/sys/netrollbacksnapshot.c` | Treat egg-lay presentation window as verify joint-anim fragile; add `syNetRbSnapshotPrepareYoshiEggLayForVerifyHash` (refresh + reconcile + restore breakout/captureyoshi sim fields + hard-pin) |
| `port/net/sys/netrollback.c` | Call prepare helper immediately after `FinalizeVerifyEffectState` before figh hash |

## Re-soak pass criteria

Samus vs Yoshi egg-lay through tick 629+: `SYNCTEST_OK`, no figh-only drift while victim in `YoshiEgg`. Egg timer/breakout mash behaves consistently; no premature hatch after resim load.
