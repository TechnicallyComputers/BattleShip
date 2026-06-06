# Sector Z Arwing deck jitter + map hash synctest (netplay)

**Date:** 2026-06-05  
**Scope:** `decomp/src/gr/grcommon/grsector.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`  
**Status:** FIX SHIPPED — soak pending (2026-06-05 double-SetPos carry follow-up applied)

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

## Follow-up (2026-06-05): deck moves without fighters (zero platform carry)

**Symptom:** Patrolling Arwing mesh advances each frame but grounded fighters on deck line 1 do not inherit lateral motion.

**Root cause:** `grSectorArwingReconcileDeckYakumonoFromFlightTree` (netmenu patrol path + snapshot reconcile) called `mpCollisionSetYakumonoPosID(1, …)` before `grSectorArwingUpdateCollisions` when `line_active && z_near`. The second SetPos at the same position made `gMPCollisionSpeeds[1] = new − old = 0`, so `ftmain` added zero `vel_speed` carry.

**Fix:** Reconcile only calls SetPos when line 1 is not yet live (`!line_active || !z_near`). When live, UpdateCollisions remains the single speed-authoritative SetPos per frame.

## Follow-up (2026-06-05): backward patrol jitter with rollbacks=0

**Symptom:** After carry fix, Arwing deck motion is correct but the mesh occasionally teleports ~1 patrol frame backward (~225 units early patrol) even with no input rollback (`rollbacks=0` in soak logs).

**Root cause:** Periodic synctest (`SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`) loads `probe_tick = completed_tick - 1` into live, runs `syNetRbSnapApplyArwing` + `grSectorRepairArwingPresentation`, verifies map hash, then emergency-restores. During patrol that is always a visible snap (e.g. tick 993 live `frame=2 tx=16045` → probe 992 restore `frame=1 tx=16270`). The existing `sector_arwing_deck` skip only covers fighters standing on / jumping from the deck, not empty-deck patrol. `s_syNetRbSnapRepairStageVerifyOnly` was not honored by Sector Arwing repair (unlike Hyrule twister / Yamabuki gate).

**Fix:**

1. **`syNetRbSnapshotSectorArwingPatrolLiveSynctestFragile`** — skip synctest while `arwing_status == Patrol` and `arwing_pilot_curr != -2` (`reason=sector_arwing_patrol`).
2. **`syNetRbSnapshotSectorArwingPatrolSlotSynctestFragile`** — skip probe when the ring slot at `probe_tick` is a patrol tick (`reason=sector_arwing_patrol_probe`).
3. **`syNetRbSnapEnsureSectorArwingAfterParticleReset`** — early-return when `s_syNetRbSnapRepairStageVerifyOnly` (belt-and-suspenders for patrol boundary ticks).

Real rollback resims still apply full Arwing presentation; only the diagnostic load/restore cycle is gated.

## Test plan (patrol jitter follow-up)

1. Sector Z soak with synctest on, fighters **not** on deck during patrol: no `sector_arwing_restore` lines paired with `SYNCTEST_OK` during patrol; expect `SYNCTEST_SKIP reason=sector_arwing_patrol` instead.
2. Deck carry unchanged when standing on hull during patrol (`sector_arwing_deck` skip still defers probes with fighters coupled).
3. Forced rollback mid-patrol: one `sector_arwing_restore` per load tick; map hash verify still passes.
