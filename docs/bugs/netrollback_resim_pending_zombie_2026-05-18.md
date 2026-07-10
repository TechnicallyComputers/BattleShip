# NetRollback: zombie `ResimPending` after forward resim complete

**Date:** 2026-05-18  
**Symptom:** Both clients freeze after a successful jump rollback (~tick 699). Logs spam `resim_rng_verify`, `rollback_post tick=0`, and `ROLLBACK_SYNC` 701→702 every frame; no further `phase_lock_commit`.

## Root cause

`syNetRollbackAdvanceResimBudget()` called `syNetRollbackEpisodeReset()` on completion but never cleared legacy `sSYNetRollbackResimPending`. `syNetRollbackIsResimulating()` stayed true with `ResimNextTick >= ResimTargetTick`, so completion re-ran every frame with `episode.mismatch_tick == 0` (`rollback_post tick=0`). `syNetRollbackUpdate()` never reached deferred/symmetric begin paths.

Nested GGPO during the first resim armed `ROLLBACK_SYNC` 701→702 via `syNetRollbackArmSymmetricNotify()` while still pending, compounding wire spam.

## Fix

- `syNetRollbackFinishForwardResim()`: set episode Live, clear `ResimPending`/gate/baseline send, invalidate `ResimNextTick`, clear symmetric notify slots.
- `syNetRollbackArmSymmetricNotify()`: no-op while `ResimPending` (defer wire notify until live; corrections still queue via `QueueDeferredInputCorrection`).

## Verify

Soak with `SSB64_NETPLAY_RESIM_RNG_VERIFY=1`: one `resim complete` per episode, sim advances past 701, no `rollback_post tick=0` loop.
