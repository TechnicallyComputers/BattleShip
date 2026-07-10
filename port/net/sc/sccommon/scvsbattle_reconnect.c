#include "scvsbattle_reconnect.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <if/ifcommon.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <ft/fighter.h>
#include "port_log.h"

void scVSBattleReconnectApplyForfeit(s32 forfeiting_slot, s32 winner_slot)
{
	s32 i;
	GObj *fighter_gobj;
	FTStruct *fp;

	if ((gSCManagerBattleState == NULL) || (forfeiting_slot < 0) || (winner_slot < 0))
	{
		return;
	}
	if ((forfeiting_slot >= ARRAY_COUNT(gSCManagerBattleState->players)) ||
	    (winner_slot >= ARRAY_COUNT(gSCManagerBattleState->players)))
	{
		return;
	}
	port_log("SSB64 Reconnect: forfeit apply forfeiter=%d winner=%d\n", (int)forfeiting_slot, (int)winner_slot);

	gSCManagerBattleState->players[forfeiting_slot].stock_count = -1;
	gSCManagerBattleState->players[forfeiting_slot].place = 2;
	gSCManagerBattleState->players[winner_slot].place = 1;

	fighter_gobj = gSCManagerBattleState->players[forfeiting_slot].fighter_gobj;
	if (fighter_gobj != NULL)
	{
		fp = ftGetStruct(fighter_gobj);
		if (fp != NULL)
		{
			fp->stock_count = -1;
		}
	}

	for (i = 0; i < (s32)ARRAY_COUNT(gSCManagerBattleState->players); i++)
	{
		if ((gSCManagerBattleState->players[i].pkind != nFTPlayerKindNot) && (i != forfeiting_slot) &&
		    (i != winner_slot))
		{
			gSCManagerBattleState->players[i].place = 0;
		}
	}

	ifCommonAnnounceEndMessage();
}

#endif
