# Sector Z Arwing rollback presentation (netplay)

**Date:** 2026-05-30  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/gr/grcommon/grsector.c`  
**Status:** FIX SHIPPED — sim + lasers verified; presentation repair added 2026-05-30 (soak pending)

## Symptoms

Sector Z netplay: Great Fox Arwing hazard never appears visually for the full match, while laser weapons may still spawn later (sim scalars advance; `map_gobj` stays hidden or patrol cancels immediately).

Trimmed soak (`netplay-session-trimmed-sectorz.log`): `effect_count` @ tick 168 is intro FX, not the Arwing; many `LOAD_HASH_DRIFT soft-continue` with `rollbacks=0` (no slot apply in that log). Root issue is still missing **presentation** on snapshot load/resim paths.

## Root cause

Rollback already captured Sector **scalars** in `SYNetRbSnapGroundSector` but not:

- `map_gobj->flags` (`GOBJ_FLAG_HIDDEN` while patrol thinks it is active)
- `map_dobjs[]` **DObj anim runtime** (`anim_wait` / AObj chain) — restoring Patrol with `map_dobjs[0]->anim_wait == AOBJ_ANIM_NULL` triggers `grSectorArwingUpdatePatrol` to hide the ship and reset `arwing_appear_timer` to ~2000
- `unk_sector_0x4C` / `0x4D` / `0x4E` / `0x52` (laser timing helpers)
- Post-particle repair (`syNetRbSnapRepairStageAfterParticleReset`) had no `nGRKindSector` case (unlike Jungle/Yoster/Hyrule)

## Fix

1. **Ground blob v2** — extend `SYNetRbSnapGroundSector` with `map_gobj_flags` + `unk_sector_*`.
2. **`SYNetRbSnapArwingBlob` slot partition** — capture/apply full `SYNetRbSnapDObjAnimBlob` per `map_dobjs[0..11]` (validity mask).
3. **`grSectorReestablishArwingVisualTree` / `grSectorSyncArwingMapGObjFlags`** — rebuild hollow map DObj tree; derive visibility from `arwing_status` + restored root `anim_wait`.
4. **`syNetRbSnapEnsureSectorArwingAfterParticleReset`** — wired into `syNetRbSnapRepairStageAfterParticleReset`.
5. **Netplay anim driver (2026-05-30)** — `grSectorProcUpdate` calls `gcPlayAnimAll(map_gobj)` when `syNetRollbackIsActive()` (priority-5 process does not advance under rollback).
6. **Presentation repair (2026-05-30)** — `SYNetRbSnapArwingBlob` pose partition (`dobj_translate` / `dobj_rotate` per `map_dobjs[0..11]`, `flight_pattern_idx`); `grSectorRepairArwingPresentation` after apply (reattach flight joints on hollow tree, restore poses, `grSectorArwingApplyAnimTransforms` without advancing anim cursor).
7. **Drawable tree reestablish (2026-05-30)** — `grSectorReestablishArwingVisualTree` rebuilds when `map_dobjs[0] != DObjGetStruct(map_gobj)`, parent coupling is wrong, or no DObj has a valid `DObjDLLink` chain (`portDObjDLLinkChainLooksValid`); re-registers `gcAddGObjDisplay` if `proc_display` was lost. Ground blob v2 adds `arwing_last_flight_pattern` (set at patrol spawn, before `arwing_flight_pattern` → -1).
8. **F32 quantization (2026-05-30)** — `arwing_target_x` quantized on ground capture/apply; live `grSectorArwingCanonicalizeSimState` after rollback `gcPlayAnimAll` (full map DObj tree translate/rotate + anim scalars); `syNetplayQuantizeAnimScalar` preserves `AOBJ_ANIM_NULL` on snapshot apply/capture.
9. **Fighter + yakumono canonicalization (2026-05-30)** — `syNetplayQuantizeFighterPhysics` / `syNetplayQuantizeMPCollData` on fighter snapshot capture/apply; `syNetplayCanonicalizeFighterSimState` on capture/apply only (not live Sector proc). Sector live: `grSectorArwingCanonicalizeSimState` once **before** a single `mpCollisionSetYakumonoPosID` per frame (second call had zeroed `gMPCollisionSpeeds[1]` → deck slide). Removed end-of-`grSectorProcUpdate` map/fighter canonicalize (stale yakumono vs visual).

Diag: `SSB64_NETPLAY_SNAPSHOT_ARWING_DIAG=1` → `sector_arwing_live_step` / `sector_arwing_restore` (`root_eq`, `draw`, `dl0`, `dlm`, `nodes`, `disp`, `dllk`, `lfp`, …).

**Tick-120 `world` desync (separate):** frame-commit `world` hash is item random-weights (`gITManagerRandomWeights.valids_num`), not Sector scalars — see soak `world_detail` / `PEER_DIVERGE_DIFF`.

## Test plan

1. Sector Z VS netplay ≥60s: Arwing visible on both peers at least once before tick ~800.
2. Force rollback load (`SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=0` briefly): mid-patrol restore keeps ship visible; no instant `appear_timer` jump to ~2000.
3. `SSB64_NETPLAY_SNAPSHOT_ARWING_DIAG=1`: `sector_arwing_restore` shows non-NULL `d0`, `flags` without `HIDDEN` during patrol.
