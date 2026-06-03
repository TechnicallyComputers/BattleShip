# Sector Z intro facing + rebirth spawn quantize (netplay)

**Date:** 2026-06-02  
**Scope:** `decomp/src/gr/grcommon/grsector.c`, `port/net/sys/netplay_sim_quantize.c`, `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED — soak pending (cross-ISA Sector Z intro + rebirth)

## Symptoms

Cross-ISA soak on Sector Z (`AUTOMATCH_STAGE_KIND=1`): fighters face wrong directions during character AppearL/Wait intro; Kirby rebirth platform spawns at wrong location (~tick 2789–3045). Persistent `ring_save_player full_ok=0` despite prior intro joint-rotate patch.

## Root cause

1. **Sector Z intro parent transforms** — Generic intro joint quantize did not canonicalize the Great Fox / Arwing **map DObj tree** driving AppearL entry; cross-ISA ULP on stage anim joints skewed world-facing while aggregate `figh`/`anim` oracles agreed.

2. **Rebirth vs deck canonicalize** — `syNetRbSnapshotCanonicalizeSectorArwingDeckFighter` and deck platform refresh ran during `RebirthDown..Wait`, fighting `syNetplayCanonicalizeRebirthFighterMapPose` halo Y derivation.

3. **Rebirth finalize order** — `syNetRbSnapRestoreRebirthFightersAfterFinalize` ran map-pose canonicalize before restoring TopN joint translate from blob, then never re-derived rebirth root Y.

4. **Ring save oracle** — `blob->physics` / `blob->coll` captured **before** `syNetplayCanonicalizeFighterSimState`, so live full hash vs slot blob always diverged (`full_ok=0` diag noise).

5. **Common Appear yaw (2026-06-02 follow-up)** — `ftCommonAppearSetStatus` stores the intended facing in
   `status_vars.common.entry.lr`, then clears `fp->lr` before `ftMainSetStatus`; `ftMainSetStatus` initializes
   `TopN.rotate.y` from the cleared `fp->lr`. Character AppearR/AppearL status selection was correct, but the
   visible TopN yaw stayed neutral unless the Captain `is_rotate` special case overwrote it.

6. **Ring save oracle NULL joints (2026-06-02 follow-up)** — blob full/anim hashes folded every joint slot,
   including NULL live joints as zeroed blob data, while live full/anim hash walkers skip NULL joints.

7. **Save-time live pose snap (2026-06-02 follow-up)** — the first follow-up canonicalized Sector map DObjs
   and fighter DObj/joint translates during snapshot **save** to make blob hashes match. That made snapshot
   capture mutate the same live presentation tree rendered later in the frame, so both peers could agree on
   hashes while fighters appeared shifted by the quantization pass.

8. **Signed-zero / p-translate oracle noise (2026-06-02 follow-up)** — Yoshi AppearL still produced anim hash
   mismatches after NULL-joint masking; the remaining pattern matched signed-zero AObj scalar drift. Full hash
   also still folded TopN as `p_translate` unconditionally in blob hashes while live only folds it when
   `coll_data.p_translate != NULL`.

## Fix

| Change | Purpose |
|--------|---------|
| Export `grSectorArwingCanonicalizeSimState`; `syNetplayQuantizeDObjAnimPose` on map tree walk | Full translate+rotate+scale grid on Sector map DObjs |
| `syNetplaySectorArwingIntroMapScopeActive` + `syNetplayCanonicalizeSectorArwingIntroMapPose` (once/tick) | Intro/Wait Sector Z map canonicalize on live sim + snapshot save/apply |
| Gate deck canonicalize + platform refresh when fighter/slot in rebirth scope | Stop deck Y/vel_speed from overriding rebirth halo descent |
| Rebirth finalize: TopN restore then `syNetplayCanonicalizeRebirthFighterMapPose`; quantize TopN on rebirth map pose | Correct respawn Y after load |
| Capture fighter `physics` / joint translate / map-coll scalars **raw** (drop capture-time quantize); `syNetRbSnapRefreshFighterBlobSimScalarsFromLive` mirrors raw live coll | Blob mirrors the raw committed state the FC token hashes (`full_ok=1`); quantize stays at apply |
| Common Appear non-rotate path sets `TopN.rotate.y = entry.lr * 90deg` | Preserve intended visible facing while `fp->lr` is temporarily zero |
| `joint_is_valid[]` in fighter blobs; skip NULL joints in blob full/anim hashes | Make `full_ok` / `anim_ok` comparable to live hash walkers |
| Remove save-time `syNetplayCanonicalizeFighterSimState` / Sector map canonicalize | Snapshot save quantizes blob copies without moving live rendered DObjs |
| Clamp quantized zero to `+0.0f`; quantize AObj `length_invert` | Avoid signed-zero / scalar-only anim hash noise in Appear animations |
| Track `coll_p_translate_valid` in fighter blobs | Mirror live full hash conditional TopN-as-`p_translate` fold |

## Correction (2026-06-02 follow-up)

Item 4's reconcile (`syNetRbSnapRefreshFighterBlobSimScalarsFromLive`) was first listed as shipped but **never landed**, so
connection soak (`netplay-session-trimmed-connection.log`) still showed `ring_save_player full_ok=0` on 100% of ticks (Sector Z
Fox vs Pikachu, both peers). The peripheral pieces (`joint_is_valid[]`, `coll_p_translate_valid`) had landed; the core reconcile
had not.

A first attempt at the reconcile (snap the **live** `fp->physics` / `fp->coll_data` to the grid in place, then refresh the blob)
still left `full_ok=0`: the blob's joint translate was quantized at capture by `syNetplayQuantizeVec3f`, while the live full hash
folds the joint translate already produced by the in-sim `gcPlayDObjAnimJoint` → `syNetplayQuantizeDObjAnimPose` hook. The two
quantize passes do not round-trip identically, so blob joint != live joint. It also mutated the live committed sim every save.

**Final approach — blob mirrors raw.** `syNetSyncHashBattleFightersFull()` (the cross-peer frame-commit enforcement token) folds
the **raw** live `FTStruct`: physics velocities, map-collision `pos_prev`/`pos_diff`/velocities, and all joint translates (whose
live value is the in-sim-hook output). The blob is consumed only by `syNetRbSnapApplyFighter`, which re-canonicalizes the whole
fighter through `syNetplayCanonicalizeFighterSimState` on apply — so the blob never needs to be pre-quantized at capture.

- Removed the inline `syNetplayQuantizeFighterPhysics(&blob->physics)` and `syNetplayQuantizeVec3f(&blob->joint_translate[ji])`
  in `syNetRbSnapCaptureFighter` (physics + joints now captured raw; joints copy the in-sim-quantized live value verbatim).
- `syNetRbSnapCaptureMPColl` is shared with items/weapons, so it is left untouched; `syNetRbSnapRefreshFighterBlobSimScalarsFromLive`
  now mirrors the raw live map-collision scalars back into the fighter blob (no live mutation).
- Reverted the live-state mutation: the forward-sim `FTStruct` is no longer altered by the save path.

Net effect: `blob_full == live_full == slot->hash_fighter`, so `full_ok`/`blob_ok`/`figh_ok` agree and the oracle is a clean
serialization check. Quantization for cross-ISA convergence stays where it belongs — on apply (`syNetplayCanonicalizeFighterSimState`)
and in the in-sim joint hook — and the committed forward sim is no longer perturbed at save time. The FC enforcement token is
unchanged.

## Correction 2 — blob hash folded the wrong fields (2026-06-02 follow-up)

After the raw-capture change deployed, `full_ok=0` still held on 100% of ticks from tick 0 (`vel_y_live == vel_y_blob`,
`anim_ok=1`, `figh_ok=1`, `blob_ok=0`). `figh_ok=1` (live self-consistent) with `blob_ok=0` (blob vs live) localized the defect to
`syNetRbSnapHashFighterBlobLight` itself — a **wrong-field-source**, not a value/quantize problem. Both fold paths quantize via
`syNetplayQuantizeF32` (1/65536 grid, signed-zero clamped) and the FNV/F32 primitives agree (`anim_ok=1` proves it), so the
divergence had to be a field folded in *full* but not *anim*.

**Root cause:** the live light hash folds `*coll_data.p_translate`, which `ftManagerMakeFighter` sets to
`&DObjGetStruct(fighter_gobj)->translate.vec.f` — the fighter **root GObj DObj** (world position). The blob folded the **TopN
joint** translate (`top`) for that slot instead. TopN joint ≠ root DObj (`ftmain.c:4769` copies transforms between them), and
`coll_data.p_translate` is non-NULL for the entire match, so `blob_full != live_full` on every tick from spawn. The blob already
captured the correct value as `blob->gobj_translate`; the fold just read the wrong field.

Fix + the three status-specific structural gaps in the same pass (`syNetRbSnapHashFighterBlobLight`):

| Branch | Live folds | Blob now folds |
|--------|-----------|----------------|
| `p_translate` (every tick) | `*coll_data.p_translate` (root DObj world pos) | `blob->gobj_translate` (was: TopN joint translate) |
| Twister | `tornado_gobj->id` + `DObjGetStruct(tornado_gobj)->translate` (if DObj) | `coupled_twister_gobj_id` + new `twister_tornado_translate` (captured raw in `…CaptureFighterCoupledIds`, gated on `twister_tornado_dobj_valid`); dropped the bogus `gobj_translate` fold |
| CaptureYoshi / YoshiEgg | breakout_wait/lr/ud, `motion_vars.flag0`, captureyoshi stage/breakout_wait/lr/is_damagefloor, egg-lay effect id | mirrored from blob fields + `status_vars` union + `captureyoshi_effect_gobj_id` (branch was entirely absent) |
| Sector Z Arwing deck | `coll.vel_speed` + `floor_flags` when on deck (`floor_line_id==1`) | same, gated on `syNetRbSnapshotSectorArwingDeckCollisionLive()` + `blob->coll.floor_line_id == 1` (branch was entirely absent) |

The Twister/CaptureYoshi branches never fire in the Sector Z Fox-vs-Pikachu soak (no tornado, no Yoshi), so they did not affect
that log — but they would have re-pinned `full_ok=0` during Hyrule tornado capture, Yoshi egg-lay, and Arwing-deck play. New blob
fields `twister_tornado_translate` / `twister_tornado_dobj_valid` live in local ring memory (never sent over the wire).

## Correction 3 — rollback dropped joint rotate/scale (facing forward after GO) (2026-06-02 follow-up)

After Correction 2, the connection soak still showed two coupled symptoms: persistent `full_ok=0` lingering in
some scopes, and — the visible one — **fighters facing the camera** (a normally-impossible direction) after the
GO / intro animations, with a real `figh`/`anim` hash divergence at the `LOAD_HASH_DRIFT` around tick 887.

**Root cause:** the rollback snapshot (`SYNetRbSnapFighterBlob`) captured and restored only joint **translate**
(`joint_translate[]`) plus the AObj anim cursor — never per-joint **rotate** or **scale**. Facing yaw lives in
`joints[nFTPartsJointTopN]->rotate.y` (= `fp->lr * 90deg`), which is set on status entry and, during the
intro, rewritten every frame by `ftCommonAppearProcPhysics`. While Appear is self-healing the yaw, a rollback is
invisible. After GO the yaw is set **only on status entry**, so a rollback that loads a post-GO snapshot (or
crosses the intro→GO boundary) leaves `TopN.rotate.y` at the model-neutral (camera-facing) value left by the
last figatree pose. The aggregate `figh`/`anim` oracles only folded joint **translate**, so the rotate gap was
invisible to them until it perturbed downstream sim state at tick 887.

Fix (all in local ring memory, never sent over the wire):

| Change | File | Purpose |
|--------|------|---------|
| Add `joint_rotate[]` / `joint_scale[]` to `SYNetRbSnapFighterBlob` | `netrollbacksnapshot.c` | Carry the full joint pose, not just translate |
| Capture `rotate`/`scale` next to `joint_translate` in `syNetRbSnapCaptureFighter` | `netrollbacksnapshot.c` | Snapshot the committed pose raw (in-sim-hook-quantized value verbatim) |
| Restore `rotate`/`scale` + `syNetplayQuantizeVec3f` in the apply joint loop | `netrollbacksnapshot.c` | Rewind restores facing; resim only refines child joints, leaves TopN yaw intact |
| Fold joint `rotate` after `translate` in `syNetSyncFoldFighterSlotFullContribution` (live) and `syNetRbSnapHashFighterBlobFull` (blob), same order | `netsync.c`, `netrollbacksnapshot.c` | Oracle now catches facing desyncs; closes the `full_ok` gap |

**FC-token safety:** joint rotate is quantized in-sim every frame by `gcPlayDObjAnimJoint` →
`syNetplayQuantizeDObjAnimPose` (objanim.c:1007), and quantized again in the fold, so adding it to
`syNetSyncHashBattleFightersFull()` is cross-ISA safe for the same reason joint translate already is — both peers
land on the same 1/65536 grid point. The live and blob folds iterate the same joint set (`fp->joints[ji] != NULL`
≡ `blob->joint_is_valid[ji]`) in the same order, so `blob_full == live_full` continues to hold.

## Correction 4 — root GObj pose was still pre-quantized on save (2026-06-02 follow-up)

After the global repro showed `full_ok=0` from tick 0 on every stage with `figh_ok=1` / `anim_ok=1`, the remaining
always-on mismatch was in the raw-capture contract itself: `syNetRbSnapCaptureFighterGobjPose` still quantized
`blob->gobj_translate` / `blob->gobj_rotate` during save. The live full hash folds the raw live root DObj through
`coll_data.p_translate`, then quantizes inside the hash fold; pre-quantizing the blob copy could keep
`blob_light != live_light` even when the live sim was otherwise clean.

Fix: capture the fighter root GObj pose raw, just like physics and joint transforms. Apply still quantizes the
loaded root pose before resim. Diagnostics now print `blob_light` next to `live_light` and include root
`gobj_translate` / `gobj_rotate.y` plus TopN / first-joint rotate field diffs under
`SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`, so any remaining `full_ok=0` points to the exact field rather than
only the aggregate hash.

## Correction 5 — blob light hash folded tap/hold after `pos_diff` (2026-06-02 follow-up)

The next global repro still showed `full_ok=0` from tick 0, but the new diagnostics narrowed it to
`live_light != blob_light` while `anim_ok=1` and sampled live/blob physics bits matched. The remaining always-on
structural mismatch was hash order: live `syNetSyncHashFighterStructLight` folds tap/hold stick counters before
`coll_data.pos_diff`, while `syNetRbSnapHashFighterBlobLight` folded `pos_diff` first. FNV is order-sensitive, so
identical field values still produced different hashes on every fighter and stage.

Fix: reorder the blob light fold to match live exactly: `tap_stick_x/y`, `hold_stick_x/y`, then
`coll.pos_diff.xyz`, then `coll_p_translate` / `gobj_translate`.

## Correction 6 — fighter quantization was rollback-load-only (2026-06-02 follow-up)

After `blob_ok=1` stabilized the save-side oracle, global visual/respawn corruption still reproduced with
`SSB64_NETPLAY_ROLLBACK_SYNCTEST=0`. That ruled out the diagnostic emergency-restore path and exposed a deeper
asymmetry: rollback apply canonicalized fighter physics, map collision, root pose, and joint pose, while
uninterrupted forward sim did not run the same broad fighter canonicalization at the accepted post-tick boundary.
Hashes could still agree because the hash folds quantize before comparing, but rollback loads were mutating the
raw live fighter state in a way forward play did not.

Fix: add `syNetplayCanonicalizeActiveFightersForNetplay()` and call it from `syNetRollbackAfterBattleUpdate()`
before post-tick snapshot save/hash, alongside the existing active-item canonicalization. The shared-grid fighter
pass is now a sim-boundary invariant instead of a rollback-load side effect.

## Test plan

1. Sector Z cross-ISA (Kirby vs Yoshi): correct facing at Wait; no sideways AppearL poses.
2. Stock loss rebirth on Sector Z: halo/platform at stage center on both peers; no visible X/Y teleport.
3. Intro ticks 0–150: `ring_save_player full_ok=1` when quantize on (both peers).
4. Rebirth window: no `sector_arwing_deck` platform refresh mutating rebirth fighter coll.
5. Sector Z Arwing deck (fighter standing on deck, `floor_line_id==1`): `full_ok=1` sustained.
6. Hyrule tornado capture (Twister status): `full_ok=1` while caught in the tornado.
7. Yoshi egg-lay (CaptureYoshi / YoshiEgg): `full_ok=1` through the capture/breakout cycle.
8. Post-GO rollback (force a `LOAD_HASH_DRIFT` after intro): fighters keep sideways facing on both peers; no camera-facing pose. `figh`/`anim` hashes match across the load.

Related: [`netplay_intro_facing_quantize_2026-06-02.md`](netplay_intro_facing_quantize_2026-06-02.md), [`netplay_sector_arwing_yoshi_deck_2026-06-01.md`](netplay_sector_arwing_yoshi_deck_2026-06-01.md), [`netplay_rebirth_halo_snapshot_2026-05-27.md`](netplay_rebirth_halo_snapshot_2026-05-27.md).
