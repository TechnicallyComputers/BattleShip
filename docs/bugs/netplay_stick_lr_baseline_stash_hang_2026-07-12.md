# Stick L/R: early baseline stash never opens gate (dual-initiator hang)

**Date:** 2026-07-12  
**Session:** `2125145770` seed `1915743095` (Android client ↔ Linux host, D=2)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Near-immediate desync on rapid L/R stick. Stick REPLACE consolidation queued GGPO correctly; session still died.

## Timeline

1. Ep1 `411→415` (`0→-20`) and Ep2 `415→417` (`0→-73`) complete on **both** peers with matching baseline/post hashes.
2. Release/flick REPLACE `-73→2,11` opens Ep3 `417→419` load=`416`; **both** peers begin as `local_initiator`.
3. Android: `RESIM_BASELINE_RECV` → `digest matched` → Replay → **complete epoch=3**.
4. Linux: recv baselines only **before** `resim begin` (`BASELINE_PREEMPTIVE_LIVE_CAP`); after begin, **0×** `digest matched`.
5. Linux pump: `BASELINE_UNIVERSE_STORM_CAP` with `peer_figh==local_figh==0x607AE79F` → `RESIM_BASELINE_TIMEOUT baseline_matched=0` → forever `ROLLBACK_SYNC_SEND` while Android lives on.

## Root cause

1. **Stash not re-compared:** Peer baseline arriving before `AwaitingBaseline` is stored as `LastPeerOutcome` / preemptive live-cap, but `TryOpenResimBaselineGateFromPeerDigest` no-ops until awaiting. After await arms, no fresh RECV (peer already matched and finished).
2. **Storm cap on matching figh:** Pump counted repeated `(load, peer_figh, local_figh)` even when equal and suppressed retransmit instead of opening the gate.
3. **Episode spam:** Post-complete REPLACE immediately opened a third initiator episode within phase_lock of the prior target.

## Fix

1. `syNetRollbackTryOpenBaselineGateFromStashedPeerOutcome` — on episode begin (and each baseline pump), compare `LastPeerOutcome` at `load_tick` via `TryOpenResimBaselineGateFromPeerDigest`.
2. Storm cap only when `PeerDigestUniverseMismatch` (real disagree); matching stash opens gate / continues seal pump.
3. Post-episode stick absorb window (`completed_target + phase_lock`): `QueueOrWidenStickCorrection` arms/widens deferred instead of a fresh Request.

## Verify

Re-soak L/R stick storms. Expect `BASELINE_STASH_COMPARE` → `digest matched` when peer digest arrived early; no `RESIM_BASELINE_TIMEOUT baseline_matched=0` with equal figh; fewer back-to-back initiator episodes during continuous analog.
