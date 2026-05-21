# Symmetric follower epoch-hold deadlock (2026-05-19)

## Symptoms

- Long soak @671: host `GGPO deferred input correction resim` on player=1; client `peer symmetric local authority queued` but never `symmetric local authority resim`.
- Client `rollback_epoch_hold owner=peer_epoch sim=675 cap=674` while host completes resim; client frozen, session ends on stall.

## Root cause

1. **`correction_not_allowed`** — `syNetRollbackCorrectionAllowedAtTick` required `LastCommittedMismatchTick` on the follower; peer-symmetric local-authority deferred work had no initiator commit on that peer, so `TryBeginDeferredMismatch` never started.
2. **Live sim overshoot** — peer epoch cap (`target + slack`) allowed live sim to advance past the mismatch tick before symmetric resim ran, widening the fork.

## Fix

1. **Correction bypass** — allow correction for ticks in `[DeferredMismatchTick, DeferredMismatchTargetTick]` while `DeferredMismatchFromPeerSymmetric` is pending.
2. **Live cap** — `syNetRollbackGetLiveSimCap` adds `symmetric_deferred` cap at `mismatch_tick - 1` until symmetric resim starts.
3. **Peer epoch target** — include deferred symmetric `target_tick` in `syNetRollbackComputePeerEpochLiveCap` (aligns with notify target).

## Verify

Host GGPO on client slot @671: client log must show `symmetric local authority resim` and matching `rollback_post` combat state vs host; no sustained `rollback_epoch_hold` at 675 without resim.
