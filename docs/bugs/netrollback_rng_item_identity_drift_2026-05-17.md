# NetRollback RNG and item identity drift

**Date:** 2026-05-17  
**Status:** DIAGNOSTIC FIX

## Symptoms

Forced rollback with `FORCE_MISMATCH` proved rollback could load a clean snapshot and run visually stable resim, but an identity rollback still changed post-resim hashes. Later in the same run, item spawns appeared to diverge even when fighter positions looked acceptable.

The logs did not show `LOAD_HASH_DRIFT`, which meant snapshot load matched the stored slot hashes. The gap was post-load execution: the periodic NetSync line only printed input, fighter, and map hashes, so RNG/item/world drift was hidden unless load verification failed.

## Root Cause

The snapshot covered the global RNG seed and the natural item appear actor, but did not capture the separate `gITManagerRandomWeights` table used by container/item selection. Also, visual effect and particle code consumed the same global RNG stream as gameplay, so unsnapshotted cosmetic execution during netplay could perturb later gameplay RNG consumers such as item spawns.

## Fix

Rollback diagnostics now print the full subsystem hash set during resim and periodic NetSync validation: fighter, world, active items, active weapons, map, RNG seed, camera, and animation. Forced identity rollback records pre-resim hashes and logs `ROLLBACK_IDENTITY_DRIFT` when unchanged confirmed input does not reproduce the same post-resim state.

Snapshots now save and restore `gITManagerRandomWeights` scalars plus its active `kinds` and `blocks` arrays. The rollback world hash covers those fields too, so item spawn-policy drift is visible in both load verification and NetSync logs.

Effect and particle systems now use PORT cosmetic RNG wrappers. Outside netplay they delegate to the original gameplay RNG for behavior compatibility; during netplay they advance a separate cosmetic seed, initialized from the gameplay seed at VS session start, so visual randomness does not alter gameplay RNG advancement.

**Resim baseline logging (2026-05-17 follow-up):** `resim complete` used to compare post-resim state against the **pre-rollback diverged live world** (captured before snapshot load), which always looked like identity drift. Baseline hashes are now taken **after** loading snapshot tick `mismatch-1`, with `resim baseline (post-load tick=…)` and `baseline/post` log labels. Compare host/client `resim baseline` lines at the same `load_tick` to diagnose cross-peer snapshot divergence; `RESIM_STATE_DELTA` after input correction is expected.

**Battle clock + peer snapshot verify (2026-05-17):** `gSCManagerBattleState->time_passed` no longer advances from wall-clock scheduler tics during netplay (`ifCommonTimerFuncRun` early-out; `syNetSyncReconcileBattleTimePassedFromSimTick` derives it from authoritative sim tick at each battle step and before snapshot save/load). Cross-peer rollback baseline digests ride `SYNETPEER_PACKET_ROLLBACK_BASELINE` (subsystem hashes at `load_tick`); mismatch logs `PEER_SNAPSHOT_DIVERGE` and stops the session unless `SSB64_NETPLAY_ROLLBACK_PEER_SNAPSHOT_ABORT=0`. Resim target is capped to `highest_remote + input_delay + 1` so both peers reconcile the same span.

**Snapshot load tick for `time_passed` (2026-05-17):** Rollback load at `mismatch-1` used to call `syNetSyncReconcileBattleTimePassedFromSimTick()` while `syNetInputGetTick()` was still at the post-prediction frontier (e.g. load tick 4586, live tick 4589), so `time_passed` and `hash_world` no longer matched the stored slot → spurious `LOAD_HASH_DRIFT` and client-only session abort while the host kept simming. Save/load/verify now call `syNetSyncReconcileBattleTimePassedForSimTick(completed_sim_tick)` with the snapshot index.

**Symmetric resim execution (2026-05-18):** Rollback coordinated mismatch ticks via wire notify but peers could run different resim spans (`ClampResimTargetTick` used per-peer `highest_remote + D + 1`), save snapshots during active resim (tainting later load slots), and forward-sim before cross-peer baseline agreement. Fixes: wire-locked target clamp for symmetric episodes (frontier cap only), skip snapshot save while `resim_pending` or inside episode cooldown, hold `AdvanceResimBudget` until `ROLLBACK_BASELINE` digest matches local post-load `figh`/`world`/`item`/`rng` (or short timeout), reset cosmetic RNG from gameplay seed on snapshot load, and require confirmed remote rows during resim reconcile (no predicted fallback).

**Out of scope (longer term, 2026-05-18):**

- **Full snapshot byte exchange** — not implemented; baseline digest gate + snapshot save hygiene are the chosen approach.
- **Disabling symmetric notify for pure independent GGPO** — notify remains the execution contract until independent detection is proven symmetric (`SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` is experimental).
- **Anim subsystem in snapshot / hard-fail on anim-only `LOAD_HASH_DRIFT`** — anim-only drift continues to soft-continue; do not block resim unless gameplay regressions require it.
