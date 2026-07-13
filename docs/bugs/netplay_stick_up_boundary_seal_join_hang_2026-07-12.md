# Stick-up boundary seal join hang (ep2 after jump onset)

**Date:** 2026-07-12  
**Session:** soak1 Android↔Linux seed `3199530087` (`STRICT_INPUT=1`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Simple stick-up / jump onset desyncs within ~500 ticks. Linux opens ep0/ep1 OK then ep2 `502→504`; Android logs `peer symmetric rollback queued` + `peer symmetric rollback at tick 502` (×3) but **never** `resim begin` for 502. Linux `RESIM_BASELINE_TIMEOUT seal_rows_missing=0x2` → `VS_SESSION_END`. FC clean (`state_diverge=0`).

## Root cause

Back-to-back stick REPLACE episodes at `resolved_through` (CORRECTION_CLAMP_RESOLVED), then follower fails to join:

1. **Stale live-cap clear at boundary:** `MaybeClearStalePeerSymmetricRejectLiveCap` cleared when `sim >= resolved_through && sim > cap`. After ep1 (`resolved=502`), preemptive baseline armed `cap=501` for mismatch=502; clear fired immediately → sim advanced to 503 while initiator waited on seals.
2. **GlobalCooldown blocked peer-symmetric TryCommit:** Follower joined ep1 at live frontier (`LastBegin≈502`); ep2 SYNC one tick later (`sim=503`) failed cooldown (default 2) after logging "at tick" — no `try_begin_fail` on that path.
3. **Stick absorb still armed new GGPO:** Absorb window called `QueueDeferred`, which `CORRECTION_CLAMP_RESOLVED` into a fresh episode every phase_lock ticks.
4. **PendingEpisode raise lock (defense):** `PendingEpisodeSet` rejected mismatch raises when `epoch_id=0` re-Set after a settled tuple (QueuePeerSymmetricNotify).

## Fix

1. Live-cap stale clear: only when `(cap+1) < resolved_through` (same strict boundary rule as `PreemptiveBaselineCapIsStale`).
2. `GlobalCooldownAllows`: bypass for matching pending / deferred peer-symmetric notify (same class as deferred GGPO bypass).
3. Stick absorb: skip opening a new deferred during the window when none is open (wire REPLACE still updates confirmed history; open deferred/resim still widens via `DeferRemote`).
4. `PendingEpisodeSet`: allow boundary raise when locked mismatch is behind `resolved_through` and new mismatch is at/after it.
5. Always-on `try_begin_fail` reasons inside `TryCommitCorrectionBegin` + `peer_sym_begin_resim` / `peer_sym_not_allowed` on join paths.

## Verify

Re-soak stick-up / jump with `SSB64_NETPLAY_STRICT_INPUT=1`. Expect: no `LIVE_CAP_CLEAR stale` for `cap_mismatch == resolved_through`; follower `resim begin` for each initiator SYNC; absorb does not open ep every 2 ticks; if join still fails, logs show `try_begin_fail stage=commit_*` or `peer_sym_begin_resim`.
