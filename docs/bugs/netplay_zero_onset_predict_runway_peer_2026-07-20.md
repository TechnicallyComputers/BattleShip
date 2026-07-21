# Zero-onset predict runway → PEER at dual-stick Go (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `871504438` seed `4075570583` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE`

## Symptom

| Signal | Detail |
|--------|--------|
| First mismatch | sim_state tick **402** `figh`/`anim` |
| Status | Android P1 **12** (Walk) + stick `(30,29)` vs Linux P1 **10** + `(0,0)` |
| GGPO | Linux queues correction for P1@402 only at frontier **410** (`pred=1` → wire `30,29`) |
| Kill | `PEER_SNAPSHOT_DIVERGE load_tick=412` after seal wait / dual onset storm |
| Scan | `RESIM_STICK_FORK` @402–409 host smash vs guest zero (first-pass truth) |

Prior `REPLACE_REJECT_NEUTRAL_DOWNGRADE` did not apply — zeros came **first**, smash **late**.

## Root cause

1. Linux `REMOTE_PUBLISH_SKIP wire_neutral` for P1@402–409 while inventing predicted `(0,0)` past remote `hr` under a full phase_lock predict window.
2. Android applied local stick immediately → Wait→Walk at 402 → hard figh fork before any GGPO.
3. Mid-heal: `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` wrote `(62,9)→(0,0)` @413; `EPISODE_SEAL_ROWS_WAIT` raced peer baseline compare.

## Fix

| Layer | Change |
|-------|--------|
| Predict admit | `syNetInputRemoteHumanZeroOnsetPredictRestrict` — when inventing remote `(0,0)` onset, cap shared-commit `effective_window` and Advance `runway_cap` to **D+1** (off during Wait / post-Go soft pacing). |
| Ledger | `LEDGER_REJECT_NEUTRAL_DOWNGRADE` / `LEDGER_REFRESH_REJECT_NEUTRAL_DOWNGRADE` — mirror wire REPLACE reject. |
| PEER kill | Suppress `PEER_SNAPSHOT_DIVERGE` stop while `resim_seal_wait` or stick-absorb window is active (bounded; deeper exhaust still fail-closes). |

## Acceptance (re-soak)

Dual sticks at Go, `D=2`, onset + strict-input logs on:

- No 8-tick zero-predict runway: Linux should R-stall / pump until wire onset (or ≤D+1 invent)
- No first-pass status 10 vs 12 at stick onset for the same tick
- No `LEDGER_REFRESH` smash→`(0,0)` on completed sim
- Soft recovery may still GGPO once; PEER kill must not fire mid-`EPISODE_SEAL_ROWS_WAIT`
- Do **not** SoftLip-harden

## Related

- [`netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md`](netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md) — smash→neutral REPLACE poison
- [`netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md`](netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md) — soft onset peek
- [`netplay_post_go_wire_need_hang_2026-07-18.md`](netplay_post_go_wire_need_hang_2026-07-18.md) — why onset stall stays off during post-Go grace
