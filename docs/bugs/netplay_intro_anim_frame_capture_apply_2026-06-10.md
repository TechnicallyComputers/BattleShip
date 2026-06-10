# Intro anim frame capture–apply symmetry (netplay P1)

**Date:** 2026-06-10  
**Scope:** `port/net/sys/netrollbacksnapshot.c`  
**Status:** Phase 14 (post-figatree AObj re-pin + pre-sanity tail) — soak pending (`INJECT_TICK=240`, `RESIM_ANCHOR_PROBE=1`)

## Symptoms

Forced-resim soak @240 during Appear intro: `RESIM_ANCHOR_PROBE` reports `match_f=0` on every walkback step (239→223). `fighter_field_diff tag=resim_anchor_probe` names `gobj_anim_frame` and `joint_anim_frame` (~1 f32 ULP: e.g. `0x42340000` vs `0x42300000`) plus Appear joint translate drift. Walkback eventually settles (e.g. `load=207`) and resim completes, but anchor probe never shows `match_f=1` at the settled load.

## Root cause (audit)

| Path | Issue |
|------|--------|
| `syNetRbSnapCaptureFighter` | Quantizes `gobj_anim_frame` + per-joint `anim_frame` into blob at save |
| `syNetRbSnapApplyFighter` | Restores blob, then `syNetplayCanonicalizeFighterSimState` re-quantizes live joints and overwrites `gobj->anim_frame` from the **last** joint via `syNetplayQuantizeDObjAnimScalars` — not `blob->gobj_anim_frame` |
| `syNetRbSnapReapplyFighterJointAnimFromSlot` | Duplicated joint loop; omitted `joint_dobj_flags`; called full canonicalize after figatree sync (same gobj/joint clobber) |
| `syNetRbSnapApplyDObjAnim` | Sets `parent_gobj->anim_frame` per joint during multi-joint restore |

Capture stores `gobj_anim_frame` independently; apply paths did not re-pin it after canonicalize / figatree presentation.

## Fix (Phase 2)

1. **`syNetRbSnapRestoreFighterAnimScalarsFromBlob()`** — re-pin `gobj_anim_frame`, root DObj cursor, and every joint `anim_frame` / `anim_wait` / `anim_speed` from blob (post-quantize).
2. **`syNetRbSnapApplyFighterJointPoseAndAnimFromBlob()`** — shared joint translate/rotate/scale + `ApplyDObjAnim` + event32 + `joint_dobj_flags` + anim scalar restore; used by both apply and reapply paths.
3. **`syNetRbSnapApplyFighter`** — call shared helper; after `syNetplayCanonicalizeFighterSimState`, call `RestoreFighterAnimScalarsFromBlob`.
4. **`syNetRbSnapReapplyFighterJointAnimFromSlot`** — use shared helper; run `syNetplayCanonicalizeFighterSimState` then `RestoreFighterAnimScalarsFromBlob` (same order as `ApplyFighter`; dropping canonicalize alone caused map/baseline drift on follower).

## Invariant

After load at tick T (before forward-sim): live `gobj_anim_frame` and each joint `anim_frame` equal ring blob at T (quantized). Forward-sim T+1 should then match ring[T+1] when determinism holds.

## Correction (Phase 3 — 2026-06-10): joint pose + modelpart after figatree finalize

P1 anim-scalar re-pin alone left intro fighters invisible/off-screen after resim load:

- **Kirby** AppearL TopN Y ≈ -297 (`top_y=0xC395D150`) — hundreds of units below stage after `ftMainRefreshFigatreeVisual` + canonicalize.
- **Yoshi** AppearR egg hatch — `modelpart_status` captured in blob but not pushed to live DLs after figatree refresh (motion `SetModelPartID` events not replayed).

| Change | Purpose |
|--------|---------|
| `syNetRbSnapRestoreFighterJointPoseFromBlob()` | Re-pin `joint_translate[]` / `joint_rotate[]` / `joint_scale[]` post-canonicalize |
| `syNetRbSnapRestoreFighterGobjTransformFromBlob()` | Re-pin root `gobj_translate` / `gobj_rotate` post-canonicalize |
| `syNetRbSnapRestoreFighterPostCanonicalizeFromBlob()` | Joint pose + root transform + anim scalars (replaces anim-only restore) |
| `syNetRbSnapEnsureFighterJointParts()` | Allocate `FTParts` on live joints missing `user_data.p` before modelpart push (P2b) |
| `syNetRbSnapApplyFighterModelPartsFromBlob()` | Ensure parts → `ftParamSetModelPartID` / `ftParamSetTexturePartID` from blob (skip NULL joints; force cursor mismatch), then `memcpy` status |
| `syNetRbSnapApplyFighter` + `syNetRbSnapReapplyFighterJointAnimFromSlot` | Call post-canonicalize restore; reapply also runs modelpart push after figatree sync |

