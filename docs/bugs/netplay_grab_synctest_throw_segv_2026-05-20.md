# Netplay grab/throw SYNCTEST SIGSEGV (all slots)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (coupling rebind + throw guards; synctest skip re-enabled 2026-05-28 pending throw round-trip soak)

## Symptoms

2P soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: Fox (P0) standard grab → throw crashes at **sim tick ~1752** on both peers:

- `ftCommonThrownReleaseThrownUpdateStats+0x3f`, `fault_addr=0xe0` → NULL victim `capture_gobj` / `capture_fp->throw_desc`
- Yoshi (P1) grab/throw OK (different capture path, no `ReleaseThrownUpdateStats`)
- First Fox throw (~866) OK; second grab failed after **`SYNCTEST_OK tick=1739`** mid-grab

**2026-05-28 regression (Mario vs Luigi automatch):** `SYNCTEST_FAIL` + `LOAD_HASH_DRIFT` mid-throw (P0 `ThrownCommon`, P1 `ThrowB`), then SIGSEGV in `ftCommonThrownReleaseFighterLoseGrip` at `fault_addr=0xe0` — NULL `interact_fp->coll_data` when `is_catch_or_capture` is set but `catch_gobj` failed to resolve after snapshot apply.

## Root cause

Synctest emergency restore during active grab broke **bidirectional** `catch_gobj` ↔ `capture_gobj` coupling. Blobs save both IDs per slot, but one side could restore NULL while throw release still ran on the victim via `ftCommonThrowProcUpdate`.

`throw_desc` is runtime-only (motion `SetThrow` event), not snapshotted — secondary risk if coupling survives but anim state is stale.

Mid-throw synctest can also leave `is_catch_or_capture` TRUE with a NULL partner GObj after `syNetRbSnapResolveLiveGobj` miss; release paths dereference the partner without checking.

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapshotAnyFighterGrabCouplingActive()` — scan **all** fighter link GObjs | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Skip synctest while any slot has `catch_gobj` or `capture_gobj`; retry next tick | `syNetRbSnapshotSynctestShouldSkip` → `reason=grab_coupling` *(re-added 2026-05-28 until throw round-trip soak passes)* |
| `syNetRbSnapRebindFighterGrabCoupling()` — repair both directions for every fighter after apply + on `RebindAllFighters` | `netrollbacksnapshot.c` |
| `syNetRbSnapScrubFighterGrabCouplingState()` — clear stale `is_catch_or_capture` / dangling partner pointers after apply | `netrollbacksnapshot.c` |
| NULL guard on partner / `capture_gobj` / `capture_fp` in throw release + lose-grip | [`decomp/src/ft/ftcommon/ftcommonthrown2.c`](decomp/src/ft/ftcommon/ftcommonthrown2.c) |

## Soak pass criteria

- Fox grab → throw with synctest enabled across a 120-tick probe during Catch/CatchWait/Throw
- Log `SYNCTEST_SKIP reason=grab_coupling` during grab, then `SYNCTEST_OK` after release
- No SIGSEGV at throw release; knockback applies normally
- Yoshi grab path unchanged
- Mario/Luigi (and other pairs) automatch grab→throw with synctest: no `LOAD_HASH_DRIFT` mid-throw SIGSEGV
