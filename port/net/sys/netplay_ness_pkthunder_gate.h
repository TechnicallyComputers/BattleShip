#ifndef NETPLAY_NESS_PKTHUNDER_GATE_H
#define NETPLAY_NESS_PKTHUNDER_GATE_H

#include <ssb_types.h>

struct FTStruct;
struct GObj;
struct WPStruct;
union FTStatusVars;

#if defined(PORT) && defined(SSB64_NETMENU)

extern sb32 syNetplayNessFighterInPKJibakuCatchUpScope(const struct FTStruct *fp);

extern sb32 syNetplayNessFighterInPKThunderLandingFallScope(const struct FTStruct *fp);

extern sb32 syNetplayNessFighterInFcResimDeferScope(s32 status_id);

extern sb32 syNetplayNessAnyLiveFighterInJibakuBurstScope(void);

extern sb32 syNetplayNessAnyLiveFighterInFcResimDeferScope(void);

extern sb32 syNetplayNessAnyLiveFighterInFcStateRecoveryDeferScope(void);

extern sb32 syNetplayNessClampFcRecoveryLoadTick(u32 *io_load_tick, u32 *io_mismatch_tick);

extern void syNetplayNessResimReplayHardeningAfterLoadStep(void);

extern void syNetplayNessResimHardeningAfterSnapshotLoad(void);

