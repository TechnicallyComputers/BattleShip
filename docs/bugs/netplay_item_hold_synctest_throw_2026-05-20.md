# Netplay item hold SYNCTEST throw failure (Chansey egg)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

Long 2P soak with synctest: first Chansey egg pickup → A throw OK (~tick 716–740). Later egg pickup looked held visually but **A did not throw** — sim never re-entered throw status after ~tick 856. Matching `item`/`figh` hashes throughout (symmetric logic bug, not desync).

## Root cause

Synctest emergency restore during held-item windows broke **bidirectional** `fp->item_gobj` ↔ `ip->owner_gobj` / `is_hold` coupling. Fighter blobs save `item_gobj_id`, item blobs save `owner_gobj_id` + `is_hold` flag, but restore could leave one side NULL while joint presentation still showed the egg. `ftCommonLightThrowCheckItemTypeThrow` requires non-NULL `fp->item_gobj`.

Multi-item stages (item_count 2–3) increased synctest probes mid-hold.

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapshotAnyItemHoldCouplingActive()` — any item `is_hold` or any fighter `item_gobj` | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Skip synctest while hold coupling active; log `SYNCTEST_SKIP reason=item_hold` | [`port/net/sys/netrollback.c`](port/net/sys/netrollback.c) |
| `syNetRbSnapRebindFighterItemHoldCoupling()` — repair both directions for all fighters/items | `netrollbacksnapshot.c` (apply, reconcile, `RebindAllFighters`) |

## Soak pass criteria

- Chansey multi-egg: second+ pickup → A throw enters throw motion on both peers
- Log `SYNCTEST_SKIP reason=item_hold` during hold, not `SYNCTEST_OK` mid-hold
- No orphan visual hold with NULL `item_gobj` after restore
