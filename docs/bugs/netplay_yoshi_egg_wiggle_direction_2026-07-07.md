# Netplay: Yoshi egg mash wiggle lacks directional stick response

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ft/ftcommon/ftcommoncaptureyoshi.c`, `decomp/src/ef/efmanager.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Soak2 (Samus vs Yoshi): victim in `YoshiEgg` (status 178) shows egg shell squash/wiggle, but stick mash does not shift the shell left/right/up — only a uniform vertical squish (reads as “down wiggle only”), unlike vanilla. **Follow-on:** slow wiggle looks correct during the initial wait window (`index` 0 / `flag0` 0). About 1–2 seconds after capture, `breakout_wait` expires, `flag0`→1, and the shell binds EggLayBreak (`force_index` 1). Post-anim wiggle reapply used absolute stick translate and zeroed idle axes, stomping break-anim scrunch and collapsing directional motion to down-only squash — not mash-acceleration timing.

Sim/hatch timing and synctest were OK; presentation-only.

## Vanilla behavior

Directional wiggle is **not** separate anims. `ftCommonYoshiEggProcInterrupt` offsets the egg-lay effect **child DObj** translate by ±22 px from stick X/Y when `ga == Ground`. Base `EggLayWait` (index 0) anim drives rotation squash on the same child.

Process order: fighter interrupt (priority 5) runs before effect `gcPlayAnimAll` (priority 3). Stick translate must be reapplied after anim advance so mash direction survives the wait-anim tick.

## Root cause

1. **Effect snapshots capture anim frames only, not AObj chain state** — interpolator chains are live-only. Fix history:
   - *Unconditional `SetAnim` every apply:* restarted all joints at 1.0 each load.
   - *Unconditional `lbCommonAddDObjAnimJointAll` every apply:* break phase replayed early scrunch frames in a loop (soak2 @623–719: anim 1→9→0 cycles).
   - *Iteration 6:* `prev_live_index` from live `index` before memcpy; anim speed clamp — still failed soak2 @13:17.
   - *Iteration 7 (soak2 post-mortem):* (a) wait re-pin fired on single-frame `live > blob` delta every rb tick → removed; (b) effect blob kept `index=0` while fighter `flag0=1` → `ProcUpdate` reset break to frame 1.0 every tick (1→9 squish @623+); reconcile break vars from fighter before anim apply; (c) verify ejected `ep=nil` shells still listed in slot — slot-authoritative guard added.
   - *Iteration 8 (soak2 still failing):* reconcile never logged `break_frame_only` — parent resolve failed and `index==2` early-return blocked wait cleanup (stale `force_index=2` @621–630). Full reconcile from slot fighter blob; re-pin on intro residue; `ProcUpdate` defers break `SetAnim` to snapshot apply + physics one-shot entry.
   - *Iteration 9 (soak2 @631–719):* Identified break-phase eject/remint loop; stopped it by ticking post-flag0 countdown (15) every frame while `flag0==1`, but that escaped @647 (~15f after flag0) instead of vanilla break-complete timing — **too soon** vs mash/wiggle expectation.
   - *Iteration 10:* Revert iter-9 forced countdown while `index==1`; restore `index != 1` stalled-shell fallback only. Escape on break anim end via `ftCommonYoshiEggTryEscapeFromBreakAnimComplete` from effect proc + `efManagerNetplayTryCancelYoshiEggLayBreakEject` on `gcEjectGObj`/snapshot eject. Wait-phase mash (`captureyoshi.breakout_wait` from `ESCAPE_WAIT_MAX`) unchanged.
   - *Iteration 11 (soak2 post-mortem):* (a) `BeginPrepareAnim` index-2 early return left adopted intro residue uncleared; (b) break-complete escape in `ProcUpdate` lacked `flag0==1` → instant hatch on recycled `index==1 anim_frame<=0` (~19–24f captures); (c) stalled-shell `(index!=1)` countdown escaped ~15f after `flag0` without break scrunch when reconcile lagged effect index behind fighter `flag0` (variable 19–117f in-egg).
   - *Iteration 12 (soak2 synctest ON):* `FinalizeVerifyEffectState` on every hash pass drifted egg timers but restore ran only in verify-only prepare → first capture ~13f with mash; restore now runs after finalize + post-presentation repair on rollback load.
   - *Iteration 13 (soak2 no-mash):* All escapes via `break_eject_hook`; `fp_breakout=750` confirms no mash. In-egg duration = ticks-to-next-synctest-probe + ~11 break frames (120-tick probe grid). Removed live-path `RestoreSimFieldsAtTick` from synctest verify; stash/restore wait-phase countdown across synctest emergency round-trip; timer diag at egg enter / flag0 arm / synctest stash-restore.
2. **Mash `gcSetAnimSpeed(5.0)` during netplay rollback** — stacked with per-tick restore / stale speed into break phase read as constant fast squish; offline keeps vanilla 5.0 on mash. VS/resim clamps egg shell to 1.0 (breakout countdown unchanged).
3. **Synctest verify ejected active egg shells** — skip eject when shell is coupled to an active trap victim.
3. **Post-anim wiggle pass touched break anim translate** — netmenu post-`gcPlayAnimAll` reapply runs for wait anim (`index` 0) only (vanilla parity).
4. **Incomplete DObj tree guard** — sanitize path on missing child instead of silent null.
5. **No post-anim wiggle pass** — reapply stick offsets after `efManagerYoshiEggLayProcUpdate` when rollback semantics active.

