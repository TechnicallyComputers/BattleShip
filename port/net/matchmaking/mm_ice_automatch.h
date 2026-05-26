#ifndef MM_ICE_AUTOMATCH_H
#define MM_ICE_AUTOMATCH_H

#include <PR/ultratypes.h>
#include <ssb_types.h>
#include <mm_matchmaking.h>

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

extern sb32 mnVSNetAutomatchAMIcePlayerReady(const char *bind_spec, char *wan_out, u32 wan_cap, char *lan_out,
                                             u32 lan_cap, char *ice_sdp_out, u32 ice_sdp_cap);
extern void mnVSNetAutomatchAMIceBeginConnect(const MmMatchResult *mr);
extern sb32 mnVSNetAutomatchAMIceConnectTick(void);
extern sb32 mnVSNetAutomatchAMIceBootstrapPeer(const MmMatchResult *mr, const char *bind);

#else

#define mnVSNetAutomatchAMIcePlayerReady(b, w, wc, l, lc, s, sc) FALSE
#define mnVSNetAutomatchAMIceBeginConnect(mr) ((void)0)
#define mnVSNetAutomatchAMIceConnectTick() FALSE
#define mnVSNetAutomatchAMIceBootstrapPeer(mr, b) FALSE

#endif

#endif /* MM_ICE_AUTOMATCH_H */
