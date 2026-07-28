#pragma once

/*
 * FTStatusVars overlay witness — see netplay_statusvars_witness.c.
 * syNetplayStatusVarsWitnessNoteAccess() is declared from ft/ftstatusvars.h
 * once FTStruct is complete (via ft/fighter.h).
 */

#include <ft/ftstatusvars.h>

struct FTStruct;

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * C2a tag source: status_id (+ camera fallback) -> owning FTStatusVars overlay
 * (ftstatusvars_overlay_map ownership table). nFTStatusVarsOverlayNone when unowned.
 */
extern enum FTStatusVarsOverlay syNetplayStatusVarsExpectedOverlay(const struct FTStruct *fp);
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
