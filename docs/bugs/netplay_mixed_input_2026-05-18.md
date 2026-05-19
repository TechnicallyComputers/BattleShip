# Netplay mixed keyboard / analog input

**Date:** 2026-05-18  
**Status:** FIX SHIPPED — quantize default off + prediction/baseline opts (2026-05-18); soak verification pending

## Product requirement

Opponents may use different input devices (gamepad vs keyboard) and may switch devices mid-match. The wire format is unified (`stick_x` / `stick_y` / `buttons`); peers never see HID type.

## Symptoms (pre-fix soak)

- Long pad-vs-pad soak stable; failure when client switched pad → keyboard mid-match.
- Host GGPO: `pred=1` neutral vs remote `sx=-23 sy=79` (partial diagonal keyboard, not ±85).
- `PEER_SNAPSHOT_DIVERGE` at `load_tick` with `world`/`rng`/`item` match but `figh`/`anim`/`map` mismatch.
- Client `resim complete` showed baseline/post hash drift; asymmetric rollback counts (client `R=35`, host `R=1`).

## Root cause

Two independent gaps:

1. **Prediction** — Remote predictor only treated exact ±85 as digital. Partial keyboard encodings hit analog neutral-guard (`sx=0 sy=0`) instead of hold-last cardinals. No handling for encoding switches on the wire.

2. **Rollback convergence** — Snapshot ring slots for replayed ticks were not refreshed during forward resim (`syNetRollbackAfterBattleUpdate` skips save while `resim_pending`). Passive `RESIM_BASELINE_ECHO` at `load_tick` could advertise **stale** pre-resim slot digests while live sim had converged — cross-peer `PEER_SNAPSHOT_DIVERGE` on fighter partitions.

## Fix

### `port/net/sys/netinput.c`

- **Quasi-digital detection** — `syNetInputStickEncodingLooksDigital()` (±85, dominant cardinal, diagonal partials).
- **Wire quantize (local)** — `syNetInputQuantizeStickToDigitalCardinals()` on local/delay frames (**default off**; `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE=1` to enable).
- **Remote prediction** — Dominant-axis snap only (`syNetInputSnapStickDominantAxisForPrediction`); hold-last under neutral guard when recent confirmed history is digital or encoding grace active.
- **Encoding switch** — On remote confirm encoding flip: clear `last_non_neutral`, 3-tick grace; log with `SSB64_NETPLAY_MIXED_INPUT_LOG=1`.

### `port/net/sys/netrollback.c`

- **Resim snapshot refresh** — After each replayed tick in `syNetRollbackAdvanceResimBudget`, `syNetRbSnapshotSave(t)` (default on; `SSB64_NETPLAY_RESIM_SNAPSHOT_REFRESH=0` to disable).

## Soak

Source `scripts/netplay-mixed-input-soak.env.example` on both peers. Cases:

| Case | Pass |
|------|------|
| Pad vs keyboard from GO | 5+ min, no `PEER_SNAPSHOT_DIVERGE` |
| Mid-match device switch (once) | Session recovers |
| Paired `resim complete` | Same `load_tick` / `target_tick` on both peers |

## Env knobs

| Variable | Default | Role |
|----------|---------|------|
| `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE` | 0 | Snap local keyboard-like sticks to ±85 on wire (opt-in) |
| `SSB64_NETPLAY_MIXED_INPUT_LOG` | off | Log encoding switches (budget 8) |
| `SSB64_NETPLAY_RESIM_SNAPSHOT_REFRESH` | 1 | Refresh snapshot ring each resim replay tick |

## Related

- [`netplay_mixed_input_quantize_off_2026-05-18.md`](netplay_mixed_input_quantize_off_2026-05-18.md)
- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md)
- [`netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md)