extern void syNetRbSnapRebindNessPKJibakuProcs(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessSanitizePKJibakuStatusVars(struct FTStruct *fp);

extern void syNetplayNessSanitizePKThunderThrowStatusVars(struct FTStruct *fp);

extern f32 syNetplayNessCanonicalPKThunderHoldFallVelY(struct FTStruct *fp);

extern void syNetplayNessHardenPKThunderHoldAirFallAfterTranslate(struct GObj *fighter_gobj);

extern sb32 syNetplayNessHoldJibakuCollideBlocked(const struct FTStruct *fp);

extern void syNetplayNessSanitizeAllFightersAfterSlotApply(void);

extern void syNetplayNessSyncHoldEntryTrackingFromApply(struct FTStruct *fp);

extern void syNetplayNessCatchUpPKJibakuIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessRunLiveJibakuCatchUpAll(void);

extern void syNetplayNessNotifyThrowStarted(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessNotifyHoldEntered(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessNotifyHoldEarlyExit(struct GObj *fighter_gobj, struct FTStruct *fp, const char *reason);

extern void syNetplayNessNotifyJibakuTriggered(struct GObj *fighter_gobj, struct FTStruct *fp, s32 from_status_id);

extern void syNetplayNessFinishJibakuTransition(struct GObj *fighter_gobj);

extern void syNetplayNessNotifyJibakuPhase(struct GObj *fighter_gobj, const char *phase);

extern void syNetplayNessReconcilePKThunderWeaponsAfterApply(struct GObj *fighter_gobj);

extern void syNetplayNessRefreshPKThunderPosFromHead(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessSyncPKThunderPosDuringHold(struct GObj *fighter_gobj);

extern void syNetplayNessPrepareHoldSelfHitCoupling(struct GObj *fighter_gobj);

extern void syNetplayNessPrepareJibakuCoupling(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessNotifyJibakuCollide(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessNotifyJibakuLaunchDist(struct GObj *fighter_gobj, struct FTStruct *fp, f32 dist_x,
                                               f32 dist_y);

extern void syNetplayNessNotifyJibakuCouplingSample(struct GObj *fighter_gobj, struct FTStruct *fp, const char *site);

extern void syNetplayNessNotifyAirJibakuProcMapDefer(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessRefreshPKThunderPosForJibakuLaunch(struct GObj *fighter_gobj, struct FTStruct *fp);

extern void syNetplayNessRefreshPKThunderPosInBlobFromHead(struct GObj *fighter_gobj, struct FTStruct *fp,
                                                           union FTStatusVars *blob_status_vars);

extern sb32 syNetplayNessShouldDeferPKThunderHeadProcTeardown(struct WPStruct *wp);

extern sb32 syNetplayNessShouldDeferPKThunderTeardownForPlayer(s32 player);

extern sb32 syNetplayNessShouldBlockAirJibakuGroundSnap(const struct FTStruct *fp);

extern void syNetplayNessNotifyAirJibakuGroundSnap(struct GObj *fighter_gobj, struct FTStruct *fp, const char *source);

extern void syNetplayNessNotifyAirJibakuGroundSnapBlocked(struct GObj *fighter_gobj, struct FTStruct *fp);

extern sb32 syNetplayNessIsPKThunderGlobalDeferActive(void);

extern void syNetplayNessNotifyJibakuPostFinish(struct GObj *fighter_gobj);

extern void syNetplayNessProbeFighterNaN(struct GObj *fighter_gobj, struct FTStruct *fp, const char *site);

#else

#define syNetplayNessFighterInPKJibakuCatchUpScope(fp) (FALSE)
#define syNetplayNessFighterInPKThunderLandingFallScope(fp) (FALSE)
#define syNetplayNessFighterInFcResimDeferScope(status_id) (FALSE)
#define syNetplayNessAnyLiveFighterInJibakuBurstScope() (FALSE)
#define syNetplayNessAnyLiveFighterInFcResimDeferScope() (FALSE)
#define syNetplayNessAnyLiveFighterInFcStateRecoveryDeferScope() (FALSE)
#define syNetplayNessClampFcRecoveryLoadTick(io_load_tick, io_mismatch_tick) (FALSE)
#define syNetplayNessResimReplayHardeningAfterLoadStep() ((void)0)
#define syNetplayNessResimHardeningAfterSnapshotLoad() ((void)0)
#define syNetRbSnapRebindNessPKJibakuProcs(fighter_gobj, fp) ((void)0)
#define syNetplayNessSanitizePKJibakuStatusVars(fp) ((void)0)
#define syNetplayNessSanitizePKThunderThrowStatusVars(fp) ((void)0)
#define syNetplayNessCanonicalPKThunderHoldFallVelY(fp) (0.0F)
#define syNetplayNessHardenPKThunderHoldAirFallAfterTranslate(fighter_gobj) ((void)0)
#define syNetplayNessHoldJibakuCollideBlocked(fp) (FALSE)
#define syNetplayNessSanitizeAllFightersAfterSlotApply() ((void)0)
#define syNetplayNessSyncHoldEntryTrackingFromApply(fp) ((void)0)
#define syNetplayNessCatchUpPKJibakuIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayNessRunLiveJibakuCatchUpAll() ((void)0)
#define syNetplayNessNotifyThrowStarted(fighter_gobj, fp) ((void)0)
#define syNetplayNessNotifyHoldEntered(fighter_gobj, fp) ((void)0)
#define syNetplayNessNotifyHoldEarlyExit(fighter_gobj, fp, reason) ((void)0)
#define syNetplayNessNotifyJibakuTriggered(fighter_gobj, fp, from_status_id) ((void)0)
#define syNetplayNessFinishJibakuTransition(fighter_gobj) ((void)0)
#define syNetplayNessNotifyJibakuPhase(fighter_gobj, phase) ((void)0)
#define syNetplayNessReconcilePKThunderWeaponsAfterApply(fighter_gobj) ((void)0)
#define syNetplayNessRefreshPKThunderPosFromHead(fighter_gobj, fp) ((void)0)
#define syNetplayNessSyncPKThunderPosDuringHold(fighter_gobj) ((void)0)
#define syNetplayNessPrepareHoldSelfHitCoupling(fighter_gobj) ((void)0)
#define syNetplayNessPrepareJibakuCoupling(fighter_gobj, fp) ((void)0)
#define syNetplayNessNotifyJibakuCollide(fighter_gobj, fp) ((void)0)
#define syNetplayNessNotifyJibakuLaunchDist(fighter_gobj, fp, dist_x, dist_y) ((void)0)
#define syNetplayNessNotifyJibakuCouplingSample(fighter_gobj, fp, site) ((void)0)
#define syNetplayNessNotifyAirJibakuProcMapDefer(fighter_gobj, fp) ((void)0)
#define syNetplayNessRefreshPKThunderPosForJibakuLaunch(fighter_gobj, fp) ((void)0)
#define syNetplayNessRefreshPKThunderPosInBlobFromHead(fighter_gobj, fp, blob_status_vars) ((void)0)
#define syNetplayNessShouldDeferPKThunderHeadProcTeardown(wp) (FALSE)
#define syNetplayNessShouldDeferPKThunderTeardownForPlayer(player) (FALSE)
#define syNetplayNessShouldBlockAirJibakuGroundSnap(fp) (FALSE)
#define syNetplayNessNotifyAirJibakuGroundSnap(fighter_gobj, fp, source) ((void)0)
#define syNetplayNessNotifyAirJibakuGroundSnapBlocked(fighter_gobj, fp) ((void)0)
#define syNetplayNessIsPKThunderGlobalDeferActive() (FALSE)
#define syNetplayNessNotifyJibakuPostFinish(fighter_gobj) ((void)0)
#define syNetplayNessProbeFighterNaN(fighter_gobj, fp, site) ((void)0)

#endif

#endif /* NETPLAY_NESS_PKTHUNDER_GATE_H */
