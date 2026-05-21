# Symmetric local-authority frontier boundary (2026-05-19)

## Symptoms

- First client stick @529: host completes GGPO resim (`local_initiator`); client logs `peer symmetric local authority queued` with `follower_local_auth=1` but never `symmetric local authority resim` / `resim begin`.
- Client sim overshoots to 533; both peers enter sustained `rollback_epoch_hold` (`peer_target=531`, host `sim=531`, client `sim=533 cap=532`).

## Root cause

`syNetRollbackQueueDeferredInputCorrectionEx` rejected all corrections with `sim_tick >= frontier`. Symmetric notify often arrives with `mismatch_tick == frontier` (next tick to simulate). Queue returned early → no `DeferredMismatchPending` → `TryBeginDeferredMismatch` no-op → live sim ran past the correction span.

The `follower_local_auth` flag and slot routing were correct; this is a fencepost bug in the deferred queue, not the notify wire format.

## Fix

1. **Relaxed guard** — when `DeferredMismatchFromPeerSymmetric`, allow `sim_tick == frontier`; still reject `sim_tick > frontier`.
2. **Reject live cap** — if peer-symmetric queue still fails, arm `PeerSymmetricRejectLiveCap` at `mismatch_tick - 1` until retried.
3. **Defer diag** — `symmetric_queue_at_frontier`, `symmetric_queue_ok`, `symmetric_queue_failed` when `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG=1`.

## Verify

Automatch first-stick @529: client should log `symmetric local authority resim` and complete paired resim; no permanent `rollback_epoch_hold`. Optional: `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG=1` shows `symmetric_queue_at_frontier` then `symmetric_queue_ok`.
