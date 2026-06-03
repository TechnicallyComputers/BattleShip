# Hyrule twister rollback SIGSEGV after particle wipe

**Date:** 2026-05-29  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/gr/grcommon/grhyrule.c`  
**Status:** ROOT CAUSE FIXED 2026-05-31 (`obj_kind`/`id`, synctest skip, GetLR burst) + **2026-05-31 rider physics quantize** (contact `figh` drift) — extended soak pending

## Symptoms

Cross-ISA soak on **Hyrule Castle** with `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=1`:

- Match runs to sim tick ~3000+ (FC recovery resim from tick 3047) or synctest probe ~3480.
- **SIGSEGV** in `grHyruleTwisterUpdateMove+0xae`, `fault_addr=0x38`.
- Synctest `LOAD_HASH_DRIFT` with **`map=` split** while `world`/`item` agree (twister-active window, `world=0xE34EBAAA`).
- GFX stale-DL diag: `badCmd host=0x38` (render fallout, not root cause).

Not the earlier `pc=0x0` emergency-restore abort — that path is bypassed with soft load-hash.

## Root cause

Rollback apply order:

1. `syNetRbSnapApplyGround` restores Hyrule twister scalars + `twister_gobj` via `gcFindGObjByID`.
2. `syNetRbSnapResetParticlesForRollback` ejects **all** LBParticles (including twister VFX).
3. `twister_xf` becomes NULL/dangling while `twister_status` remains `Move`/`Turn`/…
4. Next stage tick: `grHyruleTwisterUpdateMove` dereferences stale twister state → crash.

Ground blob captured motion/status/gobj id but not `twister_xf` (particle-owned pointer). Initial fix recreated xf/gobj on missing-id path only; **existing gobj id path** did not sync `twister_pos` onto the DObj, clear/re-add ground obstacles, or re-run after finalize — causing **map hash drift** at synctest verify.

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapEnsureHyruleTwisterAfterParticleReset` — validate gobj kind/link, sync pose from blob, respawn if stale, clear/re-add obstacle for Move/Turn, force-recreate xf | `port/net/sys/netrollbacksnapshot.c` |
| `syNetRbSnapRepairStageAfterParticleReset` dispatcher — Hyrule + Pupupu Whispy + Yoster clouds + Castle bumper | same |
| Second repair pass in `syNetRbSnapshotFinalizeLoadFromSlot` (before effect reconcile) + synctest pre-verify via `syNetRbSnapRepairStageAfterParticleResetForTick` | `netrollbacksnapshot.c`, `netrollback.c` |
| `grHyruleTwisterRestorePoseFromPos` — floor snap from blob line id / projection | `decomp/src/gr/grcommon/grhyrule.c` |
| Capture/apply `twister_pos` (DObj translate) in `SYNetRbSnapGroundHyrule` | `netrollbacksnapshot.c` |
| NULL guards in `grHyruleTwisterUpdateMove`, `GetLR`, `UpdateSummon`, `UpdateStop`; guard `twister_xf` write in Move | `grhyrule.c` |
| Export `grHyruleTwisterStatus` enum in `grhyrule.h` | same |
| **2026-05-30:** Cross-ISA F32 grid on twister vel/edges/translate (`grHyruleTwisterCanonicalize*`) in MakeTwister, RestorePoseFromPos, UpdateMove/Turn/Summon | `grhyrule.c` |
| **2026-05-30:** Quantize all Hyrule ground blob f32 on snapshot save; re-quantize + `grHyruleTwisterRestorePoseFromPos` on apply | `netrollbacksnapshot.c` |
| **2026-05-30:** Orphan twister eject (`syNetRbSnapEjectOrphanHyruleTwisterGObjs`), invalid-gobj path uses `gcEjectGObj`, apply gates pose restore by status | `netrollbacksnapshot.c` |
| **2026-05-30:** `syNetRbSnapResyncFighterTwisterGobjs` after particle reset (mirror Jungle Taru Cann rider resync) | `netrollbacksnapshot.c` |
| **2026-05-30:** `grHyruleGetTwisterGobj`; NULL `twister_gobj` after `UpdateStop` eject; PORT guard in `ftCommonTwisterProcPhysics` | `grhyrule.c`, `ftcommontwister.c` |
| **2026-05-30:** Obstacle re-register for Stop + blob normalize (`twister_gobj_id==0`); Subside fade-only repair; `SSB64_NETPLAY_HYRULE_TWISTER_DIAG` | `netrollbacksnapshot.c` |

