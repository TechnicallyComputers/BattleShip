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
- [ ] **Phase 2** — re-baseline: expect cushion > 0, predict ≈ 0, light
      episodes only on genuine late arrivals; re-derive RTT→D bands; verify
      DELAY_SYNC under new meaning.
- [ ] **Phase 3** — `sRbeRealDelayForced` TRUE; promote rbe tier 2, then
      tier 3 (adaptive D regulating a real cushion). WAN soaks.
