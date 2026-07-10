# Netplay: frame-commit live-hash guard false positive + seal stale after FSM Live

**Date:** 2026-05-23  
**Status:** Fix shipped (soak pending)  
**Soaks:** Third automatch soak @ tick **480** (`client-auto.log` = host, `host-auto.log` = client)

## Symptoms

- `FRAME_COMMIT_COMPARE` @480: cross-peer tokens agree (`inp`, `figh`, `world` match).
- Client immediately logs `FRAME_COMMIT_LIVE_HASH_GUARD` (`live_world`/`live_figh` differ from token digests).
- `FRAME_COMMIT_INPUT_AGREE_REANCHOR` → 120-tick resim (`load=360`, `target=481`).
- Client `EPISODE_SEAL_ROWS_WAIT missing_slots=0x1` (host slot 0); `RESIM_SEAL_ROWS_TIMEOUT` → session stop.
- Host rejects client `EPISODE_SEAL_ROWS` with `stale_episode_tuple` (`active_epoch=0`); never sends `slot=0` seal rows.

## Root causes

### 1. Live-hash guard compared post-ingress live sim to snap N−1 tokens

`syNetFrameCommitBuildToken` commits hashes from rollback snapshot **`validation_tick − 1`**. Compare often runs after `INPUT recv` on the same display tick, so live `figh`/`world` reflect tick **N** while tokens still describe **N−1**. Cross-peer tokens already agreed — guard was a false positive.

### 2. `EpisodeFsmSetPhase(Live)` called `FsmSessionReset()`

Transition to `Live` zeroed `epoch_id` / seal state while the peer was still in recovery. Host stayed at `active_epoch=0` and rejected in-flight seal packets; client waited forever for host authority rows.

## Fixes

1. **`syNetFrameCommitLiveHashGuardTripped`:** Primary check: ring snapshot @ `validation_tick−1` vs local token digests. Fallback live compare only when sim is **>1 tick** past validation and snapshot slot is missing (preserves strict-R “host ran ahead” case).
2. **`syNetRollbackEpisodeFsmSetPhase(Live)`:** No longer calls `SessionReset()` — tuple + seal send state retained until `FinishForwardResim()` / session reset. `EpisodeTupleMatches` accepts matching tuple in `Live` phase for late seal rows.

## Verify

- Re-soak with same FC diag env: no `LIVE_HASH_GUARD` when `FRAME_COMMIT_COMPARE` agrees @120/240/360/480.
- If recovery does run: host accepts client seal rows; client receives `EPISODE_SEAL_ROWS_RECV` for slot 0; no `stale_episode_tuple` storm.

## Files

- `port/net/sys/netpeer_frame_commit.c`, `.h` — snapshot-primary guard
- `port/net/sys/netpeer.c` — pass `validation_tick` into guard
- `port/net/sys/netrollback_episode.c` — Live phase no premature reset
