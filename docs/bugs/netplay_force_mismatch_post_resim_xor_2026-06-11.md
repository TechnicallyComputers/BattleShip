# Netplay: FORCE_MISMATCH post-resim XOR on symmetric follower

**Date:** 2026-06-11  
**Status:** Fix shipped (re-soak pending)  
**Soak:** `soak1-linux.log` + `soak1-android.log` (same session, `cseed=0xE7847115`)

## Symptoms

- Intro resim @230 (`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `INJECT_TICK=230`) completes on both peers with matching baseline/post digests.
- Within ~3 ticks of live resume: Linux strict-stall @sim 233 (`rok0=0`, `STRICT … MISS (R)`), session abort; Android keeps simming to 235+ on divergent published history.
- No `FRAME_COMMIT_STATE_DIVERGE` — input-history split, not FC hash validation.

## Root cause

`FORCE_MISMATCH` arms on wire tick 230 on **both** sides. During resim, `syNetRollbackUpdate` only runs the resim budget path and **never** calls `syNetRollbackDebugTryApplyPendingForceMismatch`.

| Peer | When XOR fires |
|------|----------------|
| Initiator (Linux) | Pre-resim @sim 231 → `INPUT_MISMATCH` → resim; `InjectConsumed=TRUE` |
| Follower (Android) | **After** `resim complete` on the next live `Update` — second XOR into published history for tick 230 |

The follower resim used sealed inputs for `[230,232)`, but post-commit XOR mutates published history outside that contract. Linux strict gate rejects remote admission; Android continues forward sim → split brain.

## Fix

In `port/net/sys/netrollback.c`:

1. **`syNetRollbackConsumePendingForceMismatchAfterResim`** — on resim complete, if pending inject tick ∈ `[mismatch, target)`, clear pending + mark `InjectConsumed` without XOR (sealed span is authoritative). Log: `FORCE_MISMATCH consume post-resim`.
2. **`syNetRollbackDebugTryApplyPendingForceMismatch`** — early return while `ResimPending` (belt-and-suspenders).

## Verification

Re-run cross-OS soak with `FORCE_MISMATCH` + `INJECT_TICK=230`. Expect:

- At most one `FORCE_MISMATCH detected … XOR` per peer (initiator only, pre-resim).
- Follower logs `FORCE_MISMATCH consume post-resim tick=230 span=[230,232)`.
- Forward sim past 233 without strict-stall abort.
