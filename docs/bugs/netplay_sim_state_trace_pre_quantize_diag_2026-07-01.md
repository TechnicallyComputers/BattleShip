# Netplay — sim-state/fighter-slot-hash diagnostic trace sampled before quantize

**Date:** 2026-07-01
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`) — diagnostic-only, verified via clean build
**Discovered while investigating:** [netplay_fox_appear_firefox_charge_soak2_2026-07-01](netplay_fox_appear_firefox_charge_soak2_2026-07-01.md) round 5

## Symptom

Every soak in the Fox Firefox investigation (5 sessions across 5 rounds of unrelated fixes) reported `netplay-trim-logs.py --sync-report`'s "first sim_state mismatch" at **the same tick (523)**, regardless of what the fighters were actually doing at that tick — sometimes mid-Firefox-charge, sometimes a plain Fox short-hop. A mismatch tied to a fixed tick number independent of gameplay content is a strong signal of a harness/tooling artifact rather than a real content-dependent bug.

## Root cause

`syNetPeerMaybeLogSimStateTickTrace()` (emits the `SSB64 NetSync: fighter_slot_hash` / `sim_state_tick` diagnostic lines) was called from inside `syNetPeerUpdate()`. Per `scVSBattleFuncUpdate()` (`decomp/src/sc/sccommon/scvsbattle.c`), that call happens **before** `syNetRollbackAfterBattleUpdate()` — which is where `syNetplayCanonicalizeActiveFightersForNetplay()` (the cross-ISA quantize pass over fighter physics/joints/camera) actually runs for the tick that just finished simulating, immediately followed by `syNetRollbackSavePostTick()` (the ring/snapshot capture).

The real cross-peer consensus mechanism, `syNetFrameCommitBuildToken()` (`port/net/sys/netpeer_frame_commit.c`), reads its `fighter_digest` from that same post-quantize ring via `syNetRbSnapshotGetStoredSubsystemHashesEx(validation_tick - 1, ...)`. So the actual frame-commit comparison was always correct (post-quantize), but the diagnostic trace we were reading to *localize* divergences was one step earlier in the pipeline — the raw, pre-quantize live hash.

This matters specifically at rollback misprediction boundaries: a peer's original forward-sim pass may guess wrong about a not-yet-received remote input (normal, expected, self-correcting via resim before the tick is ever captured to the ring). The pre-quantize trace shows this transient guess-vs-confirmed disagreement as a "figh mismatch," even though both peers' corrected states — and the actual ring/frame-commit values — agree. Every prior round of the Firefox investigation that cited "first split @523" was chasing this transient, not a real joint-fold gap.

## Fix

- `syNetPeerMaybeLogSimStateTickTrace()` made non-`static` (`port/net/sys/netpeer.c`), declared in `port/net/sys/netpeer.h`.
- Removed its call from inside `syNetPeerUpdate()`.
- Added the call in `scVSBattleFuncUpdate()` (`decomp/src/sc/sccommon/scvsbattle.c`), immediately after `syNetRollbackAfterBattleUpdate()` and before `syNetInputAdvanceAuthoritativeSimTick()` — same tick number (`syNetInputGetTick()` hasn't advanced yet), now sampled after quantize instead of before.

Diagnostic-only: does not touch `syNetFrameCommitBuildToken`, the ring, or any rollback/gameplay logic. `scVSBattleFuncUpdateBattleSimOnly()` (the skew-pacing net-slice path) never called `syNetPeerUpdate()` and so never ran this trace either; left as-is.

## Verify

Built clean (`cmake --build build --target ssb64`). Next soak should show:
- "First sim_state mismatch" ticks that vary with gameplay content instead of landing on a fixed lag-boundary tick every run
- Any genuine remaining `FRAME_COMMIT_STATE_DIVERGE` localizable directly from the (now post-quantize) `fighter_slot_hash` trace without needing to cross-reference the ring separately
