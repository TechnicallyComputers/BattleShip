# REAL-DELAY Flip — Consumption-Mapping Contract (Phase 0)

**Date:** 2026-08-31
**Status:** Phase 0 landed (contract + observe-only witness). No behavior change.
**Prereq for:** promoting retcomm-rbengine tiers 2/3 out of shadow
(`sRbeRealDelayForced`, `docs/netplay_rbe_sched_integration_2026-08-21.md`).

---

## 1. The current mapping (ZERO-DELAY), anchored to code

All four legs verified in source on 2026-08-31. `D` =
`syNetPeerGetCommittedInputDelay()` (DELAY_SYNC-committed input delay).

| Leg | Arithmetic | Code |
|---|---|---|
| **Local feel** | HID sampled at sim tick `t` → gameplay ring row `t`; local sim consumes it at `t`. **Zero local input latency.** | `syNetInputLocalGameplayOwnerTick` returns `sample_tick` under NETMENU (`netinput.c`) |
| **Egress label** | The same sample also stages at send-lead row `t + D`; hold-last fill covers `(t+1)…(t+D)` so bundles can emit labels through `sim + 2D`. | `syNetInputLocalSendLeadOwnerTick` = `sample_tick + D` (`netinput.c`) |
| **Ingress decode** | Wire row `w` maps back to sim tick `w − D`. | `syNetPeerDelaySimTickFromWire` (`netpeer.c`) |
| **Admission demand** | To run sim tick `T`, admission requires wire row `T + D`. | `syNetPeerGetBaseRequiredWireTick` = `sim_tick + D` (`netpeer.c`), consumed by `syNetPeerEvaluateSharedCommitStep` |

### The cancellation

Encode adds `D`, decode/demand subtract it. Substituting: the row demanded for
sim tick `T` (wire `T + D`) is the sender's sample **from the sender's sim tick
`T`**. The `±D` is pure labeling; it cancels end to end.

