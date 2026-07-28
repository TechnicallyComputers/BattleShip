# Portable input contract (GGPO stick replace) — frozen decision table

**Status:** Phase 1+2 implemented (2026-07-27) — pure core extracted to
[`port/net/sys/netinput_contract.c`](../port/net/sys/netinput_contract.c), SSB64 gates behind a
host vtable. Export target: `recomp-net` rollback mode.

This is the **frozen contract** for "published input row vs late wire row → rewind or
promote?". The core is a standalone C TU with **zero engine includes** (`stdint.h` only): no
fighter status, no rings, no env reads, no logging. Everything game- or engine-specific enters
through `SYNetInputContractParams` (numeric thresholds) or `SYNetInputContractHostGates`
(host callbacks, all optional).

## Decision order (`syNetInputContractStickReplaceDecide`)

For published row `old` vs authoritative/late row `wire` at tick `T`
(`completed_sim` = sim has already advanced past `T`):

| # | Condition | Decision |
|---|-----------|----------|
| 1 | `old` == `wire` (tick, buttons, sticks) and `old` predicted and host `equal_predicted_force_rewind` | **Rewind** (`equal_deferred` — BRANCH_DEFERRED class) |
| 2 | `old` == `wire` | Promote (`equal`) |
| 3 | buttons equal and host `absorb_stick_replace` | Promote (`absorb` — e.g. Dead\* stick cannot affect sim) |
| 4 | completed_sim and buttons differ | **Rewind** |
| 5 | completed_sim and release (analog → neutral / mag-shed, `confirmed_deadband`) | **Rewind** |
| 6 | completed_sim, both analog, same intent, no dash-gate X disagree: host `protect_promote(dx,dy,micro,pred)` | Promote (`protect`) |
| 7 | … same scope, host `block_deadband_promote` false, `old` confirmed, Δ ≤ `micro_deadband` | Promote (`micro`) |
| 8 | … `old` confirmed, Δ ≤ `continuity_deadband` | Promote (`continuity`) |
| 9 | … `old` predicted, Δ ≤ `continuity_deadband`, host `hash_confirm_promote` | Promote (`hash_confirm`) |
| 10 | completed_sim, anything else | **Rewind** |
| 11 | runway (not completed): release | **Rewind** |
| 12 | runway: host `defer_predicted_correction` (onset-ahead) | Promote (`defer` — wait for wire) |
| 13 | runway: correction significant (`confirmed_deadband`, facing flip, large delta, onset-from-neutral) | **Rewind** |
| 14 | else | Promote (`insignificant`) |

Key invariants (soak-derived, do not relax without a new soak):

- **Predicted rows never get a bare deadband promote** (soak `740113729` JA PEER) — only
  `hash_confirm` (peer state watermark agreed past `T`) or a host protect can promote them.
- **Release always rewinds** on both completed-sim and runway paths (feel-0 fork class,
  soaks `2132381039`, `1945843913` mag-shed).
- **Dash-gate X disagree blocks all same-intent promotes** (soak1 `179193526`).
- Same-intent sign conflicts use `same_intent_min_active` (8) with `>=` so |s|==8
  participates (soak `932522105` Y-flip poison).

## Params (numeric only — env-tunable in BattleShip)

| Field | Default | BattleShip env |
|-------|---------|----------------|
| `confirmed_deadband` | 12 | `SSB64_NETPLAY_GGPO_STICK_DEADBAND` |
| `predict_deadband` | 14 | `SSB64_NETPLAY_GGPO_STICK_DEADBAND_PREDICT` |
| `micro_deadband` | 3 | `SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_MICRO_DEADBAND` |
| `continuity_deadband` | 12 (≥ micro) | `SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_CONTINUITY_DEADBAND` |
| `analog_min_mag` | 12 | (`ANALOG_PRED_MIN_MAG` — looks-analog floor) |
| `same_intent_min_active` | 8 | — |
| `same_intent_tolerance` | 14 | — |
| `onset_facing_thresh` | 4 | `SSB64_NETPLAY_ANALOG_ONSET_FACING_THRESH` |
| `onset_large_delta` | 40 | `SSB64_NETPLAY_ANALOG_ONSET_LARGE_DELTA` |
| `dash_gate_min` | 56 (≤0 disables) | — (SSB64 dash product proxy) |
| `digital_axis_mag` | 85 (0 disables) | — (SSB64 keyboard encoding) |

