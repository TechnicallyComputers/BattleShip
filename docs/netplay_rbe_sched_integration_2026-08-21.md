# retcomm-rbengine admission scheduler in BattleShip (shadow integration)

**Status (2026-08-21):** landed, shadow-only, default OFF. The netpeer
phase-lock gate remains authoritative. Enable with `SSB64_NETPLAY_RBE_SCHED=1`.

## Why

The authoritative VS admission gate (`syNetPeerEvaluateSharedCommitStep`,
`port/net/sys/netpeer.c`) is a **pure function of tick arithmetic**: `sim`
vs `hr` vs `D` vs a prediction window measured in ticks. Audit finding that
motivated this work:

- `netinput.c` (12.5k LOC) contains **zero wall-clock reads**; `netpeer.c`
  reads the monotonic clock only in the exec-sync bootstrap latch. No
  per-tick admission decision consults time.
- The consumption mapping is `wire = sim + D` (a row sampled at admit T is
  consumed at T). In rbengine terms this is **ZERO-DELAY (legacy)** mode:
  `D` is a wire label offset, not a latency cushion, and prediction depth
  is permanently ≥ 1 whenever ingress lags.
- The only adaptive-D path (`syNetPeerMatchDelayStarvationUpdate…`) bails
  when rollback is enabled — rollback sessions run **no D controller**.

Consequences show up as mispredict episodes on press/release edges whose
rows land 1–2 ticks late (invent-on-first-miss), and as per-fighter
compensation gates tuned tick-by-tick. `retcomm-rbengine`'s `rbe_sched`
(MotK/psxrecomp-proven) is exactly the missing layer: **invent grace,
post-episode cushion rebuild, pcap freeze, mispredict-driven timesync
pacing, arrival-driven delay controllers** — the time domain.

## Layering (updated architecture)

```text
BattleShip netmenu host (authoritative)
  ├── netpeer / netinput          tick ownership, wire contract, rings,
  │                               publish, zero-onset + per-fighter gates
  ├── netrollback(+snapshot)      episodes, typed snapshot ring, resim
  │     └── rnet_rb bridge        (netrollback_episode_rnetrb.c, opt-in FSM shadow)
  ├── netsched_rbe  ◄─ NEW        admission-policy shadow / (later) authority
  │     └── retcomm-rbengine      rbe_sched policy (shared with RetComM recomps)
  └── recomp-net                  episode FSM core, input contract, wire protocol
```

