#pragma once

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Yoshi Z-shield / shield-roll union-pool bisect (Training + netplay).
 * Enable with SSB64_NETPLAY_YOSHI_SHIELD_ESCAPE_PROBE=1 (netmenu build only).
 *
 * Logs guard vs escape overlay aliasing and live effect attachment at:
 *   escape_enter_pre/post  — ftCommonEscapeSetStatus
 *   guard_reenter_*        — ftCommonGuardSetStatusFromEscape
 */
sb32 syNetplayYoshiShieldEscapeProbeEnabled(void);

void syNetplayYoshiShieldEscapeProbeLogEscapeSetStatus(struct GObj *fighter_gobj, s32 status_id,
                                                       s32 itemthrow_buffer_tics, const char *phase);

void syNetplayYoshiShieldEscapeProbeLogGuardFromEscape(struct GObj *fighter_gobj, const char *phase,
                                                       sb32 adopt_attempted, sb32 adopt_ok);

#ifdef __cplusplus
}
#endif
