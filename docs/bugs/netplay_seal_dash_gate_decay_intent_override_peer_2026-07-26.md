# Seal INTENT_OVERRIDE on same-sign dash-gate decay → PEER (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `68816755` seed `2828342898` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (`agree_through_load=1`)

## Symptom

Post [auth_stage false zero](netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md) fix: prior signals gone (0× `REPLACE_REJECT` storm, figh matched through **440**). Still PEER.

| Check | Result |
|-------|--------|
| Go `ADVANCE_FORCE` | OK @390 |
| NetSync `figh` @120/240/360/400/440 | Match both peers |
| Kill | Deepen exhaust @**443** — figh-only PEER; map/world match; Android `VS_SESSION_END` @450 |
| Not | `RESIM_SEAL_ROWS_EXHAUSTED` / invent clamp / auth_stage false `(0,0)` |

## Timeline

| Tick | Detail |
|------|--------|
| 442 first-pass | Both peers P1 `(63,10)` status **15** (Dash) |
| Wire 444 | Linux `REPLACE_NEWER` thrash `(63,10)→(18,4)→(63,10)` (sim 442) |
| Android seal | `SEAL_PACK` @442 `src=transmitted` **`(63,10)`** |
| Linux resim | `SEAL_LEDGER_INTENT_OVERRIDE` sealed `(63,10)` → ledger `(18,4)` (dash-gate XOR) |
| | `RESIM_INPUT_SOURCE selected=sealed sx=18` — played poisoned stick |
| 443 | `BASELINE_UNIVERSE` / deepen exhaust → PEER |

## Root cause

1. **Wire thrash** — a later packet rewrote strict-confirmed smash `(63,10)` to same-sign sub-dash `(18,4)` for wire 444 / sim 442, poisoning the authority ledger.
2. **`StickSealIntentDisagree` false positive** — treated **same-sign dash-gate decay** (`|sx|≥56` → `|sx|<56`) as intent disagree (because `DashGateDisagreeX`). `SEAL_LEDGER_INTENT_OVERRIDE` then preferred poisoned ledger over the owner’s seal `(63,10)`.
3. Original override (soak `582675261`) needed **opposite smash** (`−66` vs `+78`), which already fails `SameAnalogIntent` — dash-gate XOR alone was overly broad.

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| `StickSealIntentDisagree` | Opposite analog intent when both analog; dash-gate XOR only when one side is non-analog |
| Wire `REPLACE_NEWER` | Refuse same-sign smash→sub-dash revision (`REPLACE_REJECT_DASH_GATE_DOWNGRADE`); still allow flips / hard-zero (stall-gated) |

## Acceptance

Dual-stick Dash/Turn window with mag decay on the stick:

- Owner seal `(≥56)` not stomped by ledger same-sign `(<56)` via INTENT_OVERRIDE
- No `REPLACE_NEWER` smash→sub-dash same-sign for an already-strict wire tick
- Soft protocol GGPO OK; 0× deepen-exhaust PEER from this seed
- Opposite-smash seal vs ledger (`−66` vs `+78`) still overrides to ledger

Rebuild desktop **and** Android APK before re-soak.

**Residual soak `985824253`:** INTENT_OVERRIDE / dash-gate REPLACE family cleared (sealed resim heals); PEER@415 from soft onset ahead+floor invent — [`netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md`](netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md).

## Related

- [`netplay_seal_pack_latch_turn_dash_soft_nz_2026-07-20.md`](netplay_seal_pack_latch_turn_dash_soft_nz_2026-07-20.md) — original INTENT_OVERRIDE (opposite smash)
- [`netplay_seal_local_intent_physics_fork_2026-07-20.md`](netplay_seal_local_intent_physics_fork_2026-07-20.md) — local vs remote seal intent
- [`netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md`](netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md) — prior soak family (cleared)
- [`netplay_wire_resend_gap_restage_revision_2026-07-20.md`](netplay_wire_resend_gap_restage_revision_2026-07-20.md) — owner wire self-revision class
- [`netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md`](netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md) — post-fix residual
