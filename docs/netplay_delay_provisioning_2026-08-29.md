# Netplay delay provisioning: RTT bands and the path to auto-D

## Why D was raised

Soak 2026-08-29 negotiated `D=2` from `rtt_ms=23` and then spent **~40% of the match at
`predict_depth` 5–6** on the peer running ahead. Cost, same session:

| | predicting peer | input owner |
|---|---|---|
| push-frame deficit | **75 / 964 (7.8%)** | 34 / 963 (3.5%) |
| resims | 21 | 15 |
| `load_fail_hold` blocks | 17 | 8 |
| admission R-stalls | 18 | 0 |
| ticks at `predict_depth` 5–6 | **386** | 0 |

85–91% of each peer's deficit is accounted for by resim replay + load-fail holds +
admission stalls — all downstream of D being too small to cover the link. The owner, which
never speculates, pays less than half the cost.

D is a cushion, not a tax: raising it trades a few frames of input delay for removing the
speculation that drives rollback churn, hitching, and the mispredict storms that corrupt
mid-grab state.

## The band table

`syNetSessionParamsDelayFromRttMs()`, 40 ms bands:

| RTT (ms) | D | | RTT (ms) | D |
|---|---|---|---|---|
| 0–40 | 3 | | 201–240 | 9 |
| 41–80 | 5 | | 241–280 | 10 |
| 81–120 | 6 | | 281–320 | 11 |
| 121–160 | 7 | | 321+ | 12 |
| 161–200 | 8 | | | |

Previous table for comparison: `<60 → 2`, `<100 → 2`, `<150 → 3`, `<200 → 5`, `<280 → 7`,
`else 8`. Every band moved up, most sharply at the low end where nearly every match sits
(23 ms went from D=2 to D=3).

`SYNETSESSION_PARAMS_ROLLBACK_D_MAX` raised 10 → 12 so the top two bands are not clamped
away. `PREDICTION_MAX` stays at 7 deliberately — higher D means *less* prediction runway is
needed, not more.

## Next: promote rbe's auto-delay from shadow to authority

The band table sets a good starting D; it does not adapt when the link changes mid-match.
`retcomm-rbengine`'s §57 arrival-driven controller does exactly that — it provisions D from
measured arrival misses and lateness rather than from a single RTT handshake sample, which
is the known weakness of any static table.

Two things are in the way, both known:

1. **`np_auto_delay_tick()` early-returns unless REAL-DELAY mode is active**
   (`"D is not a latency budget in legacy zero-delay mode"`). BattleShip binds
   `rbe_sched_set_real_delay(0)` because live VS consumes `wire = sim + D`. Under that
   mapping D *is* still the latency budget, just consumed a tick earlier — so the guard is
   wrong for this host specifically. Needs a host opt-in in rbengine rather than flipping
   the guard for MotK/PSX too.
2. **The bridge rejects proposals.** `syNetRbeSchedOpsRequestDelayChange()` logs
   `would_delay_change` and returns 0 by design (shadow mode). The wiring point on the host
   side is `syNetPeerApplyAutoNegotiatedDelayContract(delay, delay_ceil, tag)`.

### Sequencing

Land and soak the band table **first**, alone. Wiring auto-D at the same time changes two
variables at once: if the next soak regresses, there is no way to tell which change did it,
and the controller's proposals are themselves relative to the new baseline D. The shadow
already logs `would_delay_change`, so a table-only soak still shows what the controller
*would* have done — free evidence for whether step 2 is even needed at these new values.

## What to measure next soak

- `session_params host propose rtt_ms=… D=…` — confirm the band applied.
- `predict_depth` distribution — the 5–6 population should collapse.
- push-frame deficit — target the owner's ~3.5% on both peers.
- resim count, `load_fail_hold`, admission `pct_R` — all should fall together.
- `would_delay_change` lines — what rbe's controller still wants at the new baseline.