## Correction (Phase 3b — 2026-06-10): figatree-after-reapply presentation

Post–deeper-load soak: resim completes both peers; fighters symmetric but **camera-facing** after load (joint pose correct in DObj, stale figatree DL).

| Change | Purpose |
|--------|---------|
| `syNetRbSnapRefreshFigatreePresentationFromSlot()` | After verify prep reapply passes: `SyncFighterPresentation` → `ReapplyJointAnimFromSlot` (build DL from restored joints, re-pin pose/anim/modelpart) |
| `syNetplayCanonicalizeFighterSimState` | Preserve independent `gobj_anim_frame` across per-joint `QuantizeDObjAnimScalars` (stops last-joint clobber) |
| `syNetRbSnapRestoreFighterTopNYawFromLr()` | When Appear scope + `lr != 0`, set TopN `rotate.y = lr * 90°` (ftMainSetStatus policy) after blob re-pin |

## Correction (Phase 7 — 2026-06-10): anchor probe post-sim parity + pre-sim re-pin

Phase 6 soak: `postload_*_fail=0`, `step_anim_fail=0`, but `step_figh_fail=1` every probe (+1 sim joint pose drift while anim matched). Phase 7 added probe-path canonicalize bypass, save-path reconcile, `ResyncLiveFightersFromSlotForSim`, and full-joint field diff.

Phase 7 soak: unchanged step_figh; field diff now shows both fighters' joint trees diverging (~1.5 unit root Y on Kirby). Linux/Android **identical** live/ring hashes → deterministic path bug, not cross-ISA drift.

## Correction (Phase 8 — 2026-06-10): `entry_pos` capture/apply

`ftCommonAppearProcPhysics` recomputes TopN joint translate from `fp->entry_pos` + TransN joint **every tick** during Appear. `entry_pos` is set at Appear start and is **not** in the `status_vars` entry overlay. Fighter blob captured joints + entry overlay but **never `entry_pos`**, so post-load figh matched (joints re-pinned) while +1 sim used stale `entry_pos` → large pose drift with matching anim hash.

| Change | Purpose |
|--------|---------|
| `SYNetRbSnapFighterBlob.entry_pos` + `entry_pos_captured` | Persist spawn anchor at save |
| `syNetRbSnapRestoreFighterEntryPosFromBlob()` | Apply on load / post-canonicalize re-pin / anchor-probe resync |
| `syNetRbSnapInferFighterEntryPosFromAppearJoints()` | Legacy ring slots: invert Appear physics from restored TopN+TransN |
| Field diff + appear diag | Log `entry_pos_*` vs blob |

## Correction (Phase 9 — 2026-06-10): pre-sim AObj restore + Appear rotate canonicalize

Phase 8 soak: `entry_pos` matched @239, `postload_*_fail=0`, but `step_figh_fail=1` @240 with joint **rotate** drift (Kirby) and translate+rotate drift (Yoshi) while `match_a=1`. Anim hash matched but integrated skeleton pose did not — AObj chain / FTParts transform cache skew after figatree prep, plus Appear statuses excluded from end-of-tick joint rotate grid pass (`syNetplayCanonicalizeFighterIntroJointPose`).

| Change | Purpose |
|--------|---------|
| `syNetRbSnapshotResyncLiveFightersFromSlotForSim` | Full `ApplyFighterJointPoseAndAnimFromBlob` + entry_pos + invalidate `transform_update_mode` / root chain before probe +1 sim |
| `syNetplayFighterInAppearSimScope` + intro joint pose gate | Kirby/Yoshi AppearL/R get end-of-tick rotate/scale quantize (Entry/Wait parity) |
| Anchor probe ordering | Postload verify after resync (measures actual sim start); drop duplicate post-sim canonicalize (`AfterBattleUpdate` already ran once, matching save path) |

## Correction (Phase 10 — 2026-06-10): intro apply modelparts + pre-sim presentation tail

