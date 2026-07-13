# Stick REPLACE policy consolidation (feel-0 / episode absorb)

**Date:** 2026-07-12  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Why

Soaks kept hitting the same feel-0 machine from different angles (jump debounce, Pass onset, mid-air release, L/R Turn storm, seal stall). Each got a surgical gate. This consolidates the shared policy so the next stick motion does not invent another IFDEF.

Related: `netplay_jump_feel0_debounce_blocks_ggpo`, `netplay_feel0_release_deadband_skips_ggpo`, `netplay_ggpo_behind_resolved_through_seal_stall`, `netplay_stick_storm_cooldown_livecap_deadlock`, `netplay_feel0_provisional_remote_phase_lag`, `netplay_baseline_universe_mismatch_ignored`.

## Policy (three layers)

| Layer | Helper | Rule |
|-------|--------|------|
| **A. Wire → history** | `syNetInputStickReplaceNeedsRewind` | Completed sim (`GetTick() > sim_tick`): any gameplay delta → rewind. Release (analog → nearer/at neutral): always rewind. Else: confirmed-deadband significance unless true onset-ahead defer. See `netplay_stick_ramp_predict_deadband_silent_promote`. |
| **B. Episode scheduling** | `syNetRollbackQueueOrWidenStickCorrection` | Resim pending / deferred pending → widen/merge only. Else → `RequestInputCorrection`. |
| **C. Live-cap / pump** | (already landed) | Deferred cooldown bypass; `lift_livecap` / abandon on stuck `commit_begin_failed`. |

## Code

- **`netinput.c` / `.h`:** `syNetInputStickReplaceNeedsRewind`; CommitRemote feel0 / late_wire / pre_promote / ggpo_queued paths call it + `QueueOrWiden` (mark-predicted helper shared).
- **`netrollback.c` / `.h`:** `syNetRollbackQueueOrWidenStickCorrection`; `DeferRemoteInputCorrection` absorbs open deferred (no active resim); `RequestInputCorrection` significance uses `StickReplaceNeedsRewind`.

## Verify

Rebuild netmenu; re-soak stick-heavy seeds (jump ramp/release, Pass onset, L/R Turn storm). Expect GGPO on completed-sim REPLACE, target widen during open episodes (not ep spam), and no permanent `epoch_hold` / `commit_begin_failed` after storm.
