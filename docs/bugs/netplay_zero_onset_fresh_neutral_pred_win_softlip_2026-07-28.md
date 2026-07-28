# Zero-onset fresh-neutral allow age ≤ pred_win → SoftLip / FC inputs_agree=0 (2026-07-28)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 seed `3839642009` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / SoftLip / `FRAME_COMMIT`  
**After:** [zero-onset hold-neutral R-stall v11](netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md)

## Symptom

FC@1134 `FRAME_COMMIT_INPUT_AGREE_ONSET` `onset=1125` `inputs_agree=0` with escalate bypass; P0 SoftLip status 33 vs 30 at snap 1133. Later PEER@2826 figh+anim (map matched).

## Timeline

| Tick | Detail |
|------|--------|
| 1118–1124 | Linux `ZERO_ONSET_PREDICT` invent `(0,0)` for remote P1; `last_conf` near-neutral advances with D=2 / `pred_win`=4 |
| **1125** | Android owner P1 gameplay `(0,-31)`; Linux invent `(0,0)` (`last_conf=1122` age=3 ≤ pred_win) |
| 1125 | Aggregate `figh` still matches; **P1 `fhash_light` already forks** (`0x16BD1793` vs `0xEDD9AEAF`) |
| 1126 | First `figh` DIFF — Android `(5,-81)` vs Linux still inventing `(0,0)` |
| 1125→1126 | Linux GGPO + light span-1 (`LIGHT_WIRE_READY_CLAMP`); post `figh=0x9E6154F1` ≠ Android `0xDF699C9F` |
| 1126→1129 | Second light with correct wires; SoftLip residue / P0 path stays forked |
| 1130 | Android also hold_last P0 `(-23,-79)` vs owner `(-6,-83)` (secondary invent skew on poisoned universe) |
| 1134 | FC state diverge + `inputs_agree=0` |

## Root cause

v11 allowed hold-neutral invent `(0,0)` when `sim_tick - last_conf.tick ≤ pred_win` (floor 2, often 4). That covers idle age 1–2 (good — avoids host `pct_R≈23%`), but also the **third predict slot past confirmed wire** where a soft analog onset (`|sy|=31`, past deadband) is still in flight.

Soft GGPO + light span-1 is the accepted recovery for that invent class — on Dream Land SoftLip it did not converge (`RESIM_BASELINE_WIRE_SKIP` light; exclusive frontier invalidate; FC still saw input digest skew).

## Fix (v12)

In `syNetInputRemoteHumanZeroOnsetPredictRestrict` (`port/net/sys/netinput.c`):

- Fresh-neutral invent allow age = **committed input delay `D`** (floor 1), not `pred_win`.
- Idle age 1–2 with D=2 still invents (v11 R-stall fix preserved).
- Age 3…pred_win HardStalls / Restricts so ingress can land the soft-onset wire before inventing hard zero.

## Acceptance

Matched APK + Linux, Dream Land dual-stick:

- No `ZERO_ONSET_PREDICT phase=invent` at `last_conf` age `> D` while remote soft-onset is about to land
- Host `pct_R` stays well below v10’s ~23% on idle remote (age ≤ D invent OK)
- This seed class: no FC@~1134 `inputs_agree=0` from P1 zero-vs-soft onset; SoftLip P0 status fork at 1130 absent when onset stalls for wire

## Related

- [`netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md`](netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md) — v11 pred_win allow (tightened here)
- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — HardStall≡Restrict runway
- [`netplay_hold_last_nn_reinflate_after_release_2026-07-28.md`](netplay_hold_last_nn_reinflate_after_release_2026-07-28.md) — adjacent hold_last invent SoftLip
- [`netplay_deepen_resim_map_save_phase_skew_peer_2026-07-28.md`](netplay_deepen_resim_map_save_phase_skew_peer_2026-07-28.md) — separate map PEER class (pending)
