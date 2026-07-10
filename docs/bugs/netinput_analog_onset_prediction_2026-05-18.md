# NetInput analog onset prediction (2026-05-18)

## Symptoms

- Idle → walk onset: host predicted `sx=0 sy=0` while remote confirmed small analog tilt (`sx=10 sy=-4`) then full walk vector.
- GGPO queued input correction and resim; remote fighter wrong facing/velocity for several predicted frames before rollback.
- Soak with `symmetric_diag_only=1` showed baseline echo only on guest — not a prediction bug; re-soak with default symmetric follower.

## Root cause

`syNetInputMakePredictedFrameRemoteHuman()` applied **`neutral_guard`**: when `last_confirmed` was near-neutral, force `(0,0)` for up to `min(D, phase_lock)` ticks (often 6–7), overriding hold-last and `TryGetPredictionStickSeed` (8-tick lookback). Keyboard jump had separate ±85 digital paths; analog movement onset had none.

## Fix

1. **`last_non_neutral`** on `SYNetInputSlot` — updated on strict remote confirm when sticks exceed predict deadband.
2. **Analog onset prediction** — within capped `NEUTRAL_GUARD_TICKS` (default 3) after neutral confirm, if recent non-neutral history exists, predict facing stick from source frame magnitude clamped (`ANALOG_ONSET_STICK_MAG`..`MAG_MAX`, default 28–80); if no history, peek last 4 remote confirmed rows for first partial analog instead of zero.
3. **Facing-aware significance** — neutral→non-neutral, horizontal sign flip, large per-axis delta (default 40); **same-intent analog tolerance** (default 14): predicted vs confirmed both analog with matching axis signs and Δ≤14 does not queue GGPO resim (covers raw stick without quantize).
4. **Default deadbands** — GGPO stick deadband 12 / predict 14 (was 4 / 6) for no-env gameplay.
4. **Resim seed restore** — rewind `last_non_neutral` from confirmed history when preparing resim.

Digital tap patch path unchanged (disabled during active rollback unless `PREDICTION_RECOVERY=1`).

## Env knobs

| Variable | Default | Role |
|----------|---------|------|
| `SSB64_NETPLAY_NEUTRAL_GUARD_TICKS` | 3 | Max ticks after neutral confirm to apply onset/zero policy (0–3) |
| `SSB64_NETPLAY_ANALOG_ONSET_STICK_MAG` | 28 | Onset floor magnitude per axis (8–80) |
| `SSB64_NETPLAY_GGPO_STICK_DEADBAND` | 12 | Per-axis insignificant delta (bisect) |
| `SSB64_NETPLAY_GGPO_STICK_DEADBAND_PREDICT` | 14 | Neutral test + predict compare slack |
| `SSB64_NETPLAY_ANALOG_ONSET_LOOKBACK` | 60 | Max age of `last_non_neutral` for onset (8–120 sim ticks) |
| `SSB64_NETPLAY_ANALOG_ONSET_FACING_THRESH` | 4 | Min \|sx\| for horizontal sign-flip significance |
| `SSB64_NETPLAY_ANALOG_ONSET_LARGE_DELTA` | 35 | Per-axis delta always significant |
| `SSB64_NETPLAY_ANALOG_ONSET_LOG` | off | Log first few `analog_onset_predict` lines |

## Related

- [`netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md) — analog onset + symmetric soak cases
- [`netrollback_episode_anchor_blocks_late_resim_2026-05-18.md`](netrollback_episode_anchor_blocks_late_resim_2026-05-18.md)
