#include <sys/netpeer_frame_commit.h>

#include <sys/netinput.h>
#include <sys/netsync.h>

#if defined(PORT)
#include <sys/netrollbacksnapshot.h>
#if defined(SSB64_NETMENU)
extern void syNetRbSnapshotCollectFrameCommitFighterDiagAtTick(u32 tick, SYNetFrameCommitFighterDiag *out_diag);
#endif
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
	 * Snapshot is saved in syNetRollbackAfterBattleUpdate for completed_tick; frame-commit runs
	 * before syNetInputAdvanceAuthoritativeSimTick with validation_tick = completed_tick + 1, so
	 * tick_anchor and snap_tick both name the completed boundary.
	 */
	{
		u32 snap_tick;
		u32 snap_f;
		u32 snap_w;
		u32 snap_i;
		u32 snap_r;
		u32 snap_ef;
		u32 slot_hash[SYNET_FRAME_COMMIT_FIGHTER_SLOTS];
		s32 si;

		snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
		if (syNetRbSnapshotGetStoredSubsystemHashesEx(snap_tick, &snap_f, &snap_w, &snap_i, &snap_r, &snap_ef) !=
		    FALSE)
		{
			out->fighter_digest = snap_f;
			out->world_digest = snap_w;
			out->item_digest = snap_i;
			out->rng_digest = snap_r;
			out->effect_digest = snap_ef;
#if defined(SSB64_NETMENU)
			syNetRbSnapshotCollectFighterSlotHashesAtTick(snap_tick, slot_hash);
			syNetRbSnapshotCollectFrameCommitFighterDiagAtTick(snap_tick, out->fighter_diag);
#else
			syNetSyncCollectFighterSlotHashes(slot_hash);
#endif
			for (si = 0; si < SYNET_FRAME_COMMIT_FIGHTER_SLOTS; si++)
			{
				out->fighter_slot_digest[si] = slot_hash[si];
			}
		}
		else
		{
			out->fighter_digest = syNetSyncHashBattleFightersFull();
			out->world_digest = syNetSyncHashRollbackWorld();
			out->item_digest = syNetSyncHashActiveItemsForRollback();
			out->rng_digest = syNetSyncHashRNGSeed();
			out->effect_digest = syNetSyncHashActiveEffectsForRollback();
			syNetSyncCollectFighterSlotHashes(slot_hash);
			for (si = 0; si < SYNET_FRAME_COMMIT_FIGHTER_SLOTS; si++)
			{
				out->fighter_slot_digest[si] = slot_hash[si];
			}
		}
	}
#else
	{
		out->fighter_digest = syNetSyncHashBattleFightersFull();
		out->world_digest = syNetSyncHashRollbackWorld();
		out->item_digest = syNetSyncHashActiveItemsForRollback();
		out->rng_digest = syNetSyncHashRNGSeed();
		out->effect_digest = syNetSyncHashActiveEffectsForRollback();
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
	    (a->item_digest != b->item_digest) || (a->rng_digest != b->rng_digest) ||
	    (a->effect_digest != b->effect_digest))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetFrameCommitItemOnlyCosmeticDiverge(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b)
{
	if ((a == NULL) || (b == NULL))
	{
		return FALSE;
	}
	if (a->item_digest == b->item_digest)
	{
		return FALSE;
	}
	if ((a->fighter_digest != b->fighter_digest) || (a->world_digest != b->world_digest) ||
	    (a->rng_digest != b->rng_digest) || (a->effect_digest != b->effect_digest))
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetFrameCommitLiveHashGuardTripped(const SYNetFrameCommitToken *local, const SYNetFrameCommitToken *peer,
					  u32 validation_tick, u32 *out_diag_figh, u32 *out_diag_world)
{
	u32 sim_tick;
	u32 snap_tick;
	u32 snap_figh;
	u32 snap_world;
	u32 snap_item;
	u32 snap_rng;

	if ((local == NULL) || (peer == NULL))
	{
		return FALSE;
	}
	if (syNetFrameCommitStateDigestsDiverge(local, peer) != FALSE)
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
#ifdef PORT
	if (syNetRbSnapshotGetStoredSubsystemHashes(snap_tick, &snap_figh, &snap_world, &snap_item, &snap_rng) != FALSE)
	{
		if (out_diag_figh != NULL)
		{
			*out_diag_figh = snap_figh;
		}
		if (out_diag_world != NULL)
		{
			*out_diag_world = snap_world;
		}
		if ((snap_figh != local->fighter_digest) || (snap_world != local->world_digest))
		{
			return TRUE;
		}
		return FALSE;
	}
#endif
	/*
	 * No ring slot: only compare live hashes when sim has run more than one tick past validation.
	 * A single-tick lag at the validation boundary is normal (token uses snap N-1; compare may run after ingress).
	 */
	if (sim_tick <= validation_tick + 1U)
	{
		return FALSE;
	}
	{
		u32 live_figh;
		u32 live_world;

		live_figh = syNetSyncHashBattleFightersFull();
		live_world = syNetSyncHashRollbackWorld();
		if (out_diag_figh != NULL)
		{
			*out_diag_figh = live_figh;
		}
		if (out_diag_world != NULL)
		{
			*out_diag_world = live_world;
		}
		if ((live_figh != local->fighter_digest) || (live_world != local->world_digest))
		{
			return TRUE;
		}
		if ((live_figh != peer->fighter_digest) || (live_world != peer->world_digest))
		{
			return TRUE;
		}
	}
	return FALSE;
}
