# Netplay capsule orphan-hold + Saffron double tower spawn

## Summary

Netplay soak on Saffron City (`host-auto1.log` / `client-auto1.log`) showed two deterministic gameplay bugs:

1. **Capsule visual hold without throw** — Fox appeared to hold a capsule but A did normal attacks. Logs showed `is_hold=0`, no `fp->item_gobj`, and pickup interrupted by `DamageFlyN` instead of `LightGet`.
2. **Double Porygon from tower door** — `item_count` went 1→2 at sim tick 2376 on both peers while the first stage monster was still alive. Rollback synctest at tick 2459 then `respawned=1` and item count jumped 2→4.

## Root cause

### Capsule orphan hold

Hold linkage requires `itMainSetFighterHold()` (`is_hold=TRUE`, `fp->item_gobj`, display joint). Knockback during the get animation left a display joint / owner reference without sim hold state, so `ftCommonAttack1CheckInterruptCommon` never routed A to throw.

Rollback item blobs also omitted `is_hold`, so resim could restore `owner_gobj` without hold semantics.

### Yamabuki double spawn

`grYamabukiGateMakeMonster()` overwrote `monster_gobj` without checking whether the prior tower Pokémon item was still alive. Porygon (and other tower exits) did not call `grYamabukiGateClearMonsterGObj()` before `grYamabukiGateSetClosedWait()`.

### Rollback item duplication

`syNetRbSnapApplyItems()` matched live GObjs to snapshot blobs by `gobj_id` only and allowed multiple live items to match the same blob index. Unmatched blobs were respawned, producing duplicates (`matched=2 respawned=1` at synctest tick 2459).

## Fix

- **gryamabuki.c:** Skip `grYamabukiGateMakeMonster()` when `monster_gobj` still has a live item payload; clear stale pointer otherwise.
- **itporygon.c / itmarumine.c / itfushigibana.c:** Call `grYamabukiGateClearMonsterGObj()` before closing the gate when the walk-out / lifetime ends.
- **itmain.c (PORT):** `itMainDetachOrphanHoldDisplay()` + `itMainSweepOrphanItemOwnersForFighter()`; destroy path clears fighter refs when `owner_gobj` is set but `is_hold` is false.
- **ftcommonwait.c / ftcommondamage.c (PORT):** Sweep orphan item owners when entering Wait or damage without `fp->item_gobj`.
- **netrollbacksnapshot.c:** Persist `is_hold` / `is_allow_pickup` in item blobs; one-to-one blob matching (skip already-matched blobs); kind+position fallback match; post-apply orphan-hold reconcile (`syNetRbSnapReconcileOrphanHeldItems` — see [`netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md`](netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md) for follow-on fix to reconcile condition).

## Verification

Rebuild `ssb64` and re-run automatch on Saffron:

- Pick up capsule under knockback pressure; A should throw or show no phantom hold.
- Let tower Pokémon spawn twice; only one live monster per gate cycle.
- Rollback synctest probes with two items on field should not log `respawned=1` with rising `item_count`.
