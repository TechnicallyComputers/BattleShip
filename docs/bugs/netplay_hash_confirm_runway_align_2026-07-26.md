# Hash-confirm StickReplace + runway cap align (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** invent / portable GGPO stick + pacing  
**After:** [pre-sim invent + continuity](netplay_presim_invent_confirm_without_rewind_2026-07-26.md); [predicted micro skip ban](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)

## Symptom

Soak `seed=4218591584` (~2758 ticks): invent/continuity felt better (`resim` ~7.2/100 vs ~8.7) but:

- `skipped_continuity` = 0 — almost all ledger REPLACE was still **predicted** `hold_last` → wire (med lateness 2)
- ~24 Android micro + ~36 same-intent ≤12 ledger GGPOs still rewound (740113729 ban)
- Log `ahead≈-7` looked like deep predict; sim was actually on the **confirmed** frontier

## Root cause

1. **Predicted → wire** never Promote-only on deadband alone (correct after soak `740113729`). Continuity only helps **confirmed** rows.
2. **`remote_cap = remote_sim + D + PL`** double-counted D (already removed in `DelaySim`). Matched cadence logged `ahead = tick - (hr+PL) ≈ -(D+PL) ≈ -6…-7` while `tick ≈ remote_sim`. SharedCommit predict admit was already `remote_sim+PL`.

## Fix

### A. Hash-equality confirm-without-rewind (`netinput.c`)

For completed-sim **predicted** same-intent stick REPLACE within continuity deadband, Promote-only when:

- snapshot committed at `sim_tick` with stored subsystem hashes
- `syNetRollbackGetLastFrameCommitStateAgreedTick() > sim_tick` (FC already matched peer on first-pass state)

Fail-closed otherwise (same as always-rewind). Killswitch: `SSB64_NETPLAY_STICK_REPLACE_HASH_CONFIRM=0`.

Telemetry: `skipped class=hash_confirm`, `GGPO_CLASS_SUMMARY … skipped_hash_confirm=`.

Symmetric FC agreement avoids asymmetric silent Promote (the 740113729 failure mode).

### B. Runway / log align (`netpeer.c`, `netinput.c`, `netrollback.c`)

| Piece | Change |
|-------|--------|
| `syNetPeerGetRemoteSimRunwayCap()` | `DelaySim(hr) + phase_lock` |
| AdvanceAllowed / SharedCommit hard cap / resim cap | use that helper |
| `sim_state_tick` | `ahead = tick - remote_cap`; add `predict_depth = tick - remote_sim` |

At matched confirmed: `predict_depth≈0`, `ahead≈-PL` (e.g. −4). Full predict window: `predict_depth≈+PL`, `ahead≈0`.

## Acceptance

- Prerequisite: agreed watermark advances in time for T+2 REPLACE — [snap agree](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md) (`snap_agree matched>0`); FC pairing grid still required for diverge ([pairing grid](netplay_frame_commit_pairing_grid_2026-07-26.md))
- `skipped_hash_confirm` / `class=hash_confirm` > 0 on predicted micro/continuity ledger path
- Ledger GGPO count ↓ especially ≤12 same-intent; flips/release still rewind
- `predict_depth` median near 0 (not confused with old ahead≈−7)
- 0× JA SoftLip / PEER class from hash_confirm (740113729 soak class)
- No new runway R-stall storms vs prior soak

## Env

| Env | Default | Role |
|-----|---------|------|
| `SSB64_NETPLAY_STICK_REPLACE_HASH_CONFIRM` | 1 | Predicted hash-confirm Promote-only |

## Related

- [`netplay_presim_invent_confirm_without_rewind_2026-07-26.md`](netplay_presim_invent_confirm_without_rewind_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
- [`netplay_phase_lock.md`](../netplay_phase_lock.md)
