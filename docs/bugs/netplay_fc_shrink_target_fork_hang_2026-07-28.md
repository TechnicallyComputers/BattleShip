# Netplay: FC seal shrink leaves deepen target → dup_pending / invent hang

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `2437168353` (Dream Land, Android guest / Linux host)  
**Related:** [seal epoch skew mismatch fork](netplay_seal_epoch_skew_mismatch_fork_2026-07-28.md), [fc join keep onset](netplay_fc_join_keep_onset_resolved_clamp_2026-07-28.md), [fc yield commit behind frontier](netplay_fc_yield_commit_behind_frontier_2026-07-28.md)

## Symptom

After FC recovery around onset `2278`:

```text
[Linux]  EPISODE_SEAL_ROWS_SHRINK_TO_PEER_PREFIX mismatch=2278 target=2296->2288
[Linux]  Commit -> Live epoch=108 mismatch=2278 target=2288
[Linux]  PEER_SYMMETRIC_FC_JOIN_KEEP_ONSET … target=2296 resolved_through=2288
[Linux]  PEER_SYMMETRIC_NOTIFY_REJECT reason=dup_pending … pending=2278
[Linux]  try_begin_fail stage=commit_suppress_reload target=2296
[Android] invent / wire_need spin frontier_sim=2294 next_sim=2297 → VS stop
```

Wall clock advances; not a watchdog deadlock. Peers agree on onset/`inputs_agree` but fork on recovery end (`2288` vs abandoned `2296`).

## Root cause

1. Initiator armed FC / `ROLLBACK_SYNC` at deepen target `2296`.
2. Compatible seal apply of peer's shorter prefix correctly shrunk the **FSM** target `2296→2288` and committed Live there.
3. `FcStateRecoveryTargetTick`, outbound `SymmetricNotifyTarget`, and pending peer-symmetric targets stayed at `2296`.
4. `OnResimCompleted` only clears the FC arm when `ResimTarget >= FcStateRecoveryTarget` — `2288 >= 2296` failed, so the arm (and FC_RECOVERY SYNC) kept advertising the abandoned deepen.
5. Peer keep-onset-joined `2296` again; initiator queued `dup_pending` and could not `TryBegin` (`commit_suppress_reload`) → invent / `wire_need` hang.

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetRollbackOnEpisodeTargetShrunkToPeerPrefix`** — on `SHRINK_TO_PEER_PREFIX`, sync FC arm, deferred FC, resim/episode targets, pending/deferred peer-symmetric, peer-epoch, outbound `ROLLBACK_SYNC` notify targets, and pending-episode slots down to the peer prefix.
2. **`CloseCorrectionEpisode`** — clear pending/deferred peer-symmetric (and clamp outbound notify) when the same onset still holds a deepen past the completed target.
3. **`FcJoinKeepOnset`** — reject wire-only FC deepen for an onset already closed (`LastCommittedMismatch` + no local FC arm/deferred). Legitimate escalate re-arms local FC before notify.

## Verification

Re-soak Dream Land Ness ditto through FC recoveries that hit `SHRINK_TO_PEER_PREFIX`:

- Prefer `EPISODE_SHRINK_SYNC_TARGETS` / `EPISODE_SHRINK_SYNC fc_arm` when FSM shrinks
- After Commit→Live at the shrunk target: no `dup_pending` storm on the abandoned deepen; no invent/`wire_need` freeze at the deepen tick
- Escalate deepen with a live local FC arm still joins via keep-onset
