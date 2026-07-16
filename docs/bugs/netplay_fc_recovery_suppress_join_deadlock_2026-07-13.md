# FC recovery: unstarted arm suppresses peer SYNC → follower hang

**Date:** 2026-07-13  
**Session:** soak2 Android ↔ Linux (FC@480 figh diverge, inputs MATCH)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

After FC@480 (`diverged=figh`, inputs agree; P1 one-tic `status_total_tics` skew):

```text
[Linux]  deferred frame-commit state resim 379→481; resim begin owner=local_initiator
[Linux]  EPISODE_SEAL_ROWS_WAIT missing_slots=0x2; RESIM_BASELINE_TIMEOUT seal_rows_missing=0x2
[Android] peer symmetric suppressed by frame-commit state recovery … fc_mismatch=379 fc_target=481
[Android] BASELINE_PREEMPTIVE_LIVE_CAP load_tick=378 … sim=480
[Android] rollback_epoch_hold … cap=378; EPISODE_SEAL_ROWS_REJECT stale_episode_tuple active_epoch=0
[Android] FRAME_COMMIT_DIAG deferred_armed=1 recovery_started=0
```

Android frozen until Linux solo-completes / session ends. Not soak1 `ness_pk_defer` (no PK Hold in these logs).

## Root cause

1. Both peers arm `FcStateRecoveryActive` + deferred state mismatch for the same span on FC diverge.
2. `PeerSymmetricSuppressedByFcStateRecovery` returned TRUE whenever the notify span was covered — **even when local recovery never `BeginResim`'d**.
3. Loser’s Accept dropped initiator SYNC → no follower episode (`active_epoch=0`) → seals rejected; preemptive baseline live-cap held sim at load−1.
4. Orphan risk: no-snap / bad-tuple paths could clear deferred pending while leaving `FcStateRecoveryActive`, which keeps suppress armed forever.

## Fix

1. **Suppress only in-flight** — `PeerSymmetricSuppressedByFcStateRecovery` returns FALSE unless `ResimPending` or `IsResimulating`.
2. **Yield unstarted FC** — on Accept of matching peer notify while FC/deferred armed but not begun: clear local FC arm + deferred pending (`EPISODE_YIELD unstarted FC recovery to peer notify`), then join as follower.
3. **No orphan FC latch** — clear `FcStateRecovery` on no-snap / bad-tuple / commit/begin failure (use `ClearFcStateRecovery`).
4. **Breadcrumbs** — always-on rate-limited `try_begin_fail stage=fc_*` for deferred state TryBegin early returns.

## Verify

- Re-soak FC figh diverge with inputs MATCH (Turn/Dash tic skew or similar).
- Expect Android: `EPISODE_YIELD unstarted FC…` then `resim begin … owner=peer_follower` (not multi-second `cap=load_tick` + `stale_episode_tuple` with `recovery_started=0`).
- Linux: seals complete without prolonged `seal_rows_missing=0x2` storm, or both peers converge the episode.
