# Netplay: remote wire applied after battle sim (hold-last desync)

**Date:** 2026-05-20  
**Status:** Fixed  

## Symptom

Mid-match desync on P1 (remote human / Kirby): `figh` split at tick 548 while `pub_crc` and wire sticks agreed at that tick. Session later failed FC@570 with both `inp` and `figh` disagreeing.

## Root cause

Per-frame order in `scVSBattleFuncUpdate`:

1. `syNetInputFuncRead` — resolve/publish inputs into `gSYControllerDevices` for sim tick `T`
2. `ifCommonBattleUpdateInterfaceAll` — battle sim runs using those devices
3. `syNetPeerUpdate` — UDP recv, `REMOTE_PUBLISH` for tick `T` (too late)

On the host at tick 548, `sim_state_tick` logged Kirby `status=10` (hold-last stick) while the client entered `status=18` with the same `sx=-56 sy=21` wire row. Needle logs showed `REMOTE_PUBLISH player=1 sim_tick=548` **after** `sim_state_tick tick=548`.

`syNetInputResolveRemoteHumanAuthorityFrameEx` fell back to **hold-last** because wire-confirmed rows were not in the remote ring until recv ran post-sim.

## Fix

- `syNetInputPumpIngressAndPromoteRemoteThroughTick` — final ingress pump + promote wire rows for recent sim ticks before resolve/sim.
- Called from `syNetInputFuncRead` immediately before `syNetInputSynchronizeInputsForTick`.
- `syNetInputRepublishRemoteHumanControllersForTick` — same pump/promote, then re-resolve/re-publish remote slots into `gSYControllerDevices`; called from `scVSBattleFuncUpdate` immediately before `ifCommonBattleUpdateInterfaceAll`. Returns FALSE when wire is not ready (battle sim skipped, `syNetPeerUpdate` recv runs).
- `syNetInputRemoteHumanWireReadyForSimTick` — confirmed-path readiness under the authoritative wire contract. Originally stalled as **R** whenever phase-lock prediction would advance without confirmed rows (Fox @421). That veto made prediction a no-op; superseded for rollback sessions by predict-approved FuncRead/Republish + hold-last tagged `RemotePredicted` (`docs/bugs/netplay_predict_gate_veto_lockstep_2026-07-11.md`). Prediction-recovery windows still require confirmed.

## Verification

Re-run mid-match soak with `SIM_TRACE_NEEDLE` 415–425 and 545–555: P0 and P1 `fighter_slot_hash` and `sim_state_tick figh` must match cross-peer; no `REMOTE_PUBLISH` for tick `T` after `sim_state_tick tick=T` on the consuming peer; no live split while `REMOTE_PUBLISH_SKIP … wire_neutral` immediately precedes `sim_state_tick` for that tick.