`dash_gate_min` and `digital_axis_mag` are the only game-flavored numbers; a recomp host
that has no smash/dash semantics sets them to 0 and the gate/heuristic vanish.

## Host gates (`SYNetInputContractHostGates`, all optional / NULL = portable default)

| Gate | Portable default | BattleShip binding |
|------|------------------|--------------------|
| `equal_predicted_force_rewind` | never | `syNetplayBranchDeferredNeedsRewind` (Turn/Dash BRANCH_DEFERRED ticket) |
| `absorb_stick_replace` | never | `syNetplayPlayerInDeadGhostStickAbsorbScope` (Dead\* snap@tick) |
| `protect_promote` | never | `syNetplayNessStickReplaceProtectAllowsPromote` (jibaku/Hold protect) |
| `block_deadband_promote` | never | `syNetplayNessSnapTickIsPKThunderHold` (Hold aim fragile window) |
| `hash_confirm_promote` | never (fail closed) | `syNetInputStickReplaceHashConfirmAllowsPromote` (FC state watermark agreed past tick) |
| `defer_predicted_correction` | never | `syNetInputShouldDeferPredictedAnalogCorrection` (onset-ahead ring peek) |

For a master-hash + savestate recomp host: bind only `hash_confirm_promote` (= "peer master
hash agreed through tick") and leave the rest NULL. That is the intended recomp-net cut.

Gates are queried **lazily in decision order** — a host gate is only evaluated when the
portable conditions ahead of it already hold, so gate implementations may log/count freely.

## Also owned by the core

- `syNetInputContractStickReplaceIsRelease` — mag-shed / analog→neutral definition
- `syNetInputContractCorrectionIsSignificant` — runway significance (deadband + facing flip +
  large delta + onset-from-neutral + predicted false-digital heuristic)
- `syNetInputContractClassifyCorrection` — telemetry classes
  (`button` / `release` / `onset_from_zero` / `micro_stick` / `same_intent_continuity` / `real_stick`)
- Predicates: `StickLooksAnalog`, `StickSameAnalogIntent`, `StickDashGateDisagreeX`

`netinput.c` delegates `syNetInputStickReplaceNeedsRewind`, `syNetInputStickReplaceIsRelease`,
`syNetInputGameplayCorrectionIsSignificantEx`, and `syNetInputClassifyGgpoCorrection` to the
core; all logging, counters (`GGPO_CLASS_SUMMARY`), and env caching stay in `netinput.c`.

## Explicitly NOT in the core

- Invent / prediction fill (hold-last, soft onset peek, lookback) — Phase 3 candidate,
  still churning; stays in `netinput.c`.
- Episode execution (light/heavy, seal, baseline, frontier) — `netrollback*`.
- Ledger/History write-once provenance — `netinput.c` / `netinput_timeline.c`.
- Any SSB64 fighter status knowledge — host gates only.

## Export map (recomp-net)

| BattleShip | recomp-net (future) |
|------------|---------------------|
| `netinput_contract.{h,c}` | drop-in TU (`rnet_input_contract.{h,c}`, rename prefix) |
| `SYNetInputContractFrame` | built from opaque `RNetInputSample` via a host stick-layout descriptor |
| `hash_confirm_promote` | master-hash watermark (`state_agreed_through > tick`) |
| other gates | NULL (portable defaults) until a title needs them |

## Related

- [`netplay_input_contract_ledger_stickreplace_2026-07-26.md`](bugs/netplay_input_contract_ledger_stickreplace_2026-07-26.md)
- [`netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md`](bugs/netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md)
- [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](bugs/netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)
- [`netplay_presim_invent_confirm_without_rewind_2026-07-26.md`](bugs/netplay_presim_invent_confirm_without_rewind_2026-07-26.md)
- [`netplay_hash_confirm_runway_align_2026-07-26.md`](bugs/netplay_hash_confirm_runway_align_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](bugs/netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
