#include <sys/netinput_timeline.h>

#ifdef PORT

#include <string.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>

extern void port_log(const char *fmt, ...);

static u32 sSYNetInputEarliestIncorrectSimTick[MAXCONTROLLERS];
static u32 sSYNetInputLastRemoteConfirmedSimTick[MAXCONTROLLERS];

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

static sb32 syNetInputTimelineFrameGameplayEquals(const SYNetInputFrame *a, const SYNetInputFrame *b)
{
	return ((a->buttons == b->buttons) && (a->stick_x == b->stick_x) && (a->stick_y == b->stick_y))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetInputTimelinePublishedMatchesRemoteAtTick(s32 player, u32 sim_tick)
{
	SYNetInputFrame published;
	SYNetInputFrame remote;

	if ((sim_tick == 0U) || (syNetInputGetHistoryFrame(player, sim_tick, &published) == FALSE) ||
	    (syNetInputGetRemoteHistoryFrame(player, sim_tick, &remote) == FALSE))
	{
		return FALSE;
	}
	return syNetInputTimelineFrameGameplayEquals(&published, &remote);
}

/* Advance or clear per-player earliest incorrect by scanning published vs remote up to frontier. */
static void syNetInputTimelineRefreshPlayerEarliest(s32 player, u32 frontier_tick)
{
	u32 earliest;
	u32 t;

	if (syNetInputTimelineIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	earliest = sSYNetInputEarliestIncorrectSimTick[player];
	if (earliest == 0U)
	{
		return;
	}
	if (frontier_tick == 0U || earliest >= frontier_tick)
	{
		sSYNetInputEarliestIncorrectSimTick[player] = 0U;
		return;
	}
	for (t = earliest; t < frontier_tick; t++)
	{
		if (syNetInputTimelinePublishedMatchesRemoteAtTick(player, t) == FALSE)
		{
			sSYNetInputEarliestIncorrectSimTick[player] = t;
			return;
		}
	}
	sSYNetInputEarliestIncorrectSimTick[player] = 0U;
}

void syNetInputTimelineReset(void)
{
	memset(sSYNetInputEarliestIncorrectSimTick, 0, sizeof(sSYNetInputEarliestIncorrectSimTick));
	memset(sSYNetInputLastRemoteConfirmedSimTick, 0, sizeof(sSYNetInputLastRemoteConfirmedSimTick));
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
		if ((sSYNetInputLastRemoteConfirmedSimTick[player] != 0U) &&
		    (sSYNetInputLastRemoteConfirmedSimTick[player] >= from_sim_tick))
		{
			sSYNetInputLastRemoteConfirmedSimTick[player] = 0U;
		}
	}
}

void syNetInputTimelineClearResolvedSpan(u32 from_sim_tick, u32 to_sim_tick)
{
	s32 player;
	u32 t;

	if ((from_sim_tick == 0U) || (to_sim_tick <= from_sim_tick))
	{
		return;
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (syNetInputTimelineIsRemoteHumanSlot(player) == FALSE)
		{
			continue;
		}
		for (t = from_sim_tick; t < to_sim_tick; t++)
		{
			syNetInputTimelineReconcilePublishedVsRemote(player, t);
		}
		syNetInputTimelineRefreshPlayerEarliest(player, to_sim_tick);
	}
}

void syNetInputTimelineReconcilePublishedVsRemote(s32 player, u32 sim_tick)
{
	u32 frontier;

	if (syNetInputTimelineIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if ((sim_tick != 0U) && (syNetInputTimelinePublishedMatchesRemoteAtTick(player, sim_tick) != FALSE) &&
	    (sSYNetInputEarliestIncorrectSimTick[player] == sim_tick))
	{
		sSYNetInputEarliestIncorrectSimTick[player] = 0U;
	}
	syNetInputTimelineRefreshPlayerEarliest(player, frontier);
}

u32 syNetInputTimelineFindEarliestValidatedMismatch(u32 frontier_tick, s32 *out_player)
{
	u32 best;
	s32 best_player;
	s32 player;

	if (out_player != NULL)
	{
		*out_player = -1;
	}
	if (frontier_tick == 0U)
	{
		return ~(u32)0;
	}
	best = ~(u32)0;
	best_player = -1;
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		u32 earliest;

		if (syNetInputTimelineIsRemoteHumanSlot(player) == FALSE)
		{
			continue;
		}
		syNetInputTimelineRefreshPlayerEarliest(player, frontier_tick);
		earliest = sSYNetInputEarliestIncorrectSimTick[player];
		if (earliest == 0U || earliest >= frontier_tick)
		{
			continue;
		}
		if (best == ~(u32)0 || earliest < best)
		{
			best = earliest;
			best_player = player;
		}
	}
	if (best != ~(u32)0 && out_player != NULL)
	{
		*out_player = best_player;
	}
	return best;
}

u32 syNetInputTimelineFindGlobalEarliestIncorrect(u32 frontier_tick, s32 *out_player)
{
	return syNetInputTimelineFindEarliestValidatedMismatch(frontier_tick, out_player);
}

u32 syNetInputTimelineGetEarliestIncorrectForPlayer(s32 player)
{
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return 0U;
	}
	return sSYNetInputEarliestIncorrectSimTick[player];
}

u32 syNetInputTimelineGetLastRemoteConfirmedSimTick(s32 player)
{
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return 0U;
	}
	return sSYNetInputLastRemoteConfirmedSimTick[player];
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

	best_tick = syNetInputTimelineGetEarliestIncorrect();
	if (best_tick == ~(u32)0)
	{
		return -1;
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (sSYNetInputEarliestIncorrectSimTick[player] == best_tick)
		{
			return player;
		}
	}
	return -1;
}

void syNetInputTimelineNotePublishedRemoteMismatch(s32 player, u32 sim_tick)
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
	if ((sSYNetInputLastRemoteConfirmedSimTick[player] == 0U) ||
	    (sim_tick > sSYNetInputLastRemoteConfirmedSimTick[player]))
	{
		sSYNetInputLastRemoteConfirmedSimTick[player] = sim_tick;
	}
	if (syNetInputGetHistoryFrame(player, sim_tick, &published) == FALSE)
	{
		return;
	}
	if (syNetInputTimelineFrameGameplayEquals(&published, confirmed) == FALSE)
	{
		syNetInputTimelineNotePublishedRemoteMismatch(player, sim_tick);
	}
	else
	{
		syNetInputTimelineReconcilePublishedVsRemote(player, sim_tick);
	}
}

#endif /* PORT */