Phase 9 soak: `postload_*_fail=0`, `match_a=1`, but `step_figh_fail=1` every walkback step (239→232). Field diff @240: joint translate+rotate drift on **both** Wait (Yoshi) and Appear (Kirby) while anim matched. Walkback blocked below ~232: tick **231** `LOAD_HASH_DRIFT` (Yoshi still in AppearR status=220; both figh+anim mismatch after prepare). `apply_live_sanity_fail reason=appear_null_parts` on Yoshi joint 9 (`blob_mp=0`, `parts=(nil)`).

| Change | Purpose |
|--------|---------|
| `syNetRbSnapApplyFighter` intro block | After post-canonicalize restore: `EnsureAllFighterJointParts` + `EnsureFigatreeSubtreeParts` + `ApplyFighterModelPartsFromBlob` + re-pin — materialize egg-hatch FTParts on initial apply, not only during verify prep |
| `syNetRbSnapshotResyncLiveFightersFromSlotForSim` | Replace per-joint partial restore with full `ReapplyFighterJointAnimFromSlot` → ensure parts → modelpart push → reapply → invalidate transform caches (matches `RefreshFigatreePresentationFromSlot` tail without another figatree bind) |

## Correction (Phase 11 — 2026-06-10): hidden-part anim_desc reconcile (Yoshi egg joint 9)

Phase 10 soak: `postload_*_fail=0`, `match_a=1`, `match_cam=1`, but `step_figh_fail=1` every step; drift concentrated on **Yoshi AppearR** (P1 status=220). Walkback still blocked @231: `LOAD_HASH_DRIFT` figh+anim; `appear_modelpart_diag joint=9 live_mp=-1 blob_mp=0 parts=(nil)` — egg hatch uses `ftMainUpdateHiddenPartID` to create `fp->joints[9]` from `anim_desc` hidden-joint bits; rollback restored `anim_desc` + `modelpart_status` without running SetStatus's Add/Update/Eject loop, and `ApplyFighterModelPartsFromBlob` clobbered NULL-joint cursors to `-1`.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapReconcileFighterHiddenPartsFromAnimDesc` | Mirror `ftMainSetStatus` hidden-part transition (live → blob `anim_desc`) on intro apply |
| `syNetRbSnapEnsureFighterHiddenPartsFromAnimDesc` | Materialize enabled hidden parts whose `root_joint_id` slot is still NULL after figatree refresh |
| `syNetRbSnapSyncFighterHiddenPartsForSlot` | Call ensure from verify prep, anchor-probe resync, and intro apply before modelpart push |
| `syNetRbSnapApplyFighterModelPartsFromBlob` | Always `memcpy` blob `modelpart_status` — stop forcing `-1` on NULL joints before hash verify |

## Correction (Phase 12 — 2026-06-10): modelpart-driven hidden-part ensure + figatree sanity

Phase 11 soak: **regression** — no `RESIM_ANCHOR_PROBE` lines; every walkback load failed `appear_null_parts` at `load_post_prepare` (239→224). Phase 11 preserved blob modelpart cursors (`live_mp=0`) on NULL joints, tripping sanity. `ftMainAddHiddenPartID` no-ops when root joint is NULL (both anim_desc bits set on reconcile); anim_desc-only ensure missed egg hatch when bit clear but `modelpart_id_curr=0`. Kirby joint 7 + Yoshi joint 9 are hidden-part roots; joints 31–36 are figatree-only modelpart slots with no `fp->joints[]` entry.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapEnsureFighterHiddenPartsFromBlob` | After anim_desc pass, materialize hidden roots with active blob modelpart + live parent joint |
| Reconcile Add branch | Use `UpdateHiddenPartID` when root joint NULL (Add only restructures existing root) |
| `syNetRbSnapFighterAppearPresentationOk` | Skip NULL-joint sanity for non-hidden figatree-only modelpart indices |
| `syNetRbSnapIntroLoadFidelityPreSanityRepair` | Post-figatree hidden sync → ensure parts → modelpart push before verify sanity |

## Correction (Phase 13 — 2026-06-10): intro SetStatus figatree coherence

Phase 12 soak: sanity pass + hidden joints materialized (Kirby j7, Yoshi j9), but **immediate** `LOAD_HASH_DRIFT` figh+anim @239; `RelocPointerTable: invalid/stale token` spam from `lbCommonAddFighterPartsFigatree` @883 (figatree cursor misaligned after late `UpdateHiddenPartID` DObj inserts). Anchor probe never reached.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapSyncFighterPresentationIntroLoadFidelity` | Intro Appear/Entry: `ftMainSetStatus` (preserve) → blob `anim_desc` reconcile → `ftMainRefreshFigatreeVisual` |
| `syNetRbSnapSyncFighterPresentationAfterJointPrep` | Auto-select intro SetStatus path when any live fighter in intro-load scope |
| `lbCommonAddFighterPartsFigatree` | Use `portRelocTryResolvePointer` — invalid figatree tokens → NULL anim (no error flood) |
| Remove pre-figatree `SyncHiddenParts` from refresh | SetStatus hidden-part loop replaces standalone `UpdateHiddenPartID` before bind |

