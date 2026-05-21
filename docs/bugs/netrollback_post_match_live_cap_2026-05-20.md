# POST-match live sim cap freeze after analog episode (2026-05-20)

**Status:** FIX SHIPPED (soak pending)

## Symptoms

- Automatch soak @474–478: `EPISODE_SEAL_ROWS`, matching `freeze_post_inp`, `RESIM_POST_MATCH` (inp agrees).
- Immediately after, both peers freeze at **sim=478** (`rollback_epoch_hold cap=476/477`).
- Host: duplicate `RESIM_POST_MATCH` → `EPISODE_FSM phase Live -> Commit epoch=0`.
- Client: `post-resim input followup deferred mismatch=478 target=479` after successful `474→478` episode.

## Root cause

1. **Idempotent POST missing** — retransmitted peer POST digests re-ran `CompareResimPostDigests` success path (`FinishForwardResim` + `SetPhase(Commit)` from Live), corrupting FSM epoch fields.
2. **Live caps not released** — overlapping `DeferredMismatch`, `PendingPeerSymmetric`, and GGPO deferred corrections (476→478) remained in `syNetRollbackComputePeerEpochLiveCap()` after POST match.
3. **Post-resim followup** — `syNetRollbackSchedulePostResimInputFollowup()` queued a boundary correction at tick 478 before `EpisodeResolvedThrough` advanced, re-capping live sim at 477.

## Fix

1. `syNetRollbackOnResimPostMatchCommitted()` — ignore duplicate POST for same episode `(epoch,mismatch,target)`; normalize `load_tick` from active resim/FSM when peer sends `~0`; commit FSM once via `IsActive()` (not `phase == Live`, which is enum 0).
2. `syNetRollbackReleaseLiveCapsAfterResimPostMatch()` — clear overlapping deferred / pending symmetric / pending episode caps.
3. Skip `SchedulePostResimInputFollowup` when episode FSM enabled; suppress followup inside completed resim span.
4. Ignore duplicate peer-symmetric notify for the active FSM tuple during replay.
5. Broaden `MaybeClearPeerEpochAfterResimPostMatch` when POST key overlaps peer epoch span.
6. **Verify wedge (@488–490)** — `FinishForwardResim()` re-entered Verify/POST loop while POST already matched; `syNetRollbackResimPostCompletedCoversActiveResim()` now forces `Commit` → full close. `syNetRollbackTryCompleteEpisodeAfterPostMatch()` runs from `OnResimCompleted` and idempotent POST paths.
7. **Rate-limited logging** — `syNetRollbackLogResimPostMatchOnce()` emits one `RESIM_POST_MATCH` per episode; compare path no longer logs before commit. Flush/TryEmit skip when POST already committed for the episode tuple.

## Verify

Re-soak analog-stick resim @474–478 and @488–490:

- Single `RESIM_POST_MATCH` per episode (valid `load`, not `4294967295`; no hundreds of duplicate lines).
- `commit_promote` + `Verify→Commit→Live`; sim advances past episode target.
- No sustained `rollback_epoch_hold` at target; sim advances to 479+ / 491+.
- No `post-resim input followup deferred` at episode target when FSM on.
