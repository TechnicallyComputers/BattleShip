# Zero-onset hold-neutral → host R-stall / both-character lag (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1454017460` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / admission pacing  
**After:** [zero-onset predict runway v10](netplay_zero_onset_predict_runway_peer_2026-07-20.md)

## Symptom

Basic movement feels laggy on **both** characters. Not a feel-0 local-HID regress — shared sim hitch.

| Peer | Admission | Stall | Notes |
|------|-----------|-------|-------|
| Android | `pct_R=0` | `zero_onset_stall` ×21 | Initiates short GGPO |
| Linux | **`pct_R≈22.6%`** | `zero_onset_stall` ×58–97 + `wire_need` | Host freezes shared commit |

## Root cause

v10 `HardStall≡Restrict` treats any predict past confirmed wire with near-neutral `last_confirmed` as invent-(0,0)-through-onset (Wait vs Walk/Turn PEER class).

On soak `1454017460`, Android local stick stayed at `(0,0)` after Go while Linux moved. Every Linux predict step with `last_conf_sx/sy=0` and age 1–2 hard-R’d the **whole** sim — including the local feel-0 fighter. Stall samples all matched Android `STICK_SAMPLE … sx=0 sy=0`.

Go onset PEER (age≫pred_win) is a different case: stall@391 had `last_conf=384` age=7.

## Fix (v11)

In `syNetInputRemoteHumanZeroOnsetPredictRestrict`: if `last_confirmed` is valid near-neutral and `sim_tick - last_conf.tick ≤ pred_win` (floor 2), **do not Restrict** — invent `(0,0)` is hold_last of idle. Stale neutral (age > pred_win) and missing baseline still Restrict/HardStall. Peek / last_nn / non-neutral hold paths unchanged.

Soft GGPO on true onset through hold-neutral invent remains accepted (same tradeoff as analog_ramp hold_last).

## Acceptance

Matched APK + Linux, one or both sticks idle/moving after Go:

- Host `pct_R` ≪ 22% (target single-digit on LAN)
- Grep: few/no `zero_onset_stall` while remote `last_conf` age ≤ pred_win and sticks stay `(0,0)`
- Go / stale-confirm onset: still see `phase=restrict` when age > pred_win (no Wait vs Turn invent PEER)
- Dual-stick motion: no multi-second hang; soft GGPO OK

## Related

- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — v10 HardStall≡Restrict
- [`netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md`](netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md) — hold_last vs hard R on ramp
- [`netplay_stick_r_stall_sampling_holes_2026-07-12.md`](netplay_stick_r_stall_sampling_holes_2026-07-12.md) — R-stall → HID holes
