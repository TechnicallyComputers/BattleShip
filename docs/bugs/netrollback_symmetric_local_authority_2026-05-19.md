# Symmetric rollback local-authority follower (2026-05-19)

## Symptoms

- After host GGPO input correction on remote Kirby (p1), client symmetric resim from load@866 produced **Wait** at `rollback_post`@867 while host produced hit-exchange.
- Forward client tick 867 had `motion_attack_id=1`; resim did not replay attack.
- `hist_win` p1 digest matched at validation; `hist_diag` p1 differed — published stream ≠ transmitted gameplay.

## Root cause

Unified resim (`netrollback_ggpo_unified_resim_2026-05-18`) collapsed symmetric follower into blind `mismatch_from_peer_symmetric` resim:

- Follower used `ReconcileResimSpan` only (local slot = transmitted **or** non-predicted published fallback).
- No local published-vs-transmitted scan; no transmitted-only overlay (`ReconcilePeerSymmetricAuthority` became a no-op wrapper).
- Initiator `target_tick` was clamped to follower frontier when **storing** the notify, shrinking resim span (870 vs 869).

When the notified slot is **local** on the follower (host corrected its prediction of client inputs), the follower must run a **local-authority** episode (published vs transmitted), not blind symmetric resim.

## Fix

1. **`syNetInputFindEarliestLocalAuthorityMismatch`** — scan `[from,to)` for published ≠ transmitted on local sim slot.
2. **Restore `syNetInputRollbackReconcilePeerSymmetricAuthority`** — transmitted-only stamp into published history (no published fallback).
3. **`syNetRollbackBeginResim`** — after unified reconcile, call transmitted-only overlay when `correction_player` is local authority.
4. **Symmetric notify with local slot** — queue `syNetRollbackQueueDeferredInputCorrectionEx` (local GGPO path) instead of blind follower resim; set `from_peer_notify` on episode.
5. **Target alignment** — do not clamp initiator `target_tick` to frontier in `OnPeerSymmetricRollbackNotify`; wire-lock deferred target override in queue when symmetric enabled.

## Follow-up (same day)

Dispatch priority vs frame-commit recovery: see [netrollback_symmetric_fc_dispatch_2026-05-19.md](netrollback_symmetric_fc_dispatch_2026-05-19.md).

## Verify

Soak Mario vs Kirby: on host GGPO @867 for p1, client log should show `symmetric local authority queued` and `symmetric local authority resim`; `rollback_post`@867 combat state should match host. Optional: `SSB64_NETPLAY_RESIM_RECONCILE_LOG=1` around tick 867.
