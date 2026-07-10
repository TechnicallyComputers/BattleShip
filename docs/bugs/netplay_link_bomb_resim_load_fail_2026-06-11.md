# Netplay Link bomb + residual shield resim load fail — 2026-06-11

**Status:** FIX SHIPPED (re-soak pending)

## Symptom

soak1 Donkey vs Link @520 (Linux + Android): `FORCE_MISMATCH` input inject → forced resim.

1. `RESIM_ANCHOR_FRAGILE_WALKBACK` 519→503→487, `reason=down_wait_residual_shield_probe`
2. `resim initial load tick=487`, target 522
3. 18× `RESIM_LOAD_FIDELITY_RETRY` (487 down to 471)
4. `BATTLE_SIM_HOLD` @ sim 521, `reason=resim_load_fail fail_count=18`
5. No `resim complete` on either peer

Load verify @487 (every retry anchor):

- **FAIL:** `figh`, `anim`, `eff`
- **MATCH:** `world`, `item`, `wpn`, `rng`, `cam`, `map`
- Link bomb item reconciles (`kind=23`, item hash slot==live)
- `guard_shield_load_drift` on both fighters (`shield=55`, `is_shield=0`)
- Joint fold drift on legs (figatree `phase1_invalid` during load)

Context @520: Donkey `DownBounceU`, Link `DownForwardU`, Link bomb on stage, large `eff`/`item` hash transitions.

## Root cause

Same family as Yoshi egg-lay @520:

1. **Residual shield presentation** — tech-chase / down-bounce/wait with `shield_health > 0` and bubble inactive does not round-trip joint figatree through snapshot load + verify finalize. Fragile walkback only lands deeper inside the same span with worse AObj poison.
2. **Link bomb effect window** — item blob matches but orphan quake/sparkle/shield shells after particle reset leave `eff` hash drift despite sim-critical partitions matching.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

- `syNetRbSnapRefreshResidualShieldPresentationFromSlot` — PreSim unhalfswap + joint re-pin + fold hard-pin when slot blob has residual shield stamina (bubble inactive).
- `syNetRbSnapReconcileLinkBombWindowEffectsCore` — sparkle replay from ring, quake ensure, guard-shield effect reconcile when slot has `nITKindLinkBomb`.
- Hooks: `PrepareLoadedSlotForVerify`, `ResyncLiveFightersFromSlotForSim`, per-tick forward resim while residual shield active, `FinalizeVerifyEffectStateInternal`, post-item finalize load.
- Removed `down_wait_residual_shield_probe` and `link_bomb_probe` from resim-load fragile walkback (same policy as egg-lay attack probe removal).

## Re-soak 1 result (same scenario)

- **Linux:** resim completes. Walkback still ran 519→503→487 (`transient_effect_probe`), but the
  baseline digest @487 matched on all partitions including `eff` — the repair itself works.
- **Android:** still `BATTLE_SIM_HOLD reason=resim_load_fail`. Deep walkback continued
  487→471 (`explode_sparkle_probe`) →455 (`multi_item_probe`), final fail @439 with `item`+`map`
  drift. The deeper the walkback, the worse the anchors — exactly the egg-lay pattern.

## Fix, round 2

The remaining failures were all *effect-cosmetic probe classes* still steering the resim anchor
walkback into bad territory, plus a presentation gate that missed `shield=0` down-bounce blobs:

- `s_syNetRbSnapResimAnchorEffectRepairTolerant` — set only inside
  `syNetRbSnapshotIsLoadAnchorFragile`. While set, `SynctestShouldSkipProbeTick` tolerates:
  - `transient_effect_probe` / `effect_probe_mismatch` when the slot has a Link bomb (load repair
    replays/prunes one-shot VFX and enforces the slot-authoritative effect set);
  - `explode_sparkle_probe` (the window is exactly what `ReplayExplodeSparklesFromRing` rebuilds);
  - `multi_item_probe` / `post_multi_item_probe` when **all** slot items are Link bombs
    (bomb-spam explode window round-trips via the bomb blob matcher + proc reapply).
  Synctest probes themselves are unchanged — the flag is never set on that path.
