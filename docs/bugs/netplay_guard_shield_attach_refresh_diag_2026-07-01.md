# Netplay — guard shield attach-refresh: regression root-caused, mechanism REVERTED

**Date:** 2026-07-01
**Status:** **REVERTED** — `syNetRbSnapRefreshGuardShieldJointAttachFromFighters` (the failed Sector Z
shield-presentation fix from the last code commit, `9b06ece`) removed along with all 5 call sites. It
was proven a no-op for its intended purpose while its side effect (an animlock stomp, see final update
below) was the root cause of the **all-maps** "fighter joints deformed / model+shield sunk into the
ground" regression. The original **Sector Z-only** mid-fighter shield presentation is a separate,
pre-existing bug — still open, next investigation target is the Sector-specific arwing-deck rollback
repair paths. Diagnostics (`guard_shield_joint_pose`, `guard_shield_transform_rebuild`) retained.
**Area:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`,
`decomp/src/ef/efmanager.c`, `decomp/src/ft/ftcommon/ftcommonguard1.c`

## Final update 2026-07-01: root cause found — animlock stomp from the failed fix itself; REVERTED

Two decisive new facts from the user closed this out:

1. The mid-fighter shield presentation happens **only on Sector Z** (Peach's Castle unaffected).
2. The joint deformation + fighter/shield-sunk-into-ground symptoms appear on **all maps** and began
   with the recent patch (in `9b06ece`) that tried and failed to fix the Sector Z shield presentation.

And the `guard_shield_joint_pose` re-soak (session `364675497`, both peers) proved the synctest
emergency-probe pipeline is **clean**: the one probe that landed during a genuine shield hold
(tick 870, player 1) showed `yrotn_scale` bit-identical (`0x41139190`) across all 8 checkpoints on both
linux and android, `attach_ok=1` throughout, and all 1029 `shield_draw` samples in sane bubble range.
Combined with the earlier 252/252 `changed=0` attach-refresh trace, the fix's payload (re-pinning
`user_data.p`) never did anything.

Its **side effect** did, though. The fix called `syNetRbSnapInvalidateFighterPartTransformCaches`
(zeroes `FTParts.transform_update_mode` on **every** joint) + `ftParamInvalidateFighterTransformFromRoot`
from `reconcile_core` — i.e. **live-forward, every tick a shield was up, on every map**. But
`transform_update_mode == 3` is the vanilla **animlock** marker: `ftParamSetAnimLocks`
(`decomp/src/ft/ftparam.c:2610`) sets it as a pair with `xobjs[0]->unk05 = 1`, and the only clearer,
`ftParamClearAnimLocks` (`ftparam.c:2636`), clears `unk05` **only when mode is still 3**. Zeroing the
mode out from under an anim-locked joint leaves `unk05 = 1` wedged forever — vanilla can never release
the transform lock again. Result: permanently deformed joints and the fighter/shield sinking into the
ground, everywhere, exactly matching the regression profile.

**Action taken:** removed the function, its forward declaration, and all 5 call sites
(`patch_all_from_slot`, `reconcile_core`, `rebuild_intro_part_transforms`,
`figatree_presentation_from_slot`, `finalize_verify_effect_state`). The remaining
`syNetRbSnapInvalidateFighterPartTransformCaches` users are all rollback-load/verify repair paths that
predate this regression and run only during snapshot apply — left untouched. Both `build-netmenu` and
`build-offline` compile clean. A tombstone comment marks the removal site in `netrollbacksnapshot.c`.

**Still open (pre-existing, Sector Z only):** the shield bubble presenting mid-fighter on Sector Z.
Sector Z is the only stage with its own rollback repair machinery
(`syNetRbSnapshotCanonicalizeSectorArwingDeckFighter`,
`syNetRbSnapReconcileSectorArwingDeckYakumonoFromFlightTree`, arwing-deck fragile scopes — see
`docs/bugs/netplay_sector_arwing_deck_jitter_2026-06-05.md`), which Peach's Castle never exercises.
Next step: reproduce on Sector Z with `guard_shield_joint_pose` extended to log YRotN's **world matrix**
(local TRS is proven clean) and correlate against the deck yakumono/canonicalize repair timing. Do
**not** re-introduce a blanket transform invalidation as the fix.

## Update 2026-07-01 (part 3): new user report ties the bug specifically to `synctest ON`

User re-confirmed with fresh soak2 logs (`PEER_SNAPSHOT_DIVERGE` at `load_tick=518`, see the Fox
Firefox truncation doc for that half) and added a critical new data point: **the shield presents
correctly with `synctest OFF`**, and only shows the mid-fighter/same-depth clip with **`synctest ON`**.
This directly implicates the `SSB64_NETPLAY_ROLLBACK_SYNCTEST` periodic self-check probe in
`port/net/sys/netrollback.c` (`sSYNetRollbackSynctestEnabled`), not a general rollback-load code path
that would fire identically regardless of the synctest flag. Unlike GGPO's classic synctest (forced
1-frame rollback *every* frame), this port's synctest is a much lower-frequency background probe: every
~120 completed ticks after a pass (`sSYNetRollbackSynctestNextProbeTick = completed_tick + 120U`), or
immediately retried the next tick after certain skip conditions. Its sequence is:

```
syNetRbSnapshotCaptureLiveEmergency()          // snapshot the CURRENT live state into an "emergency" slot
syNetRbSnapRepairStageSetVerifyOnly(TRUE)
syNetRbSnapshotLoad(probe_tick)                // load an OLD ring slot (probe_tick = completed_tick - 1)
syNetRbSnapshotPrepareLoadedSlotForVerify(probe_tick)
syNetRollbackVerifyLoadedSlot(probe_tick)      // resim forward + compare hashes
syNetRbSnapRepairStageSetVerifyOnly(FALSE)
syNetRbSnapshotRestoreLiveEmergency()          // put the live state (captured above) back
syNetRbSnapshotRecoverGuardShieldBubblesAfterSynctest()   // <- exists specifically because the
syNetRbSnapshotRecoverYoshiEggLayHatchAfterSynctest()        restore above doesn't fully repair
                                                              shield/egg presentation on its own
