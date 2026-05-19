# NetRollback: episode anchor + patch-only path blocked late symmetric resim

**Date:** 2026-05-18  
**Symptom:** After first resim cluster (~400–405), jump-like GGPO @ 515+ logged `GGPO input correction queued` and `ROLLBACK_SYNC` but no `resim begin`. `prediction recovery armed` on neutral→stick while live sim continued.

## Root causes

1. **Episode anchor not cleared** after `FinishForwardResim` — `EpisodeExtensions` / anchor load tick could block `TryCommitCorrectionBegin` for later mismatches on the same snapshot load.
2. **Prediction-recovery + live patch** — `syNetInputCommitRemoteConfirmedWire` patched published rows and armed recovery instead of deferring sim correction until full resim.

## Fix

- `syNetRollbackCloseCorrectionEpisode()` from `FinishForwardResim` (preserve `EpisodeResolvedThrough`, clear anchor/extensions).
- Ignore peer symmetric notices with `mismatch_tick < EpisodeResolvedThrough`.
- Disable prediction recovery by default (`SSB64_NETPLAY_PREDICTION_RECOVERY=1` debug only).
- Significant predicted-remote mismatch: queue GGPO correction only; no live `PatchPublishedFromRemoteConfirmed`.
- Disable digital tap patch-without-rollback during active rollback.
- Conservative auto-negotiation tiers (higher D, lower prediction cap).

## Verify

Soak: jump @ ~500 → `GGPO deferred input correction resim` + `resim complete` on both peers; **zero** `prediction recovery armed`.