## 2026-05-30 follow-up (effect/fighter LOAD_HASH drift)

Cross-ISA soak with `LOAD_HASH_SOFT=0` showed `map=` agreeing at synctest fail ticks while **`eff=` and `figh=` split** once the twister entered Move (~tick 450+). Root cause: twister motion used raw libm (`pos + vel`, floor projection, edge clamp) with no shared grid, unlike Jungle Taru Cann / Zebes acid. Sub-ULP X/Y drift changed twister capture timing → effect count/hash divergence on rollback verify.

Fix: runtime quantize on the twister sim path + snapshot save/load canonicalization; apply ground blob now syncs `twister_pos` onto the live DObj immediately (not only in post-particle-reset repair).

## 2026-05-30 follow-up (duplicate tornado + rider SIGSEGV @ 0xC8)

Cross-ISA soak after quantization still showed **two visible tornadoes** (one non-collidable orphan + one repair spawn) and **SIGSEGV in `ftCommonTwisterProcPhysics`** (`fault_addr=0xC8`) when touching the collidable duplicate. Same failure family as DK Jungle Taru Cann (`docs/bugs/netplay_dk_jungle_tarucann_crash_2026-05-30.md`): rollback repair nulled or ejected `hy->twister_gobj` while fighters stayed in `nFTCommonStatusTwister` with a stale `tornado_gobj` at union offset `0x08`.

Fix: eject orphan ground gobjs before respawn; always `gcEjectGObj` on invalid twister slot; gate apply pose restore to active twister statuses; `syNetRbSnapResyncFighterTwisterGobjs` after Hyrule particle reset; PORT rebind/shoot-out guard in `ftCommonTwisterProcPhysics`.

## 2026-05-30 follow-up (visual-only respawn loop + non-collidable repair tornado)

Cross-ISA soak after rider-resync fix: no crash, but synctest rollback every ~120 ticks resurrected tornado VFX after the natural cycle ended (~tick 358), and visible tornadoes were not collidable. Root causes:

1. **Resurrection:** ground blob could carry Move/Turn/Stop/Summon with `twister_gobj_id==0` after `UpdateStop` ejected the gobj; repair called `grHyruleMakeTwister` anyway.
2. **Non-collidable:** repair only re-registered `ftMainCheckAddGroundObstacle` for Move/Turn, not **Stop** (vanilla still collides until `UpdateStop` tears down).
3. **Respawn flicker:** particle reset + xf recreate on every rollback boundary even when gobj survived.

Fix:

