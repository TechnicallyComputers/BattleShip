# Netplay input contract: completed-sim micro deadband + hold-last onset peek (2026-07-17)

## Symptom

Soak session `1490370675` (Android client / Linux host, seed `1782441740`) reached ~4613 ticks
with healthy synctest (38 OK, 0 FAIL) after the correction-cascade deepen fixes, but still paid
**~17 wire GGPO / 22 resims**. Many were ±1–3 same-intent stick REPLACE (`70/60→71/59`,
`-80/20→-80/21`) or hold-last **hard zero** vs remote onset (`sx=0 sy=0 pred=1` → stick).
Session later failed on FC@4311 `[figh]` inputs MATCH (Hold `topn_ty` — separate cross-ISA
surface); the input contract tax was the architectural target here, not an FC fold patch.

## Root cause

1. **`syNetInputStickReplaceNeedsRewind` completed-sim hammer** — after
   [`netplay_feel0_release_deadband_skips_ggpo`](netplay_feel0_release_deadband_skips_ggpo_2026-07-12.md),
   any gameplay delta on an already-simulated tick returned TRUE. That correctly kept feel-0
   release (`13,4→0,0`) from silent Promote, but also opened span-2 episodes for micro stick noise.
2. **Episode-FSM hold-last hard zero** — with `RemoteHumanAuthoritativeOnly`, unresolved remote
   rows copy `last_confirmed` sticks. After a true neutral confirm, that is `(0,0)` even when
   send-lead wire for the onset already sits in the ring → guaranteed GGPO on wire admit.

## Fix (`PORT && SSB64_NETMENU`)

| Change | Where | Behavior |
|--------|--------|----------|
| **Completed-sim micro skip** | `StickReplaceNeedsRewind` | Buttons / release / non-micro stick still rewind. Same-intent analog with both axes Δ ≤ `COMPLETED_SIM_MICRO_DEADBAND` (default **3**) Promote only + `skipped class=micro_stick` log. |
| **Hold-last soft onset peek** | `ResolveRemoteHumanAuthorityFrameEx` + predict path | When hold-last sticks are near-neutral, peek wire onset (`TryPeekRemoteAnalogForOnset`) and publish clamped soft onset — no invent from `last_non_neutral` history. |
| **GGPO class telemetry** | queue log + `GGPO_CLASS_SUMMARY` on VS stop | `class=button\|onset_from_zero\|release\|real_stick\|micro_stick`; sync-report / scan-drift aggregate. |

Coalesce + deepen-load live-cap behavior unchanged.

## Env knobs

| Variable | Default | Role |
|----------|---------|------|
| `SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_MICRO_DEADBAND` | 3 | Max per-axis Δ for completed-sim same-intent Promote-only (0 disables skip) |
| `SSB64_NETPLAY_ANALOG_ONSET_LOG` | off | Also logs `hold_last_soft_onset` peeks |

## Verify

- Re-soak Android ↔ Linux; expect fewer span-2 resims, `skipped_micro` > 0, queued `micro_stick` ≈ 0.
- Feel-0 mid-air release still queues `class=release` / completed-sim rewind (not silent Promote).
- Sync-report line: `ggpo_queued=… onset=… real_stick=… skipped_micro=…`
- FC inputs-MATCH Hold physics forks may still appear — out of scope for this contract change.

## Related

- [`netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md`](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — soft onset lookback after release invents floored pre-release sticks (deepen: ahead-only)
- [`netplay_correction_cascade_deepen_load_asym_2026-07-16.md`](netplay_correction_cascade_deepen_load_asym_2026-07-16.md)
- [`netplay_feel0_release_deadband_skips_ggpo_2026-07-12.md`](netplay_feel0_release_deadband_skips_ggpo_2026-07-12.md)
- [`netplay_stick_replace_policy_consolidate_2026-07-12.md`](netplay_stick_replace_policy_consolidate_2026-07-12.md)
- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md)
