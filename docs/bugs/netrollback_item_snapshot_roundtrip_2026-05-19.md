# NetRollback item snapshot round-trip (Saffron / item-heavy)

**Date:** 2026-05-19  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

After countdown/frontier fixes, Saffron City 2P soak (`gkind=7`) stayed clean through sim tick **~1499** (`SYNCTEST_OK` every 120 ticks, sentinel `item=0x811C9DC5`). Once items spawned (~**1588**), both peers logged:

- `LOAD_HASH_DRIFT` with **`item=`** mismatch while `world=` / `rng=` often matched
- `SYNCTEST_FAIL` at the same ticks (**1619**, **1739**, …)
- Visual: items jittering/duplicating; Chansey (tower door) eject storms (`gcEjectGObj id=1016 kind=2 link_id=11`)

Identical drift on host and client → **local** snapshot save/load/verify failure, not cross-peer input divergence.

## Root cause

1. **Rollback item hash still folded `gobj->id`** despite [`netrollback_item_weapon_gobj_id_verify_2026-05-18.md`](netrollback_item_weapon_gobj_id_verify_2026-05-18.md) — apply can respawn items with new GObj ids while verify compared stored hash that included old ids.
2. **Hash walked up to 48 items; snapshot stored 16** — verify could include items the blob never captured (silent asymmetry).
3. **Capture walked link order; hash sorted by `gobj->id`** — enumeration divergence risk even when caps were not hit.

## Fix

| Change | Location |
|--------|----------|
| `syNetSyncFoldActiveItemGobjForRollback` — documented field contract; **no item `gobj->id`** | [`port/net/sys/netsync.c`](port/net/sys/netsync.c) |
| `SYNETRB_SNAPSHOT_MAX_ITEMS` **16 → 32** | [`port/net/sys/netrollbacksnapshot.h`](port/net/sys/netrollbacksnapshot.h) |
| `SYNET_SYNC_ITEM_HASH_SORT_MAX` = snapshot cap | [`port/net/sys/netsync.h`](port/net/sys/netsync.h) |
| **`syNetRbEnumerateActiveItemsSorted`** — single sorted enumerator for capture + hash + trace | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Save still **fails loud** if link has more than 32 valid items; hash logs once per match if truncated | capture / `syNetSyncHashActiveItemsForRollback` |
| **`SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1`** — save `item_count`/`truncated`; apply `ejected`/`matched`/`respawned` | snapshot apply |

Diagnostic hash (`syNetSyncFoldActiveItemGobj`) unchanged (includes `gobj->id`).

## Memory / performance

- **Per ring slot:** +16 × `sizeof(SYNetRbSnapItemBlob)` vs prior 16-cap (blob includes physics, coll, `item_vars`) — on the order of a few KB per slot, tens of KB worst-case across `SYNETRB_SNAPSHOT_RING_MAX` (128) slots vs the fighter/yakumono payload.
- **CPU:** one shared insertion-sort enumeration for n ≤ 32 at save and hash-verify — **negligible** vs battle sim; replaces duplicate link walks.

## Soak pass criteria

Saffron 2P with:

```bash
export SSB64_NETPLAY_ROLLBACK_SYNCTEST=1
export SSB64_NETPLAY_ITEM_HASH_TRACE=1
export SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1
```

- `SYNCTEST_OK` through item phase (not only pre-1499)
- No recurring `LOAD_HASH_DRIFT` with `item=` only while `world=`/`rng=` match
- `item_hash_walk` count ≤ 32; host/client final hash match at probe ticks
- No synctest-driven `gcEjectGObj` bursts for tower Chansey

Control: `SSB64_NETPLAY_ROLLBACK_SYNCTEST=0` — if ghosts persist, also investigate forward prediction (separate from round-trip).

## Related

- [`netrollback_item_weapon_gobj_id_verify_2026-05-18.md`](netrollback_item_weapon_gobj_id_verify_2026-05-18.md)
- [`netrollback_resim_item_cross_peer_drift_2026-05-18.md`](netrollback_resim_item_cross_peer_drift_2026-05-18.md)
- [`../netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md) case 3, cap overflow
