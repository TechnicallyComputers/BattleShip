# Symmetric epoch initiator peer_target freeze (2026-05-19)

## Symptoms

- Automatch GGPO stick @458: host `ROLLBACK_SYNC_SEND mismatch=458 target=460`, completes resim, then `rollback_epoch_hold peer_target=0 cap=458` while `sim=460` repeats.
- Client `ROLLBACK_SYNC_RECV`, `peer symmetric local authority queued`, live sim advances to 462 without `resim begin`; `rollback_epoch_hold peer_target=460`.

## Root cause

1. **`PeerEpochTargetTick` recv-only** — `NotePeerEpochTarget` ran on `ROLLBACK_SYNC_RECV` but not when the initiator armed outbound `ROLLBACK_SYNC`, so the host never entered `peer_epoch` hold with a non-zero target.
2. **Premature clear on initiator resim complete** — `CloseCorrectionEpisode` and `FinishForwardResim` unconditionally cleared peer epoch when local `completed_target >= PeerEpochTargetTick`, before the remote peer finished its symmetric episode.
3. **Follower `correction_not_allowed`** — blind symmetric follower path did not bypass debounce when peer epoch / pending symmetric notify was active, so `TryBeginDeferredMismatch` could stall after queueing local-authority work.

## Fix

1. **`NotePeerEpochTarget` on SEND** — `ArmSymmetricNotify` records the same `(mismatch, target)` and sets `PeerEpochAwaitingPeerResimPost`.
2. **Defer clear until `RESIM_POST_MATCH`** — initiator retains peer epoch across local resim complete; release when peer POST key matches `PeerEpochMismatchTick` / `PeerEpochTargetTick`.
3. **Episode initiator flag** — `EpisodeBegin` uses `initiator = !from_peer_notify` (was always `TRUE`).
4. **Correction bypass** — `CorrectionAllowedAtTick` allows ticks in pending peer-symmetric or active peer-epoch spans.

## Verify

Host GGPO on remote slot @458–460: host log shows `peer_target=460` during hold after local resim; client runs `symmetric local authority resim` or blind `resim begin`; both sides advance past 460 after `RESIM_POST_MATCH`; no sustained freeze with rising `hr` and repeating `sim`.
