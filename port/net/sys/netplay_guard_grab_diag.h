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

/*
 * Log the CatchPull -> CatchWait anim-end edge, on EVERY pass including resim replay.
 * ftCommonCatchPullProcUpdate gates the whole grab on ftAnimEndCheckSetStatus, i.e. purely
 * on gobj->anim_frame <= 0, and only then sets the victim's capture.is_goto_pulled_wait.
 * Accepted-tick logs cannot show replayed ticks, so a predicting peer evaluating that edge
 * one frame differently is invisible — which is exactly the surviving grab failure.
 * See docs/bugs/netplay_capture_goto_pulled_wait_hash_blind_2026-08-22.md.
 */
void syNetplayGuardGrabDiagLogCatchPullAnimEnd(struct GObj *fighter_gobj, sb32 anim_end,
                                               f32 anim_frame, struct GObj *catch_gobj);

/*
 * Log any ftMainSetStatus whose from/to status touches the grab band (166..172), with the
 * caller resolved to a symbol. Fires on replayed passes too.
 */
void syNetplayGuardGrabDiagLogSetStatus(struct GObj *fighter_gobj, s32 from_status, s32 to_status,
                                        const void *caller);

#ifdef __cplusplus
}
#endif