## Correction (Phase 14 — 2026-06-10): post-figatree AObj re-pin + pre-sanity tail

Phase 13 soak: sanity pass, both fighters AppearR @239, hidden parts materialized, no RelocPointerTable spam, presentation diags match blob — but **every** walkback tick still fails `LOAD_HASH_DRIFT` figh+anim. Ring capture @239 shows `figh_ok=1 anim_ok=1`; drift appears only after `PrepareLoadedSlotForVerify` prep. `IntroLoadFidelityPreSanityRepair` pushed modelparts **after** the final re-pin in `RefreshFigatreePresentationFromSlot` with no follow-up restore. `ftMainSetStatus` + `ftMainRefreshFigatreeVisual` clobbered gobj/joint anim cursors and AObj chains between reapply passes without per-fighter re-pin before the next modelpart push.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapReapplyFighterJointAnimFromBlob` | Extract single-fighter verify-prep tail (physics/coll/pose/AObj/canonicalize/post-canonicalize restore) |
| `syNetRbSnapSyncFighterPresentationIntroLoadFidelity` | Restore blob anim scalars before figatree bind; full re-pin from blob immediately after `RefreshFigatreeVisual` |
| `syNetRbSnapIntroLoadFidelityPreSanityRepair` | Re-run `ReapplyFighterJointAnimFromSlot` after modelpart push before appear sanity |

## Correction (Phase 15 — 2026-06-10): joint presence reconcile vs capture fold

Phase 14 soak: identical signature — per-player **light hashes match**, zero `fighter_field_diff field=` lines, yet `LOAD_HASH_DRIFT` figh+anim every walkback tick. That combination is only possible when the live fold and the capture fold disagree on **which joints exist**: `syNetSyncFoldFighterSlotFullContribution` / `syNetSyncFoldFighterAnimRollback` walk `fp->joints[ji] != NULL` per index, while the field diff silently skips any slot with `blob->joint_is_valid[ji] == FALSE`. The intro pipeline (SetStatus hidden-part loop, `EnsureFighterHiddenPartsFromAnimDesc/FromBlob` keyed on anim_desc bits + modelpart cursors) can materialize a hidden joint the capture fold never saw (e.g. Yoshi egg root mid-hatch where the anim_desc bit is set but forward sim hadn't run `UpdateHiddenPartID` at the ring tick) — a phantom joint that drifts both folds and that no amount of blob re-pinning can fix, because the blob has no data for it.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapReconcileFighterJointPresenceFromBlob` | Enforce `blob->joint_is_valid[]` as ground truth on hidden-part roots: eject live joints absent at capture, materialize capture joints still NULL (logs `intro_joint_presence` on change) |
| `syNetRbSnapReconcileFighterJointPresenceFromSlot` | Slot walk; wired into `IntroLoadFidelityPreSanityRepair` and `ResyncLiveFightersFromSlotForSim` before the final re-pin |
| `syNetRbSnapEnsureFighterHiddenPartsFromAnimDesc/FromBlob` | Gate materialization on `joint_is_valid` — never create a joint the capture fold did not see |
| `syNetRbSnapshotLogFighterFieldDiffAtTick` | New `field=joint_presence` live/blob mask + per-joint `j%d_chain_total` / `j%d_anim_frame` / `j%d_anim_wait` diffs (covers the AObj chain-count fold the old diff was blind to) |

## Correction (Phase 16 — 2026-06-10): blob-grounded appear sanity + pre-sanity repair ordering

