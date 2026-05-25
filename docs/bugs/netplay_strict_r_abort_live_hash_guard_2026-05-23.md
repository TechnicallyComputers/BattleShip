# Netplay: strict R abort never fired + frame-commit live-hash guard

**Date:** 2026-05-23  
**Status:** Fix shipped (soak pending)  
**Soak:** `1748056141` (Samus vs Samus, “clean” FC through ~1100, end freeze @ tick 1104)

## Symptoms

- Guest spun **2576+** `path=R` / `STRICT: … MISS (R)` iterations at sim tick **1104** without `strict remote MISS stall abort` log (default limit **180**).
- Host advanced to sim **1108** with `path=P`; guest stuck at tick **1104** with `rcv_hw=1103`, last `INPUT recv seq=1103`.
- Cross-peer `sim_state_tick` first mismatch at **1104** (`figh` diverged); `FRAME_COMMIT_DIAG` still `state_diverge=0`.
- Frame-commit tokens at validation **1104** agreed (both built from snapshot tick **N−1** = 1103) while host **live** sim had already stepped to 1104 state.

## Root causes

### 1. Strict R abort defeated by `syNetTickCommitVerdictAllowAll`

On `syNetInputStrictRemoteMissAbortIfStuck` success, wire admission called `syNetTickCommitVerdictAllowAll(out)` and returned. `syNetInputFuncRead` then took the full **P** publish path and reset `sSYNetInputStrictRStuckFrames` at the end of FuncRead — so the stall counter never reached the abort threshold despite thousands of **R** admissions.

Secondary: counter used “reset to 0 on first stall frame” semantics (needed **181** evaluate calls for limit **180**); fixed to start at **1** on first stall evaluation.

### 2. Frame-commit token lag vs live sim

`syNetFrameCommitBuildToken` stores subsystem hashes from rollback snapshot **validation_tick − 1** when available. Peers can agree on tokens while one peer’s **live** sim has advanced (host had wire **1107**, guest missing → host simmed ahead). `syNetFrameCommitStateDigestsDiverge` only compares **token** digests, not live hashes.

## Fixes

1. **Strict abort:** On abort, verdict stays blocked (`strict_remote_stall_abort`, admission **`A`**); FuncRead returns immediately without publish. Abort also runs on skew-pacing **R** hold. Counter no longer reset on transient `shared.advance == TRUE` (removed mid-evaluate reset; only cleared after successful **P** publish or after abort fires).
2. **Live-hash guard:** When frame-commit is enabled (default on with `SSB64_NETPLAY_FRAME_COMMIT_TOKEN`), compare live `figh`/`world` to local and peer **token** digests after token state digests agree. On mismatch: `FRAME_COMMIT_LIVE_HASH_GUARD` log, `fc_live_hash_guard` diag counter, `syNetRollbackOnFrameCommitLiveHashGuard` (same recovery core as state diverge). Disable with `SSB64_NETPLAY_FRAME_COMMIT_LIVE_GUARD=0`.

## Verify

- Reproduce ingress stall class: guest should log `strict remote MISS stall abort` within ~180 display-side stall evaluations (3s @ 60Hz) and tear down VS without spinning to 2500+ **R** lines.
- Host advancing past committed token while guest stalls @1104: host should log `FRAME_COMMIT_LIVE_HASH_GUARD` at validation **1104** and arm rollback recovery (not silent `state_diverge=0`).

## Files

- `port/net/sys/netinput.c`, `port/net/sys/netinput.h` — strict abort verdict, FuncRead early return, stall counter
- `port/net/sys/netpeer_frame_commit.c`, `.h` — `syNetFrameCommitLiveHashGuardTripped`
- `port/net/sys/netpeer.c` — compare hook, env `SSB64_NETPLAY_FRAME_COMMIT_LIVE_GUARD`
- `port/net/sys/netrollback.c`, `.h` — `syNetRollbackOnFrameCommitLiveHashGuard`, shared recovery core
