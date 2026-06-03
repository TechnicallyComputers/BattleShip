# Yoshi's Island cloud rollback SIGSEGV — 2026-05-29

**Status:** FIX SHIPPED (soak pending)  
**Symptom:** Cross-ISA automatch on Yoshi's Island (`stage_kind=5`, `nGRKindYoster`): both peers hard-exit near GO (~tick 390) when `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` and/or first rollback load after intro.

## Root cause

`SYNetRbSnapGroundYoster` captured cloud scalars + `gobj_id` but **not** the three `dobj[]` pointers bound in `grYosterInitAll()`. After `syNetRbSnapshotLoad`, `grYosterUpdateCloudSolid()` dereferenced `clouds[i].dobj[0]->mobj` with stale NULL pointers → SIGSEGV on host and guest at the same synctest round-trip.

## Fix

| Layer | Change |
|-------|--------|
| Blob | Per-cloud `translate`, `dobj_valid_mask`, `dobj0_anim_wait_bits` (IEEE `f32`); stage-level `map_head` in `SYNetRbSnapGroundYoster`. Legacy cloud layout still loads (rebind only). |
| Apply | `grYosterRebindCloudDobjs()` walks the live GObj DObj tree (same topology as init); restore yakumono on/off + position from blob. |
| Sim guards | NULL checks in `grYosterUpdateCloudSolid/Evaporate/Anim` and skip clouds with no `gobj` in `grYosterProcUpdate`. |

## Test plan

- Cross-ISA automatch on Yoshi's Island with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: survive GO, no SIGSEGV through FC @120.
- Rollback resim mid-match on clouds (fighter standing on cloud pressure) with `SSB64_NETPLAY_SNAPSHOT_*_DIAG=1` optional.

## Related

- [`netplay_ground_snapshot_capture_segv_2026-05-29.md`](netplay_ground_snapshot_capture_segv_2026-05-29.md) — stale `gGRCommonStruct` GObj caches on capture (Saffron)
