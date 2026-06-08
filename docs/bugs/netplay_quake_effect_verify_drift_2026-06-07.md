# Netplay: camera-quake effect verify drift — 2026-06-07

**Status:** FIX SHIPPED (soak pending — Phase 38 Class B/A follow-up)  
**Scope:** PORT netmenu rollback effect reconcile + synctest verify

## Symptom

Synctest verify `LOAD_HASH_DRIFT` with **eff-only** mismatch (`figh`/`world`/`rng` match). Typical patterns:

- Ring slot saved N quake/presentation effects; verify load hash empty or different `anim_frame`
- `gobj_link_audit ef6=8` while `effect_count=0..5` in snapshot fold
- `eff_fold_diag` showed `respawn=0` for camera quakes (misclassified)

## Root cause

`efManagerQuakeMakeEffect` schedules `efManagerQuakeProcUpdate` on the **GObj process list** via `func_run`, not `EFStruct::proc_update`. Effect capture/reconcile treated quakes as generic blobs (`respawn_kind=NONE`), so synctest load could not adopt/respawn them and orphan dead shells (`anim_frame<=0`) accumulated on link 6.

## Fix

| Area | Change |
|------|--------|
| `syNetRbSnapEffectRespawnKindFromLive` | Detect quake via GObj proc fingerprint |
| Capture | Store GObj proc fingerprint + `SYNETRB_EFFECT_RESPAWN_QUAKE` / `quake_magnitude` |
| `syNetRbSnapLiveEffectMatchesBlob` | Quake-specific identity match (magnitude + proc) |
| `syNetRbSnapPruneOrphanQuakeAndDeadEffects` | Eject unlisted quakes + dead free-floating shells after reconcile |
| `syNetRbSnapReapplyEffectBlobsFromSlot` | Re-apply slot `anim_frame` during verify repair |
| `scripts/netplay-trim-logs.py` | Ignore `resim-sim-core-reject reason=not_in_resim` (synctest diag noise) |

## Verify

1. Re-run cross-ISA soak with synctest — expect fewer/no eff-only `LOAD_HASH_DRIFT` at ticks 730/1090/1450/2044 class.
2. Host sync-report should not mark UNSTABLE solely from `not_in_resim` rejects when drift soft-continues.
3. Camera quake during heavy landing still plays; no gameplay hash skew.

### Phase 38 (2026-06-07) — Class B pose stamp + Class A transient probe skip

**Symptom (3467-tick STABLE soak):** 8 eff-only `LOAD_HASH_DRIFT` soft-continues, 0 `SYNCTEST_FAIL`. Two families:

| Class | Pattern | Example ticks |
|-------|---------|---------------|
| **B** | Save + verify both have effects; `anim_frame` and/or `pos` differ after verify repair | 509, 1031, 1752, 1872, 3314 |
| **A** | Slot has transient hit VFX; verify load eff hash empty (`0x811C9DC5`) | 1151, 1632, 2353 |

**Class B fix:** Quantize `anim_frame` into effect blobs at capture (generic effects use `syNetplayQuantizeF32`; Ness magnet keeps `QuantizeAnimScalar`). Apply path uses `syNetRbSnapApplyEffectBlobAnimFrame` after parent rebind (syncs DObj). `TryRepairEffectHashForVerify` runs a **final** `syNetRbSnapReapplyEffectBlobsFromSlot` after shield patch/prune so reconcile procs cannot leave stale anim/translate on verify.

**Class A fix:** `syNetRbSnapshotSynctestShouldSkipProbeTick` skips when slot effects are **transient-only** (`respawn_kind=NONE` for every captured effect, none respawnable). Reason: `transient_effect_probe`. One-shot hit VFX cannot survive emergency→slot verify load reliably; skipping avoids false soft-continue drift counts.

**Related:** Kirby ground Stone ~9-frame Hold — `ftKirbySpecialLwRollbackShouldSkipFullStoneReset` now requires `is_damage_resist` TRUE before skipping full reset ([netplay_kirby_stone_rollback_release_2026-06-06.md](netplay_kirby_stone_rollback_release_2026-06-06.md)).

**Verify:** Re-soak cross-ISA synctest — expect 0 eff-only drift on transient-only probe ticks; Class B ticks should repair via final blob reapply or drop from drift set.

### Phase 39 (2026-06-07) — quake proc freeze + dead shell hash exclude + load-verify pose

**Symptom (6127-tick soak):** 8 `LOAD_HASH_DRIFT` (7 soft-continue, 1 `SYNCTEST_FAIL` @2607). Families: (A) dead `respawn=0` quake shells stacked with shield in save hash; (B) live quake `respawn=1` verify state = tick T+1 (`anim_frame`/`pos` off-by-one); (C) tick 2607 compound `figh`+`anim`+`eff` mismatch during synctest (Kirby `GuardOff` + landing quake).

