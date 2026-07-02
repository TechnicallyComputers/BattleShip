#ifndef SYS_NETPLAY_FOX_FIREFOX_GATE_H
#define SYS_NETPLAY_FOX_FIREFOX_GATE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;
struct FTStruct;

#if defined(PORT) && defined(SSB64_NETMENU)

extern sb32 syNetplayFoxFirefoxGateDiagEnabled(void);

extern sb32 syNetplayFoxFighterInFirefoxStartScope(s32 status_id);
extern sb32 syNetplayFoxFighterInFirefoxHoldScope(s32 status_id);
extern sb32 syNetplayFoxFighterInFirefoxTravelScope(s32 status_id);
extern sb32 syNetplayFoxFighterInFirefoxSynctestDeferScope(s32 status_id);
extern sb32 syNetplayFoxFighterInSpecialNScope(s32 status_id);
extern sb32 syNetplayFoxFighterInAppearScope(s32 fkind, s32 status_id);
extern sb32 syNetplayFoxFighterInResimPresentationScope(const struct FTStruct *fp);
extern sb32 syNetplayFoxLiveHasFirefoxSynctestDeferScope(void);
extern sb32 syNetplayFoxLiveHasResimPresentationScope(void);

/* After snapshot apply: if launch_delay missed zero (restore/resim), run vanilla launch. */
extern void syNetplayFoxCatchUpFirefoxLaunchIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

/* After snapshot apply: if anim_frames missed zero during travel, run vanilla end transition. */
extern void syNetplayFoxCatchUpFirefoxEndIfDue(struct GObj *fighter_gobj, struct FTStruct *fp);

/* Clamp negative gate timers and quantize travel angle before catch-up / sim hash. */
extern void syNetplayFoxSanitizeFirefoxStatusVars(struct FTStruct *fp);

/* Post load-hash verify: catch up hold/travel gates missed during apply (mirror Pikachu QA). */
extern void syNetplayFoxCatchUpAllAfterLoadVerify(void);

/* Diagnostic only: logs every forward-sim decrement of a gate countdown field (launch_delay in
 * Hold's ProcUpdate, anim_frames in Travel's ProcUpdate) with before/after values and sim tick, so
 * a soak trace can show whether the real forward-sim decrement runs more than once per elapsed sim
 * tick for the same fighter (which would explain Firefox ending far short of
 * FTFOX_FIREFOX_LAUNCH_DELAY / FTFOX_FIREFOX_TRAVEL_TIME). See
 * docs/bugs/netplay_fox_firefox_travel_truncation_diag_2026-07-01.md. */
extern void syNetplayFoxFirefoxGateLogFieldDecrement(struct GObj *fighter_gobj, struct FTStruct *fp,
                                                      const char *field, s32 before, s32 after);

/*
 * Firefox travel-span tracer (diag-only). Measures the exact frame-count loss reported by the
 * "Firefox cuts off early" soak: entry marks the tick + anim_frames when travel begins
 * (ftFoxSpecialHiInitStatusVars sets anim_frames = FTFOX_FIREFOX_TRAVEL_TIME), each forward-sim
 * decrement is counted, the end path (forward-sim ProcUpdate vs load-verify gate) is tagged, and the
 * span-end log reports span_ticks / sim_decrements / lost frames vs FTFOX_FIREFOX_TRAVEL_TIME.
 */
extern void syNetplayFoxFirefoxTravelSpanOnInit(struct FTStruct *fp);
extern void syNetplayFoxFirefoxTravelSpanOnSimDecrement(struct FTStruct *fp);
extern void syNetplayFoxFirefoxTravelSpanNoteEndPath(struct FTStruct *fp, sb32 from_gate);
extern void syNetplayFoxFirefoxTravelSpanOnEnd(struct FTStruct *fp);

#else

#define syNetplayFoxFirefoxGateDiagEnabled() FALSE
#define syNetplayFoxFighterInFirefoxStartScope(status_id) FALSE
#define syNetplayFoxFighterInFirefoxHoldScope(status_id) FALSE
#define syNetplayFoxFighterInFirefoxTravelScope(status_id) FALSE
#define syNetplayFoxFighterInFirefoxSynctestDeferScope(status_id) FALSE
#define syNetplayFoxFighterInSpecialNScope(status_id) FALSE
#define syNetplayFoxFighterInAppearScope(fkind, status_id) FALSE
#define syNetplayFoxFighterInResimPresentationScope(fp) FALSE
#define syNetplayFoxLiveHasFirefoxSynctestDeferScope() FALSE
#define syNetplayFoxLiveHasResimPresentationScope() FALSE
#define syNetplayFoxCatchUpFirefoxLaunchIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayFoxCatchUpFirefoxEndIfDue(fighter_gobj, fp) ((void)0)
#define syNetplayFoxSanitizeFirefoxStatusVars(fp) ((void)0)
#define syNetplayFoxCatchUpAllAfterLoadVerify() ((void)0)
#define syNetplayFoxFirefoxGateLogFieldDecrement(fighter_gobj, fp, field, before, after) ((void)0)
#define syNetplayFoxFirefoxTravelSpanOnInit(fp) ((void)0)
#define syNetplayFoxFirefoxTravelSpanOnSimDecrement(fp) ((void)0)
#define syNetplayFoxFirefoxTravelSpanNoteEndPath(fp, from_gate) ((void)0)
#define syNetplayFoxFirefoxTravelSpanOnEnd(fp) ((void)0)

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETPLAY_FOX_FIREFOX_GATE_H */
