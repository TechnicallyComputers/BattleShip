#ifndef SYS_NETPLAY_PIKACHU_QUICKATTACK_GATE_H
#define SYS_NETPLAY_PIKACHU_QUICKATTACK_GATE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;
struct FTStruct;

#if defined(PORT) && defined(SSB64_NETMENU)

extern sb32 syNetplayPikachuQuickAttackGateDiagEnabled(void);

extern sb32 syNetplayPikachuFighterInQuickAttackScope(s32 status_id);
extern sb32 syNetplayPikachuFighterInQuickAttackZipScope(s32 status_id);
extern sb32 syNetplayPikachuFighterInQuickAttackStartScope(s32 status_id);
extern sb32 syNetplayPikachuFighterInQuickAttackEndScope(s32 status_id);
extern sb32 syNetplayPikachuFighterInQuickAttackLandingFallScope(const struct FTStruct *fp);
extern sb32 syNetplayPikachuFighterInQuickAttackSynctestDeferScope(const struct FTStruct *fp);
extern sb32 syNetplayPikachuClampFcRecoveryLoadTick(u32 *io_load_tick, u32 *io_mismatch_tick);
extern sb32 syNetplayPikachuFighterInQuickAttackShockFxScope(const struct FTStruct *fp);
extern sb32 syNetplayPikachuLiveHasQuickAttackSynctestDeferScope(void);

extern void syNetplayPikachuSanitizeQuickAttackStatusVars(struct FTStruct *fp);
extern void syNetplayPikachuSanitizeAllFightersAfterSlotApply(void);

extern void syNetplayPikachuCatchUpQuickAttackIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);
extern void syNetplayPikachuCatchUpAllAfterLoadVerify(void);

#else

#define syNetplayPikachuQuickAttackGateDiagEnabled() FALSE
#define syNetplayPikachuFighterInQuickAttackScope(status_id) FALSE
#define syNetplayPikachuFighterInQuickAttackZipScope(status_id) FALSE
#define syNetplayPikachuFighterInQuickAttackStartScope(status_id) FALSE
#define syNetplayPikachuFighterInQuickAttackEndScope(status_id) FALSE
#define syNetplayPikachuFighterInQuickAttackLandingFallScope(fp) FALSE
#define syNetplayPikachuFighterInQuickAttackSynctestDeferScope(fp) FALSE
#define syNetplayPikachuFighterInQuickAttackShockFxScope(fp) FALSE
#define syNetplayPikachuLiveHasQuickAttackSynctestDeferScope() FALSE
#define syNetplayPikachuSanitizeQuickAttackStatusVars(fp) ((void)0)
#define syNetplayPikachuSanitizeAllFightersAfterSlotApply() ((void)0)
#define syNetplayPikachuCatchUpQuickAttackIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayPikachuClampFcRecoveryLoadTick(io_load_tick, io_mismatch_tick) (FALSE)
#define syNetplayPikachuCatchUpAllAfterLoadVerify() ((void)0)

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETPLAY_PIKACHU_QUICKATTACK_GATE_H */
