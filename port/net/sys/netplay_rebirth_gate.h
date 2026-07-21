#ifndef SYS_NETPLAY_REBIRTH_GATE_H
#define SYS_NETPLAY_REBIRTH_GATE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;
struct FTStruct;

#if defined(PORT) && defined(SSB64_NETMENU)

extern sb32 syNetplayRebirthGateDiagEnabled(void);

extern void syNetplayRebirthGateLogDeadInit(struct GObj *fighter_gobj, struct FTStruct *fp);
extern void syNetplayRebirthGateLogDeadWaitZero(struct GObj *fighter_gobj, struct FTStruct *fp);
extern void syNetplayRebirthGateLogCheckRebirth(struct GObj *fighter_gobj, struct FTStruct *fp, const char *branch);
extern void syNetplayRebirthGateLogRebirthDownSetStatus(struct GObj *fighter_gobj, struct FTStruct *fp, s32 halo_number);

extern void syNetplayRebirthSnapSyncBattleStock(struct FTStruct *fp);
extern void syNetplayRebirthSanitizeIsRebirthFlag(struct FTStruct *fp);
extern sb32 syNetplayRebirthShouldForceSleepSetStatus(const struct FTStruct *fp);
extern void syNetplayRebirthApplyEliminationPresentation(struct GObj *fighter_gobj, struct FTStruct *fp);

/*
 * Stick-only GGPO absorb: Dead* stick REPLACE cannot change hashed sim but still
 * opened resim that burned gameplay LCG asymmetrically (soak 1790844706 tick 3820,
 * Whispy wind_dur fork). Buttons/release still rewind.
 * Scope is Dead* only — not bare is_ghost (RebirthWait leave needs stick GGPO;
 * soak 1174892281). See docs/bugs/netplay_dead_stick_ggpo_resim_rng_whispy_blow_2026-07-20.md
 * and docs/bugs/netplay_rebirth_ghost_stick_absorb_leave_peer_2026-07-20.md.
 */
extern sb32 syNetplayPlayerInDeadGhostStickAbsorbScope(s32 player);

/* Always-on VS: RebirthWait → Fall/ground leave with stick + halo_despawn snapshot. */
extern void syNetplayRebirthGateLogLeaveStick(struct GObj *fighter_gobj, struct FTStruct *fp, const char *reason,
                                             s32 status_before);

/* After snapshot apply: if dead.wait missed zero (restore/resim), run vanilla check_rebirth. */
extern void syNetplayRebirthCatchUpDeadGateIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

/* Per sim tick: catch up dead gate + rebirth lifecycle for all fighters (post-proc safety net). */
extern void syNetplayRebirthCatchUpFightersTick(void);

/* After snapshot apply: if rebirth timer gates missed (restore/resim), run vanilla transitions. */
extern void syNetplayRebirthCatchUpLifecycleIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

extern sb32 syNetplayRebirthSimDiagEnabled(void);
extern void syNetplayRebirthSimDiagLogTick(u32 tick);

#else

#define syNetplayRebirthGateDiagEnabled() FALSE
#define syNetplayRebirthGateLogDeadInit(fighter_gobj, fp) ((void)0)
#define syNetplayRebirthGateLogDeadWaitZero(fighter_gobj, fp) ((void)0)
#define syNetplayRebirthGateLogCheckRebirth(fighter_gobj, fp, branch) ((void)0)
#define syNetplayRebirthGateLogRebirthDownSetStatus(fighter_gobj, fp, halo_number) ((void)0)
#define syNetplayRebirthSnapSyncBattleStock(fp) ((void)0)
#define syNetplayRebirthSanitizeIsRebirthFlag(fp) ((void)0)
#define syNetplayRebirthShouldForceSleepSetStatus(fp) FALSE
#define syNetplayRebirthApplyEliminationPresentation(fighter_gobj, fp) ((void)0)
#define syNetplayPlayerInDeadGhostStickAbsorbScope(player) (FALSE)
#define syNetplayRebirthGateLogLeaveStick(fighter_gobj, fp, reason, status_before) ((void)0)
#define syNetplayRebirthCatchUpDeadGateIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayRebirthCatchUpFightersTick() ((void)0)
#define syNetplayRebirthCatchUpLifecycleIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayRebirthSimDiagEnabled() FALSE
#define syNetplayRebirthSimDiagLogTick(tick) ((void)0)

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETPLAY_REBIRTH_GATE_H */
