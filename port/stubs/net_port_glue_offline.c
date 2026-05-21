/*
 * Satisfies symbols referenced from port/gameloop.cpp, decomp/src/sys/objman.c (PORT diagnostics),
 * and port-only declarations in port/net/sys/*.h when SSB64_NETMENU=OFF (stock decomp netpeer.c
 * has no host-frame pump / VS-active helpers; netinput.c / netrollback.c are not linked).
 */
#include <PR/ultratypes.h>
#include <ssb_types.h>

u32 syNetInputGetTick(void)
{
	return 0U;
}

void syNetPeerSendVsSessionEndNotifyPeer(void)
{
}

void syNetPeerEndVSSessionLocally(void)
{
}

sb32 syNetPeerIsVSSessionActive(void)
{
	return FALSE;
}

u32 syNetPeerGetVsContractViHz(void)
{
	return 0U;
}

sb32 syNetRollbackIsActive(void)
{
	return FALSE;
}

void syNetRollbackApplyPortSimPacing(u32 refresh_hz)
{
	(void)refresh_hz;
}

#ifdef PORT
sb32 syNetPeerShouldPumpBattleGateOnHostFrame(void)
{
	return FALSE;
}

void syNetPeerPumpBattleGateOnHostFrame(void)
{
}

void syNetPeerPumpIngressTransport(const char *caller_tag)
{
	(void)caller_tag;
}

sb32 syNetPeerWantsSyncPresentHold(void)
{
	return FALSE;
}

sb32 syNetPeerShouldHoldSimTickForSkewPacing(u32 tick, s32 *out_skew)
{
	(void)tick;
	if (out_skew != NULL)
	{
		*out_skew = 0;
	}
	return FALSE;
}

sb32 syNetPeerShouldBypassDecoupleSimPacingForTickGrid(void)
{
	return FALSE;
}

u32 syNetPeerGetSkewPacingHoldFrameCount(void)
{
	return 0U;
}
#endif
