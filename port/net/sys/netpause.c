#include <sys/netpause.h>

#include <if/interface.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <string.h>
#include <sys/controller.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netrollback.h>

extern sb32 ifCommonBattlePauseSetupFromPlayer(s32 player);
extern sb32 ifCommonBattlePausePlayerCanRequestPause(s32 player);
extern void ifCommonBattlePauseInitInterface(s32 player);
extern void ifCommonBattlePauseBeginUnpause(void);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetPausePending;
static u32 sSYNetPauseTick;
static s32 sSYNetPausePlayer;

static sb32 sSYNetUnpausePending;
static u32 sSYNetUnpauseTick;

void syNetPauseReset(void)
{
	sSYNetPausePending = FALSE;
	sSYNetPauseTick = 0U;
	sSYNetPausePlayer = 0;
	sSYNetUnpausePending = FALSE;
	sSYNetUnpauseTick = 0U;
}

static void syNetPauseArmPause(u32 tick, s32 player)
{
	if ((sSYNetPausePending != FALSE) && (tick > sSYNetPauseTick))
	{
		return;
	}
	if ((sSYNetPausePending != FALSE) && (tick == sSYNetPauseTick) && (player >= sSYNetPausePlayer))
	{
		return;
	}
	sSYNetPausePending = TRUE;
	sSYNetPauseTick = tick;
	sSYNetPausePlayer = player;
}

static void syNetPauseArmUnpause(u32 tick)
{
	if ((sSYNetUnpausePending != FALSE) && (tick > sSYNetUnpauseTick))
	{
		return;
	}
	sSYNetUnpausePending = TRUE;
	sSYNetUnpauseTick = tick;
}

static sb32 syNetPauseTryApplyPause(u32 tick)
{
	if ((gSCManagerBattleState == NULL) || (sSYNetPausePending == FALSE) || (tick < sSYNetPauseTick))
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return FALSE;
	}
	if (tick > sSYNetPauseTick)
	{
		if (syNetRollbackRewindToPauseBoundary(sSYNetPauseTick) == FALSE)
		{
			return FALSE;
		}
		tick = sSYNetPauseTick;
	}
	if (ifCommonBattlePauseSetupFromPlayer(sSYNetPausePlayer) == FALSE)
	{
		extern u8 sIFCommonBattlePauseKindInterface;

		sIFCommonBattlePauseKindInterface = 0;
	}
	ifCommonBattlePauseInitInterface(sSYNetPausePlayer);
	sSYNetPausePending = FALSE;
	sSYNetUnpausePending = FALSE;
	sSYNetUnpauseTick = 0U;
	port_log("SSB64 NetPause: applied pause tick=%u player=%d\n", tick, (int)sSYNetPausePlayer);
	return TRUE;
}

static sb32 syNetPauseTryApplyUnpause(u32 tick)
{
	if ((gSCManagerBattleState == NULL) || (sSYNetUnpausePending == FALSE) || (tick < sSYNetUnpauseTick))
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusPause)
	{
		return FALSE;
	}
	if (tick > sSYNetUnpauseTick)
	{
		if (syNetRollbackRewindToPauseBoundary(sSYNetUnpauseTick) == FALSE)
		{
			return FALSE;
		}
		tick = sSYNetUnpauseTick;
	}
	ifCommonBattlePauseBeginUnpause();
	sSYNetUnpausePending = FALSE;
	sSYNetPausePending = FALSE;
	sSYNetPauseTick = 0U;
	port_log("SSB64 NetPause: applied unpause tick=%u\n", tick);
	return TRUE;
}

sb32 syNetPauseRequestPauseFromGo(s32 player)
{
	u32 tick;

	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return FALSE;
	}
	if (ifCommonBattlePausePlayerCanRequestPause(player) == FALSE)
	{
		return FALSE;
	}
	tick = syNetInputGetTick();
	if ((sSYNetPausePending != FALSE) && (sSYNetPauseTick == tick) && (sSYNetPausePlayer == player))
	{
		return TRUE;
	}
	if (sSYNetPausePending != FALSE)
	{
		return TRUE;
	}
	syNetPauseArmPause(tick, player);
	syNetPeerSendBattlePausePacket(tick, (u8)player);
	port_log("SSB64 NetPause: request pause tick=%u player=%d\n", tick, (int)player);
	return TRUE;
}

sb32 syNetPauseRequestUnpauseFromPause(void)
{
	u32 tick;

	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusPause)
	{
		return FALSE;
	}
	tick = syNetInputGetTick();
	if ((sSYNetUnpausePending != FALSE) && (sSYNetUnpauseTick == tick))
	{
		return TRUE;
	}
	if (sSYNetUnpausePending != FALSE)
	{
		return TRUE;
	}
	syNetPauseArmUnpause(tick);
	syNetPeerSendBattleUnpausePacket(tick);
	port_log("SSB64 NetPause: request unpause tick=%u\n", tick);
	return TRUE;
}

sb32 syNetPauseTryApplyAtBattleBoundary(u32 tick)
{
	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return FALSE;
	}
	if (syNetPauseTryApplyUnpause(tick) != FALSE)
	{
		return TRUE;
	}
	if (syNetPauseTryApplyPause(tick) != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetPauseShouldDeferBattleSim(u32 tick)
{
	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return FALSE;
	}
	if (syNetPauseShouldHoldSimTick() != FALSE)
	{
		return TRUE;
	}
	if ((sSYNetPausePending != FALSE) && (gSCManagerBattleState->game_status == nSCBattleGameStatusGo) &&
	    (tick >= sSYNetPauseTick))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetPauseShouldHoldSimTick(void)
{
	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return FALSE;
	}
	if (sSYNetPausePending != FALSE)
	{
		return TRUE;
	}
	if (sSYNetUnpausePending != FALSE)
	{
		return TRUE;
	}
	switch (gSCManagerBattleState->game_status)
	{
	case nSCBattleGameStatusPause:
	case nSCBattleGameStatusUnpause:
		return TRUE;

	default:
		return FALSE;
	}
}

sb32 syNetPauseRollbackRequireStrictHash(void)
{
	return syNetPauseShouldHoldSimTick();
}

void syNetPauseOnRemotePausePacket(u32 tick, s32 player)
{
	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if (gSCManagerBattleState->game_status == nSCBattleGameStatusPause)
	{
		return;
	}
	syNetPauseArmPause(tick, player);
	port_log("SSB64 NetPause: remote pause tick=%u player=%d\n", tick, (int)player);
}

void syNetPauseOnRemoteUnpausePacket(u32 tick)
{
	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusPause)
	{
		return;
	}
	syNetPauseArmUnpause(tick);
	port_log("SSB64 NetPause: remote unpause tick=%u\n", tick);
}

void syNetPausePollSyncedInputAtTick(u32 tick)
{
	s32 player;
	SYNetInputFrame cur;
	SYNetInputFrame prev;
	u16 tap;

	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return;
	}
	if (sSYNetPausePending != FALSE)
	{
		return;
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (syNetInputGetHistoryFrame(player, tick, &cur) == FALSE)
		{
			continue;
		}
		memset(&prev, 0, sizeof(prev));
		if ((tick > 0U) && (syNetInputGetHistoryFrame(player, tick - 1U, &prev) == FALSE))
		{
			prev.buttons = 0;
		}
		tap = (u16)((cur.buttons ^ prev.buttons) & cur.buttons);
		if ((tap & START_BUTTON) != 0)
		{
			(void)syNetPauseRequestPauseFromGo(player);
			return;
		}
	}
}