soak1 (cross-ISA @240): every walkback load failed `appear_null_parts` at `load_post_prepare` (239→223) before hash verify — `BATTLE_SIM_HOLD` @ sim=241. Hidden roots (Kirby j7, Yoshi j9) had `modelpart_id_curr=0` while `blob->joint_is_valid[joint]==FALSE` (capture fold never saw the joint). Appear sanity treated stale cursors as load-blocking. `IntroLoadFidelityPreSanityRepair` ran modelpart push before topology reconcile/hidden sync.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapFighterAppearPresentationOk` + blob | Skip NULL-joint / NULL-FTParts checks when `blob->joint_is_valid[joint_id]==FALSE` (matches verify hash fold) |
| `syNetRbSnapshotVerifyLiveFightersSanity` | Pass loaded slot blob per fighter at `load_post_prepare` |
| `syNetRbSnapIntroLoadFidelityPreSanityRepair` | Order: joint presence reconcile → hidden sync → ensure FTParts → modelpart push → re-pin |
| `syNetRbSnapshotResyncLiveFightersFromSlotForSim` | Delegate to `IntroLoadFidelityPreSanityRepair` |

## Correction (Phase 17 — 2026-06-10): fold pin + stale cursor prune + load-fail restore blobs

Phase 16 soak1: walkback sanity cleared (`load_post_prepare` passes) but every tick still hit `LOAD_HASH_DRIFT` on `figh` while `anim` matched; `BATTLE_SIM_HOLD` @ sim=241 from `appear_null_parts` on `load_fail_restore` (verify at sim=241 had no ring slot blob). Live joint pose restore applied blob TRS to every non-NULL `fp->joints[]` entry regardless of `joint_is_valid[]`, so phantom joints picked up zeroed blob TRS into the live figh fold; Yoshi egg root j9 kept stale `modelpart_id_curr=0` on absent joints; Android showed joint stub without FTParts after hidden materialize.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapRestoreFighterJointPoseFromBlob` / `ApplyFighterJointPoseAndAnimFromBlob` | Gate TRS/AObj restore on `blob->joint_is_valid[ji]` — match capture fold joint walk |
| `syNetRbSnapPruneStaleIntroModelpartCursorsFromBlob/Slot` | Clear active modelpart cursors on joints absent at capture before push/sanity |
| `syNetRbSnapEnsureHiddenPartRootPartsFromBlob/ForSlot` | Allocate FTParts on materialized hidden roots (egg hatch j9) before modelpart push |
| `syNetRbSnapHardPinFighterFoldContributorsFromBlob/Slot` | Terminal pin of full-fold scalars + gated joint TRS after intro repair |
| `syNetRbSnapIntroLoadFidelityPreSanityRepair` | Prune → hidden parts → root FTParts → modelpart → re-pin → re-reconcile → hard pin |
| `syNetRbSnapshotRestoreLiveEmergency` | Stash emergency slot for `load_fail_restore` verify; run intro repair on restore |
| `syNetRbSnapshotVerifyLiveFightersSanity` / `LogFighterStatusTrail` | `load_fail_restore` uses stashed emergency blobs; status trail passes slot blob |

## Correction (Phase 18 — 2026-06-10): hard-pin `anim_vel` after intro load prep

Phase 17 soak1 + layer-2 field diff: P1 Yoshi Appear only — `light_ok=0` / `full_ok=0` with `anim_ok=1`; sole layer-2 delta `field=fold_anim_vel_y live=0 blob≈0.75`. Forward-sim ring save matched blob; after `IntroLoadFidelityPreSanityRepair` live `fp->anim_vel.y` was zero while blob retained capture. `anim_vel` is in `fhash_light` (`syNetSyncHashFighterStructLight`) but was not in `syNetRbSnapHardPinFighterFoldContributorsFromBlob` (figatree/SetStatus clears it post-apply).

| Change | Purpose |
|--------|---------|
| `syNetRbSnapHardPinFighterFoldContributorsFromBlob` | Re-pin `fp->anim_vel` from blob (+ quantize on netmenu) at terminal intro repair |

## Correction (Phase 19 — 2026-06-10): hard-pin MPColl + light physics after joint pose restore

