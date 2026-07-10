#pragma once

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

#ifdef __cplusplus
extern "C" {
#endif

/* Enable with SSB64_NETPLAY_GUARD_GRAB_DIAG=1 (netmenu build only). */
sb32 syNetplayGuardGrabDiagEnabled(void);

/* Log R/Z/A input edges after ftMain synthesizes R -> Z+A. */
void syNetplayGuardGrabDiagLogRInputEdge(struct GObj *fighter_gobj);

/* Log ftCommonCatchCheckInterruptCommon outcome (call before return). */
void syNetplayGuardGrabDiagLogCatchAttempt(struct GObj *fighter_gobj, sb32 success, const char *reason);

/* Log ftCommonGuardOnCheckInterruptSuccess when guard wins. */
void syNetplayGuardGrabDiagLogGuardOn(struct GObj *fighter_gobj, const char *site);

/* Log netplay GuardOff/GuardSetOff catch assist (rollback-active only). */
void syNetplayGuardGrabDiagLogGuardDropCatch(struct GObj *fighter_gobj, sb32 success, s32 status_id);

#ifdef __cplusplus
}
#endif
