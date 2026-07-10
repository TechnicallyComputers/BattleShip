# Netplay: rebirth halo zombie GObjProcess SIGSEGV

**Status:** FIX IMPLEMENTED (`PORT` dispatcher guard + `PORT && SSB64_NETMENU` snapshot eject lifetime fix, soak pending)
**Date:** 2026-07-02

## Symptom

soak2 session `1957932358` passed deterministic drift checks (`LOAD_HASH_DRIFT=0`, `SYNCTEST_FAIL=0`,
`netplay-scan-drift.py` `RESULT: PASS`) but both peers crashed deterministically when both fighters were KO'd
close together.

The raw Linux log symbolized the fault:

```text
SSB64: !!!! CRASH SIGSEGV fault_addr=0xc8
SSB64: x0=0x0
/tmp/.mount_.../usr/bin/BattleShip(gcPlayAnimAll+0x9)
```

The final sim events were player 0 entering `dead_init` at tick 3090 and player 1 entering
`RebirthDown` at tick 3091. `gcPlayAnimAll` is the rebirth-halo effect's update proc, and
`fault_addr=0xc8` is a NULL `GObj *` dereference of `gobj->obj` (`GObj::obj` in the LP64 layout).

## Root Cause

During the double-KO overlap, the live-forward rebirth catch-up path reclaimed stale effect shells, then minted
a new rebirth halo. `syNetRbSnapEjectGObj` called `gcEjectGObj` for effect GObjs and immediately freed/cleared the
associated `EFStruct`.

That is unsafe if `gcEjectGObj` defers because the target is the currently-running GObj: the GObj and its process
remain live until the dispatcher finishes the current proc, but the snapshot helper had already torn down the
effect-side payload. The follow-on process queue could then contain a function process with no valid parent owner,
so `gcRunGObjProcess` dispatched:

```c
gobjproc->exec.func(gobjproc->parent_gobj);
```

with `parent_gobj == NULL`, calling `gcPlayAnimAll(NULL)` and faulting at `NULL + 0xc8`.

This is the rebirth-halo variant of the recycled-effect/zombie-process crash family documented by the quake and
ImpactWave rollback-exclusion fixes.

## Fix

- `gcRunGObjProcess` now has a `PORT` safety net for function processes with `parent_gobj == NULL`: log in
  netmenu, unlink the process from the priority queue, return it to the process free list, and continue with the
  next queued process instead of calling through a NULL owner.
- `syNetRbSnapEjectGObj` now frees/clears an effect's `EFStruct` only when `gcEjectGObj` actually stamped the GObj
  as ejected. If the eject was deferred because the GObj is current, the helper returns without pre-freeing the
  payload.

Offline gameplay behavior is unchanged except for the general `PORT` dispatcher hardening against an otherwise
fatal NULL-owner process.

## Verify

Re-run the dual-KO soak with `SSB64_NETPLAY_REBIRTH_GATE_DIAG=1` and `SSB64_NETPLAY_GOBJ_EJECT_TRACE=1`.
Expected: the tick-3091 rebirth proceeds without `SIGSEGV`; if a stale process still appears, the log should show
`gcRunGObjProcess ZOMBIE_FUNC_PROC ... - unlinking` and the session should continue.