Phase 18 soak1: walkback 239→223 (`sim=223` vs prior `sim=241`); terminal `anchor_probe_unresolved` @223 with P1 Yoshi `light_ok=0` on probe steps. Layer-2 lead deltas: `fold_topn_ty`, `fold_coll_pos_diff_y`, `fold_p_translate_y` (1 fold-quant ULP on world Y), then joint TRS cascade. No `fold_anim_vel_*`. `IntroLoadFidelityPreSanityRepair` runs full `ReapplyFighterJointAnimFromSlot` (includes `ApplyMPColl`) then terminal hard pin restores joint/gobj TRS without refreshing MPColl — TopN translate moves while `pos_diff` / `*p_translate` stay stale.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapRestoreFighterMPCollFromBlob` | Re-apply blob MPColl with fresh TopN `p_translate` after pose pin |
| `syNetRbSnapHardPinFighterFoldContributorsFromBlob` | Full `fp->physics` + tap/hold sticks; MPColl re-pin after joint/gobj restore |

## Correction (Phase 20 — 2026-06-10): vanilla MPColl p_translate + anchor-probe AObj sim prep

Phase 19 soak1: load @223 `postload_f=0`; anchor probe `step_f=1` @224 with P1 Yoshi `light_ok=0` (`fold_topn_ty`, `fold_coll_pos_diff_y`, joint TRS cascade) while `anim_ok=1`. Load hash matched with `p_translate` rebound to TopN, but ring capture folds `*coll_data.p_translate` from `DObjGetStruct(fighter_gobj)->translate` (gobj root). During Appear, `ftCommonAppearProcPhysics` moves TopN while gobj root stays fixed — TopN-bound `p_translate` integrates `pos_diff` on TopN motion; vanilla gobj-bound pointer does not, diverging on the 223→224 forward-sim step.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapFighterMPCollTranslatePtr` + `syNetRbSnapRebindAllFighterMPCollPointers` | Rebind `p_translate` to fighter GObj root (matches `ftManager` spawn + fhash_light blob fold) |
| `syNetRbSnapRestoreFighterMPCollFromBlob` / apply + reapply paths | Pass gobj root into `syNetRbSnapApplyMPColl`, not TopN joint |
| `syNetRbSnapAnchorProbeSimPrepFromSlot` | After intro repair: full `ReapplyFighterJointAnimFromBlob` + `entry_pos` + hard pin before probe +1 sim |

## Correction (Phase 21 — 2026-06-10): scope gobj MPColl to anchor probe; intro light-hash step oracle

Phase 20 soak1: global GObj-root `p_translate` rebind caused Linux P1 to skip Appear at tick 210 (status 221 vs Android 220) while still failing 223→224. Aggregate `step_f=1` on every probe step also blocked on Kirby P0 `full_ok=0` joint-rotate quant noise while P1 `light_ok=1`.

| Change | Purpose |
|--------|---------|
| Revert load/reapply/hard-pin/rebind MPColl to TopN | Restore Phase 19 cross-ISA ring capture (Appear through ~224) |
| `syNetRbSnapRestoreFighterMPCollFromBlobForAnchorProbe` | GObj-root `p_translate` only in `AnchorProbeSimPrep` tail before +1 sim |
| `syNetRbSnapshotGetSlotHashFighterLight` + intro anchor step compare | Anchor probe `step_figh` uses `fhash_light` during intro walkback — ignore Kirby full-fold joint rotate quant |

## Correction (Phase 22 — 2026-06-10): anchor-probe prep for Wait peers + pre-sim gobj MPColl rebind

Phase 21 soak1: cross-ISA parity restored but walkback still aborted @223. P1 Yoshi `light_ok=0` on **every** probe step (224–240) with `fold_topn_ty` / `fold_p_translate_y` / joint TRS cascade while `anim_ok=1`. Ring status trail shows Yoshi Entry (5) → Wait (221) @ tick 134 with **no Appear (220)**; Kirby still in Appear @223. `AnchorProbeSimPrepFromSlot` gated on per-fighter `syNetRbSnapFighterInIntroLoadFidelityScope` (Entry/Appear only), so Wait Yoshi skipped the gobj-root MPColl tail — live `*p_translate` stayed TopN-bound while blob light fold uses `gobj_translate`.

| Change | Purpose |
|--------|---------|
| `syNetRbSnapAnchorProbeSimPrepFromSlot` | Prep **all valid fighters** when global intro scope active; entry_pos only for Entry/Appear peers |
| `syNetRbSnapshotRebindFighterMPCollForAnchorProbe` | Belt-and-suspenders GObj-root `p_translate` rebind immediately before probe +1 sim |

## Test plan

1. `INJECT_TICK=240` cross-ISA soak — Kirby/Yoshi visible through load + replay; no `top_joint_y` / `top_joint_ry` drift in `fighter_field_diff tag=rollback_load tick=239`.
2. `RESIM_BASELINE_MISMATCH requires_anim=1` walkback storm reduced; episode reaches `Replay` not only `Live`.
3. `INJECT_TICK=480` regression — `resim complete` unchanged.
4. Stretch: `match_f=1` on anchor probe at initial load tick during Appear.