Therefore a remote input for tick `T` physically cannot exist before the
sender's wall clock reaches its tick `T`, and cannot arrive before `T +
RTT/2`. If the two sims run in phase, **every remote tick is consumed before it
can arrive**. The receiving peer must predict (or stall) by construction, and
no value of `D` changes this — both peers' labels move together, so demand and
supply shift identically.

### Measured consequences (this is not theory)

- **cushion = 0.00 at D=4, 5, 6, 7, 8** — adaptive-D experiment, 2026-08-29;
  prediction pressure *lowest* at D=4; controller ratcheted to ceiling for +4
  frames of lag and zero benefit. Reverted.
- **rbe tier-2 veto halved the sim to 30 Hz** — `gap1_grace` treats gap=1 as a
  transient; under this mapping gap≥1 is the steady state. Reverted to shadow.
- **Permanent predictor asymmetry** — soak 197928900: linux `wire_gap=2` ×116,
  android 0; linux 134 resims vs 64; `rbe_wait_on_predict=1136/1721` vs
  468/1647. Whichever sim leads becomes the full-time predictor.
- **Stick-sweep resim storm** — hold-last prediction vs exact input equality
  (`syNetInputTimelineFrameGameplayEquals`) mispredicts nearly every tick of
  analog motion → span-1 light episode → snapshot load every ~6 ticks →
  hitching. Not throttleable: deferring light begins is a refuted class
  (`docs/bugs/netplay_light_absorb_coalesce_onset_delay_2026-07-27.md`).

What ZERO-DELAY buys in exchange: **both peers play at feel-0** (local input
lands the tick it is sampled). This is the Slippi-style trade.

---

## 2. The target mapping (REAL-DELAY)

One sentence: **the sample taken at tick `t` becomes the input for sim tick
`t + D` on both machines.**

| Leg | Target arithmetic |
|---|---|
| Local feel | Sample at `t` → gameplay ring row `t + D`; local sim consumes it at `t + D`. **Local latency = D ticks.** |
| Egress label | Wire label = consumption tick = `t + D` (unchanged label arithmetic — the *meaning* changes: label now names the tick both sims consume it at). |
| Ingress decode | Wire row `w` is the input **for sim tick `w`** — decode is identity. |
| Admission demand | To run sim tick `T`, admission requires wire row `T`. |

Now the row for tick `T` was sampled at `T − D`: it has **D ticks of wall-clock
to cross the network**. `cushion(T) = hr − T ≈ D − RTT/2·tickrate`. Prediction
happens only when RTT/2 genuinely exceeds D frames — the GGPO contract.

### What D means before vs after

| | ZERO-DELAY (today) | REAL-DELAY (target) |
|---|---|---|
| Local feel | 0 ticks always | D ticks |
| Transit budget | 0 (structural) | D ticks |
| Raising D | +lag, no benefit | +lag, +cushion — a real dial |
| Adaptive D | provably useless (cushion ≡ 0) | the correct controller |
| D=0 | today's behavior | identical to today's behavior |

**The flip adds local input latency.** That is the honest price. (An earlier
session note claimed the lag already existed — wrong for NETMENU: feel is 0
today. Only the legacy non-NETMENU path had closed-loop `+D`.) Mitigation: D
only needs to cover RTT/2, so LAN wants D=1–2, not 4–8; and post-flip the
adaptive controller can hold it at the minimum that keeps cushion ≥ target.

---

## 3. Phase 0 witness (landed with this doc)

`SSB64_NETPLAY_CONSUMPTION_WITNESS=1` (both peers, any build with this commit):

- `SSB64 NetCw: WINDOW ticks=A..B D=d admitted=120 ring=R predict=P
  hold_frames=H cushion_min=m cushion_max=M cushion_avg_x100=c` — every 120
  admitted ticks. `cushion = hr − required_wire` at admission; negative =
  prediction depth.
- `=2` adds one `NetCw: TICK` line per admitted tick.
- Once per window it self-checks the current contract
  (`encode(T) == T+D`, `decode(encode(T)) == T`) and screams
  `CONTRACT_VIOLATION` if the mapping functions drift. After the flip the
  check is updated to the new law in the same commit that changes the mapping
  — a violation is always drift, never the flip itself.

Hook: observe-only, beside `syNetRbeSchedShadowObserve` in the authoritative
admission path (`netinput.c` FuncReadWireAdmission). Reset per session in
`syNetRollbackStartVSSession`.

**Expected series today:** cushion pinned ≤ 0 (leader strongly negative,
follower ≈ 0), identical shape at any D. **Expected after flip:** cushion ≈
`D − RTT_ticks/2` > 0 in steady state, `predict` count near zero.

---

## 4. Flip implementation notes (Phase 1 — not yet started)

The flip is small in arithmetic and large in blast radius. The mechanical
change is one leg: **gameplay owner tick `t` → `t + D`**
(`syNetInputLocalGameplayOwnerTick`), plus making decode/demand consistent
(`DelaySimTickFromWire` → identity, `GetBaseRequiredWireTick` → `sim_tick`),
so labels keep naming consumption ticks.

Known assumption sites to audit before flipping (each encodes ZERO-DELAY):

1. **DELAY_SYNC commit-lead** (`sSYNetPeerDelaySync*`, netpeer.c) — a D change
   mid-match now also shifts the local sample→consume offset; the commit-lead
   protocol must switch both peers' *consumption* of the new D on the same
   tick, and the send-lead hold-last fill must bridge the gap without
   double-consuming or dropping a sample row.
2. **Bootstrap ingress symmetry** (`syNetPeerBootstrapIngressSymmetrySatisfied`)
   — startup grace is derived from `GetBaseRequiredWireTick(0)`; first-tick
   labeling changes meaning.
3. **Send-lead `sim + 2D` emission window** — becomes `consume + D`; recheck
   `AppendDelayed` bounds and the intro-Wait `DelaySim(hr)` frontier.
4. **`syNetPeerGetRemoteSimRunwayCap` / prediction windows** — every
   `DelaySimTickFromWire(hr)` site changes value by +D at the same hr.
5. **Rollback resim input selection** (`RESIM_INPUT_SOURCE`, seal rows) — rows
   are keyed by consumption tick throughout; verify no site re-derives a
   sample tick by subtracting D.
6. **rbe_sched** — set `rbe_sched_set_real_delay` / `sRbeRealDelayForced`
   TRUE; tiers 2/3 become promotable and their grace logic finally matches
   reality.
7. **`netsession_params` RTT→D bands** — currently vestigial labels; post-flip
   they set real lag/cushion and must be re-derived from measured RTT.

### Compatibility

The flip is wire-meaning-breaking between builds. Gate it as a **negotiated
session parameter**: both peers advertise support + mode; mismatch **refuses
the session at connect** with a clear log, converting the silent-desync trap
into a connection error. Env override selects mode per soak
(`SSB64_NETPLAY_REAL_DELAY=1`), default stays ZERO-DELAY until Phase 2
re-baselining is done.

### Baseline invalidation

Recorded doctrine: the flip changes what D means and **invalidates all soak
baselines** (`memory: retcomm-shared-netcode-base`). Phase 2 opens a fresh
baseline series; pre-flip and post-flip soaks are compared only via the
witness cushion series, never via resim/GGPO counts.

---

## 5. Phase ledger

- [x] **Phase 0** — this contract; witness landed observe-only (commit 4219d369).
- [x] **Phase 1** — landed 2026-09-01. **As-built formulation (simpler than §2's
      sketch):** egress staging and wire labels are physically unchanged
      (sample stages at owner `sample+D`, emitted verbatim) — under the flip
      those labels *name* the consumption tick, so **encode, decode, and demand
      all become identity**. D lives solely in `LocalGameplayOwnerTick`
      (sample → consumption keying of the gameplay ring). Demand for ticks ≤ D
      is vacuous (`required_wire = 0`, admission treats as present) — no
      startup row-seeding needed, since those ticks' samples predate the
      session on both peers symmetrically (both consume neutral).
      - Negotiation: `SYNETSESSION_ROLLBACK_FLAG_REAL_DELAY` (0x04) in
        `rollback_flags`, host-authoritative; **host env
        `SSB64_NETPLAY_REAL_DELAY=1`** arms it, guest follows. Default off —
        ZERO-DELAY sessions are bit-identical.
      - Mixed builds: `SYNETPEER_VERSION` 8 → 9; pre-flip builds refuse at the
        packet layer (`ver_ok=0` drop) instead of desyncing.
      - D frozen for the session: mid-battle DELAY_SYNC queue writers
        (auto-runway bump, adaptive request) return early under the flip; the
        startup delay-align still runs (lands in intro, neutral inputs).
        Dynamic D under REAL-DELAY is Phase 3.
      - Witness contract check is mode-aware; rbe shadow told
        `rbe_sched_set_real_delay(1)` (tiers still gated — `sRbeRealDelayForced`
        stays 0).
      - First soak: LAN, both peers same build, host `SSB64_NETPLAY_REAL_DELAY=1`,
        both `SSB64_NETPLAY_CONSUMPTION_WITNESS=1`. Expect `NetCw: WINDOW`
        cushion ≈ +D − RTT_ticks/2 > 0, predict ≈ 0, and D ticks of added local
        input latency (the feel trade).
- [~] **Phase 2** — first flip soak 257428529 (2026-09-01, LAN, D=4):
      negotiation + wire clean (`rb_flags=0x07`, dropped=0, zero
      CONTRACT_VIOLATION), match ran full length. **Finding: the flip alone
      does not hold cushion.** Both peers opened near their expected regime
      (linux ring=120 cushion 0..+8, android predicting) and then converged
      back to the zero-cushion fixed point, trading the predictor role
      (cushion_min −4 = the full D budget spent by free-running). Nothing in
      admission *waits* while prediction is allowed, so the leader consumes
      ahead until it re-pins at the prediction frontier — the flip provides
      the budget; a **pacing law** must hold it. That law is exactly the rbe
      tier-2 wait-on-predict (shadow said wait 789/328 ticks this soak), which
      was destructive under ZERO-DELAY (waiting could never be satisfied) and
      is productive under REAL-DELAY (the row is en route). Residual: two
      figh diverges @1123/1132 with byte-identical dumped scalars — a
      non-dumped per-status overlay fold (Kirby stone-scope duration/resist
      restore in ReconcileStoneAfterRollback is the suspect); small tail,
      needs a dump extension, not a class.
      Remaining for Phase 2 closure: tier-2 A/B soak (below), RTT→D bands,
      DELAY_SYNC-under-flip design.
- [x] **Phase 3 (2026-09-01)** — landed in four steps:
      1. `sRbeRealDelayForced` follows the negotiated flip; tiers unlock only
         in REAL-DELAY sessions (tier 1 auto-raises to 2 there).
      2. First armed tier-2 soak: **zero resims, full match, both peers** —
         at 34% stall duty (gap1_grace hard stalls; sim ~40 Hz; blade stutter
         is those stalls made visible). Root: production is coupled to
         consumption, so symmetric full-frame stalls converge to just-in-time
         at heavy duty; the gap1 stalls also kept resetting the 8-admit
         negative-lead streak, so rbe's one-sided timesync actuator
         (`timesync_pace`) never engaged (=1 all match).
      3. gap1 duty cap (default 12%, `SSB64_NETPLAY_RBE_GAP1_DUTY_PCT`):
         over-budget gap1 ticks admit with 1-tick prediction (span-1
         corrections, true row next frame); deeper gaps still veto. This also
         unmasks the timesync streak. Plus rbe §114 pace-to-target
         (`RBE_RB_TS_LEAD_TARGET`, bridge defaults it to 1 under the flip):
         the ahead seat keeps shaving until the follower banks a margin,
         instead of stopping at lead 0.
      4. Adaptive D unfrozen under the flip, single-step (±1) only, through
         the existing DELAY_SYNC commit-lead. A raise leaves consumption rows
         `[E+D1, E+D2)` unowned (sample E−1 owned `E−1+D1`, sample E owns
         `E+D2`) — local would read neutral while the peer predicted
         hold-last → fork. Fix: the rows are *sender-authoritative*, so each
         peer fabricates its own by holding the last pre-switch sample,
         stages them (gameplay + send-lead), and ships them as ordinary wire
         rows (`syNetInputFillDelayRaiseGapConsumptionRows`,
         `DELAY_RAISE_GAP_FILL` log). Lowering needs nothing — the colliding
         row is wire-locked to first-transmitted on both sides. Tier 3
         (adaptive proposals) remains explicit opt-in: `SSB64_NETPLAY_RBE_SCHED=3`.
      **Steps 3a (pace-to-target) and the tier-1→2 auto-raise were REVERTED
      on 2026-09-01 — see Phase 3c.**
- [x] **Phase 3c — the arrival margin is a demand offset, not a stall policy.**
      Soak with duty cap + pace-to-target got *worse*: veto 525 → 1465,
      `timesync_pace` 1 → 741, `cushion_rebuild` 0 → 665, `pct_R` 34% → 40%.
      Root cause, measured: **`lead_avg = 0` at D = 4.** `lead = hr − sim =
      (S_peer + D) − S_local`, so lead 0 means the local sim runs a full D
      ticks ahead of the peer's. Phase 1 demanded exactly row T (C = 0), which
      is satisfiable the instant the row lands — the sim races to the arrival
      frontier at session start (the ≤ D window is free) and parks there with
      zero margin forever. Every rbe policy then reads permanent starvation
      (it expects lead ≈ D; cushion-rebuild waits for lead ≥ D−1 and never
      clears), and tier 2 turns each verdict into a whole-frame hold.
      Two refutations recorded:
      - *Stalling cannot build margin.* A stall also halts local sampling, so
        it starves the peer symmetrically; both seats converge to
        just-in-time, only slower.
      - *pace-to-target destroyed one-sidedness.* `lead < 0` is true for at
        most one peer; `lead < 1` was true for **both**, so both paced.
      **Fix:** admission demands row `T + C`. Since `hr = S_peer + D`, admit
      requires `S_peer + D ≥ T + C`, i.e. the local sim may lead the peer's by
      at most `D − C`. C *is* the arrival margin; `D − C` is the slack for one
      peer to run ahead. `C < D` is required (at `C = D` the two demands are
      mutual exact lockstep → deadlock). Default `D/2`, clamped `[1, D−1]`,
      `SSB64_NETPLAY_REAL_DELAY_CUSHION` overrides. Demands below D are
      vacuous (sample t owns row t+D, so rows < D never exist).
      Witness `NetCw: WINDOW` now prints `C=`.
      **Follow-up (same day): the offset alone did nothing** — `required_wire`
      gates only the RING path. Prediction admits off
      `remote_sim_frontier + window` and ignored C, so the sim predicted
      straight through the margin: `ring=0 predict=120` in every window,
      `pct_R` 2.6%/0.3% (fast again, and rbe back in shadow with `veto=0`),
      cushion pinned negative because the metric moved by C while the physics
      did not. Fixed by anchoring the prediction path to the same margin:
      `remote_sim_frontier -= C`, prediction window clamped to
      `SSB64_NETPLAY_REAL_DELAY_PREDICT_WINDOW` (default **1**, `0` = never
      predict), and the ZERO-DELAY anti-stall window widening
      (`RollbackEffectivePredictionWindow`) disabled under the flip — it
      widens by exactly the deficit we are trying to hold. Deadlock-free for
      any `C < D`: mutual stall needs `S_local > S_peer + (D−C)` **and**
      `S_peer > S_local + (D−C)`, whose sum is `0 > 2(D−C)`.
      **Second follow-up: C must be affordable, and its wait must be bounded.**
      With the anchor live the next soak stalled 92% of frames on both peers
      before the intro countdown. Cause: the algebra assumed `hr = S_peer + D`,
      but `hr` is the highest *received* row, so it lags by the one-way
      transit — real slack is `D − C − transit`. That session negotiated
      **D = 2**; with C = D/2 = 1 and ~1 tick of transit, slack was 0 and
      neither peer could advance (production is coupled to consumption, so the
      stall was self-sustaining even though mutual *deadlock* is impossible).
      Two corrections:
      - **C is transit-aware and conservative**: default `D ≥ 3 ? D−2 : 0`,
        reserving 2 ticks for transit + frame jitter. D ≤ 2 asks for no
        margin at all (Phase 1 behaviour). D = 4 → C = 2, D = 6 → C = 4.
      - **The cushion wait is bounded**: after
        `SSB64_NETPLAY_REAL_DELAY_CUSHION_STALL_MAX` (default 2) consecutive
        cushion-driven holds, admission falls back to the true frontier. A
        mis-tuned C now costs at most N frames per drift event instead of a
        whole session.
      Corollary worth recording: **margin is bought with D, not with waiting.**
      `C < D − transit` is a hard budget, so a LAN session at D = 2 has no
      margin to hold; getting one means raising D (i.e. the Phase 3b adaptive
      controller), not tightening admission.
      Remaining: soak-verify the C equilibrium at D ≥ 4; then tier-3 soak;
      then re-derive the RTT→D bands from measured margin.

