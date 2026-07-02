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

**Class A legacy mitigation:** `syNetRbSnapshotSynctestShouldSkipProbeTick` used to skip when slot
effects were **transient-only** (`respawn_kind=NONE` for every captured effect, none respawnable).
Reason: `transient_effect_probe`. This avoided false soft-continue drift counts while one-shot hit
VFX could not survive emergency→slot verify load reliably.

**2026-07-02 policy update:** the broad `transient_effect_probe` skip was removed. Transient-only
effect ticks now run through synctest so any remaining `eff` drift is visible and can be fixed by
classifying/adopting the specific effect family instead of masking the probe. Expected failure shape,
if still present: `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` with `eff` only and live hash collapsed to the
empty sentinel (`0x811C9DC5`).

**Related:** Kirby ground Stone ~9-frame Hold — `ftKirbySpecialLwRollbackShouldSkipFullStoneReset` now requires `is_damage_resist` TRUE before skipping full reset ([netplay_kirby_stone_rollback_release_2026-06-06.md](netplay_kirby_stone_rollback_release_2026-06-06.md)).

**Verify:** Re-soak cross-ISA synctest — Class B ticks should repair via final blob reapply or drop
from the drift set. Transient-only ticks should no longer appear as `SYNCTEST_SKIP
reason=transient_effect_probe`; if they fail, use the captured effect blob fields to add a scoped
respawn/adopt path.

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

### Phase 42 (2026-06-08) — userdata-joint respawn + CopyCaptain attach unstick

**Symptom:** Kirby copied Falcon Punch flame still snapped back intermittently — Phase 40 rebind-only fix worked when the blob survived load, but particle reset + orphan prune left `is_effect_attach` stuck with no respawn path (`respawn_kind=NONE`).

**Fix:**

| Area | Change |
|------|--------|
| `SYNETRB_EFFECT_RESPAWN_USERDATA_JOINT` | New respawn kind; `EffectRespawnKindFromLive` classifies userdata-joint attach |
| `syNetRbSnapEnsureUserdataJointEffectsFromSlot` | Slot-authoritative restore via `efManagerCaptainFalconPunchMakeEffect` + blob apply/rebind |
| Capture | Kirby joint **30** / Captain joint **16** fallback when pointer match fails; set respawn kind on capture |
| `syNetRbSnapSanitizeCopyCaptainEffectAttach` | Clear stale `is_effect_attach` when slot expects flame but live reconcile did not restore it |
| Load / verify repair | Run userdata-joint ensure + sanitize before effect reconcile (with quake ensure) |

**Verify:** Re-soak Falcon vs Kirby — Falcon Punch flame should track joint consistently through rollback load and synctest verify during CopyCaptain (`status=295`).

### Phase 43 (2026-06-08) — quake dedupe + verify fighter pose re-stamp

**Symptom (post-Phase-42 soak):** Behaviors good, but tick **629** `SYNCTEST_FAIL` (`figh`+`anim` drift during early combat / Kirby `DamageHi1`); ticks 1124/2231/2351/2471/2718 had **eff-only** drift from stacked quake duplicates (save N → verify 3N identical `respawn=1` quakes).

**Fix:**

| Area | Change |
|------|--------|
| `syNetRbSnapPruneDuplicateQuakeEffects` | One live quake per slot blob (prefer ring `gobj_id`); eject extras after reconcile/repair |
| `syNetRbSnapReapplyFighterJointAnimFromSlot` | Also re-stamp `physics`, MPColl, root GObj pose from blob before canonicalize |
| `TryRepairEffectHashForVerify` | Quake dedupe + fighter pose reapply after effect repair |
| `VerifyLoadedSlot` | Verify-only final fighter pose re-stamp before hash fold |

**Verify:** Re-soak Falcon vs Kirby cross-ISA — expect 0 `SYNCTEST_FAIL` @629; eff-only quake drift at 1124+ class should drop to 0 or repair cleanly.

### Phase 44 (2026-06-08) — slot-authoritative effect enforce + shield verify respawn

**Symptom (6760-tick STABLE soak):** 15 **eff-only** `LOAD_HASH_DRIFT` (all soft-continue, 42/42 `SYNCTEST_OK`). Families: (A) quake triplication on stale ring `gobj_id` respawn (save 1 → verify 3); (B) shield missing at verify hash @3246 (`respawn=2`, verify count 0); (C) under-restore (save N → verify &lt;N); (D) orphan `respawn=0` shells stacked with slot quakes.

**Fix:**

