# Pre-sim invent refresh + confirm-without-rewind (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** invent / portable GGPO stick contract (resim tax)  
**After:** [same-intent soft refresh](netplay_hold_last_same_intent_soft_refresh_2026-07-26.md); [ledger StickReplace](netplay_input_contract_ledger_stickreplace_2026-07-26.md); [predicted micro skip ban](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)

## Symptom

Post–Phase 3 soak (`1471077218`): Turn/`lr_dash` scrub class cleared, but lag feel remained. Drivers were portable:

- ~8.7 `resim begin` / 100 ticks; ~63% of resims ±2 of ledger REPLACE
- Soft refresh fired, but **100% resim-adjacent** — documenting corrections, not preventing first-pass invent error
- ~16 micro + ~36 same-sign-large ledger GGPOs still paid rewind on confirmed rows that contract could Promote-only

## Root cause

1. **Invent lookback too short** — same-intent recent follow capped at `min(D, 4)`. With deep predict (`ahead ≈ −phase_lock`), ring mags for `tick-1..` often sat outside that window → first-pass hold_last stayed on stale `last_confirmed` → later ledger REPLACE.
2. **Confirm always rewound mag drift** — completed-sim StickReplace Promote-only only covered ±micro (3). Confirmed same-intent continuity (±4..12) still queued GGPO.
3. **Soft-refresh telemetry was Promote-path only** — battle `Republish` remints via Resolve+Publish and never tagged pre-sim refresh.

Predicted→wire must still rewind even for ±micro (soak `740113729` JA PEER). Pre-sim invent is the cut for that tax; continuity Promote-only is **confirmed** completed-sim only.

## Fix (`port/net/sys/netinput.c`)

| Layer | Behavior |
|-------|----------|
| FillHoldLast lookback | `D + phase_lock`, capped at 8 (`hold_follow_recent` / `smash_follow_recent`) |
| Pre-sim pump window | Promote through the same horizon before battle sim |
| Republish telemetry | `source=hold_last_presim_refresh` when predicted History reminted before first-pass |
| StickReplace completed-sim | Confirmed same-intent within **continuity deadband** (default 12, env `SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_CONTINUITY_DEADBAND`) → Promote-only; still no skip on predicted; dash-gate disagree / release / buttons still rewind |
| Completed-intent promote | Gates `QueueOrWiden` through `StickReplaceNeedsRewind` (same contract as ledger) |
| Telemetry | `skipped class=same_intent_continuity`; `GGPO_CLASS_SUMMARY` includes `skipped_continuity` |

## Explicitly not in this pass

- Move absorb / TryBegin defer batches / status-specific GGPO skips
- Kirby PKThunderTrail crash (separate weapon-restore class)

**Follow-on (implemented):** [hash_confirm + runway align](netplay_hash_confirm_runway_align_2026-07-26.md) — predicted Promote-only when FC agreed; `remote_cap = remote_sim+PL` + `predict_depth`.

## Acceptance

Matched APK + Linux soak:

- `hold_last_presim_refresh` / `*_follow_recent` appear **before** `resim begin` on the same tick more often than resim-adjacent soft_refresh
- `skipped_continuity` / `skipped class=same_intent_continuity` > 0 on confirmed completed-sim; predicted micro still rewinds
- `resim begin` / 100 ticks ↓ vs soak `1471077218` (~8.7); ledger↔resim overlap ↓ from ~63%
- 0× Phase 3 Turn `lr_dash` / `STATUS_FORK` regression; 0× ahead invent (`*_flip_ahead` / `*_release_ahead`)

## Env knobs

| Env | Default | Role |
|-----|---------|------|
| `SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_MICRO_DEADBAND` | 3 | Confirmed noise floor Promote-only |
| `SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_CONTINUITY_DEADBAND` | 12 | Confirmed same-intent mag continuity Promote-only (≥ micro) |

## Related

- [`netplay_hold_last_same_intent_soft_refresh_2026-07-26.md`](netplay_hold_last_same_intent_soft_refresh_2026-07-26.md)
- [`netplay_input_contract_ledger_stickreplace_2026-07-26.md`](netplay_input_contract_ledger_stickreplace_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
- [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)
