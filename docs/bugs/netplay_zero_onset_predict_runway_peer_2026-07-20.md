# Zero-onset predict runway → PEER (2026-07-20)

**Status:** FIX v3 (`PORT && SSB64_NETMENU`, re-soak)  
**Soaks:**

| Session | Detail |
|---------|--------|
| `871504438` | Original Go onset: Linux invented remote P1 `(0,0)` ×8 while Android Walk'd @402 → PEER@412 |
| `979771282` | Mid post-Go grace: Android invented remote P0 `(0,0)` @415–422 while Linux `LOCAL_PUBLISH` onset `(19,4)`@419 / `(77,-1)`@420 → soft Wait vs Turn@420 → PEER@480 |
| `250667155` | After v2: scan PASS but sync UNSTABLE — soft `STATUS_FORK@406` (owner stick vs invent `(0,0)`); ~50 resims; dual-spam `wire_need` hang |

**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / `PEER_SNAPSHOT_DIVERGE` (not KneeBend gameplay)

## Symptom (979771282)

| Signal | Detail |
|--------|--------|
| Grace | `post_go_wire_pacing_grace until=423` (covers entire onset window) |
| Owner (Linux P0) | `LOCAL_PUBLISH (19,4)`@419 → `(77,-1)`@420 → Turn |
| Remote (Android P0) | `HISTORY prediction (0,0)` + `REMOTE_PUBLISH_SKIP wire_neutral` @415–422 → stayed Wait |
| GGPO | Correction for @419 only at frontier 423 — after status fork |
| Scan | Soft `STATUS_FORK_RECOVERED@420`; durable `PEER@480` (cascade) |

Lockstep never forked: same pads ⇒ same Wait→Turn. Rollback invent of predicted zeros was the contract break.

## Root cause

**v1→v2:** Restrict early-out during post-Go grace + soft pacing skipped runway cap → invent through owner onset.

**v2→v3 (250667155):** D+1 still admitted invent inside grace for dual-stick onset; `wire_need` subtracted full `pred_window` while inventing zeros → leader ran ahead then hung; Promote still minted History `(0,0)` under Restrict.

## Fix v3

| Layer | Change |
|-------|--------|
| Restrict | Unchanged predicate (would invent hard zero); still active in grace |
| Dual-hot | `syNetInputDualStickHotPredictTighten`: local stick hot + (Restrict or remote hot) |
| Shared-commit | Post-grace Restrict **or** dual-hot Restrict in grace → hard R-stall + ingress pump (`phase=stall`). Grace-only Restrict keeps D+1. Dual-hot alone shrinks window to D+1 |
| AdvanceAllowed | Matching hard-stall / D+1 / dual-hot runway; `wire_need` pred credit = 0 under Restrict, D+1 under dual-hot |
| Promote | Skip hold-last hard-zero History mint when Restrict (`reason=zero_onset_stall`) |
| Grace | `wire_need` remains soft (ICE hr cover); invent path no longer credits full phase_lock |

## Diagnostics

```text
SSB64 NetInput: ZERO_ONSET_PREDICT phase=invent …
SSB64 NetInput: ZERO_ONSET_PREDICT phase=restrict …
SSB64 NetInput: ZERO_ONSET_PREDICT phase=stall tick=… grace=… dual_hot=…
SSB64 NetInput: sim advance blocked (zero_onset_stall|zero_onset_dual_hot_grace|runway_cap_zero_onset_grace) …
SSB64 NetInput: REMOTE_PUBLISH_SKIP … reason=zero_onset_stall
```

## Acceptance (re-soak)

Dual sticks, `D=2`, onset after Go + stick spam:

- Both peers log `phase=restrict` / `phase=stall` for the **remote** of the onset player (binary parity)
- No multi-tick `phase=invent` runway while owner `LOCAL_PUBLISH` is non-zero
- Soft Wait vs Turn / Walk at onset should not be first-pass status fork
- Dual-spam: no multi-second `wire_need` hang; resim count ≪ 50
- Soft recovery / single GGPO OK; PEER must not be seeded by zero-onset invent
- Do **not** SoftLip-harden; do **not** special-case KneeBend for this class

## Related

- [`netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md`](netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md) — post-onset hold_last mag freeze (not zero invent)
- [`netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md`](netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md)
- [`netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md`](netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md)
- [`netplay_post_go_wire_need_hang_2026-07-18.md`](netplay_post_go_wire_need_hang_2026-07-18.md) — why wire_need stays soft during grace
- [`netplay_branch_sensitive_predict_2026-07-20.md`](netplay_branch_sensitive_predict_2026-07-20.md) — branch-eval backstop (orthogonal; input contract is primary)
