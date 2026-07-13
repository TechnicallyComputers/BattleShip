# Stick ramp REPLACE: predict deadband + release defer → silent Promote

**Date:** 2026-07-12  
**Session:** soak `2132381039` seed `3637334367` (Android client ↔ Linux host)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Baseline stash fix held (ep1–4 match + `BASELINE_STASH_COMPARE`). Then ~24× `REMOTE_CONFIRMED_REPLACE_NEWER` with **0× GGPO** after ep4 (~408–427). FC@480: inputs + figh diverge; p1 `topn_tx` phase lag.

## Root cause

Continuous stick ramp/release on the feel-0 runway Promotes without rewind because:

1. Ticks are **not** completed-sim (`GetTick() ≤ sim_tick`) → completed-sim any-delta bypass does not apply.
2. Predict deadband **14** treats ramp Δ≈5–12 as insignificant → `StickReplaceNeedsRewind` false.
3. Analog-onset **defer** still fires on release (published analog + wire nearer neutral) when the tick is not yet completed-sim.
4. Open-episode absorb window (~4 ticks) is shorter than a ~20-tick ramp, so coalescing alone cannot cover the storm.

Related: `netplay_feel0_release_deadband_skips_ggpo` (completed-sim only), `netplay_stick_replace_policy_consolidate`, `netplay_stick_lr_baseline_stash_hang`.

## Fix

**`netinput.c`:**

| Change | Rule |
|--------|------|
| `syNetInputStickReplaceIsRelease` | Old not near-neutral (confirmed db) + wire nearer/at neutral or clearly shedding magnitude → release. |
| `ShouldDeferPredictedAnalogCorrection` | Release never deferred (onset-ahead only). |
| `StickReplaceNeedsRewind` | Release → always rewind; runway significance uses **confirmed** deadband (`SignificantEx(..., FALSE)`), not predict-14. |

Open resim/deferred still absorb via `QueueOrWidenStickCorrection`.

## Verify

- Rebuild netmenu; re-soak stick-heavy ramp + release (seed class of `3637334367`).
- Expect GGPO (or widen into open episode) on ramp/release REPLACE, not 24× silent Promote.
- Matching TopN / no FC inputs+figh fork from this path.
