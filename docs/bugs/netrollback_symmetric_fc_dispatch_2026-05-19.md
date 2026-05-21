# Symmetric local-authority vs frame-commit dispatch (2026-05-19)

## Symptoms

- Soak @719: client runs `GGPO deferred input correction resim`; host logs `peer symmetric local authority queued` but never `symmetric local authority resim`.
- Next tick: `FRAME_COMMIT_TOKEN_MISMATCH` @720 (`delta_input_digest=1`); host `deferred frame-commit state resim` preempts symmetric work.
- Live `figh` already split @719; frame-commit chases input-binding divergence caused by missed symmetric reconcile.

## Root cause

`syNetRollbackUpdate` ran `TryBeginDeferredStateMismatch` before `TryBeginDeferredMismatch`. Frame-commit recovery won the dispatch race against peer-symmetric local-authority deferred input correction.

Secondary: `DeferredMismatchFromPeerSymmetric` was cleared before `TryCommitCorrectionBegin`, so a failed start attempt dropped the symmetric flag while `DeferredMismatchPending` remained — unsafe for frame-commit suppression keyed on the flag alone.

## Fix

1. **Dispatch reorder** — `TryBeginDeferredMismatch` before `TryBeginDeferredStateMismatch`.
2. **Frame-commit defer** — `syNetRollbackDeferFrameCommitForSymmetric` holds deferred frame-commit while symmetric deferred work or symmetric resim overlaps the validation window (±2 ticks).
3. **Flag lifecycle** — clear `FromPeerSymmetric` only after commit succeeds; on non-retriable `commit_begin_failed`, clear pending + flag (`symmetric deferred abandon`); on `begin_resim_failed`, re-queue deferred symmetric work.

## Follow-up

Epoch-hold / `correction_not_allowed` follower deadlock @671: [netrollback_symmetric_epoch_hold_2026-05-19.md](netrollback_symmetric_epoch_hold_2026-05-19.md).

## Verify

Re-soak Mario vs Kirby with `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG=1`. On host GGPO-notify @719 for local slot 0: expect `symmetric local authority resim` before any `deferred frame-commit state resim` @720; matching `rollback_post` on both peers.
