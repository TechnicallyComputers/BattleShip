# PK Thunder Hold aim protect → wpn poison → BASELINE spike (2026-07-27)

**Status:** FIX v2 IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soaks:**

| Session | Detail |
|---------|--------|
| `932522105` | Uncapped protect + `SameAnalogIntent` `>8` Y-flip → BASELINE |
| `250129717` | After v1 (continuity cap): Δ4–11 still Promote-only each tick → BASELINE ~785–790 |

**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / Hold aim invent  
**After:** [hold same-intent GGPO](netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md); [hold-neutral R-stall](netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md)

## Symptom

Basic movement lag fixed (`pct_R` 22.6% → 1.5%). Big hitch on **air PK Thunder Hold** aim (~1066–1080): both characters rubberband.

## Timeline (Linux host, P1 Hold `status=233`)

| Tick | Event |
|------|--------|
| 1067+ | `REMOTE_PUBLISH … source=hold_last` each predict step |
| 1067–1072 | Wire aim `(67,42)→(62,19)→…→(61,8)→(61,-9)` — Y crosses deadzone |
| same | `ness_jibaku_stick_protect` Promote-only (no short GGPO) |
| 1072–1073 | `BASELINE_UNIVERSE_MISMATCH` P1 Hold — `figh` fork, inputs agree through load |
| 1070–1080 | Deepen / multi-epoch resim — felt spike |

## Root cause

1. **Uncapped Hold protect** — same-intent StickReplace/LEDGER Promote-only for entire Start/Hold/End with no mag cap. Large aim invent left first-pass thunder head wrong; peers diverged on `wpn` until universe deepen.
2. **`SameAnalogIntent` used `> 8`** — `(61,8)→(61,-9)` treated as same-intent (`|sy|==8` skipped the sign check), so Y flips also Promote-only.

Short Hold GGPO is cheaper than BASELINE deepen.

## Fix

| Version | Change |
|---------|--------|
| v1 | `SameAnalogIntent` `>= 8`; Hold protect capped at `continuity_db` (12); post-jibaku uncapped |
| **v2** | Hold protect capped at **`micro_db` (3)**; Hold + Δ>micro skips confirmed continuity / hash_confirm Promote (must short-GGPO). Post-jibaku uncapped unchanged. |

## Acceptance

Matched APK + Linux, long air Hold with active aim:

- Hold aim Δ>~3 same-intent → short `resim begin` / `QueueOrWiden` (not multi-tick Promote cascade)
- Prefer **0×** `BASELINE_UNIVERSE` mid-Hold from invent→protect alone
- Only **confirmed** noise ≤`micro_db` still `class=ness_jibaku_stick_protect` during Hold
- Predicted Hold invent → wire (any Δ) short-GGPOs — see [predicted micro protect](netplay_pkthunder_hold_predicted_micro_protect_softlip_2026-07-27.md)
- Post-jibaku entry large same-intent still Promote-only (4173754130 class)
- No `HASH_CONFIRM_FORCE_GGPO` during Hold
- Pacing: host `pct_R` stays low (hold-neutral fix)

## Related

- [`netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md`](netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md) — uncapped Hold protect (partially narrowed here)
- [`netplay_snap_agree_wpn_hold_aim_2026-07-26.md`](netplay_snap_agree_wpn_hold_aim_2026-07-26.md) — wpn in SNAP_AGREE
- [`netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md`](netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md) — pacing fix that cleared the field for this spike
