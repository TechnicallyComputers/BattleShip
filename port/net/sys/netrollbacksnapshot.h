#ifndef _SYNETRB_SNAPSHOT_H_
#define _SYNETRB_SNAPSHOT_H_

/*
 * Typed rollback snapshot ring — gameplay closure for GGPO-style resim.
 * Snapshots are keyed only to completed authoritative sim ticks.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

#define SYNETRB_SNAPSHOT_RING_DEFAULT 64
#define SYNETRB_SNAPSHOT_RING_MAX     128
#define SYNETRB_SNAPSHOT_MAX_YAKU     64
#define SYNETRB_SNAPSHOT_MAX_ITEMS    32
#define SYNETRB_SNAPSHOT_MAX_WEAPONS  32
/*
 * Effect allocations are capped (~EFFECT_ALLOC_NUM in decomp). Snapshots bounded parallel to item/weapons tooling.
 */
#define SYNETRB_SNAPSHOT_MAX_EFFECTS  48
#define SYNETRB_SNAPSHOT_MAX_MAPOBJS  16

extern void syNetRbSnapshotInit(void);
extern void syNetRbSnapshotResetSession(void);
extern u32 syNetRbSnapshotRingCapacity(void);
/* Per-match depth from auto negotiation (clamped 1..SYNETRB_SNAPSHOT_RING_MAX). */
extern void syNetRbSnapshotSetRingFramesForSession(u32 frames);

extern sb32 syNetRbSnapshotSave(u32 completed_sim_tick);
extern sb32 syNetRbSnapshotSaveMarked(u32 completed_sim_tick, sb32 is_load_safe);
extern sb32 syNetRbSnapshotLoad(u32 completed_sim_tick);
#ifdef PORT
#include <ft/ftdef.h>
/* All-or-nothing load safety: capture live world before rollback load, restore on verify failure. */
extern sb32 syNetRbSnapshotCaptureLiveEmergency(void);
extern sb32 syNetRbSnapshotRestoreLiveEmergency(void);
/* Presentation sync + fighter-coupled weapon rebind before load-hash verify. */
extern void syNetRbSnapshotFinalizeLoad(u32 completed_sim_tick);
/* Finalize + rebind/reapply/coupling/item reconcile — same path production load and synctest use before verify. */
extern void syNetRbSnapshotPrepareLoadedSlotForVerify(u32 completed_sim_tick);
/* Presentation refresh + intro repair tail for replay gate; world already at completed_sim_tick load. */
extern void syNetRbSnapshotRefreshPresentationForLoadedTick(u32 completed_sim_tick);
/* Hash-safe Appear cosmetic refresh after replay gate verify (entry yaw + live modelpart DLs). */
extern void syNetRbSnapshotCosmeticAppearPresentationAfterReplayGate(u32 load_tick);
/* Intro resim burst: cosmetic camera + figatree refresh after each forward-resim tick (no sim revert). */
extern void syNetRbSnapshotRefreshIntroPresentationAfterForwardResimTick(u32 presentation_tick);
/* Post-resim terminal intro cosmetic repair from completed target tick slot. */
extern void syNetRbSnapshotRefreshIntroPresentationAfterResimComplete(u32 target_tick);
/* Seal-wait / preemptive cap: cosmetic Appear + camera refresh while interface gcRunAll is deferred. */
extern void syNetRbSnapshotRefreshIntroPresentationForDeferWait(u32 presentation_tick);
extern void syNetRbSnapshotRefreshDeferredIntroPresentation(u32 sim_tick, u32 anchor_tick);
/* Intro Wait forward sim: live figatree + camera integrate after gcRunAll (no slot camera re-pin). */
extern void syNetRbSnapshotRefreshLiveIntroPresentationAfterInterface(void);
/* Intro Entry/Appear: evict stale EVENT32 cache + un-halfswap before gcRunAll parses joint anims. */
extern void syNetRbSnapshotPreSimUnhalfswapIntroAppearAnim(void);
/* Gameplay resim: same EVENT32 repair for locomotion + special-move fragile statuses (post-GO). */
extern void syNetRbSnapshotPreSimUnhalfswapGameplayResimAnim(void);
/* Clear one-shot intro presentation repair latch at resim replay-gate open. */
extern void syNetRbSnapshotResetIntroPresentationRepairState(void);
/* Rebind status procs after load verify (proc pointers are not hashed). */
extern void syNetRbSnapshotRebindAllFighters(void);
/* TRUE if any fighter link has catch_gobj or capture_gobj set (all slots). */
extern sb32 syNetRbSnapshotAnyFighterGrabCouplingActive(void);
/* Re-derive victim pose from grabber hand joint (hold, pulled, thrown, cargo). */
extern void syNetRbSnapshotRefreshGrabCouplingGeometry(void);
/* TRUE if any item is held or any fighter has item_gobj set (all slots). */
extern sb32 syNetRbSnapshotAnyItemHoldCouplingActive(void);
/* Shorter frame-commit validation interval during hold/throw/multi-item windows (returns default_interval if calm). */
extern u32 syNetRbSnapshotFrameCommitIntervalCap(u32 default_interval);
/* Re-apply slot item blobs + quantize before load-hash verify (after hold coupling / joint anim). */
extern void syNetRbSnapshotReconcileLoadedItemsForVerify(u32 tick);
/* Reconcile trail/shock effects from ring slot when sim core matches but eff hash drifted on verify. */
extern sb32 syNetRbSnapshotTryRepairEffectHashForVerify(u32 tick);
extern sb32 syNetRbSnapshotTryRepairWeaponHashForVerify(u32 tick);
extern void syNetRbSnapshotFinalizeVerifyEffectState(u32 completed_sim_tick);
extern void syNetRbSnapshotFinalizeEffectsForVerifyHash(u32 completed_sim_tick);
/* Guard/Yoshi shield bubble ensure/prune/dedupe from ring slot or live (tap-churn orphan cleanup). */
extern void syNetRbSnapshotReconcileGuardShieldEffectsAtTick(u32 tick);
extern void syNetRbSnapReconcileGuardShieldEffectsLive(void);
/* Live bubble/coupling repair after synctest emergency restore (dual-shield drain stall). */
extern void syNetRbSnapshotRecoverGuardShieldBubblesAfterSynctest(void);
/* Hidden hatch shell repair after synctest emergency restore (hash-excluded cosmetic). */
extern void syNetRbSnapshotRecoverYoshiEggLayHatchAfterSynctest(void);
/* Fragile synctest probe skip on escape boundary — replay hatch on live tail when shell missing. */
extern void syNetRbSnapTryEnsureLiveYoshiEggLayHatchAfterSynctestFragileSkip(const char *skip_reason,
                                                                             u32 completed_sim_tick);
