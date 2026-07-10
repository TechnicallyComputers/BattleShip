# Netplay: rebirth halo verify-load drop-in gap

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-03

## Symptom

Soak session `1260826497` no longer crashes or watchdogs (`sigsegv=0`) and has no frame-commit
item/rng divergence, but both peers report deterministic `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` at
ticks 1829, 2909, 3029, and 3149:

```text
LOAD_HASH_DRIFT tick=1829 ... eff=0xF34715DB/0x811C9DC5
eff_fold_diag tag=capture tick=1829 count=1 ... gobj_id=1011 respawn=6 parent_id=1000 ...
eff_fold_diag tag=verify  tick=1829 count=0 ... hash=0x811C9DC5
```

`0x811C9DC5` is the empty effect-fold seed. The captured slot contains one rebirth halo effect
(`SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO`), but the synctest verify-load has no effect after apply.
Tick 2909 also reports `figh`, consistent with a fighter left flagged as effect-attached while
the halo GObj was missing.

## Root cause

The general missing-effect respawn pass in `syNetRbSnapReconcileSnapshotEffectsBeforeItems` is
disabled during verify-only loads to avoid cosmetic quake respawn churn. Rebirth halos rely on the
specialized `syNetRbSnapEnsureRebirthHaloEffectsFromSlot` path instead.

That ensure path only ran while the live fighter was still in `RebirthDown..RebirthWait` or the
fighter blob still had a pending effect-attach flag. At the drop-in boundary the fighter can have
already left rebirth scope, while the snapshot still lists the one-frame lingering halo blob. The
fighter attach sanitizer already preserves `is_effect_attach` for this condition via
`syNetRbSnapSlotListsRebirthHaloForFighter`, but the halo ensure gate did not use the same predicate,
so verify-only load preserved the attach state and failed to recreate the listed halo.

## Fix

`syNetRbSnapEnsureRebirthHaloEffectsFromSlot` now treats a halo listed in the snapshot as an
authoritative reason to run the ensure path:

```c
if ((syNetRbSnapFighterRebirthHaloLifecycleActive(fp) == FALSE) &&
    (syNetRbSnapFighterBlobRebirthHaloPending(&slot->fighters[pi]) == FALSE) &&
    (syNetRbSnapSlotListsRebirthHaloForFighter(slot, (u32)fighter_gobj->id, 0U) == FALSE))
{
    continue;
}
```

The respawn remains idempotent: the existing loop first checks for a live effect with the blob's
GObj id and `EFStruct`, and the stale-halo prune keeps slot-listed halos. This only closes the
verify-only drop-in gap where the authoritative slot listed a halo but the lifecycle status no
longer did.

## Verify

- `build-netmenu` `ssb64` target: passed.
- `build-offline` `ssb64` target: passed.
- Soak pending: rerun the KO/rebirth soak from session `1260826497`; expected result is
  `SYNCTEST_FAIL=0` / `LOAD_HASH_DRIFT=0` through the rebirth drop-in ticks.
