# Netplay Turn→Dash (dash-dance) blocked under anim-end harden

**Date:** 2026-07-12  
**Soak:** `902430280` (feel-0 OK, FC clean, trails matched)  
**Status:** PARTIAL (`PORT && SSB64_NETMENU`) — SetFlag1/`allow` restored; dash-out still blocked by `lr_turn==0` (see `netplay_turn_lr_turn_stomp_2026-07-12.md`)

## Symptom

Same stick motion dash-dances in training lab but not in live VS. Peers stay synced (`status_id` trails match, no FC diverge). Netvs shows many `Wait/Dash→Turn` and **zero** `Turn→Dash` (`18→15`); every Dash→Turn run is a full 11-frame Turn then Walk/Wait.

Post-tick `STICK_SAMPLE` / `burned_dash` rates looked similar training vs netvs and hid the failure: successful Dash burns `tap_stick_x` to max before the sample line.

## Root cause (working)

Opposite smash uses `ftCommonDashCheckInterruptCommon` → `TurnSetStatusInvertLR` (`lr_dash != 0`). Dash out of Turn requires figatree `SetFlag1` → `is_allow_turn_direction` in `ftCommonTurnProcUpdate`, then stick×`lr_turn` in `ProcInterrupt` (no tap check on that path).

Training lab does **not** run `syNetplayHardenAnimEndWaitThresholdBeforeSim` (`RollbackLiveForwardSimEligible` false). Live VS did, and Turn/TurnRun sat inside the WalkSlow…FallAerial BeforeSim snap band — a plausible desync of SetFlag1 vs the allow window while both peers still agree.

## Fix

1. **Carve Turn / TurnRun** out of `syNetplayFighterInLocomotionAnimEndWaitScope` (split WalkSlow…RunBrake and KneeBend…FallAerial).
2. **`SSB64_TURN_DASH_WITNESS=1`** — log `TURN_DASH_WITNESS` from Turn update/interrupt (`flag1`, `allow`, `lr_dash`, `lr_turn`, stick, tap, `anim_frame`, `did_dash`).

## Verify

- Re-soak dash-dance with `SSB64_TURN_DASH_WITNESS=1` (and stick sample if desired).
- Expect `18→15` completions and `did_dash=1` on InvertLR turns with smash held.
- Grep `TURN_DASH_WITNESS phase=update flag1=` — should see non-zero `flag1` mid-turn; if still all `flag1=0` with carve-out, root cause is elsewhere (motion script / leftover collapse).
- FC / status trails stay matched; Turn end tics drift may return (accepted trade vs feel) — watch Wait/Turn `status_total_tics` at FC.

## Related

- `docs/bugs/netplay_dash_turn_anim_end_fc_drift_2026-07-11.md` — why Turn was in scope
- `docs/bugs/netplay_anim_end_harden_audit_2026-07-11.md`
- `docs/bugs/netplay_zero_delay_local_feel_2026-07-11.md` — feel-0 (not this bug)
