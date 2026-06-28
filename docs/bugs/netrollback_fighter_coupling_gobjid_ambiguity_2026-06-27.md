# Netplay rollback: fighter grab/throw coupling restored via ambiguous shared gobj id

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-06-27
**Subsystem:** `port/net/sys/netrollbacksnapshot.c` (fighter blob capture/apply)
**Class:** shared-`gobj_id` ambiguity on snapshot reload (same family as
[`netrollback_item_gobjid_ambiguity_resim`](netrollback_item_gobjid_ambiguity_resim_2026-06-26.md))

## Symptom

With the [emergency-restore sparkle-window overflow](netrollback_emergency_restore_sparkle_window_overflow_2026-06-27.md)
hang fixed, the synctest soak runs much further but then **crashes deterministically** (`SIGBUS` on Linux /
`SIGSEGV` on Android) around tick ~2305 during heavy grab/throw + item-throw activity. Both peers fault at the
**same tick** in collision code:

```
mpProcessCheckTestLWallCollisionAdjNew  (decomp/src/mp/mpprocess.c:1154)
    sp54.x = pos_prev->x + p_map_coll->width;   // p_map_coll deref
```

- Linux `fault_addr=0x0`, Android `fault_addr=0xdc00000000000c` — *different* bad addresses on each peer for
  the *same* logical tick. That is the fingerprint of a **dangling/garbage pointer**, not a sim desync: pointer
  values legitimately differ between peers even when the simulation is bit-identical.
- The single-frame backtrace is just omit-frame-pointer (`fp` holds a float bit-pattern, `0x42dc...`), not
  corruption. The nearby `efManagerMakeEffect` gobj_alloc is an incidental hitspark.

## Root cause

`p_map_coll` is a transient cross-object collision source: `mpCommonCopyCollDataStats` sets
`this_coll_data->p_map_coll = &other_coll_data->map_coll`, the collision runs, then `mpCommonResetCollDataStats`
restores the self-pointer — all within one call. The only path that supplies a *foreign* `other_coll_data` for a
fighter is the **thrown/captured-fighter** collision:

```c
// decomp/src/ft/ftcommon/ftcommonthrown2.c:76
mpCommonRunFighterCollisionDefault(fighter_gobj,
    &DObjGetStruct(interact_gobj)->translate.vec.f, &interact_fp->coll_data);
//  interact_gobj = this_fp->catch_gobj | this_fp->capture_gobj
//  interact_fp   = ftGetStruct(interact_gobj)
```

So a garbage `p_map_coll` means `interact_fp` (and hence `interact_gobj` = `catch_gobj`/`capture_gobj`) pointed
at freed/wrong memory. The existing PORT NULL-guards in that file only catch a *NULL* coupling, not a
*mis-resolved non-NULL* one.

The coupling is mis-resolved by the rollback restore. **All fighters share `gobj->id == nGCCommonKindFighter`
(1000)**, and the fighter blob persisted the partner via `syNetRbSnapGobjId()` (= `gobj->id` = 1000 for every
fighter). On reload:

```c
fp->catch_gobj   = syNetRbSnapResolveLiveGobj(blob->catch_gobj_id);   // resolveLiveGobj(1000)
fp->capture_gobj = syNetRbSnapResolveLiveGobj(blob->capture_gobj_id); // -> gcFindGObjByID(1000)
```

`gcFindGObjByID(1000)` returns the **first** fighter in the link list regardless of who the real partner was.
After any rollback that occurred mid-grab, `catch_gobj`/`capture_gobj`/`throw_gobj`/`search_gobj` therefore point
at the wrong fighter (often self), the bidirectional `syNetRbSnapRebindFighterGrabCoupling` reinforces the
inconsistency, and the thrown-fighter collision feeds a stale/inconsistent `interact_fp` into the map collision —
crashing on `p_map_coll` once the coupling decays to freed/garbage state over the next forward ticks.

The codebase already knew this hazard: zoom/follow camera targets (`pzoom_fighter_gobj`/`pfollow_fighter_gobj`)
and *every other* fighter reference resolve via `syNetRbSnapResolveFighterGobjByPlayer(player)` (sim slot), and
items got a dedicated `syNetRbSnapResolveItemGobj` for the identical id=1013 ambiguity. The grab/throw coupling
was the one fighter reference still going through the ambiguous `syNetRbSnapResolveLiveGobj`.

## Fix

Resolve the four fighter couplings by **sim player slot**, mirroring the camera targets. All four
(`throw_gobj`, `catch_gobj`, `capture_gobj`, `search_gobj`) are fighter GObjs (per `fttypes.h`).

`port/net/sys/netrollbacksnapshot.c`:

1. `SYNetRbSnapFighterBlob`: add `s8 throw_gobj_player / catch_gobj_player / capture_gobj_player /
   search_gobj_player` (`-1` == no coupling). The legacy `*_gobj_id` fields are kept (still captured) because a
   presence gate elsewhere uses `*_gobj_id != 0`.
2. Capture: `blob->*_gobj_player = syNetRbSnapFighterPlayerFromGobj(fp->*_gobj)` (returns `-1` for NULL).
3. Apply: `fp->*_gobj = syNetRbSnapResolveFighterGobjByPlayer(blob->*_gobj_player)` (returns NULL for `-1` or an
   absent player slot). `item_gobj` already used `syNetRbSnapResolveItemGobj` and is unchanged.

Snapshots are peer-local (never wire-serialized) and these ids are not in the hash fold, so adding fields and
changing resolution does not affect the cross-peer load hash. Both `build-netmenu` and `build-offline` compile
and link clean (the TU is netmenu-only; offline uses stubs).

## Audit hook

Any rollback restore of a *fighter* reference must resolve by sim player slot
(`syNetRbSnapResolveFighterGobjByPlayer`), never by `gobj->id` / `syNetRbSnapResolveLiveGobj` (all fighters share
`gobj->id == 1000`). A deterministic same-tick crash with *different* bad pointer values per peer, in a path that
dereferences a snapshot-restored GObj reference, is the shared-`gobj_id` ambiguity class — check items (1013) and
fighters (1000) first.

## Follow-ups

- Soak with the original repro env (`FORCE_MISMATCH=1`, `INJECT_TICK=520`, `SYNCTEST=1`, Peach's Castle
  `AUTOMATCH_STAGE_KIND=0`) to confirm the tick-~2305 crash is gone.
- Long-term: give fighters a stable per-instance id so `syNetRbSnapResolveLiveGobj` is never ambiguous, and the
  per-reference player-slot shims can be retired.
