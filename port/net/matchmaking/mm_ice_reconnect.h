#ifndef MM_ICE_RECONNECT_H
#define MM_ICE_RECONNECT_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

typedef enum MmIceReconnectStatus
{
	MM_ICE_RECONNECT_IDLE = 0,
	MM_ICE_RECONNECT_WORKING,
	MM_ICE_RECONNECT_CONNECTED,
	MM_ICE_RECONNECT_FAILED,
} MmIceReconnectStatus;

extern void mmIceReconnectBegin(u32 connect_epoch);
extern MmIceReconnectStatus mmIceReconnectTick(void);
extern void mmIceReconnectShutdown(void);
/** MM worker: POST ice/restart (host), TURN fetch, libjuice init + gather start. */
extern sb32 mmIceReconnectInitOnWorker(void);
extern void mmIceReconnectWorkerInitFinished(sb32 ok);

#else

typedef enum MmIceReconnectStatus
{
	MM_ICE_RECONNECT_IDLE = 0,
	MM_ICE_RECONNECT_WORKING,
	MM_ICE_RECONNECT_CONNECTED,
	MM_ICE_RECONNECT_FAILED,
} MmIceReconnectStatus;

#define mmIceReconnectBegin(E) ((void)(E))
#define mmIceReconnectTick() MM_ICE_RECONNECT_IDLE
#define mmIceReconnectShutdown() ((void)0)

#endif

#endif
