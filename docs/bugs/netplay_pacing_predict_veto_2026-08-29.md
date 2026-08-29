# Sim runs at 30 Hz: the rbe predict-veto drops live advances

**Symptom (player):** the game feels slow, persistently, across every soak since the rbe
scheduler was enabled at tier >= 2.
**Cause:** tier 2's conservative predict-veto applies rbe stall verdicts that are
calibrated for a consumption mapping BattleShip does not run.

## The measurement

One session, host log:

| quantity | value |
|---|---|
| render frames | 3694 |
| pushes | 1579 (43% of frames) |
| **sim ticks** | **926** (59% of pushes) |
| `tick_ema` (rbe's measured inter-tick interval) | **33 ms** |

33 ms per sim tick is **30 Hz**. The sim is advancing at half rate under a ~120 Hz render
loop, so the game plays in slow motion. That is the whole of the reported slowness.

The lost advances are the vetoes, essentially one for one:

```
pushes - sim ticks = 1579 - 926 = 653
veto                              = 645
```

Scorecard: `attempts=1552 ... rbe_wait_on_predict=645 veto=645` — 42% of admits vetoed.
Wait reasons: **`gap1_grace=592`**, `timesync_pace=27`, `runway_grace=12`,
`boot_tip_wait=11`, `cushion_rebuild=2`, `depth_stale_wait=1`.

## Why `gap1_grace` fires constantly

`gap1_grace` is a wall-clock micro-grace in `rbe_sched.c`: when the needed wire row is
exactly one tick beyond what has arrived, rbe stalls for up to
`min(RTT/2 + base, LAN|relay cap)` ms hoping the row lands, rather than inventing.

Under REAL-DELAY that is a rare, cheap wait — tick T consumes wire T, there is an arrival
cushion, and gap=1 means something is genuinely late.

Under BattleShip's ZERO-DELAY mapping, `wire = sim + D`, tick T demands the *newest* row
the peer has produced. **gap=1 is the steady state, not an anomaly.** So the grace fires on
almost every tick, and tier >= 2 converts each stall into a live R-hold
(`shared->advance = FALSE`), dropping the sim advance.

This is the same root cause as the auto-D revert on the same day: rbe's policy assumes an
arrival cushion, and under this mapping `cushion` measured 0.00 at every value of D.

## Fix

Gate the tier-2 veto on the consumption mapping, exactly as the adaptive-D path is gated:

```c
if ((tier >= 2) && (sRbeRealDelayForced != 0) && (actual_predicted != 0) && ...)
```

Tiers 2 and 3 therefore collapse to shadow-only until the REAL-DELAY flip, which is honest
— the veto was never safe to apply under a mapping its verdicts were not written for. The
tier strings now say so.

**Immediate mitigation without a rebuild:** set `SSB64_NETPLAY_RBE_SCHED=1`. Tier 1 has
always been shadow-only, so it never had the veto.

## Residual, not yet explained

Removing 645 vetoes puts sim ticks at ~1571 over the same wall clock, i.e. **~51 Hz, about
85% speed** — better, not fixed. The remaining shortfall is upstream of the scheduler: the
push rate itself is ~52/s against a 60 Hz target (1579 pushes over ~30.6 s). Whatever gates
render frames into sim pushes is losing roughly 8 Hz on its own, and that is the next thing
to measure — it was masked by the far larger veto loss.
