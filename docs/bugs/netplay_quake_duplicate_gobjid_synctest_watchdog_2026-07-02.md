# Netplay: duplicate quake `gobj_id` synctest watchdog

**Date:** 2026-07-02  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, resoak pending for verify-side follow-up)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

After the broad `transient_effect_probe` skip was removed, soak `858512918` reached a deterministic
synctest failure at tick 1110:

```
LOAD_HASH_DRIFT tick=1110 ... eff=0x166976A3/0x811C9DC5
SYNCTEST_FAIL tick=1110
WATCHDOG HANG ... gcRunGObjProcess -> gcRunAll -> ifCommonBattleUpdateInterfaceAll
```

Both peers agreed on all gameplay partitions; only `eff` failed. The watchdog was not the earlier
`LBTransform` free-list cycle class: there were no `pool_guard` or double-free logs, and the backtrace
sat in the effect GObj process walk.

## Root Cause

Tick 1110 had two live quake effects with the same stale `gobj_id=1011`:

```
idx=0 gobj_id=1011 respawn=0 quake_pri=4
idx=1 gobj_id=1011 respawn=1 quake_pri=3
```

The live effect fold hashed both, but snapshot capture deduped by `gobj_id` and saved only one blob.
The priority-4 quake shell was not recognized as a quake by `syNetRbSnapLiveEffectIsQuake`, so it entered
the duplicate-id sanitizer as `RESPAWN_NONE`; then the verify repair/enforce path pruned and respawned
around a slot that could never represent the live hash, leaving `gcRunAll` walking a corrupted/cyclic
effect process list.

## Fix

- Treat active priority-4 quake shells as quake effects for rollback identity.
- Preserve same-id quake+quake blobs instead of compacting them by stale `gobj_id`.
- Compare captured quake priority when matching quake blobs to live effects, so the priority-4 shell and
  priority-3 respawnable quake map to distinct live GObjs.
- During verify reconcile, allow distinct quake pointers to be canonical even when their stale `gobj_id`
  is already tracked; generic effect families still use the id-based guard.
- Follow-up from resoak `1244362067`: verify-side repair could still collapse a saved pri=4 + pri=2
  same-id quake pair at tick 869 (`canonical=1 slot_count=2`), leaving verify with only the pri=2 quake.
  Quake matching now keys on captured priority instead of derived magnitude (`pri=4` aliases the `0xFF`
  unknown sentinel), and verify-only reconcile creates a distinct quake shell for each missing slot blob
  before canonical pruning.

This closes the failure by making the snapshot represent the same two quake effects that the hash folds,
instead of adding another probe skip.

## Verify

Re-run the Firefox/bumper soak with synctest and effect diagnostics. Expected result: tick 1110 should
either pass synctest or repair without `effect_probe_mismatch`, with no `slot_effect_enforce` watchdog.
The tick 869 follow-up should end verify with both quake priorities present (`canonical=2 slot_count=2`
or no enforce churn) and no eff-only `LOAD_HASH_DRIFT`.
