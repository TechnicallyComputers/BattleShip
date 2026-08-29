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
| 0–40 | 4 | | 201–240 | 9 |
| 41–80 | 5 | | 241–280 | 10 |
| 81–120 | 6 | | 281–320 | 11 |
| 121–160 | 7 | | 321+ | 12 |
| 161–200 | 8 | | | |

Previous table for comparison: `<60 → 2`, `<100 → 2`, `<150 → 3`, `<200 → 5`, `<280 → 7`,
`else 8`. Every band moved up, most sharply at the low end where nearly every match sits
(23 ms went from D=2 to D=4 — see the revision below).

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


---

## Revision (2026-08-29, second soak): LAN band 3 → 4

The D=3 soak was a large win — deficit 7.8%/3.5% → **1.7%/0.8%**, deep speculation
(`predict_depth >= 4`) confined entirely to the intro window (ticks 4–390) with **none** in
~7,500 ticks of gameplay, 21 fighter mismatches in 7857 ticks with a single 2-tick
sustained run, zero crashes, zero holds, and grabs 14/1 identical on both peers.

But two independent signals said the LAN band was still one notch short:

| | predicting peer | input owner |
|---|---|---|
| `pcap FREEZE` (prediction-cap) | **502** | 0 |
| rbe `would_delay` proposals | **247**, all `3 -> 4` | 0 |

So `0–40 ms` is now **D=4**.

### Why only one line appeared in the log

`syNetRbeSchedOpsRequestDelayChange()` deduped on the proposed *value*. All 247 proposals
asked for the same 4, so it printed once and the volume survived only in the session-end
scorecard — which is how a first read of that log concluded the controller "wanted
essentially nothing". It now also logs every 50th repeat: repetition is the promotion
signal, so it has to be visible inline.

### Still true: the arrival controller is dormant

Those 247 came from `np_adapt_delay_on_pcap_enter` (freeze-driven), **not** rbe's §57
arrival-driven controller, which still early-returns outside REAL-DELAY mode. Note the log
says `rbe: auto delay ON` on both peers regardless — that message is emitted during the
enable check, *before* the real-delay guard returns, so it reports ON while doing nothing.
Do not read it as evidence the controller is running.

### Decision point after the next soak

- `pcap FREEZE` collapses at D=4 → the static table is sufficient; promoting auto-D is
  optional.
- Freezes persist → that is the argument for wiring the real controller, and the safety
  precondition already holds: `rbe_invent_on_hold = 0` across ~16k admits over two full
  sessions, i.e. rbe was never more aggressive than the live gate.

---

## Implemented (2026-08-29): adaptive D at tier 3

The static band table stays as the *starting point* — it exists so a session opens at a
sane D and does not have to hitch its way there. The controller owns everything after
that. Three pieces, one per repo.

### 1. rbengine: `RbeSchedBridge.auto_delay_in_zero_delay`

`np_auto_delay_tick()` used to refuse any session where real-delay was off:

```c
if (!rbe_sched_real_delay_enabled())
    return; /* D is not a latency budget in legacy zero-delay mode */
```

That reasoning is sound for the mapping rbe historically meant by ZERO-DELAY, but it is
not true of BattleShip. Here consumption is `wire = sim + D`, so D still buys the arrival
cushion — it is just spent a tick earlier than under REAL-DELAY. The guard now also checks
a host opt-in flag (`retcomm-rbengine` `00d1663`), defaulting to 0, so MotK and psxrecomp
are unaffected.

### 2. netpeer: `syNetPeerRequestAdaptiveInputDelay(target, tag)`

D cannot be assigned mid-match. Because `wire = sim + D`, if the two peers switch on
different ticks then every subsequent wire lookup disagrees and the session desyncs on the
spot. `syNetPeerApplyAutoNegotiatedDelayContract()` *does* assign it directly and is a
session-setup path only — it must never be called from a controller.

The new API follows the auto-runway bump instead (`netpeer.c`, `auto_runway`):

1. clamp to contract, then to `[floor, ceil]`;
2. pick `eff_tick = now + syNetPeerDelaySyncCommitLeadTicks()`;
3. stage `sSYNetPeerDelaySyncPending{,EffectiveTick,Valid}`;
4. `syNetPeerSendInputDelaySyncPacket(proposed, eff_tick)`.

Both peers then apply at the same sim tick. Guards: host-only (guests follow DELAY_SYNC),
VS-active, nothing already in flight (neither a pending delay-sync nor a host ramp), a real
value change, and `SYNETPEER_ADAPTIVE_DELAY_MIN_SPACING_TICKS` (120) between changes so a
misbehaving controller cannot churn the contract. It returns TRUE only when it actually
queued something, so the caller needs no hysteresis of its own.