```

The mere existence of `RecoverGuardShieldBubblesAfterSynctest()` as a **dedicated post-hoc patch-up**
strongly suggests `RestoreLiveEmergency()` is known to leave shield presentation in a bad state and
this is a band-aid, not a full fix. Reading it: it only re-runs the live-forward guard/shield *effect
coupling* (`syNetRbSnapReconcileGuardShieldEffectsLive`) and restores `shield_decay_wait` — it does
**not** touch `fp->joints[nFTPartsJointYRotN]`'s local transform at all.

Traced where the shield's apparent bubble *size* actually lives: it is **not** carried by the shield
effect's own `DObj` — `efManagerShieldMakeEffect` (`decomp/src/ef/efmanager.c`) only points
`user_data.p` at `fp->joints[nFTPartsJointYRotN]` (the attach pointer already ruled out above).
The actual size comes from `ftCommonGuardUpdateShieldCollision` (`ftcommonguard1.c:131`), which writes
a `shield_health`-scaled value directly onto **`fp->joints[nFTPartsJointYRotN]->scale`** every
forward-sim tick while `fp->is_shield` is set — a plain fighter-skeleton joint, doing double duty as
both a body joint and the shield's implicit size controller. `syNetRbSnapApplyFighterJointPoseAndAnimFromBlob`
does correctly round-trip `joint_scale[]` (including YRotN) through the ring blob on a normal
snapshot apply, so on paper the emergency-restore path (`ApplySlotToLive` → `FinalizeLoadFromSlot`)
should also restore it correctly — but nothing in this file has ever directly verified that for YRotN
specifically, and the probe's specific timing (a full capture → old-tick-load → resim → restore cycle,
mid-frame, only when synctest is on) is exactly the kind of window where a stale value could survive
long enough to make it into that frame's draw call before the next forward-sim tick's
`ftCommonGuardUpdateShieldCollision` would otherwise overwrite it with the correct value.

### New diagnostic: `guard_shield_joint_pose`

Added `syNetRbSnapDiagLogGuardShieldJointPose(tag)` (declared in `netrollbacksnapshot.h`, gated by
`SSB64_NETPLAY_GUARD_SHIELD_JOINT_POSE_DIAG=1`). For every fighter with `is_shield` or residual
`shield_health > 0`, logs `nFTPartsJointYRotN`'s local `scale`/`translate.y`/`rotate.y` (raw hex bits),
`shield_health`, `verify_only`, `resimulating`, and the shield effect's attach-pointer sanity, tagged
by call site:

```
SSB64 NetRbSnapshot: guard_shield_joint_pose tag=%s tick=%u player=%d status=%d is_shield=%d shield_health=%d verify_only=%d resimulating=%d yrotn_scale=0x%08X/0x%08X/0x%08X yrotn_translate_y=0x%08X yrotn_rotate_y=0x%08X shield_gobj_id=%u shield_ep=%p attach_ok=%u
```

Call sites, in pipeline order:

- `"guard_update_joints_forward"` — end of `ftCommonGuardUpdateJoints` (`ftcommonguard1.c`), right
  after the authoritative forward-sim scale write. **Baseline** — this is what "correct" looks like.
- `"emergency_capture_pre"` — start of `syNetRbSnapshotCaptureLiveEmergency`, before the live state is
  snapshotted (should match the most recent `guard_update_joints_forward` value).
- `"probe_loaded_pre_verify"` / `"probe_post_verify"` — bracketing `syNetRollbackVerifyLoadedSlot` in
  `netrollback.c` (this is the *old probe_tick* fighter state, informational only — not what gets
  drawn).
- `"emergency_restore_post_apply"` — inside `syNetRbSnapshotRestoreLiveEmergency`, right after
  `syNetRbSnapApplySlotToLive` (should already match `emergency_capture_pre` if the blob round-trips
  YRotN scale correctly).
- `"emergency_restore_post_finalize"` — same function, right after `syNetRbSnapshotFinalizeLoadFromSlot`
  (this is the generic post-rollback repair tail — `syNetRbSnapVsLoadJointFidelityRepairFromSlot` for
  an active shield — watch for a divergence introduced *here* specifically).
- `"emergency_restore_final"` — end of `syNetRbSnapshotRestoreLiveEmergency`, right before it returns.
- `"synctest_post_recover"` — in `netrollback.c`, right after both
  `RecoverGuardShieldBubblesAfterSynctest`/`RecoverYoshiEggLayHatchAfterSynctest` calls.
- `"synctest_post_whispy_presentation"` (netmenu only) — after the Whispy presentation refresh that
  follows, last checkpoint before returning to the normal tick loop.
- `"shield_draw"` — inside `efManagerShieldProcDisplay`, the actual draw call. **Ground truth** — this
  is what the player sees that frame.

### How to use it

Re-run the shield-clip repro with `synctest` **ON** and
`SSB64_NETPLAY_GUARD_SHIELD_JOINT_POSE_DIAG=1` set. Find a `SYNCTEST_OK`/`SYNCTEST_FAIL` probe near the
tick the clip becomes visible, and diff `yrotn_scale` across the tag sequence above for that
player/tick window:

- If `emergency_capture_pre` already disagrees with the immediately preceding
  `guard_update_joints_forward`, the bug is in what `syNetRbSnapFillSlotFromLive` captures (unlikely,
  but rules out the whole restore side).
- If `emergency_restore_post_apply` disagrees with `emergency_capture_pre`, the blob round-trip
  (`syNetRbSnapApplyFighterJointPoseAndAnimFromBlob`) is not restoring YRotN scale correctly during an
  emergency apply specifically — check `syNetRbSnapApplySlotToLive`'s call order for a step that runs
  *before* the joint-pose apply and clobbers it (e.g. a figatree/model-part attach that resets to the
  base pose).
- If `emergency_restore_post_apply` is correct but `emergency_restore_post_finalize` disagrees, the
  culprit is inside `syNetRbSnapshotFinalizeLoadFromSlot`'s repair tail for the active-shield branch
  (`syNetRbSnapVsLoadJointFidelityRepairFromSlot` → `ReconcileFighterJointPresenceFromSlot` /
  `HardPinFighterFoldContributorsFromSlot` are the two suspects that run after the reapply).
  in this window are the two suspects that run after the correct reapply.
- If everything up through `synctest_post_whispy_presentation` is correct but `shield_draw` disagrees,
  the corruption is happening in the normal tick loop *between* the probe returning and the next draw
  call — i.e. something in ordinary forward-sim/draw scheduling reads a cached/stale matrix rather than
  the live joint fields directly. That would point at the DObj draw-time matrix cache, not any of the
  netplay repair functions above.
- If `yrotn_scale` is correct at every single tag the whole way through, the bug is not a scale/pose
  problem at all — it's a genuine depth-sort/Z-buffer/render-mode issue in the shield's draw call
  (`gDPSetRenderMode` / z-compare config) that only manifests when the probe's load/resim briefly
  disturbs some other per-frame renderer state (matrix stack depth, sort key) that OFF-synctest ticks
  never touch. In that case pivot the investigation to `efManagerShieldProcDisplay`'s DL setup and
  `gcDrawDObjTreeDLLinksForGObj`, not fighter/joint state.

## Update 2026-07-01 (soak2 post-`ShieldPose` fix): attach pointer ruled out, symptom worsened

Re-ran with `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` already on. Across the entire soak2 session, all
362 `guard_shield_attach_refresh` lines split as 252 `outcome=refreshed` (**0** with `changed=1`, all
252 `changed=0`) + 110 `outcome=skip reason=no_live_shield_gobj`. This is the exact "not a stale
`user_data.p`" outcome this doc's own "How to use it" section predicted — the attach pointer is
already correct every single time.

User also now reports the symptom got worse, not just persisted: the shield still clips mid-body, and
**the whole fighter model now sinks into the ground** instead of the bubble drawing in front of it —
and this happens to Kirby too, a fighter with no relationship to any of the Fox-specific code touched
recently. Since a bug specific to the shield-attach mechanism wouldn't explain a fighter (not just an
effect) visually sinking, and this is downstream of the attach pointer per the ruled-out evidence
above, the next suspect is whatever recomputes the *fighter's own* joint-transform tree, not just the
shield's attach target: `syNetRbSnapRebuildIntroFighterPartTransforms()`. Despite the "Intro" name it
runs from 9 call sites covering forward resim, residual-shield/egglay figatree repair, and
prepare-verify — not just intro/countdown — via `ftParamInvalidateFighterTransformFromRoot` +
`ftParamsUpdateFighterPartsTransformAll(fp->joints[nFTPartsJointTopN])`, i.e. it recomputes the whole
skeleton from the root joint down, for every fighter, from many contexts.

Added a second diagnostic, `guard_shield_transform_rebuild`, to that function: logs each fighter's
`nFTPartsJointTopN` world-Y translate before/after every rebuild call, tagged by which of the 9 call
sites triggered it (`caller=`), gated behind the same `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`:

```
SSB64 NetRbSnapshot: guard_shield_transform_rebuild tick=%u caller=%s player=%d status=%d is_shield=%d topn_y_before=0x%08X topn_y_after=0x%08X changed=%d
```

Re-soak the shield-clip/ground-sink repro and correlate `guard_shield_transform_rebuild` against the
tick the model visibly sinks: a `changed=1` with `topn_y_after` dropping toward/through the floor
plane, tagged with a specific `caller=`, names the exact rebuild call path to fix next. If `topn_y`
never changes here either, the bug is further downstream still (draw-time matrix use, not the
transform recompute itself).

## Context

User report (soak2 session `1339981196`): the guard shield bubble still sometimes renders "in the
middle of the fighter" (half the model clipped behind it, half in front) instead of as an overlay
bubble in front of the fighter. This is the same visual family as
[netplay_guard_shield_part_transform_attach](netplay_guard_shield_part_transform_attach_2026-07-01.md)
("Fix implemented, soak pending" — this soak was effectively that fix's verification run) and its
superseded precursor
[netplay_resim_complete_guard_shield_attach](netplay_resim_complete_guard_shield_attach_2026-07-01.md).

That fix wired `syNetRbSnapRefreshGuardShieldJointAttachFromFighters()` (re-pins the shield's
`user_data.p` to `fp->joints[nFTPartsJointYRotN]` and invalidates the fighter's part-transform
cache) into every `syNetRbSnapRebuildIntroFighterPartTransforms()` call path plus the live-forward
reconcile core. The fix's call sites are confirmed present in the current tree (5 call sites), and
its own guard-scope gate (`syNetRbSnapFighterInGuardScope`, true whenever `fp->is_shield != FALSE`)
was true at the tick the crash-adjacent shield state was observed in this soak — so on paper the
attach should have refreshed. Whether it actually did, and from which call path, was previously
unobservable: the function had **zero diagnostic logging**, so a silent `continue` past any of its
five early-exit conditions (guard scope inactive, no live shield GObj, effect isn't a shield,
`joints[nFTPartsJointYRotN]` NULL, `DObjGetStruct` NULL) would leave the bubble stale with nothing
in the log to show it.

## What was added

`syNetRbSnapRefreshGuardShieldJointAttachFromFighters` now:

1. Takes a `caller_tag` string identifying which of the 5 call sites invoked it this pass:
   - `"patch_all_from_slot"` (`syNetRbSnapPatchAllGuardShieldsFromSlot`)
   - `"reconcile_core"` (`syNetRbSnapReconcileGuardShieldEffectsCore`, live-forward per-tick)
   - `"rebuild_intro_part_transforms"` (`syNetRbSnapRebuildIntroFighterPartTransforms`, the
     general-purpose wrapper the original fix leaned on for coverage)
   - `"figatree_presentation_from_slot"` (`syNetRbSnapRefreshFigatreePresentationFromSlot`,
     terminal verify-prep)
   - `"finalize_verify_effect_state"` (`syNetRbSnapshotFinalizeVerifyEffectStateInternal`)
2. Under `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` (already enabled in the user's soak logs), logs one
   `guard_shield_attach_refresh` line per fighter per invocation with the outcome:
   - `outcome=skip reason=not_active_guard` — in guard scope but not actively shielding/held guard
     this tick.
   - `outcome=skip reason=no_live_shield_gobj` — no live bubble GObj found for the fighter/player.
   - `outcome=skip reason=not_a_shield_effect` — found a GObj but it doesn't validate as a shield
     effect.
   - `outcome=skip reason=joint_yrotn_null` — fighter has no `YRotN` joint pointer yet (early in a
     load/rebuild) to attach to.
   - `outcome=skip reason=dobj_null` — shield GObj has no `DObj`.
   - `outcome=refreshed` — attach actually re-pinned; includes `joint_yrotn` (pointer fingerprint of
     the target joint), `prev_user_data_p`/`new_user_data_p` (pointer fingerprints of the shield's
     attach target before/after), and `changed=0/1` (whether the re-pin was a no-op — already
     correct — or actually moved the attach).

Log line shape:

```
SSB64 NetRbSnapshot: guard_shield_attach_refresh tick=%u caller=%s player=%d status=%d is_shield=%d outcome=%s ...
```

## How to use it

Re-run the shield-clip repro (Kirby A-spam vs shielding Fox, or the general "shield presents
mid-fighter" case) with `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` already set. At the tick the clipping
is visually observed:

- If every `guard_shield_attach_refresh` line for that player/tick shows `outcome=refreshed
  changed=0`, the attach pointer is already correct and the bug is **not** a stale `user_data.p` —
  it's downstream in the actual draw ordering / matrix computation for `nFTPartsJointYRotN`, or a
  Z-sort/transparency issue unrelated to this attach mechanism.
- If there's a `outcome=skip reason=joint_yrotn_null` or `reason=no_live_shield_gobj` right before
  the clip becomes visible, that names the exact rebuild path that needs it (via `caller=`) and the
  missing precondition.
- If `outcome=refreshed changed=1` fires but the clip is still visible next frame, the re-pin is
  landing too late relative to that frame's draw (ordering issue between this function and the draw
  call, not a missing call site).

## Next step

Not yet fixed — this is instrumentation to localize the remaining gap before changing more code.
Re-soak with a shield-clip repro and read the `guard_shield_attach_refresh` trace at the exact
clip tick.
