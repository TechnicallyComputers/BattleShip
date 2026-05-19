# NetRollback sync contract — load fail retry, snapshot barrier, anim-only peer compare

**Date:** 2026-05-18  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

- Client soak: `load post tick 410 failed` on first symmetric rollback; no `resim begin` / `resim complete` on follower.
- Hundreds of ticks later: gameplay hashes agree at baseline compare; only `anim` differs → `PEER_SNAPSHOT_DIVERGE` abort.
- `RESIM_BASELINE_ECHO_SKIP reason=no_snapshot` at `load_tick == sim_tick` (snapshot not committed yet).

## Root cause

1. **Episode anchor without resim:** `TryCommitCorrectionBegin` ran before `BeginResim`; a failed load left the episode anchor set so later symmetric retries were rejected (`target_tick <= EpisodeLastTargetTick`).
2. **Snapshot timing:** Baseline echo/compare could run at `load_tick == sim_tick` before `syNetRollbackAfterBattleUpdate` committed the ring slot for that tick.
3. **Policy mismatch:** `LOAD_HASH_DRIFT` soft-continues anim-only drift after load, but `PEER_SNAPSHOT_DIVERGE` hard-aborted on anim-only baseline mismatch while figh/world/rng/item/map/cam matched.
4. **Overlapping epochs:** `ROLLBACK_SYNC` at tick N+1 could queue while baseline compare for tick N was still in flight.

## Fix

- Reset correction episode on load failure; resolve `load_tick` via `syNetRbSnapshotFindLatestValidTickAtOrBefore` (bounded rewind).
- Track `syNetRbSnapshotLastCommittedTick`; defer baseline compare until `load_tick < sim_tick` and slot is committed (`syNetRbSnapshotIsTickCommitted`).
- `PEER_BASELINE_ANIM_ONLY` soft-continue when gameplay partitions match; resim baseline gate no longer blocks on anim.
- Defer peer `ROLLBACK_SYNC` while baseline echo retry or resim baseline gate is active; flush when quiesced.

## Verification

Build: `ssb64`. Re-soak paired host/client logs; expect symmetric `resim begin` on both peers, no anim-only session stop.