### 3. Bridge: tier 3

`SSB64_NETPLAY_RBE_SCHED` gains a tier:

| tier | behaviour |
|---|---|
| 0 | off |
| 1 | shadow only |
| 2 | shadow + conservative predict-veto |
| 3 | **+ adaptive D (authority)** |

Tier 3 binds `auto_delay_in_zero_delay = 1` and routes
`syNetRbeSchedOpsRequestDelayChange` into the netpeer API. Tiers 1–2 are untouched, so the
soaked shadow baseline stays comparable. The session scorecard gained
`adaptive_d_applied=` next to `would_delay=` — the gap between those two numbers is the
refusal rate (redundant proposals, in-flight changes, spacing floor), which is the first
thing to read in the next soak.

### What to watch

- `adaptive_d_applied` should be **small** relative to `would_delay`. A large value means
  the controller is oscillating and the spacing floor needs raising.
- `adaptive_delay queued D=x->y` lines in the peer log should pair up across the two peers
  at the same `eff_tick`. They must, or D has forked.
- `pcap FREEZE` counts should fall relative to the D=4 static baseline. If they do not, the
  controller is not the answer to freezes and the cause is upstream of D.

---

## Soak review (tier 3, first authority run)

Tier 3 confirmed active on both peers. Health for the window it ran was the best measured
so far:

| | prior soak (D=4 static) | this soak (tier 3) |
|---|---|---|
| `pcap FREEZE` | 502 | **1** |
| ticks at `predict_depth=0` | ~60% | **175/181 linux, 173/173 android** |
| `rb_applied` / `rb_load_fail` | nonzero | **0 / 0** |
| desyncs, hash mismatches, `BATTLE_SIM_HOLD`, rescues | — | **0** |

D negotiated to 4 from `rtt_ms=17` as intended (`host_compute`, echoed by `guest_recv`).
The `automatch_config` / `vs_start` lines showing `D=2 source=auto_pending` are the
pre-negotiation default and are not the contract.

**But the window is ~3 seconds.** Sim ticks reached 194 (linux) / 188 (android) and the
match never left the fighter intro, so this validates nothing about behaviour under load.
With `predict_depth` pinned at 0 there was no pressure for the controller to answer.

### The controller could not have acted anyway: ceil == D

`would_delay=1, adaptive_d_applied=0`, and the reason is structural, not a lack of demand:

```
SSB64 NetSession: apply tag=host_compute ... D=4 phase_lock=6 ... ceil=4
```

`syNetSessionParamsComputeNegotiatedDelayCeil()` returned `d_ticks` unchanged, because it
only adds headroom when the *separate* legacy `SSB64_NETPLAY_ADAPTIVE_DELAY` env var is
set — and even then only +1. So `syNetPeerRequestAdaptiveInputDelay()` clamped every
proposal to `ceil=4`, found it equal to the committed D, and refused it as redundant.
Tier 3 was authority in name only.

Fixed two ways:

- **The tier grants its own headroom.** `SYNETSESSION_PARAMS_RBE_ADAPTIVE_HEADROOM` (4,
  ~two RTT bands) applies whenever tier >= 3, independent of the legacy env var. Requiring
  a second env var to make the first one do anything is a trap, not a safety feature. The
  ceiling is additionally clamped to `ROLLBACK_D_MAX` in rollback sessions so it cannot
  promise past what D itself is allowed to reach.
- **Refusals are now logged.** The first cut returned before the shadow logging, so the
  soak recorded `would_delay=1` with nothing saying what was proposed or why it did not
  land. Tier 3 now emits `adaptive_delay queued|refused D=x -> y ceil=z` on the same
  change-or-every-50th cadence.

### Unrelated: the Android abort ended the run

Android died at tick 188 with `CRASH SIGABRT fault_addr=0x2b6a000025d4`. Every frame in the
backtrace is system code — `libc` (abort/raise) under `libbase` fatal logging, under
`libart`, under `libandroid_runtime`, entered from a JIT-compiled Java frame. There is no
`libmain.so` frame anywhere, and no surface/resize event near the crash.

That is an ART-side abort, not a native fault in the sim, and it is not attributable to
tier 3 — no D change was ever applied, so the contract never moved. The fatal reason is
printed by libbase's `LOG(FATAL)` **to logcat**, which this capture does not include, so
the next Android run needs a concurrent `adb logcat` to identify it. The debug APK runs
with CheckJNI enabled, which makes a JNI check abort the leading candidate.
