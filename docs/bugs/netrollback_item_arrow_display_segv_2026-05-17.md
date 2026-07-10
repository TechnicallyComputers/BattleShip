# Netrollback Item Arrow Display SIGSEGV

## Summary

With `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS=3`, host could SIGSEGV in `ifCommonItemArrowProcDisplay` (`fault_addr=0x38`) shortly after a stage item despawned (e.g. Fan `kind=9`). Crash followed `itMainDestroyItem` ejecting the arrow interface and item GObjs.

## Root Cause

The pickup-arrow HUD stores a raw `ITStruct*` in the interface GObj's `user_data.p`. `ifCommonItemArrowProcDisplay` dereferenced `ip->item_gobj` and `DObjGetStruct(...)->translate` without checking that the item still exists on the item link list. After destroy or rollback snapshot drift, a stale interface GObj could still run its display proc and fault.

Snapshot load also restored `ip->arrow_gobj` via `gcFindGObjByID` alone, without validating the interface still references the same live item payload.

## Fix

- **ifcommon.c (PORT):** `ifCommonItemArrowIsLiveIp` verifies the item GObj is on `nGCCommonLinkIDItem` and `itGetStruct(item_gobj) == ip`. Display proc ejects orphan HUDs.
- **`ifCommonItemArrowPruneStaleInterfaces` (PORT):** walks `nGCCommonLinkIDInterface` but the list is heterogeneous (stocks, tags, timers, player arrows, ...) and only pickup-arrow HUDs use `user_data.p` as an `ITStruct*`. The prune filters by `gobj->proc_display == ifCommonItemArrowProcDisplay` before treating user_data.p as typed; everything else is left untouched. Orphan arrow HUDs (no matching live item) are ejected.
- **netrollbacksnapshot.c:** `syNetRbSnapResolveArrowGobjForItem` validates the arrow interface points at the restored item; `syNetRbSnapResolveLiveGobj` validates typed payload (`ft`/`it`/`wp`) for item/weapon cross-refs, victim attack records, and camera fighter refs.

## Followup (same day) — heterogeneous interface list

After the initial fix, the rollback resim end-of-pass prune SIGSEGV'd in `ifCommonItemArrowPruneStaleInterfaces+0x28` (`fault_addr=0x421`) because the original sweep treated every interface GObj's `user_data.p` as `ITStruct*`. Several HUD classes store other types in `user_data` and faulted at random struct offsets. The `proc_display` filter is the load-bearing fix; the matching live-ip check still runs for arrow HUDs.

## Class of bug

GObj link lists are kind-keyed (`nGCCommonLinkIDFighter` etc.) and typed-payload safe for the major typed objects (fighter / item / weapon) when guarded with `*GetStruct(gobj) == NULL` checks. The interface link is **not** kind-keyed by `user_data` type — it groups all HUD GObjs together regardless of payload class. Any code walking `nGCCommonLinkIDInterface` must filter by `proc_display` (or a similar tag) before casting `user_data.p`.

## Verification

Rebuild `ssb64` and re-run `PHASE_LOCK_PREDICTION_TICKS=3` item-heavy soak; host should survive item despawn + rollback without `ifCommonItemArrowProcDisplay` or `ifCommonItemArrowPruneStaleInterfaces` SIGSEGV.