Deliberately **not** adopted from rbengine: `rbe_snap_ring` (BattleShip's
49.6k-LOC typed snapshot/reconcile layer is a different object), and
`rbe_input_hist` (fused with BattleShip's publish path). The scheduler is
the piece with a clean seam.

## Library-side seams (retcomm-rbengine `feat/host-session-ops`)

Added so a host with its own transport can bind the scheduler without an
`RNetSession` (BattleShip netpeer today; SNES/NES facades later):

| Seam | Purpose |
|------|---------|
| `RbeSchedBridge.sess_ops` (`RbeSchedSessionOps`) | `committed_delay`, `request_delay_change`, `remote_arrival_age_ms`, `peek_remote_input`, `get_stats`. Unset members fall back to `rnet_session_*` on `*session` — MotK/PSX hosts unaffected. |
| `rbe_sched_set_real_delay(int)` | Programmatic consumption-mapping override (REAL-DELAY vs ZERO-DELAY); beats `RBE_RB_ZERO_DELAY`. BattleShip binds 0 (zero-delay) to match live VS. |
| `rbe_logf` / `rbe_set_log_sink` | All scheduler stderr output routes through a sink; BattleShip binds `port_log`. |

Covered by `tests/sched_ops_test.c` (6/6 suite green).

## BattleShip driver: `port/net/sys/netsched_rbe.{c,h}`

Compiled only in netmenu builds (`port/net` glob); offline binary contains
zero rbe symbols (verified via `nm`). All hooks early-out at tier 0.

**Bind lifecycle** — lazy edge detection inside the observe hook: first
observed VS attempt binds (`rbe_sched_bind`, resets rbe session state);
`syNetRollbackStopVSSession` emits the final scorecard and unbinds.

**Gates / ops mapping:**

| rbe needs | BattleShip provides |
|---|---|
| `now_ms` | `syNetPeerOsMonotonicMs()` (u32 truncation; deltas only) |
| `episode_active` | `syNetRollbackIsResimulating()` |
| `episode_count` | `syNetRollbackGetAppliedResimCount()` |
| `rtt_ms` | none — rbe synthesizes its grace floor from `D` (`np_invent_rtt_ms`) |
| `committed_delay` | `syNetPeerGetCommittedInputDelay()` |
| `request_delay_change` | **logged as `would_delay_change`, rejected** (shadow) |
| `remote_arrival_age_ms` | new arrival-stamp ring (below) |
| `peek_remote_input` | `syNetInputHasRemoteInputForWireTick()` (presence only) |
| stats (`remote_lead`, `highest_remote_wire`) | `hr − sim` and `syNetPeerGetHighestRemoteTick()`; under `wire = sim + D` healthy lead ≈ D, the steady state rbe's cushion logic expects |

**Arrival stamps** — `syNetInputSetRemoteInputFromPacketEx` (the single
strict-wire ingress commit) stamps first arrival per `(slot, wire)` into a
256-entry ring keyed `wire % 256` with the wire value as generation check.
Retransmit dups keep the first stamp. Feeds rbe scorecard slack telemetry
now; becomes the provisioning signal for auto-D under REAL-DELAY later.

**Hook sites** (each a one-liner in live code):

| Site | Feeds |
|------|-------|
| `netinput.c` FuncRead wire admission (after `syNetPeerEvaluateSharedCommitStep`) | `syNetRbeSchedShadowObserve` — the comparison itself |
| `netinput.c` strict ingress commit | arrival stamp |
| `netpeer.c` zero-onset stall branch | `NoteZeroOnsetHold` — shadow skips (respects) that tick |
| `netrollback.c` `EmitResimCompleteAfterFinish` | `note_mispredict(depth)` + `note_episode_boundary()` (arms cushion rebuild) |
| `netrollback.c` `StopVSSession` | final scorecard + unbind |

**Cadence** — one rbe evaluation per `(sim_tick, push_frame)`: stall spins
re-evaluate the same tick once per VI, matching the MotK admit-attempt
cadence rbe's grace timers were tuned for; same-frame re-evaluations drop.
`post_admit` fires once per actually-advanced tick, after any veto.

**Comparison semantics** (`SSB64 NetSchedRbe:` log lines):

| Actual verdict | rbe verdict | Counter |
|---|---|---|
| advance, confirmed row | admit | `agree_hit` |
| advance, confirmed row | stall (timesync pace…) | `rbe_stricter_on_confirmed` |
| advance, prediction | invent | `agree_invent` |
| advance, prediction | stall (grace/cushion/freeze) | **`rbe_wait_on_predict`** ← headline |
| hold `R` (frontier) | stall | `agree_stall` |
| hold `R` (frontier) | invent | `rbe_invent_on_hold` |
| hold `E`/`H`/`B`, zero-onset | *(not compared)* | `skipped(host_hold / zero_onset)` |

`rbe_wait_on_predict` is the value proposition: attempts where a short
grace/cushion wait would have replaced a prediction that risks a
mispredict episode. Per-reason splits (`gap1_grace`, `cushion_rebuild`,
`pcap_freeze`, `runway_grace`, `depth_stale_wait`, `timesync_pace`, …) are
in the scorecard; `DIVERGE` detail lines are capped at 64/session.

## Env knobs

| Variable | Effect |
|----------|--------|
| `SSB64_NETPLAY_RBE_SCHED` | `0` off (default) · `1` shadow · `2` shadow + conservative veto |
| `RBE_RB_INVENT_GRACE_MS`, `RBE_RB_GAP1_*`, `RBE_RB_TIMESYNC`, `RBE_RB_AUTO_DELAY`, `RBE_RB_ADAPT_DELAY`, `RBE_CROSS_OS_PACING_DIAG` | rbengine's own knobs pass through unchanged |

Tier 2 (`=2`): an rbe stall verdict converts a prediction-window advance
into an `R`-hold. Strictly conservative — never admits anything the gate
would not — and the strict-R stall watchdog / `SSB64_NETPLAY_STRICT_R_ABORT_FRAMES`
teardown applies unchanged. Run tier 1 soaks before using it.

## Promotion plan

1. **DONE — shadow (tier 1).** Soak 1v1 LAN + WAN; read scorecards. Healthy
   signal: high `agree_*`, meaningful `rbe_wait_on_predict` clustered on
   `gap1_grace`/`cushion_rebuild`, near-zero `rbe_invent_on_hold`.
2. **DONE (mechanism) — conservative veto (tier 2).** Enable after tier-1
   soak shows waits would land where episodes actually spawned (correlate
   DIVERGE ticks with `RESIM`/episode logs).
3. **TODO — auto-D authority (tier 3).** Route `request_delay_change` into
   the host delay-ramp (`sSYNetPeerHostDelayRampTarget` path) instead of
   rejecting. Protocol work: today delay changes mid-rollback-session are
   deliberately refused; the ramp needs a rollback-safe commit lead. Soak
   `would_delay_change` first — it already shows what the controller wants.
4. **TODO — REAL-DELAY flip (step 6).** `rbe_sched_set_real_delay(1)` +
   wire remap (tick T consumes wire T; local sample stored at T+D). This
   changes what `D` *means* — a real latency cushion — and is the payoff
   step (rbe soaks measured invent storms collapsing once the cushion is
   real). It invalidates existing soak baselines and will pressure several
   per-fighter compensation gates toward deletion. Own milestone.

## Fidelity caveats (shadow)

- Mispredict notes are **per episode** (depth = resim span), not per
  mispredicted row as in MotK's reconcile path — timesync debt is the
  right order of magnitude but coarser.
- Miss attribution uses the **first remote human slot** only (1v1-correct;
  N-peer would need per-slot loops).
- `tip_holding` gate is unmapped (BattleShip has no TipHold-Live analogue);
  rbe treats those windows as live admits.
- No POST/ICE RTT sample — grace budgets ride the D-scaled synthetic floor.
- Arrival ring aliases after 256 wire ticks (fine for slack telemetry).
- Shadow feeding runs on the FuncRead evaluation only; the secondary
  `syNetInputPhaseLockPredictAdvanceAllowed` query does not feed (and at
  tier 2 is not vetoed — a vetoed tick is never admitted, so the republish
  path never consults it for that tick).

## Running a soak

```bash
SSB64_NETPLAY_RBE_SCHED=1 ./BattleShip   # both peers
# read:  SSB64 NetSchedRbe: scorecard ...            (5 s cadence)
#        SSB64 NetSchedRbe: DIVERGE predict-vs-wait  (≤64/session)
#        SSB64 NetSchedRbe: would_delay_change ...
#        rbe: ...                                    (rbengine's own scorecard/log via port_log)
```

## Related

- `docs/netplay_timebase_authority.md` — sim tick vs `hr` vs `D` vs wall.
- `docs/netplay_rollback_refactor_contracts.md` — offline/netmenu gating.
- `port/net/sys/netrollback_episode_rnetrb.c` — the same shadow-adoption
  pattern used for the shared episode FSM.
- retcomm-rbengine `docs/host_integration.md` — gates/ops reference.
