# Netplay witness false stomps (soak1 intro locomotion)

**Date:** 2026-06-11  
**Soak:** soak1-linux + soak1-android (Yoshi's Story, Kirby/Yoshi, forced mismatch @230)

## Symptom

With `SSB64_NETPLAY_STATUSVARS_WITNESS=1`, both peers spammed identical `witness stomp` lines during intro countdown / early neutral (~471–517 sim ticks), Kirby P1 (`fkind=6`):

| status_id | Status | accessed | expected (before fix) |
|-----------|--------|----------|------------------------|
| 18 | Turn | entry | turn |
| 15 | Dash | turn | entry |
| 28 | Squat | squat | entry |

Sim hashes stayed matched; no `rb_load_fail`, no gameplay desync from these lines.

## Root cause

1. **Missing ownership rows** — `squat` and `landing` overlays were not registered in `syNetplayStatusVarsWitnessFillRange`.
2. **Over-broad entry fallback** — `syNetplayStatusVarsWitnessExpectedOverlay` tagged any fighter with `camera_mode == Entry` as **expected=entry**, even during `nFTCommonStatusWait` and later actionable statuses while intro camera mode lingered.
3. **Legitimate cross-overlay reads** not allowlisted:
   - `gmcamera.c` reads `ftStatusVarsEntry()->lr` whenever `camera_mode` is Entry/Explain (sim status may already be Turn).
   - `ftCommonDashCheckTurn` reads persistent `turn.lr_turn` during Dash/Run-family statuses (union bytes not cleared on status change).

## Fix

`port/net/sys/netplay_statusvars_witness.c`:

- Register squat + landing status ranges.
- Limit camera-mode entry fallback to `status_id < nFTCommonStatusWait`.
- Allow cross-overlay: `entry` when camera Entry/Explain; `turn` when status in Wait…Ottotto.

## Verify

Re-run soak1 with witness enabled. Expect no Turn/Dash/Squat stomp spam during intro countdown; real union stomps (CatchWait, Appear entry_lr drift) should still surface if present.
