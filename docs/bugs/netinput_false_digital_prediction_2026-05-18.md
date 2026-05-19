# NetInput false digital prediction (2026-05-18)

## Symptoms

- First analog onset rollback felt acceptable (`pred 0,0` vs `remote 2,18`, short resim).
- Jump shortly after: host `pred sy=85 | remote sy=0`, 11-tick resim, character rewound to standing, inputs felt ignored during replay.

## Root cause

With `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE` off, raw analog still flowed on the wire, but remote prediction used **quasi-digital** classification (`ay >= 20 && ax <= 14`) and `syNetInputSnapStickDominantAxisForPrediction()` to promote partial Y (e.g. `0,55`) to **`sy=85`**. That is a full Smash jump input, not a small analog error — GGPO treated it as a large intentional divergence and replayed an entire fabricated jump window.

`syNetInputStickLooksAnalog()` also returned FALSE for quasi-digital partials, so same-intent tolerance did not apply to `85` vs `0`.

## Fix

1. **No ±85 promotion on remote prediction** — dominant-axis snap is a no-op; partial sticks keep wire magnitude.
2. **Confirmed-digital encoding only** — `remote_encoding_was_digital`, recent-encoding lookback, and neutral-guard hold-last use `syNetInputFrameIsDigitalKeyboard()` (±85 on wire), not quasi-digital heuristics.
3. **Analog classification** — `syNetInputStickLooksAnalog()` is TRUE for any non-neutral stick that is not already ±85 (partial analog is analog for GGPO tolerance).
4. **False-digital safety** — `syNetInputGgpoFalseDigitalHeuristicMismatch()` suppresses predicted GGPO correction when published ±85 on one axis vs remote neutral/partial on that axis (belt-and-suspenders).

## Soak

- Idle → walk: still ≤1–2 tick resim with small deltas.
- Jump with analog: no `pred sy=85 | remote sy=0`; jump arc should not rewind to standing.
- Keyboard remote (±85 on wire): hold-last and digital tap patch unchanged.

## Related

- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md)
- [`netplay_mixed_input_quantize_off_2026-05-18.md`](netplay_mixed_input_quantize_off_2026-05-18.md)
