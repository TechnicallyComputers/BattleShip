# Sector Z Arwing deck jitter + map hash synctest (netplay)

**Date:** 2026-06-05  
**Scope:** `decomp/src/gr/grcommon/grsector.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`  
**Status:** FIX SHIPPED — soak pending

## Symptoms

Sector Z netplay: Great Fox Arwing visually **jerks sideways** during patrol (especially patrol start and rollback loads). Soak with map-hash diag showed `SYNCTEST_FAIL` at ticks **991** and **3997** with `map_mismatch` while cross-peer `state_diverge=0`. `map_hash_yaku1` logged `live_tx≈16252` vs `blob_tx=0` during patrol; two `sector_arwing_restore` lines per fail tick with ~250-unit `tx` delta.

## Root cause

1. **Deck vs mesh desync** — `grSectorArwingUpdateCollisions` only updates yakumono line 1 when `is_arwing_line_active && is_arwing_z_near`. During early patrol frames the flight spline advances `map_dobjs[0]` but the deck blob/kin could remain at translate zero until line flags flip.

2. **Stale `mp_yaku[1]` blob** — `deck_derived` skipped apply restore but ring capture could still store pre-patrol yakumono at patrol ticks; hash verify read live kin after apply with a mismatched stored `hash_map`.

3. **Double Sector repair on synctest** — `syNetRbSnapshotFinalizeLoad` already runs `syNetRbSnapEnsureSectorArwingAfterParticleReset`; synctest also called `syNetRbSnapRepairStageAfterParticleResetForTick`, re-applying presentation and popping `tx`.

## Fix

1. **`grSectorArwingReconcileDeckYakumonoFromFlightTree`** — netmenu-only: during patrol with live flight anim, set yakumono line 1 translate from the flight DObj tree before vanilla line-active gating. Called from `grSectorProcUpdate` after `gcPlayAnimAll` + canonicalize.

2. **Snapshot reconcile** — `syNetRbSnapReconcileSectorArwingDeckYakumonoFromFlightTree` calls the new helper then `grSectorArwingUpdateCollisions`. `deck_derived` gated on `arwing_status == Patrol` (not merely `pilot_curr != -2`).

3. **Capture** — `syNetRbSnapCaptureMap` re-captures `mp_yaku[1]` after reconcile when deck is derived live.

4. **Synctest** — remove redundant `syNetRbSnapRepairStageAfterParticleResetForTick` after finalize; reset per-load Sector repair dedup on `syNetRbSnapshotLoad` / emergency restore via `syNetRbSnapResetSectorArwingRepairDedup`.

## Test plan

1. Sector Z cross-ISA soak ≥60s: no visible Arwing lateral pops during patrol; no deck snap under fighters standing on the hull.
2. `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`: `SYNCTEST_FAIL` count → 0; `map_hash_yaku1` `live_tx ≈ blob_tx` at patrol ticks.
3. One `sector_arwing_restore` per load tick (not two with divergent `tx`).
4. Cross-peer `state_diverge=0` unchanged; Yoshi deck jump defer still fires when appropriate.
