# Netplay Z-drop orphan sweep SIGSEGV

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

2P netplay soak @2458: Z-drop home run bat (standing, over-shoulder) → **SIGSEGV** on both peers:

- `itVisualsUpdateSpin+0xe`, `fault_addr=0x5c`, `x1=0` → NULL `item_gobj->obj`
- Preceded by `ftMainSetStatus status=0xa motion=4` (Wait) on dropping fighter
- Yoshi swallow and bat smash attacks passed; deterministic local bug, not desync

## Root cause

Vanilla keeps `ip->owner_gobj` after `itMainSetFighterRelease` (hit/ownership logic). PORT orphan-hold cleanup matched **`owner_gobj != NULL && is_hold == FALSE`**, which includes **legitimately dropped** items.

On the same tick as Z-drop:

1. `itMainSetFighterDrop` → bat enters Fall/Dropped status with valid DObj tree
2. Drop anim ends → `ftCommonWaitSetStatus` → `itMainSweepOrphanItemOwnersForFighter`
3. Sweep called `itMainDetachOrphanHoldDisplay`, ejecting DObj and setting `item_gobj->obj = NULL`
4. `itBatFallProcUpdate` → `itVisualsUpdateSpin` dereferenced NULL DObj

Same overly broad condition existed in `syNetRbSnapReconcileOrphanHeldItems` (fixed in [`netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md`](netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md) for synctest render, but Wait sweep still used the stale predicate).

## Fix

| Change | Location |
|--------|----------|
| `itMainItemHasOrphanHoldDisplay()` — true only when hold wrapper joint remains (`obj->user_data.p` == fighter item attach joint, `is_hold == FALSE`) | [`decomp/src/it/itmain.c`](decomp/src/it/itmain.c) |
| Gate `itMainDetachOrphanHoldDisplay`, Wait sweep, snapshot reconcile on helper | `itmain.c`, [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |

Dropped/thrown items retain `owner_gobj` and display tree; only stale hold-display wrappers are stripped.

## Soak pass criteria

- Z-drop home run bat while standing → no SIGSEGV; bat spins/falls normally
- Wait transition after item throw/drop on both peers
- Orphan-hold reconcile still clears capsule visual-hold after snapshot restore (Saffron case)