/* Pupupu Whispy mouth/eyes textures + LBParticles — hash-safe; run after load verify commits. */
extern void syNetRbSnapRepairPupupuWhispyPresentationAfterLoad(u32 tick, const char *reason);
#if defined(SSB64_NETMENU)
/* Ring slot ground blob: whispy_status == Blow (synctest stale-slot guard). */
extern sb32 syNetRbSnapPupupuWhispySlotIsBlow(u32 tick);
#endif
/* Guard+shield load-hash drift bisect (fighter/anim mismatch while shield active). */
extern void syNetRbSnapshotLogGuardShieldLoadDriftDiag(u32 tick, u32 live_f, u32 slot_f, u32 live_a, u32 slot_a);
/* Eject hollow/dead quake shells after synctest emergency restore (efManagerQuakeProcUpdate crash guard). */
extern void syNetRbSnapshotSanitizeLiveQuakeEffectsAfterEmergencyRestore(void);
extern void syNetRbSnapshotReconcileYoshiEggLayEffectsAtTick(u32 tick);
extern void syNetRbSnapReconcileYoshiEggLayEffectsLive(void);
extern sb32 syNetRbSnapYoshiEggLayCaptureWindowActiveWithoutEgg(void);
/* Coupled-weapon rebind + weapon hit positions only (no figatree presentation sync). */
extern void syNetRbSnapshotFinalizeLoadCoupling(u32 completed_sim_tick);
/* Live mid-sim reacquire when fighter coupling pointer was cleared but weapon still exists. */
extern struct GObj *syNetRbSnapReacquireYoshiChargeEgg(struct GObj *fighter_gobj);
extern struct GObj *syNetRbSnapReacquireChargeShotForFP(FTStruct *fp);
/* Destroy duplicate/orphan charge eggs (attack_state Off); keep keep_egg_gobj if non-NULL. */
extern void syNetRbSnapCullYoshiChargeEggsForFighter(struct GObj *fighter_gobj, struct GObj *keep_egg_gobj);
/* Destroy duplicate/orphan Samus/Kirby-copy charge shots (is_release FALSE); keep if non-NULL. */
extern void syNetRbSnapCullSamusChargeShotsForFighter(struct GObj *fighter_gobj, struct GObj *keep_charge_gobj);
extern struct GObj *syNetRbSnapReacquireFireballForFighter(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapFireballOwnedByFighter(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapFireballNeedsSpawnAtHand(struct GObj *fighter_gobj, const Vec3f *spawn_pos);
extern struct GObj *syNetRbSnapReacquireFireballAtHand(struct GObj *fighter_gobj, const Vec3f *pos, f32 radius_sq);
extern void syNetRbSnapCullOwnedFireballsNearPose(struct GObj *fighter_gobj, struct GObj *keep_fireball_gobj,
                                                  const Vec3f *pos, f32 radius_sq);
extern void syNetRbSnapCullOwnedPKThunderForFighter(struct GObj *fighter_gobj, struct GObj *keep_head_gobj);
extern struct GObj *syNetRbSnapReacquirePKThunderHeadForFighter(struct GObj *fighter_gobj);
/* Cull duplicate PK Thunder heads/trails before snapshot save and each live sim tick. */
extern void syNetRbSnapCullAllOrphanPKThunderLive(void);
/* Drop orphan Ness PK wave userdata effects when fighter is outside Start/Hold. */
extern void syNetRbSnapPruneStaleNessPKWaveEffectsLive(void);
extern sb32 syNetRbSnapPKFireOwnedByFighter(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapFireballProcAccessoryWillRun(struct GObj *fighter_gobj);
extern void syNetRbSnapTrySpawnFireballFromAccessory(struct GObj *fighter_gobj);
extern void syNetRbSnapTrySpawnPKFireFromAccessory(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapThunderJoltProcAccessoryWillRun(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapThunderJoltOwnedByFighter(struct GObj *fighter_gobj);
extern void syNetRbSnapTrySpawnThunderJoltFromAccessory(struct GObj *fighter_gobj);
extern void syNetRbSnapTrySpawnThunderFromSpecialLw(struct GObj *fighter_gobj);
/* TRUE when the live effect pool currently holds the rebirth-halo (respawn platform) effect coupled to
 * this fighter. Diagnostic use: catch the tick a restore drops the halo so the platform goes invisible. */
extern sb32 syNetRbSnapLiveFighterHasRebirthHalo(struct GObj *fighter_gobj);
#if defined(PORT) && defined(SSB64_NETMENU)
/* Eject dead/orphan effect shells so rebirth-halo MakeEffect can allocate. */
extern void syNetRbSnapReclaimStaleEffectShellsForRebirthHalo(s32 *ejected_out, s32 *ef_struct_free_out);
#endif
/* Skip held-item spawn when rollback already restored a matching projectile at this pose. */
extern sb32 syNetRbSnapHeldItemWeaponNeedsSpawn(struct GObj *owner_gobj, s32 kind, const Vec3f *spawn_pos,
                                                const Vec3f *spawn_vel);
/* TRUE when periodic synctest must defer (intro wait, item hold/throw, fighter throw, dead, rebirth, fox reflector). */
extern sb32 syNetRbSnapshotSynctestShouldSkip(const char **reason_out);
/* TRUE when live weapon link count != probe snapshot weapon_count (destroy/spawn boundary). */
extern sb32 syNetRbSnapshotSynctestProbeWeaponMismatch(u32 probe_tick);
/* TRUE when probe snapshot is fragile for round-trip (multi-item, effect count mismatch). */
extern sb32 syNetRbSnapshotSynctestShouldSkipProbeTick(u32 probe_tick, const char **reason_out);
/* PORT: live orphan effect count (userdata effects) != probe snapshot effect_count. */
extern sb32 syNetRbSnapshotSynctestProbeEffectMismatch(u32 probe_tick);
/* PORT: yakumono count / bounds capture mismatch vs live at probe tick. */
extern sb32 syNetRbSnapshotSynctestProbeMapMismatch(u32 probe_tick);
/* PORT: `SSB64_NETPLAY_GOBJ_LINK_AUDIT=1` — log per-link GObj census after snapshot apply. */
extern void syNetRbSnapshotGObjLinkAudit(u32 tick);
/* `SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1`: per-slot lines when load verify logs drift. */
extern void syNetRbSnapshotLogFighterLoadVerifyDiag(u32 tick, u32 live_f, u32 slot_f, u32 live_a, u32 slot_a);
/* Live fighter pointer/proc trail (`SSB64_NETPLAY_FIGHTER_STATUS_TRAIL=1` or fighter diag). */
extern void syNetRbSnapshotLogFighterStatusTrail(const char *tag, u32 tick);
extern void syNetRbSnapshotLogFighterBlobStatusTrail(const char *tag, u32 tick, s32 player,
						     const void *blob);
/* Ring slot blob status for each valid fighter at tick (anchor probe / load diagnostics). */
extern void syNetRbSnapshotLogRingBlobStatusTrailAtTick(const char *tag, u32 tick);
/* FALSE when any live fighter fails attr/data/proc sanity (load verify / post-restore guard). */
extern sb32 syNetRbSnapshotVerifyLiveFightersSanity(u32 tick, const char *tag);
/* TRUE for playable AppearR/L (and Captain/Ness/Boss Appear chains) via dFTCommonEntryAppearStatusIDs. */
extern sb32 syNetRbSnapshotStatusInAppearPresentationScope(s32 fkind, s32 status_id);
/* TRUE when any live fighter is in Entry or Appear intro load-fidelity scope. */
extern sb32 syNetRbSnapshotAnyLiveFighterInIntroLoadFidelityScope(void);
/* FALSE when Appear-scope fighters have null TopN or active modelpart joints without FTParts. */
extern sb32 syNetRbSnapshotVerifyAppearPresentationIntegrity(u32 tick);
/* Re-pin live fighter pose/anim from ring slot before anchor-probe +1 sim (post-prepare blob contract). */
extern void syNetRbSnapshotResyncLiveFightersFromSlotForSim(u32 load_tick);
/* Intro anchor probe pre +1 sim: Appear → TopN *p_translate; Wait/Entry → GObj root. */
extern void syNetRbSnapshotRebindFighterMPCollForAnchorProbePreSim(void);
/* Intro anchor probe post +1 sim: sync Appear GObj root translate from TopN before fhash_light rebind. */
extern void syNetRbSnapshotSyncAppearGobjTranslateFromTopNForAnchorProbe(void);
/* Intro anchor probe post +1 sim: Appear→Wait/Entry transition fold re-pin from ring@probe_tick. */
extern void syNetRbSnapshotReconcileAnchorProbeTransitionFromProbeSlot(u32 load_tick, u32 probe_tick);
/* Intro anchor probe post +1 sim: steady Appear fold re-pin from ring@probe_tick. */
extern void syNetRbSnapshotReconcileAnchorProbeAppearSteadyFromProbeSlot(u32 load_tick, u32 probe_tick);
/* Gameplay anchor probe post +1 sim: re-pin Pass/Squat/Landing/Dash leg AObj from ring@probe when live matches. */
extern void syNetRbSnapshotReconcileAnchorProbeGameplayFromProbeSlot(u32 load_tick, u32 probe_tick);
/* Intro anchor probe post +1 sim: steady Wait fold re-pin from ring@probe_tick (intro tick band only). */
extern void syNetRbSnapshotReconcileAnchorProbeWaitSteadyFromProbeSlot(u32 load_tick, u32 probe_tick);
/* Intro anchor probe post +1 sim: Wait peer fold re-pin when load slot has an Appear peer (@209 class). */
extern void syNetRbSnapshotReconcileAnchorProbeMixedAppearWaitFromProbeSlot(u32 load_tick, u32 probe_tick);
/* Intro anchor probe post +1 sim: Wait peer fold re-pin when load slot has an Entry peer (@133 class). */
extern void syNetRbSnapshotReconcileAnchorProbeMixedEntryWaitFromProbeSlot(u32 load_tick, u32 probe_tick);
/* Post MPColl rebind: idempotent Wait + mixed intro-physics+Wait terminal fold pin before step hash. */
extern void syNetRbSnapshotTerminalAnchorProbeWaitFoldFromProbeSlot(u32 load_tick, u32 probe_tick);
/* TRUE when any fighter has load/probe Wait blobs in intro anchor-probe walkback scope. */
extern sb32 syNetRbSnapshotAnchorProbeWaitSteadyScopeAtTicks(u32 load_tick, u32 probe_tick);
/* TRUE when load slot has Appear/Entry + steady Wait peer blobs at load/probe. */
extern sb32 syNetRbSnapshotAnchorProbeMixedIntroPhysicsWaitScopeAtTicks(u32 load_tick, u32 probe_tick);
/* TRUE when load slot has Appear + steady Wait peer blobs at load/probe. */
extern sb32 syNetRbSnapshotAnchorProbeMixedAppearWaitScopeAtTicks(u32 load_tick, u32 probe_tick);
/* TRUE when load slot has Entry + steady Wait peer blobs at load/probe. */
extern sb32 syNetRbSnapshotAnchorProbeMixedEntryWaitScopeAtTicks(u32 load_tick, u32 probe_tick);
/* Intro anchor probe post +1 sim: rebind all fighters' coll_data.p_translate to GObj root for fhash_light. */
extern void syNetRbSnapshotRebindFighterMPCollForAnchorProbe(void);
/* SSB64_NETPLAY_INTRO_ANCHOR_SIM_TRAIL=1: pre/post +1 sim Appear/MPColl scalar trail during anchor probe. */
extern void syNetRbSnapshotLogIntroAnchorSimTrail(const char *phase, u32 load_tick, u32 probe_tick);
/* `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`: named field lines when load verify figh drifts. */
extern void syNetRbSnapshotLogFighterFieldDiffOnLoadDrift(u32 tick);
extern void syNetRbSnapshotLogFighterFieldDiffAtTick(u32 tick, const char *tag);
#if defined(SSB64_NETMENU)
/* TRUE when a Pikachu QA catch-up transition is pending in the ring slot at tick (fc_recovery clamp). */
extern sb32 syNetRbSnapshotPikachuQuickAttackCatchUpPendingAtTick(u32 tick);
/* Defer weapon eject until load-hash verify succeeds (fc_recovery abort path). */
extern void syNetRbSnapshotCommitDeferredWeaponEject(u32 tick);
extern void syNetRbSnapshotCancelDeferredWeaponEject(void);
#endif
#endif
/*
 * Figatree presentation sync only (no status entry / motion event replay on default path).
 * Prefer syNetRbSnapshotFinalizeLoad for rollback commit paths — it runs this plus coupled-weapon rebind.
 */
extern void syNetRbSnapshotSyncFighterPresentation(void);
/* Re-apply blob joint anim after post-verify rebind (figatree attach can clobber AObj chains). */
extern void syNetRbSnapshotReapplyJointAnimAtTick(u32 completed_sim_tick);
extern sb32 syNetRbSnapshotSlotAnyFighterInYoshiShieldEscapeScopeAtTick(u32 completed_sim_tick);
/* Escape-roll or sustained guard: skip verify-only joint/figatree re-stamp (figh/anim fragile). */
extern sb32 syNetRbSnapshotSlotVerifyPresentationFragileAtTick(u32 completed_sim_tick);
/* Block eff-only LOAD_HASH_DRIFT soft-continue when guard shield coupling is unresolved. */
extern sb32 syNetRbSnapshotLoadHashEffSoftContinueBlocked(u32 tick);

/*
 * Collect active item GObjs (valid ITStruct), insertion-sorted by semantic rollback-hash key
 * (kind/player/type/multi/event_id/quantized pose — not gobj->id alone).
 * out must hold at least max entries. Returns count stored; *truncated_out TRUE if link has more than max.
 */
extern s32 syNetRbEnumerateActiveItemsSorted(struct GObj **out, s32 max, sb32 *truncated_out);
#if defined(SSB64_NETMENU)
/* Cross-ISA: snap live item translate/vel to the shared F32 grid after each sim tick (forward path). */
extern void syNetRbSnapshotCanonicalizeActiveItemsForNetplay(void);
#endif
extern s32 syNetRbEnumerateActiveWeaponsSorted(struct GObj **out, s32 max, sb32 *truncated_out);

#ifdef PORT
extern s32 syNetRbEnumerateActiveEffectsSorted(struct GObj **out, s32 max, sb32 *truncated_out);
extern u32 syNetRbSnapshotFoldGroundHash(const void *slot_opaque);
/* Live map digest: collision yakumono + ground fold (must match slot->hash_map at save). */
extern u32 syNetRbSnapshotComputeMapHashLive(void);
/* SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1: decompose kin vs ground fold on map load drift. */
extern void syNetRbSnapshotLogMapHashDriftDiag(u32 tick);
/* Same env: self-test immediately after ring save (stored hash vs ComputeMapHashLive). */
extern void syNetRbSnapshotLogMapHashSaveSelfTest(u32 tick);
#endif

#ifdef PORT
/* Subsystem hashes stored on the slot (for load verify / diagnostics). */
extern u32 syNetRbSnapshotGetSlotHashFighter(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashFighterLight(u32 tick);
#if defined(SSB64_NETMENU)
extern void syNetRbSnapshotCollectFighterSlotHashesAtTick(u32 tick, u32 *out_slot_hash);
extern u32 syNetRbSnapshotHashFightersLightFromLive(void);
#endif
extern u32 syNetRbSnapshotGetSlotHashWorld(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashItem(u32 tick);
extern u32 syNetRbSnapshotGetSlotItemCount(u32 tick);
extern void syNetRbSnapshotRebindFighterItemHoldCoupling(void);
extern u32 syNetRbSnapshotGetSlotHashWeapon(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashMap(u32 tick);
/* Stored yakumono slot count at save tick; -1 if slot invalid or map partition empty. */
extern s32 syNetRbSnapshotGetSlotMapYakumonoCount(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashRng(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashCamera(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashAnimation(u32 tick);
#ifdef PORT
extern u32 syNetRbSnapshotGetSlotHashEffect(u32 tick);
extern void syNetRbSnapRepairStageAfterParticleResetForTick(u32 tick);
/* TRUE during periodic synctest load+verify (live emergency restore follows). Skips twister repair entirely. */
extern void syNetRbSnapRepairStageSetVerifyOnly(sb32 verify_only);
extern sb32 syNetRbSnapRepairStageIsVerifyOnly(void);
extern void syNetRbSnapResetSectorArwingRepairDedup(void);
#endif
extern sb32 syNetRbSnapshotGetStoredSubsystemHashes(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng);
extern sb32 syNetRbSnapshotGetStoredSubsystemHashesEx(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng,
						    u32 *effect);
/* Portable effect identity for cross-peer hashes (bank_id + respawn_kind, not proc pointers). */
struct EFStruct;
extern u8 syNetRbSnapEffectRespawnKindFromLive(const struct GObj *gobj, const struct EFStruct *ep);
/* Ring slot published for tick (valid + tick match + save completed). */
extern sb32 syNetRbSnapshotIsTickCommitted(u32 tick);
/* Walk backward from tick down to min_tick inclusive; ~(u32)0 if none. */
extern u32 syNetRbSnapshotFindLatestValidTickAtOrBefore(u32 tick, u32 min_tick);
/* Same as above but only slots marked load-safe (no predicted-remote sim at that tick). */
extern u32 syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(u32 tick, u32 min_tick);
extern u32 syNetRbSnapshotGetLastLoadSafeTick(void);
extern void syNetRbSnapshotMarkLoadUnsafe(u32 tick);
/* TRUE when load_tick (or its first forward-sim tick) matches synctest fragile probe scopes. */
extern sb32 syNetRbSnapshotIsLoadAnchorFragile(u32 load_tick, const char **reason_out);
/*
 * Walk load_tick backward (load-safe, then valid) until non-fragile or max_rewind steps;
 * marks skipped ticks load-unsafe. Returns FALSE if still fragile at floor.
 */
extern sb32 syNetRbSnapshotResolveLoadAnchorAvoidingFragile(u32 *io_load_tick, u32 min_load, u32 max_rewind,
                                                            const char **reason_out);
/* Keep validation-boundary snapshot eligible for frame-commit reanchor loads. */
extern void syNetRbSnapshotPinLoadSafeAtTick(u32 tick);
/* Monotonic counter bumped by syNetRbSnapResetParticlesForRollback (rollback load particle wipe). */
extern u32 syNetRbSnapGetParticleResetGeneration(void);
/* Safe egg-lay effect id for sync folds — never dereferences stale captureyoshi.effect_gobj pointers. */
extern u32 syNetRbSnapHashCaptureYoshiEffectGobjId(const struct FTStruct *fp);
extern void syNetRbSnapSanitizeCaptureYoshiEffectGobj(struct FTStruct *fp);
extern void syNetRbSnapSanitizeAllFightersCaptureYoshiEffectGobjs(void);
#if defined(SSB64_NETMENU)
extern struct GObj *syNetRbSnapTryAdoptLiveYoshiShieldForEscapeEnd(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapTryAdoptLiveYoshiEggLayEffectForFighter(struct GObj *fighter_gobj);
extern void syNetRbSnapQueueYoshiEggLayHatchCosmeticsLive(struct GObj *fighter_gobj);
extern void syNetRbSnapshotFlushDeferredYoshiEggLayHatchCosmetics(void);
#endif
#endif

#endif /* _SYNETRB_SNAPSHOT_H_ */