- `syNetRbSnapBlobInResidualShieldPresentationScope` broadened: residual shield stamina **or**
  knockdown tech-chase statuses (`nFTCommonStatusDownBounceD`..`nFTCommonStatusPassive`) with
  bubble inactive. This run's @520 blobs had `shield=0`, so the stamina gate alone never fired
  the presentation repair at the actual mismatch anchor.

This also restores the symmetric anchor contract indirectly: both peers now resolve the same
shallow anchor (519) instead of Android walking deeper than the Linux baseline echo tick.

## Re-soak pass criteria

Donkey vs Link soak1 episode @520, both peers:

- Resim loads @519 (no effect-class `RESIM_ANCHOR_FRAGILE_WALKBACK`)
- `resim baseline digest` matches all partitions on initial load
- `resim complete` through target tick
- No `BATTLE_SIM_HOLD reason=resim_load_fail` on Android

## Re-soak 2 result (Peach's Castle GBumper shell)

- Stage: **Peach's Castle** (`gkind=0`). Active Link bomb resim still fails with **`item` only**
  drift on every fidelity retry.
- `item_hash_walk` after each failed load: live has **two items** — `kind=21` (`nITKindLinkBomb`)
  and `kind=23` (`nITKindGBumper`), often sharing recycled `gobj_id=1013`. Slot expected bombs;
  one peer visually shows castle bumper/platform geometry instead of a bomb.
- `multi_item_probe` walkback still ran 519→487 (Linux) — slot was **not** all-Link-bomb
  (bomb + snapshotted GBumper), so round-2 tolerance did not apply.

## Fix, round 3

- **`syNetRbSnapFindItemBlobForLiveGobj`** — reject `gobj_id` match when `blob->kind != ip->kind`
  (recycled bumper shell must not in-place absorb a bomb blob).
- **`syNetRbSnapEjectStaleCastleGBumperBeforeLinkBombApply`** — before item apply/reconcile when
  slot has Link bomb on `nGRKindCastle`, eject live `nITKindGBumper` not backed by a slot blob.
- **Apply belt-and-suspenders** — if a match still has kind mismatch, eject without marking matched
  so `syNetRbSnapRespawnLinkBombFromBlob` runs in the respawn pass.
- **`syNetRbSnapshotSlotItemsMultiItemResimTolerant`** — resim anchor `multi_item_probe` tolerance
  for Peach's Castle slots that are Link bomb(s) plus snapshotted `nITKindGBumper` only.

## Re-soak 3 pass criteria

Same as round 2, plus:

- No `item_hash_walk` with live `kind=23` GBumper alongside bomb unless slot captured GBumper
- `item` slot hash equals live hash on initial resim load @519

## Re-soak 3 result (post joint-fidelity repair — regression)

- **Linux:** baseline digest matched @519, then `LOAD_TICK_NEGOTIATE local=519 peer=487` walked to
  471, `SIGSEGV pc=0x0` on `apply_after` (Link `status=52` DamageFlyN after stale proc apply).
- **Android:** `RESIM_ANCHOR_FRAGILE_WALKBACK` 519→503→487 (`effect_probe_mismatch` /
  `transient_effect_probe`); stall at sim=487 with seal reject (never `resim complete`).
- Repeated `capture_blob_sanity_fail` Link `status=70 motion=-2` (SpecialN sentinel motion).

## Fix, round 4

- **`syNetRbSnapshotSlotInLinkBombEffectRepairScope`** — broaden resim-anchor effect tolerance beyond
  `HasLinkBombItem`: Link SpecialN pull/throw statuses, quake effect blobs, sparkle replay window.
  Applied to `transient_effect_probe`, `effect_probe_mismatch`, and `ReconcileLinkBombWindowEffectsCore`.
