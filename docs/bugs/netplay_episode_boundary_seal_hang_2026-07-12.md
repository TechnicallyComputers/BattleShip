# Episode boundary seal rendezvous hang (ep10 STRICT_INPUT soak)

**Date:** 2026-07-12  
**Session:** STRICT_INPUT soak (Android follower ↔ Linux initiator)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Synctest stable through ep9; hang at **episode 10** around load/mismatch **638–641**. Linux initiator opens ep10, waits on follower slot-1 seals (`RESIM_BASELINE_TIMEOUT seal_rows_missing=0x2`), storms `ROLLBACK_SYNC`, eventually `VS_SESSION_END`. Android follower: only `EPISODE_SEAL_ROWS_EARLY_STASH` + `BASELINE_PREEMPTIVE_LIVE_CAP_SKIP stale` loops; never `EPISODE_FSM begin epoch=10` or `resim begin` for 639. Replay stops ~641 while UI keeps rendering.

## Root cause

Back-to-back stick REPLACE episodes after ep9 settle (`resolved_through=639`):

1. **Stale preemptive live-cap:** `PreemptiveBaselineCapIsStale` used `preempt_mismatch <= resolved_through`, so load=638 (preempt=639) was treated as settled when it is the **first tick of the next episode**.
2. **Pending SYNC stranded by live-cap:** Queued `ROLLBACK_SYNC` (639→641) set preemptive live-cap (cap=638) but `syNetRollbackUpdate` returned at tick-commit gate before draining pending peer-symmetric into `BeginResim`.
3. **Stale pending not upgraded:** `QueuePeerSymmetricNotify` ignored `mismatch > pending` when initiator opened the next episode at the boundary while prior notify never drained.

## Fix

1. `PreemptiveBaselineCapIsStale`: strict `<` vs `resolved_through` (boundary mismatch is not stale).
2. `syNetRollbackTryBeginResimFromPendingPeerSymmetric`: run before tick-commit gate in `syNetRollbackUpdate`.
3. `QueuePeerSymmetricNotify`: replace stale pending when new mismatch ≥ `resolved_through`; reject only strictly older notifies.
4. `AcceptPeerSymmetricRollbackNotify`: widen pending/deferred target on duplicate mismatch instead of rejecting wider initiator spans.

## Verify

Re-soak with `SSB64_NETPLAY_STRICT_INPUT=1` on both peers. Expect follower `peer symmetric rollback queued` → `resim begin epoch=10` at mismatch=639; no infinite `BASELINE_PREEMPTIVE_LIVE_CAP_SKIP stale load=638`; initiator receives follower seals and ep10 completes.
