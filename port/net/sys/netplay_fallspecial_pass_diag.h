#pragma once

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

#ifdef __cplusplus
extern "C" {
#endif

/* Enable with SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG=1 (netmenu build only). */
sb32 syNetplayFallSpecialPassDiagEnabled(void);

/*
 * Log soft-platform pass ProcPass outcome.
 * proc_pass_block=TRUE means mpCommonCheckFighterPassCliff will not drop through.
 */
void syNetplayFallSpecialPassDiagLogProcPass(struct GObj *fighter_gobj, const char *site, sb32 proc_pass_block);

/* Log ftCommonFallSpecialSetStatus (helpless enter) for Samus / copy-Samus screw scope. */
void syNetplayFallSpecialPassDiagLogFallSpecialEnter(struct GObj *fighter_gobj, const char *site);

/* Log mpCommonCheckFighterPassCliff success (fighter is passing through a soft platform). */
void syNetplayFallSpecialPassDiagLogPassCliff(struct GObj *fighter_gobj, const char *site);

#ifdef __cplusplus
}
#endif
