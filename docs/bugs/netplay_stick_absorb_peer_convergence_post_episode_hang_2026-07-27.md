# Stick-absorb + peer_convergence hang after episode (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash — seed `3438514102` / session `111803346`  
**Bucket:** hang / admission freeze  
**After:** [multistick correction union](netplay_multistick_correction_union_2026-07-27.md), [absorb peer_convergence runway](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md)

## Symptom

Resim storm under dual-stick (~every 6t in mm 424–580), then hard freeze:

| Peer | Freeze |
|------|--------|
| Both | `sim advance blocked (rollback_epoch_cap=723 source=2)` — `next_sim=724` |
| Linux | `VS_SESSION_END` @723 |

During coalesce Linux deferred target grew **715→724** (frontier tracking).  
`stick_absorb_coalesce` TryBegin fails were live; hang landed **after** last `EPISODE_EXEC` completed (~716).

## Root cause

Prior fix lifted peer_convergence only while `StickAbsorbCoalesceWaiting` (absorb **and** local deferred pending). Gaps:

1. **Absorb window without deferred** — after Begin clears deferred, or post-complete before next REPLACE, `CoalesceWaiting` is FALSE → FSM `source=2` / `PeerEpochTarget` re-cap live.
2. **Notify during absorb** — `NotePeerEpochTarget` still armed PeerEpoch + FSM convergence; when the window expired the cap returned immediately (@723).
3. **Deferred target growth** — `ClampDeferredTarget` only ran when `MultiStickHotActive`; REPLACE widened target through the frontier during coalesce (fat span + peer notify races).
4. **Stale convergence** — once live reached `peer_convergence_target (+ slack)` with no resim/deferred work, nothing cleared the cap.

## Fix

| Layer | Change |
|-------|--------|
| Query | `syNetRollbackStickAbsorbWindowActive()` — until-sim only |
| Live-cap | `ComputePeerEpochLiveCap` / FSM `source=2` honor **WindowActive** (not only CoalesceWaiting) |
| Notify | `NotePeerEpochTarget` no-ops PeerEpoch + convergence while WindowActive |
| Stale | If `source=2` and live ≥ cap with no resim/deferred → `OnPostMatch` + `ClearPeerEpochState` |
| Target | Always clamp deferred target to `mismatch + absorb_window` while coalesce waits; clamp on every QueueDeferred merge |
| Runway | MultiStick / AnalogRamp PredictTighten use WindowActive |

## Acceptance

Matched dual-stick mash after Go:

- No perpetual `rollback_epoch_cap … source=2` after absorb/resim (especially @ end of storm)
- Deferred target does not track frontier during `stick_absorb_coalesce` (no 715→724 class)
- Absorb still coalesces REPLACE; soft GGPO after window expires still allowed
- Residual storm cadence is separate (cross-peer initiator ping-pong)

## Related

- [`netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md`](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md) — first absorb↔convergence hang (incomplete without WindowActive)
- [`netplay_multistick_correction_union_2026-07-27.md`](netplay_multistick_correction_union_2026-07-27.md) — N-remote union (2P still one remote/peer)
