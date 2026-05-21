# Netplay grab/throw SYNCTEST SIGSEGV (all slots)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

2P soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: Fox (P0) standard grab → throw crashes at **sim tick ~1752** on both peers:

- `ftCommonThrownReleaseThrownUpdateStats+0x3f`, `fault_addr=0xe0` → NULL victim `capture_gobj` / `capture_fp->throw_desc`
- Yoshi (P1) grab/throw OK (different capture path, no `ReleaseThrownUpdateStats`)
- First Fox throw (~866) OK; second grab failed after **`SYNCTEST_OK tick=1739`** mid-grab

## Root cause

Synctest emergency restore during active grab broke **bidirectional** `catch_gobj` ↔ `capture_gobj` coupling. Blobs save both IDs per slot, but one side could restore NULL while throw release still ran on the victim via `ftCommonThrowProcUpdate`.

`throw_desc` is runtime-only (motion `SetThrow` event), not snapshotted — secondary risk if coupling survives but anim state is stale.

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapshotAnyFighterGrabCouplingActive()` — scan **all** fighter link GObjs | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Skip synctest while any slot has `catch_gobj` or `capture_gobj`; retry next tick | [`port/net/sys/netrollback.c`](port/net/sys/netrollback.c) |
| `syNetRbSnapRebindFighterGrabCoupling()` — repair both directions for every fighter after apply + on `RebindAllFighters` | `netrollbacksnapshot.c` |
| NULL guard on `capture_gobj` / `capture_fp` / `throw_desc` in throw release | [`decomp/src/ft/ftcommon/ftcommonthrown2.c`](decomp/src/ft/ftcommon/ftcommonthrown2.c) |

## Soak pass criteria

- Fox grab → throw with synctest enabled across a 120-tick probe during Catch/CatchWait/Throw
- Log `SYNCTEST_SKIP reason=grab_coupling` during grab, then `SYNCTEST_OK` after release
- No SIGSEGV at throw release; knockback applies normally
- Yoshi grab path unchanged