- **`LOAD_TICK_NEGOTIATE` refuse** when local baseline digest already matched at `resim_load_tick`
  and peer announces a deeper load (prevents initiator downgrade 519→487).
- **Early proc rebind in `syNetRbSnapApplySlotToLive`** — after fighter blob apply, before
  map/item/effect repair (prevents null/stale proc crash during deep-walkback reload).
- **`syNetRbSnapFighterBlobSanityOk`** — allow vanilla motion sentinels `-1` / `-2` (Link SpecialN).

## Re-soak 4 pass criteria

Same as round 3, plus:

- No `LOAD_TICK_NEGOTIATE` downgrade after `resim baseline digest matched` @519
- No `SIGSEGV` during resim reload
- Both peers `resim complete` through target tick

## Re-soak 4 result

- **Episode 1 (@520 inject): PASS** — both peers `resim complete` @519, no anchor walkback,
  no negotiate downgrade, no `motion_negative` spam.
- **~tick 632:** natural `FRAME_COMMIT` item+rng diverge (`kind=23` GBumper `gobj_id=1013`),
  `INPUT_AGREE_REANCHOR` reload @512 → **SIGSEGV pc=0x0** on both peers (effect gobj_id recycle
  + stale Castle bumper ground pointer during item apply).

## Fix, round 5

- **`syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`** — always on Peach's Castle (not
  only Link-bomb slots): eject live GBumper when slot blob at `gobj_id` is missing or non-GBumper.
- **`syNetRbSnapEnsureCastleBumperAfterParticleReset`** at start/end of `syNetRbSnapApplyItems`
  (keep `grCastle.bumper_gobj` valid across eject/respawn).
- **GBumper proc rebind** in `syNetRbSnapApplyItemBlobToGObjPort` after in-place blob apply.
- **`syNetRbSnapEnsureQuakeEffectsFromSlot`** — null `efGetStruct` after `gcFindGObjByID` → respawn
  instead of applying effect blob to recycled item shell.
- **`syNetRbSnapApplyEffectBlobToGObj`** — reject non-effect gobj; respawn when `ep == NULL`.

## Re-soak 5 pass criteria

Episode 1 @520 still passes; match survives past tick 632 without SIGSEGV on frame-commit reload.

## Re-soak 5 result (regression @519)

- **Episode 1 (@520 inject): FAIL** — both peers `SIGSEGV fault_addr=0x0 lr=0x0` immediately after
  `apply_after` for Link (`status=106` LightThrowF, `motion=92`) during `resim initial load tick=519`.
  No `resim complete`. Distinct from re-soak 4 pass (`status=52/54` knockback window).
- Donkey `status=70` (`DownWaitU`), `motion=-2`; slot `item_count=2` (bomb + Castle hazard item).
- Crash window: inside `syNetRbSnapApplySlotToLive` after fighter blob apply (before
  `load_post_prepare` / `PrepareLoadedSlotForVerify` logs).

## Fix, round 6

- **LightThrow hold guard** — before joint anim re-pin, null `fp->item_gobj` when resolved gobj is not
  `is_hold` (recycled id aliasing GBumper shell during throw motion replay).
- **Early item hold coupling** — after early proc rebind, prune stale `item_gobj` + run
  `syNetRbSnapRebindFighterItemHoldCoupling` before map/particle/item repair (was end-of-apply only).
- **LightThrow proc fallback** — if `ftMainRebindStatusProcs` leaves `proc_update` NULL for throw
  statuses, retry rebind once.

## Re-soak 6 pass criteria

Episode 1 @520 passes (`resim complete` @519) **and** match survives past tick 632 (round 5 target).

## Re-soak 6 result (DownBounceU regression @519)

- **Episode 1 (@520 inject): FAIL** — both peers `SIGSEGV fault_addr=0x0 lr=0` during `resim initial load
  tick=519` (not @632). Last log: `apply_after` Link/Donkey `status=68` (`DownBounceU`), `shield=55`.
  Symbolized PC: `syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply` (item apply phase).