| Change | Location |
|--------|----------|
| `syNetRbSnapHyruleTwisterNormalizeFromBlob` — `twister_gobj_id==0` + active status → force Wait (Subside keeps fade-only path) | `netrollbacksnapshot.c` apply + repair |
| `syNetRbSnapHyruleTwisterReregisterObstacle` — Move/Turn/**Stop** after every repair | same |
| Subside repair — gobj clear + effect id 7 only (no `MakeTwister`) | same |
| Valid gobj path — pose restore + xf recreate only; sync xf from `MakeTwister` when spawn needed | same |
| Optional diag: `SSB64_NETPLAY_HYRULE_TWISTER_DIAG=1` logs status/gobj_id/obstacle on repair | same |

## 2026-05-30 follow-up (Summon trap: visible ghost, obstacle=0)

Cross-ISA soak with diag enabled showed repair stuck at **`status=Summon`, `gobj_id!=0`, `obstacle=0`** on every synctest boundary (~120 ticks). `NormalizeFromBlob` only fired when `twister_gobj_id==0`, so rollback kept restoring Summon with a valid gobj id but never ran `grHyruleTwisterUpdateSummon`'s Move transition or obstacle registration. Triple xf recreate per boundary came from duplicate `syNetRbSnapRepairStageAfterParticleReset` calls on the same tick.

Fix:

| Change | Location |
|--------|----------|
| `syNetRbSnapHyruleTwisterNormalizeAtCapture` — NULL gobj / idle status → `twister_gobj_id=0`; active status without gobj → Wait | `netrollbacksnapshot.c` capture |
| `syNetRbSnapHyruleTwisterNormalizeFromBlob` — stale non-zero gobj id with failed lookup → Wait | apply + repair |
| `syNetRbSnapHyruleTwisterCompleteSummonIfReady` — `twister_wait==0` → Move + vel/LR (mirror UpdateSummon) | repair |
| `syNetRbSnapHyruleTwisterReregisterObstacleForRepair` — Summon through Stop (netplay repair only) | repair |
| Per-tick repair guard — skip duplicate Ensure on same `slot->tick` | repair |
| Diag logs `twister_wait`, `summon_done` | same |

## 2026-05-30 follow-up (instant desummon / blind Wait repair)

Soak after Summon-trap fix: respawn loop gone, but tornadoes still died in ~44–88 ticks with **zero** `hyrule_twister_repair` lines (Wait/Sleep path cleared gobj without logging). Capture could downgrade active→Wait silently.

Fix:

| Change | Location |
|--------|----------|
| `syNetRbSnapHyruleTwisterLogRepair` / `LogCapture` — all repair exits + capture notes (`live_summon`, `normalize_active_to_wait`, …) | `netrollbacksnapshot.c` |
| Apply/repair Wait/Sleep — only `ClearGObj` when no valid gobj; else resume from blob active status or promote to Summon for xf/obstacle repair | apply + repair |
| Repair `blob_active_resume` — blob still has Summon/Move/Stop + gobj id while hy was normalized Wait | repair |

## 2026-05-30 follow-up (FC resim idle_clear + map drift after twister entry)

Soak with `SYNCTEST=0`: live tornado spawned, stayed collidable, fighter entered at tick ~2960. ~40 ticks later frame-commit `figh` diverge triggered resim load at tick 2879. Repair logged **`idle_clear`** with `status=1 blob_status=4 gobj_id=1010 gobj=0x0`, then **`resim-sim-core-reject reason=map`**. Host hit `RESIM_BASELINE_TIMEOUT` / hard desync.

Root cause: `NormalizeFromBlob` downgraded to Wait when `gobj_id!=0` but `gcFindGObjByID` missed after particle reset; Wait/Sleep repair then **`idle_clear` + early return** instead of respawning from blob active status.

Fix:

| Change | Location |
|--------|----------|
| `syNetRbSnapHyruleTwisterCopyActiveFromBlob` — shared blob scalar restore | `netrollbacksnapshot.c` |
| `NormalizeFromBlob` — stale gobj id + blob Summon..Stop → keep active scalars for repair respawn | apply + repair |
| Wait/Sleep repair — `blob_active_respawn` when blob active + gobj id but live gobj NULL; only `idle_clear` when blob truly idle | repair |
| `hyrule_twister_rider_resync` diag after `syNetRbSnapResyncFighterTwisterGobjs` | repair |
| Fold twister `release_wait` + `tornado_gobj->id` into `syNetSyncHashFighterStructLight` when in Twister status | `netsync.c` |

## 2026-05-30 follow-up (twister lifecycle split + hidden world drift)

Soak with `SYNCTEST=0` (Linux host + Android guest): twister entered Move on guest ~tick 2985 while host stayed Wait through ~3175. Frame-commit `world` hash did not include twister scalars/gobj pose, so hidden drift accumulated until `figh` blew up at tick 3189. Resim load at 3069 logged **`active_repair obstacle=0`** (2-slot ground obstacle table full of stale entries). Rider tosses were vanilla `release_wait` or cross-peer status mismatch, not repair eject (`rider_resync rebound=0 eject=0`).

Fix:

| Change | Location |
|--------|----------|
| `ftMainPurgeStaleGroundObstacles` / `ftMainEnsureGroundObstacle` — purge freed GObj slots before re-register; `ftMainClearGroundObstacle` clears `proc_update` | `decomp/src/ft/ftmain.c` |
| `syNetSyncFoldHyruleTwisterRollbackWorld` — fold twister status/wait/vel/gobj_id/DObj translate into `syNetSyncHashRollbackWorld` (frame-commit `world_digest`) | `port/net/sys/netsync.c` |
| Extended `syNetSyncHashFighterStructLight` — Twister folds tornado DObj translate x/y/z | `netsync.c` |
| `syNetRbSnapHashFighterBlobLight` — mirror Twister release_wait + gobj translate | `netrollbacksnapshot.c` |
| `CompleteSummonIfReady(hy, src)` — blob Move+ resumes from blob instead of rolling fresh Summon RNG | `netrollbacksnapshot.c` |
| Obstacle repair uses `ftMainEnsureGroundObstacle`; diag `hyrule_twister_obstacle_fail` (reason + slots_used) | `netrollbacksnapshot.c` |
| Diag: `hyrule_twister_apply_drift`, extended `hyrule_twister_capture` (vel/speed_wait/turn_wait) | `netrollbacksnapshot.c` |
| Diag: `hyrule_twister_rider_shoot` / `hyrule_twister_rider_rebind` in live physics guard | `ftcommontwister.c` |

## Soak pass criteria

Hyrule Castle cross-ISA (Link vs DK) with `LOAD_HASH_SOFT=1`, `SYNCTEST=1`, bomb spam through twister-active window:

- No SIGSEGV (`fault_addr=0x38` or `0xC8`) during synctest or FC recovery resim.
- No duplicate tornado visuals after rollback boundaries (single collidable twister during Move/Turn/Stop).
- Tornado collidable during Move/Turn/Stop after rollback repair (not visual-only Summon ghosts).
- Synctest must not resurrect twister after natural cycle end (`twister_gobj_id==0`).
- No `map=` split on `LOAD_HASH_DRIFT` at twister-active ticks.
- No `eff=` / `figh=` split on `LOAD_HASH_DRIFT` while `map=` agrees (cross-ISA twister-active window).
- Frame-commit `world` hash must detect twister status/gobj drift before `figh` diverges (Hyrule only).
- No `hyrule_twister_obstacle_fail reason=table_full` during active twister repair after resim load.
- No `wait_gobj_promote_summon` or periodic `active_repair`/`blob_active_respawn` every ~120 ticks after natural Subside with `SYNCTEST=1`.
- Session survives **past tick 4000** without process exit.

## 2026-05-30 follow-up (SYNCTEST respawn loop + non-collidable respawn)

`SYNCTEST=1` soak after lifecycle-split fixes: no duplicate meshes, but tornadoes **respawned every ~120 ticks** after the natural cycle ended and respawned copies were **non-collidable**. Root causes:

1. **`wait_gobj_promote_summon`:** Wait/Sleep repair saw a valid orphan gobj while the blob was idle and forced Summon → `MakeTwister` on every particle-reset boundary.
2. **Finished-cycle blobs:** `twister_gobj_id==0` with raw status still Summon..Stop (post-`UpdateStop` capture window) resurrected via `blob_active_respawn` / `MakeTwister`.
3. **Synctest verify:** load+repair+restore ran full twister respawn on probe ticks even though `RestoreLiveEmergency` immediately reverted live state — visible flicker and orphan meshes left on the ground link.

Fix:

| Change | Location |
|--------|----------|
| `syNetRbSnapHyruleTwisterEffectiveBlobStatus` — `gobj_id==0` + active raw status → treat blob as Wait | `netrollbacksnapshot.c` |
| `NormalizeAtCapture` — Stop/Subside with NULL live gobj → Wait + `gobj_id=0` | capture |
| `NormalizeFromBlob` — blob `gobj_id==0` + active raw status → Wait before repair decisions | apply + repair |
| Replace `wait_gobj_promote_summon` with `idle_orphan_clear` (`EjectOrphanHyruleTwisterGObjs` + `ClearGObj`) | repair |
| `idle_clear` also ejects orphan ground meshes before early return | repair |
| `syNetRbSnapRepairStageSetVerifyOnly` — synctest probe skips `blob_active_respawn` + `MakeTwister` (`verify_skip_*` diag) | `netrollbacksnapshot.c`, `netrollback.c` |
| `blob_active_respawn` path runs `CompleteSummonIfReady` after `MakeTwister` for collidable Move/Turn/Stop | repair |

## 2026-05-30 follow-up (pickup SIGSEGV @ 0x40 + duplicate Summon during active Move)

Soak log `netplay-session-trimmed-hyrule.log`: host in **Move** since tick ~2512; guest still in **Wait** and spawned a fresh **Summon** at tick ~2737 (~225 ticks later). FC resim at **2760** loaded tick **2749** with:

```
hyrule_twister_apply_drift live_gobj_id=0 blob_gobj_id=1010
hyrule_twister_obstacle_fail status=3 reason=invalid_gobj
hyrule_twister_repair active_repair obstacle=0
```

Visible duplicate tornado + **non-collidable** mesh after load. Approaching the mesh crashed both clients in **`ftMainSetStatus+0x685`** (`fault_addr=0x40`) when the ground obstacle tried to enter Twister status on a fighter whose `fp->data` was not restored yet.

Fix:

| Change | Location |
|--------|----------|
| `ftCommonTwisterSetStatus` / `grHyruleTwisterCheckGetDamageKind` / `ftMainSetHitHazard` — NULL guards on `fp->data`, `fp->attr`, tornado/fighter DObj | `ftcommontwister.c`, `grhyrule.c`, `ftmain.c` |
| `syNetRbSnapIsValidHyruleTwisterGObj` rejects stage-controller GObj; ApplyGround clears id lookup hits on controller | `netrollbacksnapshot.c` |
| Repair retries `MakeTwister` + obstacle when Move/Turn/Stop repair gets `invalid_gobj` | `netrollbacksnapshot.c` |

Remaining: twister **spawn timing** still depends on unconsumed shared RNG staying aligned across peers (guest Wait ending ~225 ticks after host Summon). World hash + resim recover, but brief duplicate visuals can appear until replay converges.

## 2026-05-30 follow-up (SYNCTEST verify ejects live mesh → guest Summon freeze)

Soak log `netplay-session-trimmed-hyrule.log` (post pickup-guard build): single tornado, collidable on host, desync when DK walked near Move-phase hazard at frame-commit **2880**. Root cause chain:

1. **Tick 2789 SYNCTEST verify:** `apply_drift live_gobj_id=0 blob_gobj_id=1010` → `verify_skip_respawn` + `idle_clear` ejected the live twister mesh while emergency snapshot still referenced id **1010**.
2. **Emergency restore (`tick=0xFFFFFFFF`):** `gcFindGObjByID(1010)` miss → `obstacle_fail invalid_gobj` / `active_repair obstacle=0`.
3. **Peer split from tick 2791:** host `twister_wait` decrements and enters **Move** at **2818**; guest **`twister_wait` stuck at 28** (stage proc never advances) while `world=0xB8CCDDF4` flatlines. RNG/figh diverge ~60 ticks later; inputs still agree at validation **2880**.

Fix:

| Change | Location |
|--------|----------|
| `verify_skip_all` — skip **entire** `syNetRbSnapEnsureHyruleTwisterAfterParticleReset` during synctest verify (no idle_clear / orphan eject before emergency restore) | `netrollbacksnapshot.c` |
| `syNetRbSnapFindHyruleTwisterMeshGObj` + `syNetRbSnapHyruleTwisterRebindMeshFromLink` — when id lookup fails, rebind valid mesh from ground link before orphan eject / `MakeTwister` | apply + repair |
| `invalid_gobj` obstacle retry uses `StatusNeedsObstacleForRepair` (Summon..Stop), rebind-then-`MakeTwister`, then `CompleteSummonIfReady` | repair |
| Remove `verify_skip_respawn` / `verify_skip_make_twister` live downgrades (superseded by verify_skip_all) | repair |

Soak pass addendum: no `idle_clear` / `verify_skip_respawn` at synctest probe ticks; guest/host `hyrule_twister_capture` must show matching `wait` decrement through Summon→Move; no `obstacle_fail invalid_gobj` on emergency restore.

## 2026-05-31 ROOT CAUSE (wrong GObj field: `obj_kind` vs `id`)

Soak log `netplay-session-trimmed-hyrule.log` (post `verify_skip_all` build): no crash, collisions *mostly* worked, but tornadoes **spawned as duplicates and respawned every ~120 ticks** — one `active_repair` per `SYNCTEST_OK` (2430, 2550, 2670, …), each at the emergency-restore sentinel `tick=4294967295`. Diag showed the live twister captured as `note=live_active_no_gobj` / `normalize_active_to_wait` (`live_status=3/4`, `gobj_id=0`) and every repair logged `obstacle_fail reason=invalid_gobj` + `active_repair obstacle=0`.

**The real bug:** `gcMakeGObjSPAfter(u32 id, ...)` stores its first argument in `gobj->id`. Both the Hyrule stage controller (`grHyruleMakeGround`) and the twister mesh (`grHyruleMakeTwister`) are created with id `nGCCommonKindGround` (**1010**). `gobj->obj_kind` is a *different* field — the DObj/SObj/CObj append marker (`nGCCommonAppendDObj==1` once the mesh DObj is attached, see `objman.c` `gcAddDObjForGObj`).

`syNetRbSnapIsValidHyruleTwisterGObj` and `syNetRbSnapEjectOrphanHyruleTwisterGObjs` both tested **`gobj->obj_kind != nGCCommonKindGround`** — i.e. `1 != 1010` — which is **always true**, so:

- `IsValid()` returned **FALSE for every real twister mesh**. `gcFindGObjByID(1010)` returns the controller first (then `ApplyGround` nulls it via the stage-controller guard), so `syNetRbSnapHyruleTwisterRebindMeshFromLink` was the *only* path that could recover the still-alive mesh — and it was dead code (its `IsValid` filter never matched). Result: `apply_drift live_gobj_id=0` even though the mesh was on the ground link, then `MakeTwister` spawned a fresh mesh on every probe.
- Obstacle re-registration hit the `IsValid` gate → `invalid_gobj` → `obstacle=0` (non-collidable repair mesh; collision only survived via vanilla `UpdateSummon` registration).
- Rider resync saw `IsValid==FALSE` → `ftCommonTwisterShootFighter` ejected riders (the earlier DK "approach desync").
- Orphan eject's `obj_kind != nGCCommonKindGround` `continue` skipped **every** gobj → duplicate meshes accumulated.

Every prior 2026-05-30 rebind/orphan/obstacle workaround was layered on top of this always-false predicate, so none of them actually executed.

Fix:

| Change | Location |
|--------|----------|
| `syNetRbSnapIsValidHyruleTwisterGObj` — test `gobj->id != nGCCommonKindGround` (was `obj_kind`); stage-controller + DObj checks still split mesh from controller | `netrollbacksnapshot.c` |
| `syNetRbSnapEjectOrphanHyruleTwisterGObjs` — orphan filter `gobj->id != nGCCommonKindGround` (was `obj_kind`) | `netrollbacksnapshot.c` |
| `syNetRbSnapshotSynctestShouldSkip` — new `hyrule_twister` skip (live status Summon..Subside, live `twister_gobj`, or any fighter in `nFTCommonStatusTwister`); diagnostic probe no longer tears down the particle/mesh/obstacle/rider coupling mid-flight | `netrollbacksnapshot.c` |

With `IsValid` fixed, the emergency restore rebinds the live mesh instead of respawning it (no duplicates, obstacle retained, riders rebound); the synctest skip removes the destructive probe during the fragile coupled window entirely (matching `item_hold` / `grab_coupling` / `fighter_throw` / `fox_reflector`).

**Latent same-bug elsewhere (not changed this pass):** effect-link scans `syNetRbSnap*` at `netrollbacksnapshot.c:~4920` and `~9849` test `gobj->obj_kind == nGCCommonKindEffect` (1011), also always false — "particle-shell effects with no EFStruct" are never captured/ejected. Left untouched to avoid cross-stage regression; flag for a focused effect-link pass.

Soak pass addendum: `hyrule_twister_apply_drift` must show `live_gobj_id==blob_gobj_id` (1010) after emergency restore (no longer `live_gobj_id=0`); zero `active_repair` / `obstacle_fail invalid_gobj` at `SYNCTEST_OK` cadence; `SYNCTEST_SKIP reason=hyrule_twister` during the active window.

## 2026-05-31 follow-up (abrupt end-of-life: LBParticle handling + diag + quantization)

Post-root-cause soak (`reason=hyrule_twister` skip x1406, zero `apply_drift`): respawn/duplicate churn gone, but the tornado **pops out with no dissipation animation** at end of life. Log analysis showed the full lifecycle ran on the authoritative timeline with **no synctest probe and no rollback** in the Stop→Subside window (Stop 2665 → Subside 2725, 32 ticks → Wait 2757); twister repairs during the active life were **zero** `active_repair`/`subside_fade` (only idle-tick `idle_clear`/`verify_skip_all`). So the abrupt cut is in the **vanilla LBParticle path as ported**, not the snapshot logic.

Mechanics: the twister mesh (`twister_gobj`) is created with a NULL model (`gcAddDObjForGObj(gobj, NULL)`) — it is an invisible collision/position anchor. The **visible** tornado is the `twister_xf` LBParticle (swirl, effect id 3). `grHyruleTwisterUpdateStop` ejects the (invisible) mesh, spawns a one-shot dissipation puff (effect id 7) **only `if (twister_xf != NULL)`**, and leaves the swirl running; `grHyruleTwisterUpdateSubside` ejects the swirl 32 ticks later (`lbParticleEjectStructID` → vortex soft-fade). If `twister_xf` is NULL at the Stop→Subside boundary, the puff is skipped and the swirl is already gone → instant pop.

Changes this pass:

| Change | Location |
|--------|----------|
| **Diag:** `grHyruleTwisterDiagEnabled` + `hyrule_twister_make_effect` (effect_id/pc/xf), `hyrule_twister_subside_enter` (xf/gobj at Stop→Subside — xf==NULL means fade skipped), `hyrule_twister_subside_end` (xf/gen_id at eject) | `decomp/src/gr/grcommon/grhyrule.c` |
| **LBParticle handling:** rollback Subside repair now recreates the **id-3 swirl** as `twister_xf` (was id-7 puff only) so a rollback landing mid-Subside keeps the visible tornado and the end-of-life eject still soft-fades it; adds the id-7 puff separately for parity with `grHyruleTwisterUpdateStop` | `port/net/sys/netrollbacksnapshot.c` (`syNetRbSnapEnsureHyruleTwisterAfterParticleReset` Subside branch) |
| **Quantization:** canonicalize the spawned `xf->translate` inside `grHyruleTwisterMakeEffect` (gated `syNetplaySimQuantizeActive`) so every twister effect (swirl + puff, incl. repair recreate) spawns on the shared F32 grid | `grhyrule.c` |

Diagnostic next step (gated by `SSB64_NETPLAY_HYRULE_TWISTER_DIAG=1`): confirm whether `subside_enter` logs `xf=0x0` (fade skipped — handling bug to chase in the active-window repair/particle-reset path) or `xf!=0x0` with a `make_effect effect_id=7` that nonetheless doesn't render (particle-script/render issue for id 7). The `grHyruleTwisterGetLR` fighter-joint-x vs twister-x compare is a remaining un-quantized determinism edge (folds into the turn decision / world hash) — left for a focused fighter-geometry pass.

## 2026-05-31 follow-up (cross-peer vel=50 burst / resim GetLR split)

Cross-ISA soak (`android host` + `linux guest`): live forward sim agreed on `vel=50` burst at tick 2902 on both peers, but **resim replay from load 2879** reproduced `vel=-10` (burst missed) while the ring slot still held `vel=50`. Guest log showed `speed_wait=65535` on the next tick (u16 underflow after a missed burst). Visible symptom: tornado **5× faster on one peer** during the desync window; frame-commit split at 3000 was **`figh` not `world`** (inputs still agreed).

Root cause: `grHyruleTwisterGetLR()` compared raw `fp->joints[TopN]->translate.x` vs twister DObj X without the shared F32 grid. Resim load at 2879 had `joint_anim_frame` ULP drift vs blob → `GetLR` returned 0 on replay while forward sim had `lr!=0` and rolled the burst. Secondary: `twister_speed_wait--` when already 0 wrapped u16 to 65535.

Fix:

| Change | Location |
|--------|----------|
| `grHyruleTwisterQuantizeCompareF32` + quantize twister X and fighter TopN X inside `grHyruleTwisterGetLR` when `syNetplaySimQuantizeActive` | `grhyrule.c` |
| NULL guard on `fp->joints[nFTPartsJointTopN]` in GetLR | `grhyrule.c` |
| Burst roll only on `speed_wait` 1→0 transition; skip decrement when `speed_wait==0` (no u16 wrap) | `grHyruleTwisterUpdateMove` |

Soak pass addendum: resim replay at tick 2902 must match ring `vel`/`turn_wait`; no `speed_wait=65535` after missed burst; both peers show same burst window or neither.

## 2026-05-31 follow-up (twister rider contact / `figh` drift at tick 3107+)

Cross-ISA soak (`netplay-session-trimmed-hyrule.log`): DK enters `nFTCommonStatusTwister` at tick 3096 with matched `world`/`rng`/twister scalars; **`figh` diverges at 3107** while `world` stays aligned through 3119; frame-commit split at 3120 is fighter-only. Resim from load 3000 (120 ticks before contact) could not recover — rider spiral physics (`ftCommonTwisterProcPhysics`) used unquantized `cos`/`sin`/velocity while twister DObj translate was already on the shared grid.

Root cause: cross-ISA FP drift in rider spiral target, `physics.vel_air`, root `rotate.y`, and joint translates — not folded early enough in `figh` hash (only `release_wait` + tornado DObj).

Fix:

| Change | Location |
|--------|----------|
| Netplay canonical rider pass: quantize spiral `pos`, capped `vel_air`, root translate snap, `rotate.y`, all joint translates under `syNetplaySimQuantizeActive` | `decomp/src/ft/ftcommon/ftcommontwister.c` |
| `ftCommonTwisterReconcileRiderAfterRollback` — shared repair entry for rollback load/resync/apply | `ftcommontwister.c`, `ftcommonfunctions.h` |
| Fold `physics.vel_air` into twister branch of `syNetSyncHashFighterStructLight` | `port/net/sys/netsync.c` |
| `coupled_twister_gobj_id` capture/scrub/rebind; blob light hash adds twister `vel_air`; capture/apply/resync call rider reconcile | `port/net/sys/netrollbacksnapshot.c` |

Soak pass addendum: no `FRAME_COMMIT_STATE_DIVERGE` during twister ride window; host/guest `figh` agree ticks 3096–3120 with agreed inputs; resim from tick 3000 reproduces status-60 trajectory.

## 2026-05-31 follow-up (Subside bottom-up vortex ring decay)

Forward-play soak with valid `subside_enter` / `subside_end` (32 ticks, non-NULL `twister_xf`, effect 7 spawned) still showed an **abrupt pop** at end-of-life: the Subside state machine ran, but vortex rings did not visibly decay incrementally during the 32-tick window — full swirl stayed until `lbParticleEjectGeneratorID` at `subside_end`.

Root cause: `lbParticleBeginVortexSoftFadeID` only halts **new** ring spawn (`update_rate=0`); existing vortex ring particles have long individual lifetimes and do not expire bottom-up within 32 ticks on the port.

Fix:

| Change | Location |
|--------|----------|
| `lbParticleEjectVortexBottomRingID` — eject lowest-`vel.y` vortex ring for a generator | `decomp/src/lb/lbparticle.c` |
| `lbParticleStepVortexSoftFadeID` — distribute remaining ring count evenly over Subside ticks remaining | `lbparticle.c` |
| `lbParticleGetVortexRingCountID` — diag helper | `lbparticle.c`, `lbparticle.h` |
| Call `lbParticleStepVortexSoftFadeID` each Subside tick before `twister_wait--` | `decomp/src/gr/grcommon/grhyrule.c` |
| Diag: `hyrule_twister_subside_step` logs `wait` + remaining ring count | `grhyrule.c` |

Soak pass addendum: with `SSB64_NETPLAY_HYRULE_TWISTER_DIAG=1`, `hyrule_twister_subside_step` must show `rings` decrementing across the 32-tick Subside window (not flat until `subside_end`); visible tornado should shrink bottom-up before Wait.

## 2026-05-31 follow-up (Subside fade pacing — progress-based ring removal)

Soak (`netplay-session-trimmed-hyrule.log`): Subside ran without emergency-restore interrupt, but `subside_step` showed rings hitting 0 by tick ~4206 / ~7306 while `twister_wait` still had ~20+ ticks left — visible swirl gone early, then empty Subside tail reads as abrupt pop.

Root cause: first-pass `StepVortexSoftFade` used `ceil(rings_remaining / wait)` ejections per tick against `generator_vars.vortex.lifetime`, clearing the (already small) ring pool in ~N ticks instead of pacing across the full 32-tick window; lifetime counter also desynced from live vortex structs (effect-7 puff briefly bumped `rings` after hitting 0).

Fix:

| Change | Location |
|--------|----------|
| Snapshot initial vortex **struct** count in `lbParticleBeginVortexSoftFadeID` | `lbparticle.c` |
| `lbParticleStepVortexSoftFadeID(gen, wait, 32)` — target removed = `ceil(initial * elapsed / 32)`; eject bottom rings until live count matches | `lbparticle.c`, `grhyrule.c` |
| `lbParticleGetVortexRingCountID` — prefer live struct count for diag | `lbparticle.c` |

Soak pass addendum: `subside_step` rings should track roughly linearly  initial→0 over ticks enter..enter+31, not collapse in the first ~10 ticks with a long `rings=0` plateau.

## Related

- [`netrollback_snapshot_upgrades_2026-05-25.md`](netrollback_snapshot_upgrades_2026-05-25.md) — Hyrule ground blob baseline
- [`netplay_link_bomb_rollback_2026-05-29.md`](netplay_link_bomb_rollback_2026-05-29.md) — item cross-ISA work
- [`netplay_yoster_cloud_rollback_2026-05-29.md`](netplay_yoster_cloud_rollback_2026-05-29.md) — Yoster cloud rebind pattern
- [`netplay_dk_jungle_tarucann_crash_2026-05-30.md`](netplay_dk_jungle_tarucann_crash_2026-05-30.md) — rider resync template for Twister fix
