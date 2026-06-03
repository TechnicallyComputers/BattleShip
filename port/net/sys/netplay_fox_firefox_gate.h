#ifndef SYS_NETPLAY_FOX_FIREFOX_GATE_H
#define SYS_NETPLAY_FOX_FIREFOX_GATE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;
struct FTStruct;

#if defined(PORT) && defined(SSB64_NETMENU)

extern sb32 syNetplayFoxFirefoxGateDiagEnabled(void);

extern sb32 syNetplayFoxFighterInFirefoxHoldScope(s32 status_id);
extern sb32 syNetplayFoxFighterInFirefoxTravelScope(s32 status_id);
extern sb32 syNetplayFoxFighterInFirefoxSynctestDeferScope(s32 status_id);
extern sb32 syNetplayFoxLiveHasFirefoxSynctestDeferScope(void);

/* After snapshot apply: if launch_delay missed zero (restore/resim), run vanilla launch. */
extern void syNetplayFoxCatchUpFirefoxLaunchIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

/* After snapshot apply: if anim_frames missed zero during travel, run vanilla end transition. */
extern void syNetplayFoxCatchUpFirefoxEndIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

#else

#define syNetplayFoxFirefoxGateDiagEnabled() FALSE
#define syNetplayFoxFighterInFirefoxHoldScope(status_id) FALSE
#define syNetplayFoxFighterInFirefoxTravelScope(status_id) FALSE
#define syNetplayFoxFighterInFirefoxSynctestDeferScope(status_id) FALSE
#define syNetplayFoxLiveHasFirefoxSynctestDeferScope() FALSE
#define syNetplayFoxCatchUpFirefoxLaunchIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayFoxCatchUpFirefoxEndIfDue(fighter_gobj, fp) ((void)0)

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETPLAY_FOX_FIREFOX_GATE_H */
