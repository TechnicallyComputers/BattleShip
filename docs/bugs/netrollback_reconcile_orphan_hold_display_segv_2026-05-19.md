# NetRollback orphan-hold reconcile SIGSEGV (synctest + items)

**Date:** 2026-05-19  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

Saffron 2P automatch with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` (`host-auto1.log` / `client-auto1.log`):

- Clean `SYNCTEST_OK` every 120 ticks through **1499** (no items).
- First item spawn at **1599**; client stick / walk at **1600**.
- First probe with items at **1619**: `LOAD_HASH_DRIFT` (item only), `SYNCTEST_FAIL`, then **SIGSEGV** on both peers:
  - `gcDrawDObjTreeForGObj+0x1a`, `fault_addr=0x88`, `x2=0` → NULL `gobj->obj` / DObj.

Unrelated to symmetric follower routing or tick-529 frontier epoch hold (no resim/epoch logs in this run).

## Root cause

Post-apply `syNetRbSnapReconcileHeldItems()` (added with capsule orphan-hold fix) used:

```c
else if ((ip->owner_gobj != NULL) || (item_gobj->obj != NULL))
    itMainDetachOrphanHoldDisplay(item_gobj);
```

The `|| (item_gobj->obj != NULL)` disjunct matched **every non-held ground item** with a display tree. `itMainDetachOrphanHoldDisplay()` then ejected the DObj and set `item_gobj->obj = NULL` while the GObj stayed on the item link and display list.

During synctest (load probe → verify → emergency restore, same frame, pre-render):

1. Item sim state restored (`matched=1`) then display destroyed → item hash drift (position no longer folded).
2. Emergency restore repeated apply+reconcile → render walked a GObj with `obj == NULL` → crash.

## Fix

| Change | Location |
|--------|----------|
| Restrict reconcile to true orphan holds: `owner_gobj != NULL && is_hold == FALSE` | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Rename → `syNetRbSnapReconcileOrphanHeldItems()` | same |
| Diag: `reconciled orphan hold item=…` when `SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1` | same |
| Defensive NULL guard in `gcDrawDObjTreeForGObj` (belt-and-suspenders) | [`decomp/src/sys/objdisplay.c`](decomp/src/sys/objdisplay.c) |

## Soak pass criteria

Saffron 2P with synctest + item diag:

```bash
export SSB64_NETPLAY_ROLLBACK_SYNCTEST=1
export SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1
```

- `SYNCTEST_OK` through tick **1619+** (first probe with `item_count=1`).
- No item-only `LOAD_HASH_DRIFT` / `SYNCTEST_FAIL` at 1619.
- No SIGSEGV on first movement / item phase.

If item hash drift persists after this fix, investigate deeper round-trip work in [`netrollback_item_snapshot_roundtrip_2026-05-19.md`](netrollback_item_snapshot_roundtrip_2026-05-19.md).

## Related

- [`netplay_capsule_orphan_hold_yamabuki_double_spawn_2026-05-19.md`](netplay_capsule_orphan_hold_yamabuki_double_spawn_2026-05-19.md)
- [`netrollback_item_snapshot_roundtrip_2026-05-19.md`](netrollback_item_snapshot_roundtrip_2026-05-19.md)
