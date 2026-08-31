# Derived input-latch forks (family)

The Kirby/ledge investigation has converged on one recurring family: **per-tick DERIVED
input state that live and replay compute from different sources.** Three members so far,
all with the fighter's actual behaviour identical across peers:

| member | symptom | status |
|---|---|---|
| `input.pl.button_tap` (edge latch) | ledge ATTACK on one peer, CLIMB on the other, with `btn=0x0000` in the authoritative history | captured, deliberately NOT restored (restoring caused the Kirby-inhale fork @524); diag armed |
| `tap_stick_*` / `hold_stick_*` (counters) | soak 2026-09-01 @1932: p1 `tapy`/`holdy` **5 vs 8**, EVERY other field byte-identical -- position, velocity, status, joints | captured AND restored, with `stick_prev` and `stick_range` alongside -- yet still drifts |
| rng consumption | same soak @1941: `figh` and `world` byte-IDENTICAL, `rng` alone forked (peer still holds the 1933 value) | one peer consumed a draw the other did not |

## Why the counter case matters

`tap_stick_*` / `hold_stick_*` are the one member that cannot simply be dropped from the
fold: they drive tap detection (dash, smash), so a drift is genuinely sim-relevant even
though this occurrence produced no behavioural difference. Their whole derivation chain is
snapshot-covered (counters, `stick_prev`, `stick_range`, `button_hold`), so a missing
restore is ruled out -- the drift must come from live and replay running the derivation a
different number of times, or over different input rows.

That is the same shape as the `button_tap` case, and it is the shape the double-step
witnesses were originally built to catch (they cover the fighter UPDATE; this is the INPUT
derivation, which runs elsewhere).

## Next instrument

Log `tap_stick_y` / `hold_stick_y` / `stick_prev.y` / raw `sy` per tick per pass
(live vs resim) for a short window around a resim, then diff the passes on one peer. If a
replayed tick derives a different counter than the live pass did with the same history row,
the derivation is reading something outside the snapshot -- and that source is the fix.
Cheap to add next to the existing `STICK_SAMPLE` diag.
