# Rollback item snapshot mispairing — shared `gobj_id` ambiguity → resim SIGABRT

**Status:** FIXED (`PORT && SSB64_NETMENU`, rollback snapshot apply); per-instance `instance_id` follow-up implemented 2026-06-27
**Date:** 2026-06-26
**File:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/it/itmanager.c`, `decomp/src/it/ittypes.h`, `decomp/src/it/itmanager.h`

## Symptom

Deep rollbacks that walk back across a window containing two or more identical
live items (e.g. two Link bombs on Peach's Castle, plus the persistent Bumper)
desync on reload:

- `LOAD_HASH_DRIFT` on the item subsystem (observed slot item hash `0xDFCABA43`
  vs live `0x56E896AB`), repeated rollback/resim retries, then
- `SIGABRT` in `gcRunGObjProcess` during replay when a mis-paired item's owner
  link (`ip->owner_gobj`) resolves to `NULL` (orphan-hold) and `itMainDestroyItem`
  dereferences it.

Reproduced deterministically with `debug-resimtest.env`
(`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `..._INJECT_TICK=520`), which forces
a desync at tick 520 and a walk-back to ~514 with two live Link bombs in flight.

## Root cause

All gameplay items share the single `nGCCommonKindItem` category `gobj_id`
(`1013`). The snapshot-apply matcher `syNetRbSnapFindItemBlobForLiveGobj` had a
`gobj_id` fast-path: if it found *any* unmatched blob with the same `gobj_id`
**and** the same `kind`, it returned that blob immediately
(`syNetRbSnapFindItemBlobByGobjId` → first-in-slot order).

With two-plus identical instances (two Link bombs, both `gobj_id=1013` and the
same `kind`), the first-in-slot blob is **not** a stable identity. After a
walk-back that respawns/reorders items within the window, slot order no longer
tracks live-link order, so bomb A absorbs bomb B's blob (position/velocity/owner
swapped). That mispairing both poisons the reload hash (drift → retry storm) and
can leave an item pointing at a stale/freed owner, producing the orphan-hold and
the eventual `gcRunGObjProcess` abort.

Unlike `SYNetRbSnapWeaponBlob`, `SYNetRbSnapItemBlob` carries no per-instance
`instance_id`, so there is no exact-identity key to break the tie — the matcher
must fall back to position/velocity for ambiguous instances.

## Fix

Gate the `gobj_id` fast-path on **uniqueness**. The id+kind match is only an
unambiguous identity when exactly one unmatched blob of that `(gobj_id, kind)`
remains; otherwise fall through to the existing position/velocity matchers
(`syNetRbSnapFindLinkBombBlob` for Link bombs, `syNetRbSnapFindItemBlobByKindPos`
otherwise), which already disambiguate identical instances spatially.

New helper `syNetRbSnapCountUnmatchedItemBlobsByGobjIdKind` counts unmatched
valid blobs sharing the `(gobj_id, kind)` pair; the fast-path returns only when
that count is `<= 1`. The single-instance case (one bomb, the Bumper, normal
pickups) is unchanged — still takes the cheap id path. Only genuinely ambiguous
multi-instance cases defer to positional matching.

```c
if ((slot->items[found].kind == ip->kind) &&
    (syNetRbSnapCountUnmatchedItemBlobsByGobjIdKind(slot, matched, gobj->id, ip->kind) <= 1))
{
    return found;
}
found = -1; /* ambiguous or kind mismatch -> positional matcher below */
```

## Audit hook

- Item resim drift that only appears with **multiple identical items live** (two
  bombs, two of the same projectile) and vanishes with one → suspect shared
  `gobj_id` mispairing, not a capture/serialize bug.
- The proper long-term fix is a per-instance `instance_id` on `SYNetRbSnapItemBlob`
  (mirroring `SYNetRbSnapWeaponBlob`) so the matcher has an exact key and never
  needs the positional fallback. ~~Tracked as follow-up~~; this change is the surgical
  stopgap that removes the order-dependent mispairing.

## Follow-up implemented (2026-06-27): item `instance_id`

The per-instance key now exists, mirroring weapons exactly:

- `ITStruct.instance_id` (`#ifdef PORT`) — `decomp/src/it/ittypes.h`.
- `itManagerAssignInstanceId()` / `itManagerResetInstanceIds()` + `static u32
  sITManagerInstanceID` — `decomp/src/it/itmanager.c` (gated `PORT && SSB64_NETMENU`,
  same monotonic-counter shape as `wpManagerAssignInstanceId`). Assigned once in
  `itManagerMakeItem` (the single chokepoint all `itXxxMakeItem` spawns route through).
- Counter reset at session start via `itManagerResetInstanceIds()` in
  `syNetRbSnapshotResetSession` (next to `wpManagerResetInstanceIds`).
- `SYNetRbSnapItemBlob.instance_id` captured from `ip->instance_id`; restored in
  `syNetRbSnapApplyItemBlobToGObj` (covers both matched and respawned items, since
  respawn re-routes through `itManagerMakeItem` then this apply overwrites the fresh id).
- `syNetRbSnapFindItemBlobByInstanceId` is now tried **first** in
  `syNetRbSnapFindItemBlobForLiveGobj` (exact key), ahead of the uniqueness-gated
  gobj_id fast-path and the Link-bomb / kind+pos positional fallbacks. id 0 (legacy /
  not-yet-assigned) and post-snapshot spawns still fall through to the heuristics.

`instance_id` is **local identity only** — it is *not* folded into
`syNetSyncHashActiveItemsForRollback` and not sent on the wire (same contract as the
weapon `instance_id`), so divergent per-peer rollback/respawn counts cannot desync.
The positional matchers are retained as fallback for the id-0 / cross-spawn cases.
Verified: netmenu and offline builds compile clean.

## Related

- `docs/bugs/netrollback_item_snapshot_roundtrip_2026-05-19.md`
- `docs/bugs/netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md`
- `docs/bugs/netplay_link_bomb_resim_load_fail_2026-06-11.md`
