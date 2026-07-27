# Hold-last same-intent soft refresh (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** invent / resim tax  
**After:** [ledger StickReplace contract](netplay_input_contract_ledger_stickreplace_2026-07-26.md); [tick-wire mag follow](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md); [ahead invent retire](netplay_hold_last_flip_ahead_peer_2026-07-26.md)

## Symptom

Soak ledger GGPOs are almost all `hold_last` → late `ledger_wire` (typically `sim_now = tick+2`). Ticks that **re-published** hold_last with updated sticks before/with wire paid **0×** LEDGER_REFRESH; ticks with a single stale hold_last paid the correction storm.

## Root cause

1. **Completed-sim promote skip** only allowed tick-wire re-mint when `StickSealIntentDisagree` (opposite intent). Same-intent mag follow on completed ticks was `hold_last_completed_sim` skipped — History stayed stale until ledger hammered GGPO (or silently mismatched).
2. **Tick wire missing at invent** — hold seeded from `last_confirmed` even when a newer same-intent row for `tick-1..` already sat in the ring.

## Fix (`port/net/sys/netinput.c`)

| Layer | Behavior |
|-------|----------|
| FillHoldLast | Same-intent **recent lookback** (`hold_follow_recent` / `smash_follow_recent`) when tick wire missing — delay window, no release/flip from lookback |
| Completed promote | Tick-wire followed onto resolved + predicted published disagrees → promote + GGPO for **opposite intent and same-intent** mag |
| Telemetry | Pre-sim re-mint logs `source=hold_last_soft_refresh` |

Predicted→wire still never micro-skips (soak `740113729`). Late wire with no ring row at invent still short-GGPO (accepted).

**Follow-on:** soak showed soft_refresh mostly resim-adjacent — [pre-sim invent + confirm-without-rewind](netplay_presim_invent_confirm_without_rewind_2026-07-26.md) widens lookback and adds confirmed continuity Promote-only.

## Acceptance

Matched APK + Linux:

- `hold_last_soft_refresh` / `*_follow_recent` appear when ring has newer same-intent before first-pass
- Completed same-intent tick-wire follow → `hold_last_completed_intent` + short resim (not silent History rewrite)
- 0× ahead `*_flip_ahead` / `*_release_ahead`
- Expect fewer `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` on dash-dance holds when `tick-1` wire beats stale `last_confirmed`

## Related

- [`netplay_input_contract_ledger_stickreplace_2026-07-26.md`](netplay_input_contract_ledger_stickreplace_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
- [`netplay_hold_last_flip_ahead_peer_2026-07-26.md`](netplay_hold_last_flip_ahead_peer_2026-07-26.md)
