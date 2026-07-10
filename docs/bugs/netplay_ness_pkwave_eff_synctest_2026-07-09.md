# Netplay: Ness PK Thunder wave VFX fails synctest verify (eff-only)

**Date:** 2026-07-09  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `855185501` / seed `4046082390`  
**Match:** Captain Falcon (P0) vs Ness (P1), Dream Land  
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

```
SYNCTEST_FAIL tick=1589,1949,2309,2429,2549
LOAD_HASH_DRIFT: diverged=eff
effect_eject reason=verify_non_canonical gobj_id=1011 anim_frame=23.0
eff_fold_diag capture: count=1 respawn=5 parent_id=1000
eff_fold_diag verify:  count=0 hash=0x811C9DC5
```

All other partitions (`figh`, `wpn`, `rng`, `map`, `cam`, `anim`) matched on both peers. `FRAME_COMMIT_DIAG state_diverge=0` at session end — local rollback verify round-trip failure, not peer gameplay desync.

Failures correlate with Ness PK Thunder **air hold** (`status=233`, `motion=208`); synctest probes pass when `effect_count=0` outside hold windows.

## Root cause

Ness PK Thunder wave (`efManagerNessPKThunderWaveMakeEffect`, `respawn=NESS_PK_WAVE`, `proc_update=gcPlayAnimAll`, joint `SYNETRB_NESS_PKWAVE_JOINT`) is captured in effect blobs and folded into the rollback hash during hold.

Two independent gaps caused the first fix pass to still fail at tick 1589 (session `412154438`):

1. **Ambiguous `fighter_gobj_id` (1000):** all fighters share `nGCCommonKindFighter`. Parent resolution via `syNetRbSnapPlayerForFighterGobjId` / `gcFindGObjByID` matched P0 (Captain) instead of Ness, so find/respawn/enforce never adopted the live wave on P1.
2. **Stale joint userdata after figatree rebind:** `syNetRbSnapLiveEffectIsNessPKWave` required `dobj->user_data.p == fp->joints[5]`. Post-rollback figatree walks recreate joint pointers; the live shell on recycled `gobj_id=1011` failed identity, Ensure skipped respawn because the id shell still had an `EFStruct`, and verify ejected it as non-canonical.

Snapshot **apply** already called `syNetRbSnapEnsureNessPKWaveEffectsFromSlot`, but **verify finalize** did not repin joints or resolve parent by player slot. The verify-only enforce/retrack path also lacked relaxed find + player-index capture in `quake_magnitude`.

## Fix

| File | Change |
|------|--------|
| `port/net/sys/netrollbacksnapshot.c` | Capture: store Ness player in `quake_magnitude` for `NESS_PK_WAVE` blobs |
| | `syNetRbSnapResolveNessPKWaveParentGobj` — slot player / PK Thunder scope (never `gcFindGObjByID(1000)`) |
| | `syNetRbSnapRepinNessPKWaveJoint` + `RepinAllNessPKWaveJointsForSlot` — refresh joint[5] after figatree rebind |
| | `syNetRbSnapFindLiveNessPKWaveEffectRelaxedForFighter` — verify adopt before mint |
| | `syNetRbSnapEnsureNessPKWaveEffectsFromSlot` — player-keyed blob match; eject stale id shells |
| | Apply/respawn/enforce paths wired to parent resolver + repin |
| | Captain Falcon kick: `syNetRbSnapRepinCaptainFalconKickJoint` + relaxed joint identity (presentation) |

Mirrors Yoshi egg-escape player capture (`quake_magnitude`) and post-rebind joint repin patterns.

## Re-soak pass criteria

Session class `855185501` / seed `4046082390`: `SYNCTEST_OK` at ticks 1589, 1949, 2309, 2429, 2549 during Ness PK Thunder hold. No `verify_non_canonical gobj_id=1011` with `respawn=5` in `eff_fold_diag`.
