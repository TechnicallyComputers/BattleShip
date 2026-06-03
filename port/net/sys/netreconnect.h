#ifndef NETRECONNECT_H
#define NETRECONNECT_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

extern sb32 syNetReconnectEnabled(void);
/* Full mid-match gate: VSBattle + contracts + boot window + remote wire_base armed. */
extern sb32 syNetReconnectMidMatchEligible(void);
/* Alias for syNetReconnectMidMatchEligible (legacy name). */
extern sb32 syNetReconnectBattleEligible(void);
/* Poll remote wire_base readiness and arm transport monitoring when satisfied. */
extern void syNetReconnectPollTransportArm(void);
extern sb32 syNetReconnectHoldActive(void);
extern sb32 syNetReconnectBlocksUnpause(void);
extern sb32 syNetReconnectShouldPreserveAutomatchContext(void);
extern sb32 syNetReconnectOverlayEnabled(void);
extern u32 syNetReconnectGraceFramesRemaining(void);

extern void syNetReconnectReset(void);
extern void syNetReconnectShutdown(void);
extern void syNetReconnectUpdate(void);

extern void syNetReconnectOnHoldIngress(u32 sim_tick, u32 epoch, u8 pausing_slot, u8 reason);
extern void syNetReconnectOnReadyIngress(u32 sim_tick, u32 epoch);
extern void syNetReconnectOnAckIngress(u32 sim_tick, u32 epoch);
extern void syNetReconnectOnForfeitIngress(u32 sim_tick, u8 forfeiting_slot, u8 winner_slot);

extern void syNetReconnectNotifyTransportBad(void);
extern void syNetReconnectNotifyNetworkChange(void);
extern void syNetReconnectNotifyPeerDisconnect(u8 slot);

extern sb32 syNetReconnectExportPeerDisconnect(s32 slot);
extern void syNetReconnectDrawOverlayCpp(void);

#else

#define syNetReconnectEnabled() FALSE
#define syNetReconnectMidMatchEligible() FALSE
#define syNetReconnectBattleEligible() FALSE
#define syNetReconnectPollTransportArm() ((void)0)
#define syNetReconnectHoldActive() FALSE
#define syNetReconnectBlocksUnpause() FALSE
#define syNetReconnectShouldPreserveAutomatchContext() FALSE
#define syNetReconnectOverlayEnabled() FALSE
#define syNetReconnectGraceFramesRemaining() 0U
#define syNetReconnectReset() ((void)0)
#define syNetReconnectShutdown() ((void)0)
#define syNetReconnectUpdate() ((void)0)
#define syNetReconnectNotifyTransportBad() ((void)0)
#define syNetReconnectNotifyNetworkChange() ((void)0)
#define syNetReconnectNotifyPeerDisconnect(S) ((void)(S))
#define syNetReconnectExportPeerDisconnect(S) 0
#define syNetReconnectDrawOverlayCpp() ((void)0)

#endif

#endif
