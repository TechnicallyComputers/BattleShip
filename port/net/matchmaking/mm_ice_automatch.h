#ifndef MM_ICE_AUTOMATCH_H
#define MM_ICE_AUTOMATCH_H

#include <PR/ultratypes.h>
#include <ssb_types.h>
#include <mm_matchmaking.h>

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

extern void mnVSNetAutomatchAMIceReset(void);
extern void mnVSNetAutomatchAMIceOnTicketAssigned(const char *ticket);
extern sb32 mnVSNetAutomatchAMIcePlayerReady(const char *bind_spec, char *wan_out, u32 wan_cap, char *lan_out,
                                             u32 lan_cap, char *ice_sdp_out, u32 ice_sdp_cap);
extern sb32 mnVSNetAutomatchAMIceBindTick(char *ice_sdp_out, u32 ice_sdp_cap);
/** Discovered LAN host:port (refreshed after gather; may be empty until bind tick). */
extern const char *mnVSNetAutomatchAMIceLocalLan(void);
/** FALSE when shared-LAN / LAN-direct gather should not register coturn relay on queue join. */
extern sb32 mnVSNetAutomatchAMIceShouldQueueTurnEndpoint(void);
extern sb32 mnVSNetAutomatchAMIceBeginConnect(const MmMatchResult *mr);
/* 0 = in progress, 1 = ICE completed and path validated, -1 = failed */
extern s32 mnVSNetAutomatchAMIceConnectTick(void);
extern const char *mnVSNetAutomatchAMIceConnectFailureReason(void);
extern sb32 mnVSNetAutomatchAMIceShouldIgnorePollError(const MmMatchResult *ev);
extern sb32 mnVSNetAutomatchAMIceBootstrapPeer(const MmMatchResult *mr, const char *bind);
/* Trickle poll cadence while ICE_CONNECT (0 = skip poll this tick). */
extern u32 mnVSNetAutomatchAMIceConnectTricklePollInterval(void);
extern void mnVSNetAutomatchAMIceNotifyPeerAbort(const MmMatchResult *mr);

#else

#define mnVSNetAutomatchAMIceReset() ((void)0)
#define mnVSNetAutomatchAMIceOnTicketAssigned(t) ((void)0)
#define mnVSNetAutomatchAMIcePlayerReady(b, w, wc, l, lc, s, sc) FALSE
#define mnVSNetAutomatchAMIceBindTick(s, sc) FALSE
#define mnVSNetAutomatchAMIceLocalLan() NULL
#define mnVSNetAutomatchAMIceShouldQueueTurnEndpoint() FALSE
#define mnVSNetAutomatchAMIceBeginConnect(mr) FALSE
#define mnVSNetAutomatchAMIceConnectTick() 0
#define mnVSNetAutomatchAMIceConnectFailureReason() "ICE connection failed"
#define mnVSNetAutomatchAMIceShouldIgnorePollError(ev) FALSE
#define mnVSNetAutomatchAMIceBootstrapPeer(mr, b) FALSE
#define mnVSNetAutomatchAMIceConnectTricklePollInterval() 0U
#define mnVSNetAutomatchAMIceNotifyPeerAbort(mr) ((void)0)

#endif

#endif /* MM_ICE_AUTOMATCH_H */
