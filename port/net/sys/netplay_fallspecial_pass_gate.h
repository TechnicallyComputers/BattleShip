#ifndef SYS_NETPLAY_FALLSPECIAL_PASS_GATE_H
#define SYS_NETPLAY_FALLSPECIAL_PASS_GATE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

#if defined(PORT) && defined(SSB64_NETMENU)

/*
 * Netplay rollback only: restore fallspecial.is_allow_pass before ProcPass.
 * Vanilla sets it TRUE once in SetStatus and never clears it; FALSE reads are union stomp.
 */
extern void syNetplayFallSpecialPassGateHardenAllowPass(struct GObj *fighter_gobj);

#else

#define syNetplayFallSpecialPassGateHardenAllowPass(fighter_gobj) ((void)0)

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETPLAY_FALLSPECIAL_PASS_GATE_H */
