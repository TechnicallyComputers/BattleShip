# LEDGER_REFRESH hammer bypassed StickReplace contract (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** input contract / resim tax  
**After:** [jibaku absorb retire](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md) (invent stability next); [micro deadband](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md); [predicted never micro-skips](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)

## Symptom

Soak `1065668144` (and peers): short-span resim storm dominated by `hold_last` → `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` → `QueueOrWiden`. Sample micro deltas (`82,-10→81,-13`, `84,-1→84,-3`) still queued GGPO even though admit/wire paths honor completed-sim ±micro Promote-only.

## Root cause

`syNetInputRefreshPublishedFromAuthorityLedger` treated **any** published≠ledger gameplay delta on a completed tick as mandatory GGPO ("regardless of deadband significance"), calling `QueueOrWiden` without `syNetInputStickReplaceNeedsRewind`. That bypassed the portable frame-delta contract used everywhere else (admit, late wire, pre-promote).

## Fix (`port/net/sys/netinput.c`)

| Layer | Behavior |
|-------|----------|
| Stick delta + `sim_now > sim_tick` | Queue only when `StickReplaceNeedsRewind(published, ledger)` |
| BRANCH_DEFERRED same-stick | Still force rewind (sticks equal; owner Dash / peer Turn) |
| Skip telemetry | `LEDGER_REFRESH_COMPLETED_SIM_CORRECT … skipped class=<Classify>` |
| Predicted hold_last | Unchanged — never micro-skips (soak `740113729`); those still GGPO |

## Acceptance

Matched APK + Linux:

- Confirmed→ledger same-intent Δ≤micro: Promote + `skipped class=micro_stick` (or ledger skip line); **0×** Queue for that class
- Predicted hold_last→wire micro: still short `resim begin` (policy)
- Feel-0 release / buttons / sign flip: still `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` + GGPO
- BRANCH_DEFERRED same-stick: still queues

## Follow-on (Phase 2 — invent)

Most soak ledger GGPOs are **predicted** same-sign / flip after late wire — contract skip cannot absorb them. Next: same-intent invent soft refresh so first-pass matches tick wire before sim when the ring already has the row.

## Related

- [`netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md`](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
- [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)
- [`netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md`](netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md)
