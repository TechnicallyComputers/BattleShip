#ifndef PORT_ENHANCEMENT_NETPLAY_CVAR_H
#define PORT_ENHANCEMENT_NETPLAY_CVAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NETMENU allowlist for enhancement / cheat CVars.
 *
 * Offline builds: always honor the real CVar.
 * Netplay builds: only allowlisted gEnhancements.* names are read from cfg;
 * all other gEnhancements.* and all gCheats.* return vanilla (int 0 / float def).
 * New offline gameplay hacks are auto-denied until explicitly allowlisted.
 */
int port_enhancement_netplay_cvar_allowed(const char *name);

int32_t port_enhancement_cvar_get_integer(const char *name, int32_t default_value);
float port_enhancement_cvar_get_float(const char *name, float default_value);

#ifdef __cplusplus
}
#endif

#endif
