/*
 * Satisfies symbols referenced from port/gameloop.cpp and port-only declarations in
 * port/net/sys/netpeer.h when SSB64_NETMENU=OFF (stock decomp netpeer.c has no host-frame pump /
 * VS-active helpers; netrollback.c is not linked).
 */
#include <PR/ultratypes.h>
#include <ssb_types.h>

sb32 syNetPeerIsVSSessionActive(void)
{
	return FALSE;
}

u32 syNetPeerGetVsContractViHz(void)
{
	return 0U;
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
