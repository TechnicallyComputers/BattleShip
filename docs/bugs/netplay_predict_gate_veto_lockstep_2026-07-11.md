# Netplay: phase-lock prediction vetoed into soft lockstep

**Date:** 2026-07-11  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom

With low committed delay (`SSB64_NETPLAY_MATCH_INPUT_DELAY=1` on ~17 ms LAN) the match felt input-starved and low-framerate — as if there was no forward sim prediction and lockstep was strictly enforced. Admission could still show `pct_R≈0` / `pct_P≈100` when remote confirmed rows arrived just-in-time (`hr ≈ wire_base`, slack≈0): soft delay lockstep, not R-starvation.

## Root cause

`syNetPeerEvaluateSharedCommitStep` could set `uses_prediction` and `advance=TRUE` inside the phase_lock window, but FuncRead wire admission then re-applied:

1. `syNetPeerShouldHoldSimTickForSkewPacing` → **R** + scene suppress  
2. `syNetInputRemoteHumanWireReadyForSimTick` → requires strict-confirmed remote history → **R**

So prediction never reached publish/sim. Battle `syNetInputRepublishRemoteHumanControllersForTick` also returned FALSE without confirmed wire, skipping `ifCommonBattleUpdateInterfaceAll`.

Hold-last fallback in `syNetInputResolveRemoteHumanAuthorityFrameEx` was tagged `RemoteConfirmed` / `is_predicted=FALSE`, so even if a tick had advanced, rollback mismatch / `NoteSimTickPredictedRemoteUsage` could not see speculation.

The confirmed-only gate was added after Fox @421 (`docs/bugs/netplay_remote_apply_before_sim_2026-05-20.md`) to stop predict-advance + asymmetric hold-last/confirmed desync when ingress landed after sim. The durable fix is: allow predict-approved advance, tag speculation as predicted, pump/promote before resolve, rollback on mismatch — not permanent lockstep.

## Fix

1. **FuncRead (rollback sessions):** after shared `advance`, skip skew re-hold; skip confirmed-wire veto when `uses_prediction` unless `syNetRollbackPredictionRecoveryRequiresConfirmed`.  
2. **Republish:** if confirmed missing but phase-lock predict advance is allowed, resolve/publish speculative remotes and allow battle sim.  
3. **Hold-last speculation:** tag `nSYNetInputSourceRemotePredicted` / `is_predicted=TRUE` so recovery can correct when wire arrives.

## Verification

- LAN soak with `MATCH_INPUT_DELAY=1`: sim advances at wall rate when `hr` briefly lags `wire_base` (predict path), not only when ring is ready.  
- Expect occasional predicted remote usage + rollback corrections under induced jitter; no sustained soft lockstep with `wire_slack=0` forever unless remote truly keeps pace.  
- Cross-peer `figh` stays matched after mispredict resim (no Fox @421-class live split with late `REMOTE_PUBLISH`).
