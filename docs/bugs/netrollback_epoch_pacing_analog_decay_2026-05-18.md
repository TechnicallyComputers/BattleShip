# Rollback epoch pacing and analog prediction decay (2026-05-18)

**Status:** FIX SHIPPED (soak pending; seal-frontier hotfix 2026-05-18)

## Hotfix (fast desync @ ~tick 405)

Phase 3 initially required `EpisodeResolvedThrough != 0`, which blocked **every** passive baseline echo before the first completed episode (`snapshot_not_ready` spam → `RESIM_BASELINE_TIMEOUT` → hard desync). Also used `load_tick >= ResimLoadTick`, which blocked compares at the resim **load anchor** needed to open the baseline gate. Fixed: seal only when `resolved_through != 0 && load_tick > resolved_through`; resim span excludes anchor (`load_tick > load`).

## Symptoms

Mixed keyboard/analog soak (~tick 403–425): rollback churn, not hard desync. Host resimmed 411→421 while the follower kept live-simming 412→422. Mirrored `RESIM_BASELINE_MISMATCH` at `load_tick=410` with matching `figh` partitions — two speculative branches, not corruption. GGPO logs oscillated `pred 0,0 | remote 44,74` then `pred 44,74 | remote 0,0` at tick 411.

## Root cause

1. **Asymmetric speculative progression:** `syNetPeerEvaluateSharedCommitStep` returned early when `syNetRollbackIsResimulating()` (includes `resim_pending`), leaving `advance=TRUE` and skipping pacing caps while the peer replayed via `AdvanceResimBudget`.
2. **Binary remote stick policy:** hold-last analog forever after neutral confirm — stale analog resurrection on the next predicted frame.
3. **Baseline compare before seal:** snapshot ring slot existed at `load_tick` but the tick was not globally stable (peer epoch / resim span still open).

## Fix

### Phase 1 — epoch live-sim cap

- `syNetRollbackGetLiveSimCap`: `min(local_resim_target, peer_symmetric_target + slack)`; slack from `SSB64_NETPLAY_ROLLBACK_EPOCH_CAP_SLACK` (default 1, 99 = disable).
- `syNetPeerEvaluateSharedCommitStep`: hold `B` + pump ingress when `sim > cap` (no early return on resim).
- `syNetRollbackShouldBlockLiveBattleAdvance`: blocks live battle when `sim > cap` during peer epoch.
- Epoch logging: `rollback_epoch_hold epoch=…`, `resim begin/complete epoch=…`.

### Phase 2 — analog prediction decay

- Fixed-point decay (`3/4` per tick past window); env `SSB64_NETPLAY_ANALOG_PRED_DECAY_TICKS` (default 3, 0 = off), `SSB64_NETPLAY_ANALOG_PRED_MIN_MAG` (default 12).
- `last_confirmed` neutral → never resurrect stale analog (except analog-onset / quasi-digital paths).
- `SSB64_NETPLAY_STICK_MISMATCH_RECOVERY=1` (default): arms short confirmed-only window on neutral↔analog GGPO correction (not full `PREDICTION_RECOVERY`).

### Phase 3 — seal frontier

- `syNetRollbackSnapshotReadyForBaselineCompare`: requires `load_tick <= EpisodeResolvedThrough`, ring committed, and tick not inside active local/peer resim span.

## Bisect env

| Variable | Default | Effect |
|----------|---------|--------|
| `SSB64_NETPLAY_ROLLBACK_EPOCH_CAP_SLACK` | 1 | Epoch cap slack; 99 disables |
| `SSB64_NETPLAY_ANALOG_PRED_DECAY_TICKS` | 3 | Decay window; 0 off |
| `SSB64_NETPLAY_STICK_MISMATCH_RECOVERY` | 1 | Confirmed-only window on stick flip; 0 off |
| `SSB64_NETPLAY_PREDICTION_RECOVERY` | 0 | Unchanged (debug only) |

## Soak pass criteria

- Paired `resim begin` / `resim complete` with same mismatch/target.
- No `sim_state_tick` `figh` split while peer epoch active and `sim > cap`.
- `rollback_epoch_hold` logs with `epoch_id` and cap source.
- No mirrored `RESIM_BASELINE_MISMATCH` at the same `load_tick`.
- 5+ min, no `PEER_SNAPSHOT_DIVERGE`.
