# Netplay — guard shield YRotN draw-matrix refresh (animlock-safe)

**Date:** 2026-07-01  
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/ef/efmanager.c`

## Symptom

Netmenu VS (synctest on **or** off, any stage): during guard, shield bubble and sometimes the fighter
silhouette render **lowered into the ground** or with malformed joints. Authoritative sim/cargo
(`coll_data.pos.y`, cross-peer `fhash_full`) stays correct — presentation-only.

Fork-inflicted during Sector Z shield troubleshooting (Phase 39 attach-refresh family). Reverting
`syNetRbSnapRefreshGuardShieldJointAttachFromFighters` fixed the **animlock stomp** regression but
left stale YRotN world matrices at draw time.

## Root cause

Shield draw (`func_ovl0_800C994C` in `lbcommon.c`) copies `joints[YRotN]->FTParts.mtx_translate` after
`func_ovl2_800EDBA4(YRotN)` — **not** local TRS. Rollback figatree/joint repair and
`syNetRbSnapRebuildIntroFighterPartTransforms` could leave `mtx_translate` stale while local scale/translate
diagnostics looked clean.

The reverted attach-refresh tried to fix this with `syNetRbSnapInvalidateFighterPartTransformCaches`
(all joints `transform_update_mode = 0`), which stomped animlock mode-3 pairing
(`ftParamSetAnimLocks` / `ftParamClearAnimLocks` contract).

## Fix

1. **`syNetRbSnapRefreshGuardShieldYRotNDrawMatrix(GObj *fighter_gobj)`** — presentation-only,
   gated `syNetplayRollbackSemanticsActive()`. Uses animlock-safe
   `ftParamsUpdateFighterPartsTransformAll(TopN)` (only clears mode when it is 1) +
   `func_ovl2_800EDBA4(YRotN)`, then re-pins shield `user_data.p` to YRotN.

2. **`efManagerShieldProcDisplay`** — call refresh immediately before `gcDrawDObjTreeDLLinksForGObj`.

3. **`syNetRbSnapRebuildIntroFighterPartTransforms`** — replace blanket invalidate +
   `ftParamInvalidateFighterTransformFromRoot` with TopN `ftParamsUpdate` +
   `func_ovl2_800EDBA4(TopN)`; call shield draw refresh when `is_shield || shield_health > 0`.

4. **`guard_shield_joint_pose` diagnostic** — add `yrotn_world_y`, `topn_world_y`, `coll_pos_y`,
   `mtx_y` (cached matrix translation used at draw).

## Verify

Re-soak cross-ISA shield spam on Peach's Castle and Sector Z with `SYNCTEST=0` and `=1`. Shield bubble
should track fighter height; no ground clip. Optional:
`SSB64_NETPLAY_GUARD_SHIELD_JOINT_POSE_DIAG=1` — expect `yrotn_world_y` ≈ `topn_world_y` ≈ `coll_pos_y`
during sustained guard; `mtx_y` matches after refresh at `shield_draw`.
