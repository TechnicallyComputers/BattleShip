#include <sys/netpeer_frame_commit.h>

#include <sys/netinput.h>
#include <sys/netsync.h>

#ifdef PORT
#include <sys/netrollbacksnapshot.h>
#endif

#include <string.h>

static u32 syNetFrameCommitFnvMixU32(u32 h, u32 v)
{
	h ^= v;
	h *= 16777619U;
	return h;
}

u32 syNetFrameCommitHashSlotBindings(s32 local_sim_slot, s32 remote_sim_slot, s32 extra_local_sim_slot,
				     s32 peer_sender_count, const u8 *peer_sender_slots)
{
	u32 h;
	s32 i;
	s32 n;

	h = 2166136261U;
	h = syNetFrameCommitFnvMixU32(h, (u32)local_sim_slot);
	h = syNetFrameCommitFnvMixU32(h, (u32)remote_sim_slot);
	h = syNetFrameCommitFnvMixU32(h, (u32)extra_local_sim_slot);
	h = syNetFrameCommitFnvMixU32(h, (u32)peer_sender_count);
	n = peer_sender_count;
	if (n < 0)
	{
		n = 0;
	}
	if (n > 4)
	{
		n = 4;
	}
	if (peer_sender_slots != NULL)
	{
		for (i = 0; i < n; i++)
		{
			h = syNetFrameCommitFnvMixU32(h, (u32)peer_sender_slots[i]);
		}
	}
	return h;
}

void syNetFrameCommitBuildToken(SYNetFrameCommitToken *out, u32 validation_tick, u32 hist_win_begin, u32 hist_win_len,
				s32 local_sim_slot, s32 remote_sim_slot, s32 extra_local_sim_slot, s32 peer_sender_count,
				const u8 *peer_sender_slots)
{
	u32 sums[MAXCONTROLLERS];
	u32 inp_all;

	memset(out, 0, sizeof(*out));
	out->frame_id = (s32)validation_tick;
	syNetInputGetFrameCommitAuthorityChecksumWindow(hist_win_begin, hist_win_len, sums, &inp_all);
	out->input_digest = inp_all;
	out->slot_binding_hash = syNetFrameCommitHashSlotBindings(local_sim_slot, remote_sim_slot, extra_local_sim_slot,
								  peer_sender_count, peer_sender_slots);
	out->tick_anchor = syNetInputGetTick();
#ifdef PORT
	/*
	 * Snapshot is saved in syNetRollbackAfterBattleUpdate for the tick completed before
	 * syNetInputAdvanceAuthoritativeSimTick; frame-commit runs on the advanced tick.
	 */
	{
		u32 snap_tick;
		u32 snap_f;
		u32 snap_w;
		u32 snap_i;
		u32 snap_r;

		snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
		if (syNetRbSnapshotGetStoredSubsystemHashes(snap_tick, &snap_f, &snap_w, &snap_i, &snap_r) != FALSE)
		{
			out->fighter_digest = snap_f;
			out->world_digest = snap_w;
			out->item_digest = snap_i;
			out->rng_digest = snap_r;
		}
		else
#endif
		{
			out->fighter_digest = syNetSyncHashBattleFightersFull();
			out->world_digest = syNetSyncHashRollbackWorld();
			out->item_digest = syNetSyncHashActiveItemsForRollback();
			out->rng_digest = syNetSyncHashRNGSeed();
		}
#ifdef PORT
	}
#endif
}

sb32 syNetFrameCommitTokensDesync(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b,
				  sb32 *out_delta_frame_id, sb32 *out_delta_input_digest, sb32 *out_delta_slot_binding,
				  sb32 *out_delta_tick_anchor)
{
	sb32 df;
	sb32 di;
	sb32 ds;
	sb32 dt;

	df = (a->frame_id != b->frame_id) ? TRUE : FALSE;
	di = (a->input_digest != b->input_digest) ? TRUE : FALSE;
	/* slot_binding_hash mixes each peer's local_sim/remote_sim; mirrored roles always differ in 1v1. */
	ds = (a->slot_binding_hash != b->slot_binding_hash) ? TRUE : FALSE;
	/* Intentionally ignore tick_anchor for equality: peers may hit NetSync validation one sim tick apart. */
	dt = FALSE;
	if (out_delta_frame_id != NULL)
	{
		*out_delta_frame_id = df;
	}
	if (out_delta_input_digest != NULL)
	{
		*out_delta_input_digest = di;
	}
	if (out_delta_slot_binding != NULL)
	{
		*out_delta_slot_binding = ds;
	}
	if (out_delta_tick_anchor != NULL)
	{
		*out_delta_tick_anchor = dt;
	}
	/* Cross-peer desync: frame_id + input_digest only (state digests use syNetFrameCommitStateDigestsDiverge). */
	return ((df != FALSE) || (di != FALSE)) ? TRUE : FALSE;
}

sb32 syNetFrameCommitStateDigestsDiverge(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b)
{
	if ((a->fighter_digest != b->fighter_digest) || (a->world_digest != b->world_digest) ||
	    (a->item_digest != b->item_digest) || (a->rng_digest != b->rng_digest))
	{
		return TRUE;
	}
	return FALSE;
}
