# Startup hitching: 251 prediction-cap freezes in the first 6.5 seconds

**Status:** root cause identified; fix landed behind `SSB64_NETPLAY_BOOTSTRAP_CONTRACT_GATE`
(default on), needs a two-peer soak to confirm.
**Symptom:** hitching early in a match. Steady-state play is clean.

## The window

Measured on the tier-1 run (`veto=0`, so the scheduler is not involved):

| tick range | mean lead | max lead | ticks at `predict_depth >= 6` |
|---|---|---|---|
| 0-500 | 5.48 | 8 | **384** |
| 500-1000 | 1.05 | 3 | 0 |
| 1000-5274 | 0.4-2.8 | 3-5 | **0** |

All 251 `pcap FREEZE enter` events occur below wire 394. After tick 500 the lead never
exceeds 5 and the cap is never reached again across the remaining ~80 seconds. **The
hitching is entirely a match-start convergence problem**, not ongoing pacing.

Freeze gaps: 8 (169x), 7 (80x), 9 (2x) — against `P=6`, `D=4`.

## Root cause

Two contributions, one gate.

**1. The peers do not start together.** At the guest's tick 1 it already reports
`remote_sim=16`: the host began simulating ~16 ticks (~270 ms) earlier. Each peer anchors
the sim-led decouple to its **own** local monotonic clock at its **own** latch instant:

```
linux   decouple_sim_led_init anchor_mono_ms=1149364147 contract_hz=60
android decouple_sim_led_init anchor_mono_ms=564545840  contract_hz=60
```

Both then free-run at 60 Hz, so any skew between the two latch instants becomes a sim-phase
offset with nothing to correct it.

**2. Execution was released below the delay contract.** `bootstrap_ingress_warmup complete
role=host outbound=1 hr=2 staged=2` — released at `hr=2` with `D=4`. The gate asked only
for *some* inbound evidence:

```c
ingress_ok = (sSYNetPeerRemoteIngressSeen || syNetPeerGetHighestRemoteTick() > 0U ||
              sSYNetPeerFramesStaged > 0U);
```

So the host started already owing 2 ticks of prediction, and never held a cushion.

Together: the host runs ahead, pins at lead 8 (past `P=6`), and hard-freezes repeatedly
until the offset bleeds off through the freezes themselves.

## Fix

Require the inbound frontier to cover `D` before declaring execution ready.

This is self-resolving rather than deadlocking, which is the part that makes it safe:
`syNetPeerMaybeSendBootstrapWarmupInput()` sends INPUT-shaped warmup frames *precisely
while this gate is unsatisfied*, so both peers' frontiers climb and both release together.
It is bounded anyway at `SYNETPEER_BOOTSTRAP_CONTRACT_WAIT_MAX_FRAMES` (180), so the worst
case is today's behaviour a few frames later, logged as `bootstrap_contract_gate fallback`.

Properties:
- can **delay** a start, never **block** a join (bounded);
- cannot desync — tick identity is agreed separately by exec sync, this only gates when a
  peer begins;
- escape hatch without a rebuild: `SSB64_NETPLAY_BOOTSTRAP_CONTRACT_GATE=0`.

`bootstrap_ingress_warmup complete` now also reports `contract_wait=` and `D=`.

## What to check next soak

- `contract_wait=` should be small and non-zero, and `D=` should be covered by `hr=`.
- Any `bootstrap_contract_gate fallback` line means the bound bound — the wait did not
  resolve, and the frontier is not climbing during warmup as expected.
- `pcap FREEZE enter` count should fall sharply from 251. It will not necessarily reach 0:
  this fix addresses contribution 2 and, indirectly, much of 1. If freezes persist, the
  remaining work is the anchor itself — deriving the decouple anchor from the agreed
  exec-sync point rather than each peer's local latch instant.

---

## Soak 2026-08-30: the gate released on a stale contract

The fix did not engage, and the instrumentation added with it said exactly why:

```
bootstrap_ingress_warmup complete role=host outbound=1 hr=2 staged=2 ingress_seen=1 sim=0 contract_wait=0 D=2
```

`D=2` is the automatch bootstrap default (`automatch_config` / `vs_start`,
`source=auto_pending`). Session params negotiate `D=4` slightly later, so
`syNetPeerGetCommittedInputDelay()` at warmup time returns a contract the match will not
run. The gate compared `hr=2 >= need=2`, passed instantly, and gated on nothing.

Confirmed by the outcome: **255 freezes, all below wire 393** — statistically identical to
the 251 below wire 394 before the change — and all 255 report `D=4`, the negotiated value.

Fixed by requiring session params to be negotiated first and comparing against that delay.
The bound and the env escape hatch are unchanged, and the fallback line now also reports
`negotiated=`.

Otherwise this run was clean over ~145 s of play: tick/push 98.4% (host) and 99.3%
(guest), zero desyncs, zero hash mismatches, zero `BATTLE_SIM_HOLD`, zero rescues, and
after tick 1000 the lead stayed at mean 0.16-1.75 with max 3 and no deep prediction at all.
The startup freezes remain confined to the first ~390 ticks, which is why the player
reported the session as completely smooth — the window lands in the intro rather than in
play.

---

## Open risk in the fix itself: can `hr` climb before execution?

The gate now waits for the negotiated `D`, but there is an unproven assumption underneath
it: that the inbound frontier can reach `D` **while execution is still held**. The evidence
argues both ways.

Against:

- Held peers do not advance their sim, so no new wire rows are produced by play.
- `execution hold role=client ... highest_remote=0` across 120 held frames.
- The host's warmup topped out at exactly `hr=2` — with `D=2` at the time.

For:

- That `hr=2` matched `D=2` exactly, which suggests the warmup bundle spans `0..D` rather
  than stopping at 2 for its own reasons. With `D=4` negotiated *before* the check (which
  the ordering now guarantees, since `syNetPeerSessionParamsNegotiationSatisfied()` is
  already an execution gate), the same behaviour would give `hr=4` and the gate passes.

If the pessimistic reading holds, gating on wire rows is the wrong mechanism entirely and
the start has to be coordinated on **peer readiness** instead — the `peer_ready` /
`start_sent` / `start_recv` handshake already carried in the execution-hold line.

So the bound is deliberately short (**30 frames, ~0.5 s**, down from 180): if the wait
cannot be satisfied, that half second is the whole cost of learning it. The fallback line
now reports `hr_max=`, which is the actual diagnosis:

- `hr_max` reaching `need` → the gate works; freezes should drop.
- `hr_max` stuck at its entry value → the frontier never moves while held; abandon this
  approach and coordinate on peer readiness.

Either way the next soak answers it definitively, and neither outcome costs more than half
a second of startup.

