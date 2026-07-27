# Baseline echo `snapshot_not_ready` while ring hashes ready (2026-07-26)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`)  
**Soak (pre-fix):** session `1482152420` seed `3088210091` — Android client ↔ Linux host  
**Soak (accept):** session `391759253` seed `2713861087` — 0× `BASELINE_ECHO_RETRY_DEFER`; digest matched healthy; prior TryBegin phases still 0×; max span 6  
**Bucket:** `REPLAY_DETERMINISM` / protocol  
**Phase:** 4 (defer-removal series; after [`netplay_fc_commit_behind_frontier_deepen_2026-07-26.md`](netplay_fc_commit_behind_frontier_deepen_2026-07-26.md))

## Symptom

Phase 3 cleared `commit_behind_*`. Hot residual:

| Peer | `BASELINE_ECHO_RETRY_DEFER (snapshot_not_ready)` |
|------|--------------------------------------------------|
| Linux (guest label) | 207× |
| Android (host label) | 62× |

Typical cluster (linux load=422):

```
RESIM_BASELINE_ECHO live_apply load_tick=422 sim=425
RESIM_BASELINE_ECHO load_tick=422 slot figh=…
BASELINE_ECHO_RETRY_DEFER load_tick=422 sim=425 attempt=1 (snapshot_not_ready)
ROLLBACK_SYNC_RECV … load_tick=422 resolved=416
BASELINE_ECHO_RETRY_DEFER … attempt=2
… later: resim baseline digest matched load_tick=422
```

Echo succeeds; compare still defers. `EchoRetryLoadTick` also blocks `BaselineCompareQuiesced`.

## Root cause

`syNetRollbackSnapshotReadyForBaselineCompare` returned FALSE when `load_tick > EpisodeResolvedThrough` (422 > 416) even though:

1. Peer initiator already chose that load as episode anchor
2. Local `RESIM_BASELINE_ECHO` committed ring hashes for that tick

Hard “never compare ahead of resolved” was meant for unsealed speculative ticks; peer episode loads ahead of local seals are the normal follower path.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| `SnapshotReadyForBaselineCompare` | If `load > resolved_through` but ring tick committed → ready |
| `OnPeerBaselineDigest` | If readiness false but `GetStoredSubsystemHashes` OK → `BASELINE_COMPARE_RING_READY` and compare now (no EchoRetry arm) |

Offline unchanged. True missing-slot cases still defer / retry.

## Acceptance

Matched APK + Linux:

- Large drop in `BASELINE_ECHO_RETRY_DEFER` (expect near-zero after successful echo)
- `BASELINE_COMPARE_RING_READY` and/or immediate `resim baseline digest matched` on follower recv
- No seal-hang / PEER storm from comparing ahead-of-resolved committed loads
- Phase 2/3 stages remain 0×

## Related

- [`netplay_follower_seal_reject_echo_retry_hang_2026-07-12.md`](netplay_follower_seal_reject_echo_retry_hang_2026-07-12.md) — echo_retry blocked flush
- [`netrollback_epoch_pacing_analog_decay_2026-05-18.md`](netrollback_epoch_pacing_analog_decay_2026-05-18.md) — original resolved_through seal gate
- [`netplay_baseline_universe_mismatch_ignored_2026-07-12.md`](netplay_baseline_universe_mismatch_ignored_2026-07-12.md) — `load >= sim` ready park
- Defer-removal series closed on protocol gates (Phases 1–4). Next soak targets: invent/locomotion (Turn-Dash, hold_last) and seal traffic (`EPISODE_SEAL_ROWS_*`, `BASELINE_PREEMPTIVE_LIVE_CAP`).
