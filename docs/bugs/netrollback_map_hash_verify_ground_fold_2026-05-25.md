# Rollback: LOAD_HASH_DRIFT on `map` after ground snapshot fold

**Date:** 2026-05-25  
**Status:** FIX SHIPPED  
**Evidence:** `client-auto1.log` — Sector Z automatch, FC @120 `figh`/`world` peer diverge; reanchor load @119 `LOAD_HASH_DRIFT` with **only** `map` mismatch (`figh`/`world`/`anim` matched live vs slot).

## Root cause

`syNetRbSnapFillSlotFromLive` stores:

```c
slot->hash_map = syNetSyncHashMapCollisionKinematics();
slot->hash_map = FNV(slot->hash_map ^ syNetRbSnapshotFoldGroundHash(slot), 0x47524F55);
```

`syNetRollbackVerifyLoadedSlot` compared **`live_m = syNetSyncHashMapCollisionKinematics()`** (yakumono only) against **`slot->hash_map`** (yakumono + ground). On any stage with `ground_captured == TRUE` (e.g. `nGRKindSector`), verify always failed after a correct apply → session stop and failed resim.

## Fix

- `syNetRbSnapshotComputeMapHashLive()` — same fold as save (live collision + live `gGRCommonStruct` ground payload).
- `syNetRollbackVerifyLoadedSlot` and `syNetRollbackCollectHashes` use it for the `map` partition.

## Related (instant cross-peer fork / wrong facing)

Per-peer **Neutral Spawns** or **Disable Stage Hazards** CVars changed spawn `lr`, joint placement, or Sector hazards without changing wire inputs. Netplay now forces vanilla spawns and vanilla hazard sim (`port_enhancement_neutral_spawns` / `port_enhancement_stage_hazards_tick` no-op while `syNetPeerIsVSSessionActive`).

## Also

- Removed `fighter_gobj->id` from `syNetSyncFoldFighterAnimRollback` (rollback `anim` hash) — GObj id allocation order must not affect sim digests.
