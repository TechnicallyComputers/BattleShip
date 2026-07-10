# Symmetric reject live cap stuck after resim (2026-05-20)

**Status:** FIX SHIPPED (soak pending)

## Symptoms

- Automatch soak @501–504: GGPO symmetric episode succeeds (`EPISODE_EXEC` 500/501/504, `RESIM_POST_MATCH` on both sides).
- Immediately after, both peers freeze at **sim=504** for 800+ render frames; `hr` stuck at 516.
- Client logs `rollback_epoch_hold sim=504 cap=500 source=none` (misleading label — cap source bit 16).
- Host logs `rollback_epoch_hold sim=504 cap=501 source=peer_target` until session end.

## Root cause

`syNetRollbackArmPeerSymmetricRejectLiveCap(mismatch)` runs when a peer-symmetric notify is queued/deferred (hold live sim below `mismatch-1` until follower load — pre-load figatree freeze class).

**`syNetRollbackClearPeerSymmetricRejectLiveCap()` was never called on successful resim completion.** After follower resim finished at target 504, client cap remained **500** (`501-1`), so `syNetRollbackShouldBlockLiveBattleAdvance(504)` permanently blocked live advance (`504 > 500`).

Clear paths only existed for deferred-input queue/clear — not `syNetRollbackFinishForwardResim()`.

## Fix

1. Clear `PeerSymmetricRejectLiveCap` in `syNetRollbackFinishForwardResim()` after episode completes.
2. Label cap-source bits 16/32 in `rollback_epoch_hold` log as `sym_reject_cap` / `sym_notify_cap` (were shown as `none`).

## Verify

Re-soak automatch past GGPO @~501:

- `RESIM_POST_MATCH` followed by sim advancing past target (505+).
- No sustained `rollback_epoch_hold` with `cap=mismatch-1` after resim complete.
- Log shows `sym_reject_cap` only **during** pending notify, not after `resim complete`.