- Distinct from re-soak 5 (`status=106` LightThrowF): fighters in knockdown tech-chase, not throw arc.
- `item_count=1` on ring @520 (Link bomb only); live Peach's Castle bumper not snapshotted.

## Fix, round 7

- **`syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`** — eject GBumper only when a slot blob
  at the same `gobj_id` is a **non-GBumper** kind (id reuse). When no slot blob references the id, keep
  the live stage-hazard bumper. Clear `castle.bumper_gobj` and null item procs before ejecting recycled
  shells.

## Re-soak 7 pass criteria

Episode 1 @520: `resim complete` @519 without SIGSEGV; match still survives past tick 632.

## Re-soak 7 result (Link smash @520)

- **Episode 1 (@520 inject): FAIL** — Link `status=204` (`Attack13` smash), Donkey `Wait`; same
  `SIGSEGV lr=0` at `+0x5ceeb0` immediately after `apply_after` during `resim initial load tick=519`.
- Round 7 stopped ejecting the live castle bumper when the ring has no blob at its id, but **id-reuse**
  still ejects (`item_count=1` bomb blob at `gobj_id=1013` / `x0=0x3f5` at fault) through
  `syNetRbSnapEjectGObj` → stale LBParticle xf or item teardown SIGSEGV.

## Fix, round 8

- **`syNetRbSnapEjectItemGObjForRollback`** — item-only rollback eject: detach hold coupling, null
  coupled `arrow_gobj` / proc pointers, strip GObj processes via `gcEndGObjProcess`, then
  `itManagerSetPrevStructAlloc` + `gcEjectGObj`. Never routes items through
  `lbParticleFindStructForEffectGobj` / `efManagerDestroyParticleGObj`.
- **`syNetRbSnapEjectGObj`** — dispatch items to the helper before particle/effect lookup.
- **`syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`** — call item helper directly on the
  id-reuse eject path.

## Re-soak 8 pass criteria

Episode 1 @520 with Link smash / Donkey idle: `resim complete` @519, no SIGSEGV; past tick 632 still clean.

## Re-soak 8 result (DownBounceU @519)

- **Episode 1 (@520 inject): FAIL** — both peers `SIGSEGV lr=0` immediately after `apply_after` for Link
  (`status=68` `DownBounceU`, `shield=55`) during `resim initial load tick=519`.
- `item_count=1` (Link bomb only). Symbolized PC still in
  `syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`.
- Root cause: **all items share `gobj->id == nGCCommonKindItem` (1013)** — not per-instance ids. Round 7–8
  treated a bomb blob with `gobj_id=1013` as conflicting with the live castle GBumper at the same id and
  ejected the stage hazard (SIGSEGV in teardown, or dangling `castle.bumper_gobj`).

## Fix, round 9

- **Never match item blobs by `gobj_id` alone** when kinds differ (comment + stale-shell policy).
- **`syNetRbSnapSlotHasItemBlobKind`** — gate castle GBumper eject/preserve on whether the ring actually
  captured a GBumper blob.
- **`syNetRbSnapShouldPreserveCastleBumperOnApply`** — skip eject for the live singleton bumper when the
  ring is bomb-only (or singleton pos match failed but ring has a GBumper blob for ground repair).
- **`syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`** — only eject non-singleton GBumper shells
  that fail kind+position match when the ring has a GBumper blob; no-op when bomb-only.
- **`syNetRbSnapStripItemGObjProcessesForRollback`** — Func-kind process unlink without `gcEndGObjProcess`
  callback path (belt-and-suspenders for rare true stale-shell ejects).

## Re-soak 9 pass criteria

Episode 1 @520 (`DownBounceU` tech-chase and Link smash variants): `resim complete` @519, no SIGSEGV; past tick 632 clean.

## Re-soak 9 result (DownBounceU @519)

- **Episode 1 (@520 inject): PASS** — both peers `resim complete` @519, no SIGSEGV.
- **Cosmetic FAIL** — bomb explosion VFX replayed during resim while DK stayed `DownBounceU` (`status=68`);
  authoritative sim kept pre-explode bomb on stage (`item_count=1` through tick 521).

