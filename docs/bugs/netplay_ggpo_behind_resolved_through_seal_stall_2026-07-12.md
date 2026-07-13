# GGPO mismatch behind resolved_through → seal stall / hard desync

**Date:** 2026-07-12  
**Session:** `1279881942` seed `3866176352` (Android client lp=1 ↔ Linux host lp=0)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Forward drift scan PASS (no `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT`). Session still dies:

```text
[Linux]  RESIM_BASELINE_TIMEOUT ... baseline_matched=0 seal_rows_missing=0x2
[Linux]  RESIM_SEAL_ROWS_TIMEOUT missing_slots=0x2
[Linux]  RESIM_ANCHOR_PROBE_MISMATCH → hard desync recovery
```

Stick-down Pass/Squat onset: GGPO #1 `406→409` succeeds (both peers seal, baselines match). Stick keeps changing → GGPO #2 `mismatch=408 target=411` with **408 < `resolved_through=409`**.

Android rejects follower join (`PEER_SYMMETRIC_NOTIFY_REJECT reason=resolved_through`), only `EPISODE_SEAL_ROWS_EARLY_STASH` for slot 0, **never sends slot-1 seals**. Linux waits on `seal_rows_missing=0x2`; self-seal fallback never runs (`baseline_matched=0`).

Statuses: 28=Squat, 33=Pass (Android ahead into Pass while Linux lagged on seal gate).

## Root cause

1. After episode complete, feel-0 / late-wire stick REPLACE re-queues GGPO for a tick already inside the sealed span (`mismatch < resolved_through`).
2. `TryCommitCorrectionBegin` allows that when `sim > resolved_through` by resetting the episode anchor — SYNC goes out behind the follower’s sealed frontier.
3. Follower `AcceptPeerSymmetricRollbackNotify` rejects; early-stashed initiator seals never flush into a live episode; follower never replies with local-authority seals.
4. Initiator times out on missing peer seal rows; deepen / probe / hard desync.

Secondary: `DeferRemoteInputCorrection` dropped REPLACE when `sim_tick >= ResimTargetTick`, so mid-episode stick updates became a post-complete behind-resolved GGPO instead of extending the deferred target.

## Fix

1. **`syNetRollbackQueueDeferredInputCorrectionEx`** — shallow clamp (`behind ≤ max(4, phase_lock)`) of local GGPO mismatch up to `resolved_through` (`CORRECTION_CLAMP_RESOLVED`). Deep FC reanchors still bypass clamp and use TryCommit’s episode reset.
2. **`syNetRollbackOnPeerSymmetricRollbackNotifyEx`** — same shallow clamp before Accept (`PEER_SYMMETRIC_CLAMP_RESOLVED`) so in-flight / pre-clamp peers still join and exchange seals.
3. **`syNetRollbackDeferRemoteInputCorrection`** — when REPLACE lands at/after the open episode target, fold into deferred target extension instead of dropping.

## Verify

- Re-soak Android ↔ Linux stick-down Pass/Squat shortly after a successful GGPO: expect `CORRECTION_CLAMP_RESOLVED` (or no second episode behind resolved), follower join + seal exchange, no `seal_rows_missing` → hard desync.
- Grep should not show `PEER_SYMMETRIC_NOTIFY_REJECT reason=resolved_through` for mismatch = resolved−1 after a completed episode while stick is still moving.
- Deep FC reanchor (mismatch ≫ phase_lock behind resolved) must still be able to open past resolved_through.
