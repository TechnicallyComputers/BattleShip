# Follower hang: baseline echo retry blocks ROLLBACK_SYNC + seal reject storm

**Date:** 2026-07-12  
**Session:** soak1 Android client ↔ Linux host (Z press @413, Z release @423)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

After first GGPO resim (Z on @413 → Guard) matched on both peers, second GGPO (Z off @423) left Android frozen under live-cap for seconds while wall frames continued. Linux completed initiator resim. No `FC@` / `state_diverge` (early synctest OK). Android: **0×** second `resim begin`, **91×** `EPISODE_SEAL_ROWS_REJECT … stale_episode_tuple` (`pkt_epoch=2`, `active_epoch=0`).

## Root cause

1. Host baseline arrived; follower `RESIM_BASELINE_ECHO` armed `BASELINE_ECHO_RETRY_DEFER` (`snapshot_not_ready`) → `sSYNetRollbackBaselineEchoRetryLoadTick` set.
2. `syNetRollbackBaselineCompareQuiesced()` returns FALSE while echo retry is pending.
3. `ROLLBACK_SYNC` therefore only **deferred** peer-symmetric; `FlushDeferredPeerSymmetric` used the same quiesce check → never queued Begin.
4. Live-cap (`BASELINE_PREEMPTIVE_LIVE_CAP` / reject cap) held sim; seals arrived before `FsmBegin` and were rejected because `EpisodeTupleMatches` required an active epoch (still 0 / Live).
5. `FsmBegin` also **cleared all** pending seal stashes, so even a late stash could not survive Begin.

## Fix

1. **Peer-symmetric flush quiesce** ignores echo-retry alone — SYNC still queues / flushes so follower can Begin under live-cap.
2. **Early seal stash** when FSM is not active (Live): stash by packet tuple instead of `stale_episode_tuple` reject.
3. **`FsmBegin`** clears only non-matching pending seal chunks so early stashes apply after `SealInputs`.

## Verify

- Mash Z on / off after GO: Android should log `resim begin … owner=peer_follower` for the release episode (not a multi-second live-cap freeze).
- Prefer `EPISODE_SEAL_ROWS_EARLY_STASH` / `SEAL_ROWS_RECV` over `stale_episode_tuple` storms.
- Optional: `peer symmetric queue despite echo_retry` when SYNC races echo defer.