## Fix, round 10 (cosmetic ghost explosion)

Forward sim at tick 520 (mismatched `btn`) ran bomb explode procs and spawned sparkle/quake VFX. Load tick
519 ring state is pre-explode tech-chase with live bomb blob. `syNetRbSnapReplayExplodeSparklesFromRing` and
`syNetRbSnapEnsureQuakeEffectsFromSlot` replayed those cosmetics from ring history during apply/reconcile,
flashing an explosion without DK taking bomb damage.

- **`syNetRbSnapSlotHasLiveLinkBombOnStage`** — load slot has non-hold Link bomb blob not in `Explode` status.
- **`syNetRbSnapSkipLinkBombExplodeCosmeticReplayOnLoad`** — gate sparkle replay + quake ensure on that state.
- **`syNetRbSnapReplayExplodeSparklesFromRing`** — early return with optional `particle_replay skip` diag.
- **`syNetRbSnapReconcileLinkBombWindowEffectsCore`** — skip sparkle/quake block; guard-shield reconcile unchanged.
- **`syNetRbSnapshotFinalizeVerifyEffectStateInternal`** — skip standalone quake ensure when live bomb on stage.

## Re-soak 10 pass criteria

Episode 1 @520 (`DownBounceU` tech-chase): `resim complete` @519, no SIGSEGV, no ghost bomb explosion VFX
during resim ticks 520–521 while bomb remains on stage and DK is not hit.

## Re-soak 10 result (DownBounceU @519)

- **Episode 1 (@520 inject): PASS** — both peers `resim complete` @519, no SIGSEGV, no ghost explosion VFX.
- **Presentation FAIL** — when bomb hits Donkey during resim replay, sim is correct (`dmg`/`status` OK) but
  Donkey stays `DownBounceU` on the ground **rotated ~90°** (TopN yaw / figatree leg streams).

## Fix, round 11 (tech-chase joint yaw during resim)

Same family as `netplay_gameplay_resim_presentation_repair_2026-06-11.md` (DK SpecialAirNStart leg spin):
`syNetRbSnapshotPreSimUnhalfswapGameplayResimAnim` and first-tick
`syNetRbSnapRefreshGameplayAnimFragilePresentationFromSlot` only covered locomotion + special-table statuses.
DownBounce..Passive tech-chase relied on per-tick `RefreshResidualShieldPresentationFromSlot` without PreSim
unhalfswap before `scVSBattleFuncUpdateBattleSimOnly`, so DownBounce figatree streams ran poisoned and yaw
drifted off `fp->lr * 90°` during resim ticks 520–521.

- **`syNetRbSnapStatusInGameplayResimAnimFragileScope`** — include
  `syNetRbSnapStatusInDownTechChaseScope` (`DownBounceD`..`Passive`).

## Re-soak 11 pass criteria

Episode 1 @520 (`DownBounceU` tech-chase + bomb hit during resim): `resim complete` @519, Donkey facing correct
on ground through resim ticks 520–521 (no 90° yaw glitch when bomb connects).

## Re-soak 11 result (hold bomb @519 SpecialN)

- **Episode 1 (@520 inject): PASS** — `resim complete` @519, no SIGSEGV.
- **Cosmetic FAIL (round 10 regression)** — stale `link_bomb_sparkle` replay from ring `hist_tick=471`
  during load @519 (`particle_replay` at pos `(134.8,1747.6)`). Load anchor: Link `SpecialN` (`status=70`)
  with **hold** bomb (`item_count=1`); round 10 skip gate only counted non-hold stage bombs, so
  `ReplayExplodeSparklesFromRing` scanned the 48-tick window and flashed an old explosion while Link
  pulled a new bomb.

## Fix, round 13 (hold bomb + SpecialN cosmetic skip)

