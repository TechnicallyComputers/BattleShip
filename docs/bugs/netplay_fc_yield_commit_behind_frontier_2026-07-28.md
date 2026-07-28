# Netplay: FC yield clears arm → peer join `commit_behind_frontier`

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `2824158137` (post FC join keep-onset)  
**Related:** [fc join keep onset](netplay_fc_join_keep_onset_resolved_clamp_2026-07-28.md), [fc commit behind frontier deepen](netplay_fc_commit_behind_frontier_deepen_2026-07-26.md)

## Symptom

Keep-onset worked (`PEER_SYMMETRIC_FC_JOIN_KEEP_ONSET flags=0x02`, no clamp), then Android never began the FC follower:

```text
EPISODE_YIELD unstarted FC recovery … mismatch=1452 target=1464
try_begin_fail stage=commit_behind_frontier mismatch=1452 resolved_through=1457
PEER_SYMMETRIC_NOTIFY_REJECT reason=dup_pending …
(only EARLY_STASH seals; recovery_started=0)
[Linux] SELF_SEAL_FALLBACK → unilateral FC complete → later VS_SESSION_END
```

## Root cause

1. `EPISODE_YIELD` cleared `FcStateRecoveryActive` (and deferred FC) so local TryBegin would not dual-init.
2. Peer-symmetric TryCommit uses `allow_frontier_deepen` only when FC arm / deferred FC is live.
3. After yield, join at onset `1452 < resolved_through=1457` looked like ordinary GGPO → `commit_behind_frontier` → pending stuck forever.

## Fix

`PORT && SSB64_NETMENU`:

1. **Yield** — clear deferred local FC TryBegin only; **keep** `FcStateRecoveryActive` (`keep_fc_arm=1`) so deepen still applies.
2. **Pending/deferred notify flags** — carry `FC_RECOVERY` through queue/defer/flush.
3. **`allow_frontier_deepen`** — also TRUE when pending (or in-flight peer-sym) notify has `FC_RECOVERY`.
4. **PendingEpisodeSet** — pass full `notify_flags` (no longer drop FC bit).

## Verification

Re-soak Dream Land Ness ditto through FC escalate past a light frontier:

- `EPISODE_YIELD … keep_fc_arm=1` then follower `EPISODE_FSM begin` at peer onset
- No `commit_behind_frontier` / `dup_pending` storm for FC_RECOVERY joins
- Bilateral seal exchange (not only initiator `SELF_SEAL_FALLBACK`)