## Fix

| Change | Purpose |
|--------|---------|
| `ftCommonYoshiEggApplyEggLayWiggleGfxToJoint` | Shared stick → child translate helper (offline + netmenu) |
| `ftCommonYoshiEggApplyEggLayWiggleGfx` | Netmenu export; after `gcPlayAnimAll` reapplies absolute stick offset for wait anim (`index==0`) only; no-op once break anim (`index==1`) owns translate (vanilla parity) |
| `efManagerYoshiEggLayProcUpdate` | Reapply wiggle gfx after anim when rollback semantics active |
| `ftCommonYoshiEggProcInterrupt` | On missing child, `syNetRbSnapSanitizeCaptureYoshiEffectGobj` instead of silent null |
| `syNetRbSnapGObjCoupledToYoshiEggLayTrap` | Skip `hidden_cosmetic_verify` eject for shells coupled to active `YoshiEgg` victims (live pointer) |
| `syNetRbSnapReconcileYoshiEggLayFromFighter` | Slot-fighter `flag0` drives break (`1/1`) or wait (`0/0`, stomps stale intro `force_index=2`); works when parent GObj resolve fails |
| `syNetRbSnapApplyYoshiEggLayAnimFromBlob` | Re-pin on stale intro (`prev_index/force==2`) or break→wait cleanup; wait re-pin only when `live_frame - blob_frame > 1.5` |
| `efManagerYoshiEggLayProcUpdate` | Rollback: skip break `SetAnim` reset loop; intro→wait `SetAnim(0)` only |
| `ftCommonYoshiEggProcPhysics` | Rollback: one-shot `SetAnim(1)` when `flag0` arms break and `index!=1` |
| `syNetRbSnapSlotAuthoritativeYoshiEggLayShell` | Verify eject guard via slot `captureyoshi_effect_gobj_id` / egg-lay blob (covers `ep=nil` orphans) |
| `ftCommonYoshiEggProcPhysics` | Netmenu: egg shell anim speed 1.0 during rollback VS/resim (vanilla 5.0 mash boost offline); reset to 1.0 when `flag0` arms break |
| `efManagerYoshiEggLaySetAnim` | Netmenu: `gcSetAnimSpeed(1.0)` after joint rebind during rollback |
| `ftCommonYoshiEggProcUpdate` (iter 9) | Rollback: escape countdown (15) ticks every frame while `flag0==1` (drops `index != 1` gate) — bounds escape when the shell is silently ejected at break-anim end; vanilla anim-complete escape wins when shell survives |
| `gcEjectGObj` trace | ENTER log gains `caller=%p` return address to name the silent shell ejector on next soak |
| `ftCommonYoshiEggTryEscapeFromBreakAnimComplete` (iter 10) | Shared netmenu escape when break anim completes (`index==1`, `anim_frame<=0`, `flag0==1`) |
| `efManagerYoshiEggLayProcUpdate` (iter 10) | Calls break-complete escape after `gcPlayAnimAll` (effect proc runs after fighter update) |
| `efManagerNetplayTryCancelYoshiEggLayBreakEject` (iter 10) | `gcEjectGObj` / snapshot eject hook: escape instead of ejecting break-complete trap shell |
| `efManagerNetplayTryCancelYoshiEggLayBreakEject` (iter 10a) | Gate on `gobj->id == nGCCommonKindEffect` before `efGetStruct` — interface GObj eject @tick 225 reused `user_data` as `0x1`, faulted at `ep->proc_update` (`fault_addr=0x29`) |
| `ftCommonYoshiEggBeginPrepareAnimForNetplay` (iter 11) | Always bind wait anim on adopt/recovery (remove index-2 early return) |
| `ftCommonYoshiEggProcUpdate` (iter 11) | Netmenu break-complete escape requires `flag0==1`; stalled-shell countdown only when `ep==NULL` (not `index!=1`) |
| `syNetRbSnapRestoreYoshiEggLaySimFieldsAtTick` (iter 12) | Re-pin `breakout_wait` / `flag0` from slot blob after presentation repair on rollback load (verify-only gate; removed from live synctest verify iter 13) |
| `syNetRbSnapLogYoshiEggLayEscape` (iter 12) | `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`: escape reason + timer fields at hatch |
| `syNetRbSnapStash/RestoreYoshiEggLayWaitTimer*` (iter 13) | Synctest: stash live wait-phase countdown before probe load; restore after emergency apply |
| `syNetRbSnapLogYoshiEggLayTimerDiag` (iter 13) | `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`: egg enter / flag0 arm / synctest stash-restore |
| `ftCommonYoshiEggProcUpdate` (iter 10) | Restores `index != 1` stalled-shell fallback; removes iter-9 forced post-flag0 countdown while break anim plays |

## Verify

Yoshi neutral-B egg Samus; mash stick hard left/right/up/down on ground after egg lands. Shell should shift ±22 px in each direction while squashing. Re-run soak2-linux + soak2-android through egg window.

Related: [netplay_yoshi_egg_second_capture_early_escape_2026-07-07.md](netplay_yoshi_egg_second_capture_early_escape_2026-07-07.md), [netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md](netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md).
