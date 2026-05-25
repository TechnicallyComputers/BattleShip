# Episode seal-rows gate: span>64 completion + recovery hardening (2026-05-23)

**Date:** 2026-05-23  
**Status:** FIX SHIPPED (soak pending)

## Symptom

WAN 1v1 soak (pair 1): FC state diverge @720 with agreed `inp`; `INPUT_AGREE_REANCHOR` load 600, span 120; baseline digest matched; sustained `EPISODE_SEAL_ROWS_WAIT missing_slots=0x2` / `0x1`; `RESIM_SEAL_ROWS_TIMEOUT` after 10 frames; no `resim replay gate open`; guest `VS_SESSION_END` @722.

## Root cause

1. **`syNetRollbackEpisodePeerSealTickMaskComplete`** returned **FALSE** for any `span > 64` (early guard). FC recovery spans of **120** ticks never satisfied `syNetRollbackEpisodeAllPeerSealRowsComplete()`, so replay never opened even after all seal chunks were received.
2. **Fixed 10-frame** baseline gate timeout was too short for multi-chunk seal exchange on WAN.
3. **`BASELINE_UNIVERSE_STORM_CAP`** suppressed the baseline+seal pump bundle while seal rows were still missing after baseline match.
4. First seal timeout **cleared `resimPending`** and armed weak `peer baseline resync` with `mismatch_tick == load_tick` (off-by-one vs `load+1`).

## Fix

| Change | Location |
|--------|----------|
| Split `peer_seal_tick_mask` into lo/hi `u64` per slot; track offsets 0–127; remove `span > 64` reject | `netrollback_episode.c` |
| Scale gate timeout: `10 + 4 * ceil(span/24)` capped at 96; env floor `RESIM_BASELINE_GATE_TIMEOUT_MIN` | `netrollback.c` |
| Baseline storm cap: still pump **seal rows only** when baseline matched + seal incomplete | `syNetRollbackPumpResimBaselineSend` |
| Seal timeout: up to 2 retries (reseal send cursors + pump) before abandoning episode | `syNetRollbackOnBaselineGateTimeout` |
| `EPISODE_SEAL_ROWS_WAIT_DETAIL` log (first invalid idx, mask bit counts) | `syNetRollbackEpisodeLogSealRowsWaitDetail` |
| Peer baseline resync: `mismatch_tick = load_tick + 1` | `syNetRollbackArmPeerBaselineResync` |

## Verify

Re-soak WAN pair with `FRAME_COMMIT_DIAG=2`, episode FSM on:

1. On FC recovery span 120: expect `resim replay gate open` (not sustained `SEAL_ROWS_WAIT` through timeout).
2. No `RESIM_SEAL_ROWS_TIMEOUT` unless true packet loss; retries log `RESIM_SEAL_ROWS_TIMEOUT retry`.
3. If stall persists, `EPISODE_SEAL_ROWS_WAIT_DETAIL` shows `first_invalid_idx` / mask counts.

## Related

- [`netrollback_episode_input_seal_2026-05-20.md`](netrollback_episode_input_seal_2026-05-20.md)
- [`netplay_episode_seal_target_mismatch_2026-05-24.md`](netplay_episode_seal_target_mismatch_2026-05-24.md)
- [`netrollback_fc_input_agree_reanchor_2026-05-21.md`](netrollback_fc_input_agree_reanchor_2026-05-21.md)