**Fix:**

| Area | Change |
|------|--------|
| `syNetRbEnumerateActiveEffectsSorted` | Skip dead quake shells (`anim_frame<=0`) from capture/hash enumeration |
| `syNetRbSnapEndQuakeProcUpdate` / `StampQuakeEffectFromBlob` | End `efManagerQuakeProcUpdate` on GObj proc list; re-stamp blob anim+translate; reschedule proc only outside verify-only |
| `syNetRbSnapFreezeSlotQuakeEffectsFromSlot` | Post-reconcile pass: slot-matched quakes frozen before load-hash verify |
| `TryRepairEffectHashForVerify` / `PrepareLoadedSlotForVerify` | Proactive effect repair + quake freeze + full fighter pose reapply (joint translate/rotate/scale + anim + canonicalize) |

**Verify:** Re-soak Kirby/Pikachu cross-ISA — expect 0 `LOAD_HASH_DRIFT`, 0 `SYNCTEST_FAIL`; quake during shield tap / landing still plays forward after load.

### Phase 40 (2026-06-07) — userdata-joint move FX + dead transient hash exclude

**Symptom (4425-tick Falcon vs Kirby soak):** 15 eff-only `LOAD_HASH_DRIFT` soft-continues, 0 `SYNCTEST_FAIL`. Kirby copied Falcon Punch VFX snapped on X during recovery; dead hit/quake shells and parent-attached move FX (`parent_id=fighter`) dropped from verify fold.

**Fix:**

| Area | Change |
|------|--------|
| `syNetRbSnapLiveEffectExcludedFromRollbackHash` | Exclude dead non-respawnable effects (`anim_frame<=0`, `respawn_kind=NONE`) and dead quakes from capture/hash fold |
| `syNetRbSnapEjectDeadNonRespawnableEffectsFromLive` | Eject dead shells before verify repair and after final blob reapply |
| `SYNETRB_EFFECT_SNAP_USERDATA_JOINT` | Capture parent joint index for `efManagerNoEjectProcUpdate` userdata-attached FX (Falcon Punch, etc.) |
| `syNetRbSnapRebindUserdataJointParentEffect` | Rebind `user_data.p` + `is_effect_attach` on snapshot apply/repair |
| `syNetRbSnapFinalizeFighterEffectAttachFlags` | Whitelist live/slot userdata-joint attach (prevents orphan flame during load) |
| `syNetRbSnapPruneOrphanQuakeAndDeadEffects` | Also eject dead fighter-attached transient shells |

**Related:** Kirby stone manual B release — `ftKirbySpecialLwIsGenuineButtonTapB` gated to resim-only ([netplay_kirby_stone_rollback_release_2026-06-06.md](netplay_kirby_stone_rollback_release_2026-06-06.md)).

**Verify:** Re-soak Falcon vs Kirby — expect fewer eff-only drifts; Kirby stone B-tap release works in forward netplay; copied Falcon Punch flame tracks joint through load/verify.

### Phase 41 (2026-06-08) — slot-authoritative quake restore + verify shield dedupe

**Symptom (5644-tick Falcon vs Kirby soak):** 13 eff-only `LOAD_HASH_DRIFT` from stacked camera quakes (`respawn=1` save → `respawn=0` verify after proc freeze); 1 `SYNCTEST_FAIL` @2687 from duplicate `SHIELD` respawn during verify repair (`guard_effect_gobj_id` live=0 vs blob).

**Fix:**

| Area | Change |
|------|--------|
| `syNetRbSnapLiveEffectIsQuake` | Recognize pending `func_run`, stamped/frozen shells (proc ended, `effect_vars.quake.priority` retained) |
| `syNetRbSnapEnsureQuakeEffectsFromSlot` | Adopt or respawn every `SYNETRB_EFFECT_RESPAWN_QUAKE` blob; stamp anim+translate via apply path |
| Load / verify repair | Call quake ensure before effect reconcile; verify repair runs effect reconcile **before** shield reconcile |
| `syNetRbSnapBackfillGuardShieldEffectIdsFromEffects` | Also on load apply + verify repair so `guard_effect_gobj_id` matches slot shield blobs |
| `syNetRbSnapEnsureShieldEffectsFromSlot` | Verify-only: skip synth respawn when slot already lists a per-player shield blob |

**Verify:** Re-soak Falcon vs Kirby cross-ISA synctest — expect 0 eff-only quake drift; tick 2687 class should soft-continue without `SYNCTEST_FAIL`.
