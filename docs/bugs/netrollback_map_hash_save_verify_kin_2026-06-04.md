# Rollback map hash save/verify kin parity (Sector Arwing deck)

**Date:** 2026-06-04  
**Scope:** `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED — Sector Z synctest soak pending

## Symptoms

Sector Z soak with `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`: 10 `SYNCTEST_FAIL` ticks with `map_mismatch` while cross-peer `mph` stayed matched (`state_diverge=0`). `map_hash_drift` showed `slot_stored!=live_full` on every fail, `ground_fold` round-trip OK, and large `map_hash_yaku1` / `map_hash_arwing_d0` translate deltas (blob vs live). Two fails also had `user_live!=user_blob` on yakumono line 1.

## Root cause

1. **Save capture instant ≠ hash instant** — `hash_map` was computed from live kin after `PrepareMapStateForHash`, but `mp_yaku[1]` and `arwing.dobj_translate[0]` blobs were captured earlier in `syNetRbSnapFillSlotFromLive` (before items/weapons/effects and before final deck reconcile). Save self-test only recomputed live kin, so it passed while blobs were stale.

2. **Triple non-idempotent Sector repair on verify** — `syNetRbSnapEnsureSectorArwingAfterParticleReset` ran from `ApplySlotToLive`, `FinalizeLoad`, and synctest `RepairStageAfterParticleResetForTick`. Logs showed two `sector_arwing_restore` lines per fail tick with different `tx` (e.g. 15519 → 15428 vs blob 15795).

## Fix

1. **Re-capture kin blobs at hash boundary** — after final `PrepareMapStateForHash` + ground re-capture, call `syNetRbSnapCaptureMap` and `syNetRbSnapCaptureArwing` immediately before `slot->hash_map` assignment.

2. **Single Sector Arwing repair per load** — defer sector case out of early `ApplySlotToLive` repair; run only from finalize (`include_sector_arwing=TRUE`). Per-tick dedup in `syNetRbSnapEnsureSectorArwingAfterParticleReset` suppresses duplicate repair; synctest no longer calls `RepairStageAfterParticleResetForTick` after finalize (2026-06-05).

3. **Blob authority after anim joint eval (2026-06-04 follow-up)** — `grSectorRepairArwingPresentation` re-applies snapshot `dobj_translate` / `dobj_rotate` after `grSectorArwingApplyAnimTransforms`. `gcPlayDObjAnimJoint` was overwriting flight-root translate from the spline at the restored frame (~100 units / one patrol tick behind the saved blob), causing verify-time kin drift despite matching blobs at save.

## Test plan

1. Sector Z cross-ISA soak with `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`: `SYNCTEST_FAIL` count → 0; `map_hash_yaku1` live_tx ≈ blob_tx at verify.
2. One `sector_arwing_restore` per synctest probe tick.
3. Cross-peer `mph` unchanged; no regression on Arwing presentation / deck jump defer.