| Area | Change |
|------|--------|
| `syNetRbSnapResolveLiveEffectGobjForBlobApply` | Adopt unreconciled live match before `TryRespawn` (prevents stale-id duplicate quakes) |
| `syNetRbSnapApplyEffectBlobToGObj` | Resolve live effect before respawn for all kinds |
| `ReconcileSnapshotEffectsBeforeItems` | Pass 1 uses resolve helper; pass 2 falls back to first-unreconciled; **pass 3** respawns unapplied blobs |
| `syNetRbSnapEnforceSlotAuthoritativeEffectSet` | One canonical live GObj per slot blob; eject slot-matching extras; re-stamp + freeze quakes |
| `EnsureShieldEffectsFromSlot` (verify-only) | Respawn shield when slot lists blob but live bubble missing (@3246 class) |
| `TryRepairEffectHashForVerify` | Shield ensure + enforce before hash check |
| `syNetRbSnapshotFinalizeEffectsForVerifyHash` | Pre-hash enforce hook in `VerifyLoadedSlot` |

**Verify:** Re-soak cross-ISA — expect **0** `LOAD_HASH_DRIFT`, `effect-repair ok` or silent verify; maintain `MATCH: STABLE`.

### Phase 45 (2026-06-08) — single verify enforce pass + idempotent respawn

**Symptom (4397-tick soak post-Phase-44):** Drift **15 → 7** but quake hyper-stacking worsened (save 1 → verify **10** @1924); shield @749 logged drift then `effect-repair ok` after multi-pass respawn spam.

**Fix:**

| Area | Change |
|------|--------|
| Synctest pipeline | **One** `PrepareLoadedSlotForVerify` + `VerifyLoadedSlot` (removed duplicate `TryRepair` + fold diag before hash) |
| `syNetRbSnapshotFinalizeVerifyEffectState` | Single reconcile → patch → enforce → joint re-stamp path (replaces duplicated TryRepair/Finalize/Prepare enforce) |
| `TryRespawnEffectFromBlob` | Return existing live via `ResolveLiveEffectGobjForBlobApply` before spawn; quake/shield explicit guards |
| `EnforceSlotAuthoritativeEffectSet` | Apply-only (no bare `TryRespawn` between resolve and apply) |
| `EnsureShieldEffectsFromSlot` (verify-only) | Patch-only; respawn deferred to enforce |
| `ResolveLiveEffectGobjForBlobApply` | Shield + userdata-joint live lookup |

**Verify:** Re-soak — expect **0** `LOAD_HASH_DRIFT`, verify fold `count == save count`, no shield respawn spam.

### Phase 46 (2026-06-08) — verify reconcile dedupe + quake quota cap

**Symptom (4392-tick Falcon vs Kirby soak):** 5 eff-only `LOAD_HASH_DRIFT` soft-continues (ticks 2055/2175/2533/3185/4377). Pattern: save `count=2` (shield+quake) → verify `count=6` (1 shield + 5 identical quake folds); `gobj_link_audit ef6` inflated. Triple `ReconcileSnapshotEffectsBeforeItems` on verify path (apply + finalize load + finalize verify) with pass-3 respawn stacking duplicates.

**Fix:**

| Area | Change |
|------|--------|
| `syNetRbEnumerateActiveEffectsSorted` | Dedupe by GObj pointer before hash/capture fold |
| `syNetRbSnapPruneExcessSlotMatchedQuakes` | Global cap: keep first N slot-matched quakes (N = quake blob count) |
| `FinalizeLoadFromSlot` / `FinalizeVerifyEffectState` | Verify-only: skip full reconcile; enforce-only path with quake/userdata ensure |
| `ReconcileSnapshotEffectsBeforeItems` | Verify-only: skip pass-3 `TryRespawn` (enforce handles under-restore) |
| Kirby stone diag | `port_log` instead of capped `syDebugPrintf`; trim script keeps `SSB64 KirbyStone:` |

**Verify:** Re-soak cross-ISA synctest — expect **0** eff-only `LOAD_HASH_DRIFT`, verify fold `count == save count`; Kirby stone soak shows `KirbyStone: hit` lines when `SSB64_NETPLAY_KIRBY_STONE_DAMAGE_DIAG=1`.

### Phase 47 (2026-06-09) — quake + shield gobj_id coexist + GuardOff release-lag probe

**Symptom (5096-tick soak):** `SYNCTEST_FAIL` @3602 eff-only; Whispy quake + Ness shield shared id 1011 → `effect_count=1` vs live fold `count=2`; verify eff hash empty + `guard_escape_eff_coupling` block.

**Fix:** `syNetRbSnapEffectGobjIdCollisionAllowsCoexist` for quake+shield; `guard_release_boundary_probe` extended to `GuardOff` + `release_lag>0`; verify-only shield respawn in `EnsureShieldEffectsFromSlot` / `PatchAllGuardShieldsFromSlot`. See [netplay_guard_quake_synctest_2026-06-09.md](netplay_guard_quake_synctest_2026-06-09.md).
