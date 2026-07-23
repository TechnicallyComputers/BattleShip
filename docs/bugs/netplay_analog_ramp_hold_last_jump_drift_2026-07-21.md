# Analog-ramp hold_last → Jump air-drift (2026-07-21)

**Status:** FIX v2 (`PORT && SSB64_NETMENU`, re-soak)  
**Soaks:**

| Session | Detail |
|---------|--------|
| `1040202646` | Jump-only: Linux hold_last `(-45)`@401 vs owner `(-65)` → figh soft GGPO; scan PASS |
| `1809694209` | Dual-stick after v1: MATCH STABLE but hang ~Go — `analog_ramp_grace` + `zero_onset_dual_hot_grace` hard-locked every D-lag hold tick |

**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract

## Symptom (1040202646)

P1 KneeBend→Jump matched on both peers; first diverge was P1 air-drift stick mag (hold_last freeze), soft-recovered.

## Symptom (1809694209)

Both sticks moving after Go: Android `zero_onset_dual_hot_grace`, Linux `analog_ramp_grace` on held `(-55,-8)` with normal `hr` lead → crawl/hang by ~421, then session end. Sync still STABLE.

## Root cause

**v1** treated any predict past analog `last_confirmed` without peek as a hard R-stall. Holding a stick with `D=2` lag trips that every other tick. Dual-hot zero-onset hard-stalled inside grace even while `hr` advanced.

## Fix v2

| Layer | Change |
|-------|--------|
| Predicate | TRUE only on **true ramp**: peek-ahead stick disagrees with `last_confirmed` (intent or mag > same-intent tol). Peek miss / same-mag → FALSE (allow hold_last) |
| Shared-commit | Post-grace zero-onset invent only → hard R-stall. Grace / dual-hot / true ramp → **D+1 window** only |
| AdvanceAllowed | Same: no `analog_ramp_grace` / `zero_onset_dual_hot_grace` hard locks; D+1 runway |
| wire_need | Restrict invent → pred credit 0; ramp/dual-hot → credit D+1 |
| Promote | Skip hold_last mint only on true ramp (`analog_ramp_tighten`) |
| Diag | `ANALOG_RAMP_PREDICT phase=tighten` (+ peek sx/sy) |

No KneeBend special-case. Unseen ramps without peek may still soft-GGPO (accepted).

## Acceptance (re-soak)

- Dual sticks after Go: no multi-second hang; gameplay continues past grace
- Jump stick ramp: soft GGPO OK; prefer peek fill / D+1 hitch over freeze
- Grep: `phase=tighten` only when peek≠last_conf; no spam `analog_ramp_grace` on holds

## Related

- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — invent `(0,0)` class
- [`netplay_kneebend_jump_exit_witness_2026-07-19.md`](netplay_kneebend_jump_exit_witness_2026-07-19.md) — witness only
