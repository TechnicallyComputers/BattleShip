# Netplay: Kirby inhale wind GObjProcess chain cycle watchdog

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-02
**Builds:** `build-netmenu` + `build-offline` link clean.

## Symptom

After the rebirth Stand/Wait pose fix, soak session `2134390045` passed determinism:

- `SYNCTEST_FAIL=0`
- `LOAD_HASH_DRIFT=0`
- `RESULT: PASS`
- `STABLE (soft recovery)`

The match then hit a Linux watchdog hang during rollback load:

```text
SSB64: WATCHDOG HANG since_frame=3001ms since_yield=3001ms active_tid=5(game)
... syNetRbSnapshotLoad+0x89
... syNetRollbackAfterBattleUpdate+0x4eb
```

Symbolizing the bundle binary (`build-bundle-linux-netplay-us/BattleShip`) named the exact loop:

```text
0x57b9ee -> syNetRbSnapEffectGObjHasUpdateProc
            port/net/sys/netrollbacksnapshot.c:1017
0x5ab05d -> syNetRbSnapSweepZombieKirbyInhaleWindEffects
            port/net/sys/netrollbacksnapshot.c:15670
```

The determinism fix held; this is a separate process-chain corruption exposed later in the same soak.

## Root cause

`syNetRbSnapSweepZombieKirbyInhaleWindEffects` walks live effect GObjs during
`syNetRbSnapApplySlotToLive`. For each effect it calls `syNetRbSnapEffectGObjHasUpdateProc`,
which walked the owning GObj's process chain without a bound:

```c
for (gobjproc = gobj->gobjproc_head; gobjproc != NULL; gobjproc = gobjproc->link_next)
```

That chain was cyclic.

The source is the prior zombie-`GObjProcess` crash guard in `gcRunGObjProcess`: it handled a
`parent_gobj == NULL` func proc by unlinking only from the priority queue (`func_80007784`) and
then immediately pushing the process to the free list (`gcSetGObjProcessPrevAlloc`).

That is incomplete. `func_80007784` does not unlink `link_prev` / `link_next` from the owning
GObj's process chain. The normal helper `func_800077D0` does, but it dereferences
`parent_gobj`, which is exactly what the zombie guard cannot do.

As a result, the old owner-chain neighbors could still point at the retired process, while
`gcSetGObjProcessPrevAlloc` repurposed that same `link_next` as the process free-list pointer.
Effect churn later reallocated through that free list and cross-linked owner-chain traversal with
free-list state. A later unbounded `link_next` walk in the Kirby inhale-wind sweep spun forever.

This is the same family as the 07-01 free-list splice bugs: a recycled/free-list link leaked back
into a live traversal.

## Fix

Two-part fix:

1. `gcRunGObjProcess` zombie-proc retirement is now netmenu-gated and fully cleans the process
   owner-chain links before reusing `link_next` as a free-list pointer:
   - unlink from priority queue (`func_80007784`)
   - splice stale `link_prev` / `link_next` neighbors directly without dereferencing
     `parent_gobj`
   - clear `link_prev` / `link_next`
   - push to the free list only if the process is not already on it
   - if the free-list membership scan itself caps out, log and do not push

2. `syNetRbSnapEffectGObjHasUpdateProc` now bounds its `gobjproc_head` walk at 256 nodes and
   logs `gobjproc_walk_cycle` before returning `FALSE`, so a future process-chain cycle becomes a
   diagnostic instead of a hard hang.

The earlier crash guard was also tightened from `#ifdef PORT` to
`#if defined(PORT) && defined(SSB64_NETMENU)`, matching the offline-vs-netmenu contract for
fork-only rollback repair code.

## Verify

- `build-netmenu` `ssb64` target: links clean.
- `build-offline` `ssb64` target: links clean.
- Soak pending: rerun the Kirby/rebirth soak. Expected result is still deterministic PASS, with no
  watchdog. If `gobjproc_walk_cycle` appears, the cap prevented the hang but another owner/free-list
  splice still needs tracing from the logged GObj.
