# Netplay: `mmIcePoll` candidate drain overflows GObj coroutine stack

**Date:** 2026-07-12  
**Scope:** `port/net/matchmaking/mm_ice.c`, `port/net/sys/netrollback.c`. `PORT && SSB64_NETMENU && SSB64_NETPLAY_ICE`.  
**Status:** FIX IMPLEMENTED (soak pending).  
**Class:** coroutine stack overflow (same family as `netplay_map_hash_scratch_stack_overflow_2026-07-03`).

## Symptom

Soak1 Linux guest (`sigsegv=1`) after a seal/baseline storm around sim tick 657:

```
NetRollback: resim baseline (post-load tick=656) ... figh=0x4E4C8CC5 ...
!!!! CRASH SIGSEGV fault_addr=<near sp>
pc=... mmIcePoll+0x47  lr=0  fp=0x4e4c8cc5
```

- `fp` equals the just-logged fighter hash → garbage frame pointer (omit-FP leftover / blown stack).
- `fault_addr ≈ sp-8` → stack pointer already in the coroutine `PROT_NONE` guard page.
- Single-frame backtrace (`mmIcePoll` only) — FP chain cannot walk.
- Preceded by hundreds of `rollback_load_deeper` @656 + `BASELINE_ECHO_RETRY` @657 + `seal_authority_mismatch` (peer/local figh swapped).
- Android peer stopped cleanly with `PEER_SNAPSHOT_DIVERGE` @657 (figh+anim); Linux kept restarting deeper loads until the stack died.

## Root cause

1. **`mmIcePoll` stack frame:** drained candidates into
   `MmIceCandidateSlot cand_copy[MM_ICE_CANDIDATE_QUEUE]` on the stack
   (`280 × 32 ≈ 9 KB`). Poll is called from `syNetPeerUpdate` on the VS GObj
   coroutine (`PORT_STACK_GOBJ = 64 KB`) immediately after deep
   `syNetRollbackLoadPostTick` / figatree-bind frames. The 9 KB allocation tips
   `rsp` into the guard page → SIGSEGV attributed to `mmIcePoll`.

2. **Unbounded deeper-load from seal mismatch:** `EPISODE_FSM seal_authority_mismatch`
   always called `AbortPendingResimForBaselineMismatch`, which called
   `TryRestartResimAtDeeperLoad` **without** checking
   `SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS` (that cap only gated timeout/fidelity
   sites). Result: hundreds of 656↔657 load/echo cycles on one stack budget,
   amplifying (1).

## Fix

1. Hoist candidate drain scratch to file-static `sCandDrainCopy[]` with
   `sCandDrainActive` reentrancy guard; clamp `cand_count` to queue size.
2. Gate `AbortPending` deeper restart on `DEEPER_MAX_ATTEMPTS`; when seal
   mismatch sees deeper budget exhausted, call `FailPeerSnapshotDiverge` (same
   clean stop Android already took) instead of restarting forever.

## Soak procedure

Re-run Android/Linux pair soak. Expected: no `SIGSEGV` in `mmIcePoll`; if the
underlying figh/anim fork at ~657 remains, both peers should hit
`PEER_SNAPSHOT_DIVERGE` / session stop without a crash. Separate work remains
to fix the real sim diverge at load_tick 657.
