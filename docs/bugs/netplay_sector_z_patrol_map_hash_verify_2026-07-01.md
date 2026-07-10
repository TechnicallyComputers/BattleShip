# Netplay — Sector Z patrol map hash synctest verify

**Date:** 2026-07-01  
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

After re-enabling synctest during Arwing patrol ([netplay_sector_z_arwing_patrol_synctest_unskip](netplay_sector_z_arwing_patrol_synctest_unskip_2026-07-01.md)), long Sector Z soak (`~1778` ticks) showed paired `SYNCTEST_FAIL` at ticks 1100, 1220, 1397, … with **map-only** drift:

```
map=0x184CC831/0xC9DC9B78
map_hash_yaku1: live_tx=5220.34 blob_tx=5323.07 (~103 units)
status=2 deck_derived=1
sector_arwing_repair reason=verify_skip_all
```

Fighters, world, rng, cam, anim, and eff all matched; ground fold payloads matched. Drift was `mp_yaku[1]` translate in the kin hash, not ground blob.

## Root cause

Map hash at save: `PrepareMapStateForHash()` → deck reconcile from **live flight tree** → `CaptureMap()` (re-captures deck yaku) → hash.

Synctest verify path:

1. `ApplyMap` **skips** restoring `mp_yaku[1]` when deck-derived from slot (line derives from flight tree instead).
2. `EnsureSectorArwingAfterParticleReset` **skips** `ApplyArwing` during verify-only (emergency-restore safety; same pattern as Hyrule twister / Yamabuki gate).
3. `ComputeMapHashLive()` → `PrepareMapStateForHash()` reconciles deck from **current live flight tree**, which was never restored to the probe slot → ~100 u translate gap vs slot blob saved at end-of-tick capture.

Real rollback resim loads were unaffected (full `ApplyArwing` still runs outside verify-only).

## Fix

Defer patrol flight-tree restore to the map-hash boundary only:

- `syNetRbSnapReconcileSectorArwingPatrolMapForVerify()` — when deck-derived from slot, `ApplyArwing(slot)` then existing deck reconcile (inside `ApplyArwing`).
- `syNetRbSnapshotComputeMapHashLiveForVerify(tick)` — sets verify tick, runs hash prep (patrol restore), clears tick.
- `syNetRollbackVerifyLoadedSlot` uses `ComputeMapHashLiveForVerify` when `RepairStageIsVerifyOnly`.
- Load-time `EnsureSectorArwing` verify skip unchanged; diag reason updated to `verify_skip_load` / `patrol_map_at_hash`.

Emergency restore still runs immediately after synctest; ApplyArwing mutation window is minimized (hash compare only).

## Verify

Sector Z cross-ISA soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` + `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1` through at least one full patrol window — expect `SYNCTEST_OK` at former fail ticks (1100+) and `verify_patrol_map_reconcile` diag when `SSB64_NETPLAY_SNAPSHOT_ARWING_DIAG=1`.
