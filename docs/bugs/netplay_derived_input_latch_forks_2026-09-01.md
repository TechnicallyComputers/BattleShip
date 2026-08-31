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

---

## Health snapshot after the fold fixes (soak 2026-09-01, up-B vs airborne opponent)

Best sync health measured in this investigation:

| metric | value |
|---|---|
| session length | 3097 ticks |
| FC diverges | **1** (was 3 per ~1900 ticks) |
| cross-peer forked ticks | p0 40/3060 (1.3%), p1 44/3054 (1.4%) |
| of those, status-prediction mismatches | 39/40 and 43/44 |
| fork episodes | p0 21, p1 17 — lengths 1-4 ticks, **all healed** |
| episodes involving a Kirby special (status >= 256) | **20/21 and 16/17** |

So the user's observation is exactly right and now quantified: **up-B entry is where the
peers transiently disagree**, in ~1-4 tick prediction episodes that rollback resolves. The
predicting peer has not yet seen the special start; it catches up within the lookforward
window. That is normal GGPO, not a defect — but it is also why the up-B window is where
every real bug in this investigation surfaced: it is the highest-churn prediction site in
the match.

The single FC diverge (@3095) is p1 X position off by **0.76 units**
(-2784.13 vs -2783.37) with velocity, status, motion, hitstate and all light scalars
identical; `joints` follows position. Sub-unit positional drift with matching velocity is
an accumulated-integration difference, not a state fork — the smallest surviving class yet,
and a different animal from the latch family above.

Also worth noting for the hitching: resim volume is asymmetric (93 linux vs 38 android).
The predicting peer does ~2.5x the replay work, which is where the felt cost lands.

---

## Stress soak: `shield_health` joins the family

Both FC diverges (1454, 1574) are the same thing: player 0 `shield_health` 55 vs 54, one
unit apart, with EVERY light scalar, every other full scalar, and the joints checksum
identical on both players. Shield health is a per-tick derived counter (decay while held,
regen otherwise) -- the same class as the tap_stick / hold_stick drift: one tick of
accounting apart, not a state fork.

`CliffDiag` was live this run (30/29 lines) and the peers agreed on EVERY cliff decision:
`tap=0x0000 hold=0x0000 fires=0`, identical tick by tick. The ledge grabs happened; the
ledge-attack fork did not reproduce. The instrument is confirmed working, so the next
occurrence gets captured rather than inferred.
