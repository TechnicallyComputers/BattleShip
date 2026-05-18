#include <sys/netinput_timeline.h>

#ifdef PORT

#include <string.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>

extern void port_log(const char *fmt, ...);

static u32 sSYNetInputEarliestIncorrectSimTick[MAXCONTROLLERS];

static sb32 syNetInputTimelineIsRemoteHumanSlot(s32 player)
{
	s32 i;
	s32 slot;
	s32 n;

	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (slot == player)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetInputTimelineFrameInputEquals(const SYNetInputFrame *a, const SYNetInputFrame *b)
{
	return ((a->buttons == b->buttons) && (a->stick_x == b->stick_x) && (a->stick_y == b->stick_y))
	           ? TRUE
	           : FALSE;
}

void syNetInputTimelineReset(void)
{
	memset(sSYNetInputEarliestIncorrectSimTick, 0, sizeof(sSYNetInputEarliestIncorrectSimTick));
}

void syNetInputTimelineClearIncorrectFrom(u32 from_sim_tick)
{
	s32 player;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if ((sSYNetInputEarliestIncorrectSimTick[player] != 0U) &&
		    (sSYNetInputEarliestIncorrectSimTick[player] >= from_sim_tick))
		{
			sSYNetInputEarliestIncorrectSimTick[player] = 0U;
		}
	}
}

static void syNetInputTimelineNoteIncorrect(s32 player, u32 sim_tick)
{
	u32 *earliest;

	if ((sim_tick == 0U) || (syNetInputTimelineIsRemoteHumanSlot(player) == FALSE))
	{
		return;
	}
	earliest = &sSYNetInputEarliestIncorrectSimTick[player];
	if ((*earliest == 0U) || (sim_tick < *earliest))
	{
		*earliest = sim_tick;
	}
}

u32 syNetInputTimelineGetEarliestIncorrect(void)
{
	u32 best;
	s32 player;

	best = ~(u32)0;
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (sSYNetInputEarliestIncorrectSimTick[player] == 0U)
		{
			continue;
		}
		if (best == ~(u32)0 || sSYNetInputEarliestIncorrectSimTick[player] < best)
		{
			best = sSYNetInputEarliestIncorrectSimTick[player];
		}
	}
	return best;
}

s32 syNetInputTimelineGetEarliestIncorrectPlayer(void)
{
	u32 best_tick;
	s32 player;
	s32 best_player;

	best_tick = syNetInputTimelineGetEarliestIncorrect();
	if (best_tick == ~(u32)0)
	{
		return -1;
	}
	best_player = -1;
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (sSYNetInputEarliestIncorrectSimTick[player] == best_tick)
		{
			return player;
		}
	}
	return best_player;
}

void syNetInputTimelineNotePublishedRemoteMismatch(s32 player, u32 sim_tick)
{
	syNetInputTimelineNoteIncorrect(player, sim_tick);
}

void syNetInputTimelineOnRemoteConfirmedWire(s32 player, u32 wire_tick, const SYNetInputFrame *confirmed)
{
	u32 sim_tick;
	SYNetInputFrame published;

	if ((confirmed == NULL) || (syNetInputTimelineIsRemoteHumanSlot(player) == FALSE))
	{
		return;
	}
	sim_tick = syNetPeerDelaySimTickFromWire(wire_tick);
	if (sim_tick == 0U)
	{
		return;
	}
	if (syNetInputGetHistoryFrame(player, sim_tick, &published) == FALSE)
	{
		return;
	}
	if (syNetInputTimelineFrameInputEquals(&published, confirmed) == FALSE)
	{
		syNetInputTimelineNoteIncorrect(player, sim_tick);
	}
}

#endif /* PORT */