- **`syNetRbSnapSlotHasLiveLinkBombOnStage`** — treat hold blobs as pre-explode (do not skip hold entries).
- **`syNetRbSnapSkipLinkBombExplodeCosmeticReplayOnLoad`** — also skip when Link is in SpecialN
  pull/throw fighter scope (`syNetRbSnapBlobInLinkBombFighterScope`).
- **`syNetRbSnapReconcileLinkBombWindowEffectsCore`** — sparkle replay only on finalize/verify-only
  reconcile (apply path already calls `ReplayExplodeSparklesFromRing` once).

## Re-soak 13 pass criteria

Episode 1 @520 (`SpecialN` hold bomb @519): `resim complete` @519, no SIGSEGV, no stale sparkle replay
during load (`particle_replay skip` or absent when `SSB64_NETPLAY_SNAPSHOT_PARTICLE_DIAG=1`), no ghost
explosion flash while Link pulls bomb; round 11 DK facing still OK on bomb-hit resim ticks 520–521.

## Re-soak 13 result (FC recovery @479)

- **Episode 2 (~tick 632 / FC recovery): FAIL** — `FRAME_COMMIT_STATE_DIVERGE` item+rng (`GBumper
  kind=23`), `INPUT_AGREE_REANCHOR` reload @479, `fc_recovery=1`, target 600. Both peers
  `SIGSEGV fault_addr=0x0 lr=0x0` immediately after `apply_after` Link `SpecialNGet` (`status=110`)
  during `resim initial load tick=479` (before `load_post_prepare` / item apply).
- Load anchor @479: DK `Wait`, Link `SpecialNGet`, `item_count=2`, `shield=0` on capture; live
  frontier @599 had DK `SpecialN`, Link `Wait`, GBumper item drift vs peer.

## Fix, round 12 (residual shield bubble on non-guard load)

FC recovery reload applies slot fighters not in guard scope while live frontier still has shield
bubble VFX (`shield_health` residual on blob). Mint/reconcile on stale bubbles during apply
SIGSEGV pc=0x0 (soak1 @513 family).

- **`syNetRbSnapEjectLiveShieldsForNonGuardSlotFighters`** — after fighter apply, eject live shield
  effects when slot blob is not in guard / Yoshi escape scope; clear `is_shield` + guard union.
- **`syNetRbSnapReconcileGuardShieldEffectsCore`** — prune-before-ensure; block shield mint when
  parent fighter blob is not in guard scope.

## Fix, round 14 (FC recovery @479 SpecialNGet apply SIGSEGV)

Same `apply_after` pc=0x0 family as rounds 4–6 but on **Link `SpecialNGet`** during deep FC recovery
load: stale status procs during per-fighter joint anim re-pin, recycled `item_gobj_id=1013` resolving
to live GBumper before item apply, and raw shield eject without teardown on non-guard slot fighters.

- **`syNetRbSnapFighterInLinkBombFighterScope`** — live-fighter scope mirror of blob helper.
- **`syNetRbSnapSanitizeFighterItemGobjBeforeAnimApply`** — LightThrow + Link SpecialN: null
  `item_gobj` when resolved gobj is not a held Link bomb.
- **Per-fighter proc rebind before joint anim** — `syNetRbSnapRebindFighterStatusProcs` inside
  `syNetRbSnapApplyFighter` after item sanitize (belt on post-loop rebind).
- **Link SpecialN proc fallback** — retry `ftMainRebindStatusProcs` when `proc_update` NULL after
  rebind (LightThrow parity).
- **`syNetRbSnapEjectLiveShieldsForNonGuardSlotFighters`** — sentinel unlink, orphan shield rebind
  attempt, `syNetRbSnapFighterShieldReleaseTeardown` before eject.

## Re-soak 14 pass criteria

Episode 1 @520 still passes (round 13). Episode 2 / FC recovery: `resim initial load tick=479`
completes without SIGSEGV (`load_post_prepare` log present), resim reaches target or fails load
cleanly; no crash immediately after `apply_after` for Link `SpecialNGet`.
