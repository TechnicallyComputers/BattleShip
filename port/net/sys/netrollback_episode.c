#include <sys/netrollback_episode.h>

#include <sys/netinput.h>
#include <sys/netpeer.h>

#ifdef PORT
#include <PR/os.h>
#include <stdlib.h>
#include <string.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

typedef struct SYNetRollbackEpisodeFsmState
{
	SYNetRollbackEpisodeFsmPhase phase;
	SYNetRollbackEpisodeRole role;
	u32 epoch_id;
	u32 mismatch_tick;
	u32 load_tick;
	u32 target_tick;
	s32 corrected_slot;
	sb32 inputs_sealed;
	sb32 frozen_post_input_digest_valid;
	u32 frozen_post_input_digest;
	u32 peer_convergence_target;
	sb32 peer_convergence_active;
	u32 replay_log_count;
	SYNetRollbackEpisodeReplayLogEntry replay_log[SYNETROLLBACK_EPISODE_REPLAY_LOG_MAX];
	SYNetInputFrame sealed[SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN][MAXCONTROLLERS];
	ub8 sealed_valid[SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN][MAXCONTROLLERS];
	u64 peer_seal_tick_mask_lo[MAXCONTROLLERS];
	u64 peer_seal_tick_mask_hi[MAXCONTROLLERS];
	u8 seal_send_row_begin[MAXCONTROLLERS];
	u32 event_head;
	u32 event_tail;
	SYNetRollbackEpisodeEvent event_queue[SYNETROLLBACK_EPISODE_EVENT_QUEUE_MAX];
} SYNetRollbackEpisodeFsmState;

#define nSYNetRollbackEpisodeSealSendDone 255U
#define nSYNetRollbackEpisodePendingSealMax 4U

typedef struct SYNetRollbackEpisodePendingSealChunk
{
	sb32 used;
	u32 epoch_id;
	u32 mismatch_tick;
	u32 target_tick;
	s32 slot;
	u32 row_begin;
	u32 row_count;
	SYNetInputFrame rows[SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX];
} SYNetRollbackEpisodePendingSealChunk;

static SYNetRollbackEpisodeFsmState sSYNetRollbackEpisodeFsm;
static SYNetRollbackEpisodePendingSealChunk sSYNetRollbackEpisodePendingSeal[nSYNetRollbackEpisodePendingSealMax];
static int sSYNetRollbackEpisodeFsmEnvCache = -999;
/* Any peer seal chunk received (applied, compatible or stashed) since FsmBegin — peer is alive and sealing. */
static sb32 sSYNetRollbackEpisodePeerSealChunkSeen;

#ifdef PORT
static u32 syNetRollbackEpisodeFsmSealIndex(u32 tick)
{
	if ((sSYNetRollbackEpisodeFsm.mismatch_tick == 0U) || (tick < sSYNetRollbackEpisodeFsm.mismatch_tick) ||
	    (tick >= sSYNetRollbackEpisodeFsm.target_tick))
	{
		return ~(u32)0;
	}
	return tick - sSYNetRollbackEpisodeFsm.mismatch_tick;
}

static sb32 syNetRollbackEpisodePlayerContributesToSpanDigest(s32 player)
{
	if ((player < 0) || (player >= MAXCONTROLLERS) || (syNetRollbackEpisodeInputsSealed() == FALSE))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeSlotIsLocalAuthority(player) != FALSE)
	{
		return TRUE;
	}
	return syNetRollbackEpisodeSlotRequiresPeerSealRows(player);
}

static void syNetRollbackEpisodeNormalizeSealedFrameTick(SYNetInputFrame *frame, u32 sim_tick)
{
	if (frame == NULL)
	{
		return;
	}
	frame->tick = sim_tick;
	frame->is_valid = TRUE;
}

static u32 syNetRollbackEpisodeComputeTickInputDigest(u32 tick)
{
	u32 checksum;
	s32 player;
	SYNetInputFrame frame;

	checksum = 2166136261U;
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		u32 player_checksum = 2166136261U;

		if (syNetRollbackEpisodePlayerContributesToSpanDigest(player) == FALSE)
		{
			continue;
		}
		if (syNetRollbackEpisodeGetSealedFrame(player, tick, &frame) != FALSE)
		{
			player_checksum = syNetInputAccumulateInputChecksum(player_checksum, player, &frame);
		}
		checksum ^= player_checksum;
		checksum *= 16777619U;
	}
	return checksum;
}

u32 syNetRollbackEpisodeComputeSpanInputDigest(u32 from_tick, u32 to_tick)
{
	u32 checksum;
	u32 t;

	checksum = 2166136261U;
	for (t = from_tick; t < to_tick; t++)
	{
		checksum ^= syNetRollbackEpisodeComputeTickInputDigest(t);
		checksum *= 16777619U;
	}
	return checksum;
}

static u32 syNetRollbackEpisodeSealSpan(void)
{
	if (sSYNetRollbackEpisodeFsm.target_tick <= sSYNetRollbackEpisodeFsm.mismatch_tick)
	{
		return 0U;
	}
	return sSYNetRollbackEpisodeFsm.target_tick - sSYNetRollbackEpisodeFsm.mismatch_tick;
}

static sb32 syNetRollbackEpisodeSlotInList(s32 player, const s32 *slots, s32 count)
{
	s32 i;

	for (i = 0; i < count; i++)
	{
		if (slots[i] == player)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRollbackEpisodeMarkPeerSealTick(s32 player, u32 row_offset)
{
	u64 bit;

	if ((player < 0) || (player >= MAXCONTROLLERS) || (row_offset >= SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN))
	{
		return;
	}
	if (row_offset < 64U)
	{
		bit = (u64)1 << row_offset;
		sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo[player] |= bit;
	}
	else
	{
		bit = (u64)1 << (row_offset - 64U);
		sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi[player] |= bit;
	}
}

static sb32 syNetRollbackEpisodePeerSealTickMaskComplete(s32 player, u32 span)
{
	u64 need_lo;
	u64 need_hi;
	u32 lo_span;
	u32 hi_span;
	u32 i;

	if ((player < 0) || (player >= MAXCONTROLLERS) || (span == 0U) ||
	    (span > SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN))
	{
		return FALSE;
	}
	for (i = 0; i < span; i++)
	{
		if (sSYNetRollbackEpisodeFsm.sealed_valid[i][player] == FALSE)
		{
			return FALSE;
		}
	}
	lo_span = (span < 64U) ? span : 64U;
	hi_span = (span > 64U) ? (span - 64U) : 0U;
	need_lo = (lo_span >= 64U) ? ~(u64)0 : (((u64)1 << lo_span) - 1U);
	need_hi = (hi_span == 0U) ? 0U : ((hi_span >= 64U) ? ~(u64)0 : (((u64)1 << hi_span) - 1U));
	if ((sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo[player] & need_lo) != need_lo)
	{
		return FALSE;
	}
	if (hi_span == 0U)
	{
		return TRUE;
	}
	return ((sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi[player] & need_hi) == need_hi) ? TRUE : FALSE;
}

static void syNetRollbackEpisodeClearPendingPeerSealRows(void)
{
	memset(sSYNetRollbackEpisodePendingSeal, 0, sizeof(sSYNetRollbackEpisodePendingSeal));
}

/* Drop only stashes that will not apply to the episode about to begin (keep early pre-Begin rows). */
static void syNetRollbackEpisodeClearPendingPeerSealRowsExcept(u32 epoch_id, u32 mismatch_tick, u32 target_tick)
{
	u32 i;

	for (i = 0; i < nSYNetRollbackEpisodePendingSealMax; i++)
	{
		SYNetRollbackEpisodePendingSealChunk *c;

		c = &sSYNetRollbackEpisodePendingSeal[i];
		if (c->used == FALSE)
		{
			continue;
		}
		if ((c->epoch_id == epoch_id) && (c->mismatch_tick == mismatch_tick) && (c->target_tick == target_tick))
		{
			continue;
		}
		c->used = FALSE;
	}
}

static sb32 syNetRollbackEpisodeEpisodeTupleMatches(u32 epoch_id, u32 mismatch_tick, u32 target_tick)
{
	return ((epoch_id == sSYNetRollbackEpisodeFsm.epoch_id) &&
		(mismatch_tick == sSYNetRollbackEpisodeFsm.mismatch_tick) &&
		(target_tick == sSYNetRollbackEpisodeFsm.target_tick))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackEpisodeTryShrinkTargetToPeerPrefix(u32 peer_target);

/*
 * Same epoch, overlapping episode tuple fork. Map peer rows by absolute sim tick into the local
 * seal table; ticks outside local [mismatch, target) are ignored.
 * - Mismatch fork: same target, different mismatch (stacked GGPO / FC deepen).
 * - Target fork: same mismatch, different target (tuple_align race: e.g. 4417 vs 4418 @ soak1).
 * See docs/bugs/netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md,
 * docs/bugs/netplay_seal_tuple_target_fork_stall_2026-07-18.md.
 */
static sb32 syNetRollbackEpisodeApplyCompatiblePeerSealRowsChunk(u32 epoch_id, u32 pkt_mismatch, u32 pkt_target,
								 s32 slot, u32 row_begin,
								 const SYNetInputFrame *rows, u32 row_count)
{
	u32 local_mismatch;
	u32 local_target;
	u32 local_span;
	sb32 same_target;
	sb32 same_mismatch;
	const char *fork_kind;
	u32 i;
	u32 applied;

	if ((rows == NULL) || (slot < 0) || (slot >= MAXCONTROLLERS) || (row_count == 0U))
	{
		return FALSE;
	}
	if (epoch_id != sSYNetRollbackEpisodeFsm.epoch_id)
	{
		return FALSE;
	}
	local_mismatch = sSYNetRollbackEpisodeFsm.mismatch_tick;
	local_target = sSYNetRollbackEpisodeFsm.target_tick;
	same_target = (pkt_target == local_target) ? TRUE : FALSE;
	same_mismatch = (pkt_mismatch == local_mismatch) ? TRUE : FALSE;
	if ((same_target == same_mismatch) || ((same_target == FALSE) && (same_mismatch == FALSE)))
	{
		return FALSE;
	}
	fork_kind = (same_mismatch != FALSE) ? "target" : "mismatch";
	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		return FALSE;
	}
	local_span = syNetRollbackEpisodeSealSpan();
	if ((local_span == 0U) || (pkt_mismatch >= pkt_target))
	{
		return FALSE;
	}
	applied = 0U;
	for (i = 0U; i < row_count; i++)
	{
		u32 t;
		u32 local_idx;

		t = pkt_mismatch + row_begin + i;
		if ((t < local_mismatch) || (t >= local_target))
		{
			continue;
		}
		if ((same_mismatch != FALSE) && (t >= pkt_target))
		{
			continue;
		}
		local_idx = t - local_mismatch;
		if (local_idx >= SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
		{
			continue;
		}
		sSYNetRollbackEpisodeFsm.sealed[local_idx][slot] = rows[i];
		syNetRollbackEpisodeNormalizeSealedFrameTick(&sSYNetRollbackEpisodeFsm.sealed[local_idx][slot], t);
		sSYNetRollbackEpisodeFsm.sealed_valid[local_idx][slot] = TRUE;
		syNetRollbackEpisodeMarkPeerSealTick(slot, local_idx);
		applied++;
	}
	if (applied == 0U)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: EPISODE_SEAL_ROWS_COMPATIBLE_APPLY fork=%s epoch=%u pkt_mismatch=%u local_mismatch=%u pkt_target=%u local_target=%u slot=%d begin=%u count=%u applied=%u slot_span_digest=0x%08X\n",
	    fork_kind,
	    epoch_id,
	    pkt_mismatch,
	    local_mismatch,
	    pkt_target,
	    local_target,
	    (int)slot,
	    row_begin,
	    row_count,
	    applied,
	    syNetRollbackEpisodeComputeSlotSpanInputDigest(slot, local_mismatch, local_target));
	/*
	 * Target-fork defense: if peer sealed a complete shorter prefix and we still wait on
	 * the extra local ticks, shrink to the peer's completed target so seal-wait unblocks
	 * (tuple_align max-target is the primary fix; this covers late/stale shorter seals).
	 */
	if ((same_mismatch != FALSE) && (pkt_target < local_target))
	{
		(void)syNetRollbackEpisodeTryShrinkTargetToPeerPrefix(pkt_target);
	}
	return TRUE;
}

/*
 * Shrink local episode target to peer_target when every required peer slot has a complete
 * seal mask for [mismatch, peer_target). Drops local seal rows beyond the new span.
 */
static sb32 syNetRollbackEpisodeTryShrinkTargetToPeerPrefix(u32 peer_target)
{
	u32 local_mismatch;
	u32 local_target;
	u32 peer_span;
	u32 old_span;
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 j;
	u32 p;

	if ((syNetRollbackEpisodeInputsSealed() == FALSE) || (peer_target == 0U))
	{
		return FALSE;
	}
	local_mismatch = sSYNetRollbackEpisodeFsm.mismatch_tick;
	local_target = sSYNetRollbackEpisodeFsm.target_tick;
	if ((peer_target <= local_mismatch) || (peer_target >= local_target))
	{
		return FALSE;
	}
	peer_span = peer_target - local_mismatch;
	old_span = local_target - local_mismatch;
	if ((peer_span == 0U) || (peer_span > SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return FALSE;
	}
	for (i = 0; i < count; i++)
	{
		if (syNetRollbackEpisodePeerSealTickMaskComplete(slots[i], peer_span) == FALSE)
		{
			return FALSE;
		}
	}
	sSYNetRollbackEpisodeFsm.target_tick = peer_target;
	for (j = peer_span; j < old_span && j < SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN; j++)
	{
		for (p = 0U; p < (u32)MAXCONTROLLERS; p++)
		{
			sSYNetRollbackEpisodeFsm.sealed_valid[j][p] = FALSE;
			memset(&sSYNetRollbackEpisodeFsm.sealed[j][p], 0, sizeof(sSYNetRollbackEpisodeFsm.sealed[j][p]));
		}
	}
	/* Retransmit outbound seals under the shorter tuple if any local cursor was past it. */
	for (p = 0U; p < (u32)MAXCONTROLLERS; p++)
	{
		if ((sSYNetRollbackEpisodeFsm.seal_send_row_begin[p] != nSYNetRollbackEpisodeSealSendDone) &&
		    (sSYNetRollbackEpisodeFsm.seal_send_row_begin[p] > peer_span))
		{
			sSYNetRollbackEpisodeFsm.seal_send_row_begin[p] = (u8)peer_span;
		}
	}
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest = 0U;
	port_log(
	    "SSB64 NetRollback: EPISODE_SEAL_ROWS_SHRINK_TO_PEER_PREFIX mismatch=%u target=%u->%u peer_span=%u (was %u)\n",
	    local_mismatch,
	    local_target,
	    peer_target,
	    peer_span,
	    old_span);
	return TRUE;
}

static sb32 syNetRollbackEpisodeStashPendingPeerSealRowsChunk(u32 epoch_id, u32 mismatch_tick, u32 target_tick,
							      s32 slot, u32 row_begin,
							      const SYNetInputFrame *rows, u32 row_count)
{
	SYNetRollbackEpisodePendingSealChunk *free_chunk;
	u32 i;

	if ((rows == NULL) || (row_count == 0U) || (row_count > SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX) ||
	    (slot < 0) || (slot >= MAXCONTROLLERS))
	{
		return FALSE;
	}
	free_chunk = NULL;
	for (i = 0; i < nSYNetRollbackEpisodePendingSealMax; i++)
	{
		SYNetRollbackEpisodePendingSealChunk *c;

		c = &sSYNetRollbackEpisodePendingSeal[i];
		if (c->used == FALSE)
		{
			if (free_chunk == NULL)
			{
				free_chunk = c;
			}
			continue;
		}
		if ((c->epoch_id == epoch_id) && (c->mismatch_tick == mismatch_tick) &&
		    (c->target_tick == target_tick) && (c->slot == slot) && (c->row_begin == row_begin))
		{
			free_chunk = c;
			break;
		}
	}
	if (free_chunk == NULL)
	{
		return FALSE;
	}
	free_chunk->used = TRUE;
	free_chunk->epoch_id = epoch_id;
	free_chunk->mismatch_tick = mismatch_tick;
	free_chunk->target_tick = target_tick;
	free_chunk->slot = slot;
	free_chunk->row_begin = row_begin;
	free_chunk->row_count = row_count;
	memcpy(free_chunk->rows, rows, row_count * sizeof(SYNetInputFrame));
	return TRUE;
}

static void syNetRollbackEpisodeFlushPendingPeerSealRows(void)
{
	u32 i;

	for (i = 0; i < nSYNetRollbackEpisodePendingSealMax; i++)
	{
		SYNetRollbackEpisodePendingSealChunk *c;

		c = &sSYNetRollbackEpisodePendingSeal[i];
		if (c->used == FALSE)
		{
			continue;
		}
		if (syNetRollbackEpisodeApplyPeerSealRowsChunk(c->epoch_id, c->mismatch_tick, c->target_tick, c->slot,
							       c->row_begin, c->rows, c->row_count) != FALSE)
		{
			c->used = FALSE;
		}
	}
}

void syNetRollbackEpisodePumpPendingPeerSealRows(void)
{
#ifdef PORT
	syNetRollbackEpisodeFlushPendingPeerSealRows();
#endif
}
#endif /* PORT — static seal helpers */

sb32 syNetRollbackEpisodeFsmEnabled(void)
{
#ifdef PORT
	const char *env;

	if (sSYNetRollbackEpisodeFsmEnvCache == -999)
	{
		env = getenv("SSB64_NETPLAY_ROLLBACK_EPISODE_FSM");
		if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
		{
			sSYNetRollbackEpisodeFsmEnvCache = 0;
		}
		else
		{
			sSYNetRollbackEpisodeFsmEnvCache = 1;
		}
	}
	return (sSYNetRollbackEpisodeFsmEnvCache != 0) ? TRUE : FALSE;
#else
	return FALSE;
#endif
}

void syNetRollbackEpisodeFsmSessionReset(void)
{
	memset(&sSYNetRollbackEpisodeFsm, 0, sizeof(sSYNetRollbackEpisodeFsm));
	sSYNetRollbackEpisodeFsm.phase = nSYNetRollbackEpisodeFsmPhaseLive;
	memset(sSYNetRollbackEpisodeFsm.seal_send_row_begin, nSYNetRollbackEpisodeSealSendDone,
	       sizeof(sSYNetRollbackEpisodeFsm.seal_send_row_begin));
	sSYNetRollbackEpisodePeerSealChunkSeen = FALSE;
	syNetRollbackEpisodeClearPendingPeerSealRows();
}

sb32 syNetRollbackEpisodePeerSealActivitySeen(void)
{
	return (sSYNetRollbackEpisodePeerSealChunkSeen != FALSE) ? TRUE : FALSE;
}

SYNetRollbackEpisodeFsmPhase syNetRollbackEpisodeFsmGetPhase(void)
{
	return sSYNetRollbackEpisodeFsm.phase;
}

sb32 syNetRollbackEpisodeFsmIsActive(void)
{
	return (sSYNetRollbackEpisodeFsm.phase != nSYNetRollbackEpisodeFsmPhaseLive) ? TRUE : FALSE;
}

sb32 syNetRollbackEpisodeFsmIsResimulating(void)
{
	switch (sSYNetRollbackEpisodeFsm.phase)
	{
	case nSYNetRollbackEpisodeFsmPhaseReplay:
	case nSYNetRollbackEpisodeFsmPhaseVerify:
	case nSYNetRollbackEpisodeFsmPhaseCommit:
		return TRUE;
	default:
		return FALSE;
	}
}

u32 syNetRollbackEpisodeFsmGetEpochId(void)
{
	return sSYNetRollbackEpisodeFsm.epoch_id;
}

u32 syNetRollbackEpisodeFsmGetMismatchTick(void)
{
	return sSYNetRollbackEpisodeFsm.mismatch_tick;
}

u32 syNetRollbackEpisodeFsmGetLoadTick(void)
{
	return sSYNetRollbackEpisodeFsm.load_tick;
}

u32 syNetRollbackEpisodeFsmGetTargetTick(void)
{
	return sSYNetRollbackEpisodeFsm.target_tick;
}

s32 syNetRollbackEpisodeFsmGetCorrectedSlot(void)
{
	return sSYNetRollbackEpisodeFsm.corrected_slot;
}

sb32 syNetRollbackEpisodeFsmIsFromPeerNotify(void)
{
	return (sSYNetRollbackEpisodeFsm.role == nSYNetRollbackEpisodeRoleFollower) ? TRUE : FALSE;
}

void syNetRollbackEpisodeFsmBegin(u32 epoch_id, u32 mismatch_tick, u32 load_tick, u32 target_tick, s32 corrected_slot,
				  sb32 initiator, sb32 from_peer_notify)
{
#ifdef PORT
	u32 span;

	memset(sSYNetRollbackEpisodeFsm.sealed, 0, sizeof(sSYNetRollbackEpisodeFsm.sealed));
	memset(sSYNetRollbackEpisodeFsm.sealed_valid, 0, sizeof(sSYNetRollbackEpisodeFsm.sealed_valid));
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo));
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi));
	memset(sSYNetRollbackEpisodeFsm.seal_send_row_begin, nSYNetRollbackEpisodeSealSendDone,
	       sizeof(sSYNetRollbackEpisodeFsm.seal_send_row_begin));
	sSYNetRollbackEpisodeFsm.replay_log_count = 0U;
	sSYNetRollbackEpisodeFsm.inputs_sealed = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest = 0U;
	sSYNetRollbackEpisodeFsm.peer_convergence_target = 0U;
	sSYNetRollbackEpisodeFsm.peer_convergence_active = FALSE;
	sSYNetRollbackEpisodePeerSealChunkSeen = FALSE;
	sSYNetRollbackEpisodeFsm.epoch_id = epoch_id;
	sSYNetRollbackEpisodeFsm.mismatch_tick = mismatch_tick;
	sSYNetRollbackEpisodeFsm.load_tick = load_tick;
	sSYNetRollbackEpisodeFsm.target_tick = target_tick;
	sSYNetRollbackEpisodeFsm.corrected_slot = corrected_slot;
	sSYNetRollbackEpisodeFsm.role =
	    (from_peer_notify != FALSE) ? nSYNetRollbackEpisodeRoleFollower : nSYNetRollbackEpisodeRoleInitiator;
	(void)initiator;
	span = (target_tick > mismatch_tick) ? (target_tick - mismatch_tick) : 0U;
	if (span > SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM span=%u exceeds seal max %u — clamping target\n",
		    (unsigned int)span,
		    (unsigned int)SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN);
		sSYNetRollbackEpisodeFsm.target_tick = mismatch_tick + SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN;
	}
	syNetRollbackEpisodeClearPendingPeerSealRowsExcept(epoch_id, mismatch_tick,
							   sSYNetRollbackEpisodeFsm.target_tick);
	sSYNetRollbackEpisodeFsm.phase = nSYNetRollbackEpisodeFsmPhaseSealInputs;
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM begin epoch=%u mismatch=%u load=%u target=%u role=%s corrected=%d\n",
	    epoch_id,
	    mismatch_tick,
	    load_tick,
	    sSYNetRollbackEpisodeFsm.target_tick,
	    (from_peer_notify != FALSE) ? "follower" : "initiator",
	    (int)corrected_slot);
#endif
}

static const char *syNetRollbackEpisodeFsmPhaseName(SYNetRollbackEpisodeFsmPhase phase)
{
	switch (phase)
	{
	case nSYNetRollbackEpisodeFsmPhaseLive:
		return "Live";
	case nSYNetRollbackEpisodeFsmPhaseSealInputs:
		return "SealInputs";
	case nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline:
		return "AwaitingBaseline";
	case nSYNetRollbackEpisodeFsmPhaseReplay:
		return "Replay";
	case nSYNetRollbackEpisodeFsmPhaseVerify:
		return "Verify";
	case nSYNetRollbackEpisodeFsmPhaseCommit:
		return "Commit";
	case nSYNetRollbackEpisodeFsmPhaseAbort:
		return "Abort";
	default:
		return "Unknown";
	}
}

void syNetRollbackEpisodeFsmSetPhase(SYNetRollbackEpisodeFsmPhase phase)
{
#ifdef PORT
	const char *from_name;
	const char *to_name;

	if (sSYNetRollbackEpisodeFsm.phase == phase)
	{
		return;
	}
	from_name = syNetRollbackEpisodeFsmPhaseName(sSYNetRollbackEpisodeFsm.phase);
	to_name = syNetRollbackEpisodeFsmPhaseName(phase);
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM phase %s -> %s epoch=%u mismatch=%u target=%u\n",
	    from_name,
	    to_name,
	    sSYNetRollbackEpisodeFsm.epoch_id,
	    sSYNetRollbackEpisodeFsm.mismatch_tick,
	    sSYNetRollbackEpisodeFsm.target_tick);
	sSYNetRollbackEpisodeFsm.phase = phase;
#endif
}

void syNetRollbackEpisodeFsmSyncToLegacy(sb32 *out_resim_pending, sb32 *out_awaiting_baseline, sb32 *out_baseline_gate_open,
					 u32 *out_mismatch, u32 *out_load, u32 *out_target, s32 *out_corrected,
					 sb32 *out_from_peer_symmetric)
{
	if (out_resim_pending != NULL)
	{
		*out_resim_pending = (sSYNetRollbackEpisodeFsm.phase != nSYNetRollbackEpisodeFsmPhaseLive) ? TRUE : FALSE;
	}
	if (out_awaiting_baseline != NULL)
	{
		*out_awaiting_baseline =
		    (sSYNetRollbackEpisodeFsm.phase == nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline) ? TRUE : FALSE;
	}
	if (out_baseline_gate_open != NULL)
	{
		*out_baseline_gate_open = (sSYNetRollbackEpisodeFsm.phase == nSYNetRollbackEpisodeFsmPhaseReplay ||
					   sSYNetRollbackEpisodeFsm.phase == nSYNetRollbackEpisodeFsmPhaseVerify ||
					   sSYNetRollbackEpisodeFsm.phase == nSYNetRollbackEpisodeFsmPhaseCommit)
					      ? TRUE
					      : FALSE;
	}
	if (out_mismatch != NULL)
	{
		*out_mismatch = sSYNetRollbackEpisodeFsm.mismatch_tick;
	}
	if (out_load != NULL)
	{
		*out_load = sSYNetRollbackEpisodeFsm.load_tick;
	}
	if (out_target != NULL)
	{
		*out_target = sSYNetRollbackEpisodeFsm.target_tick;
	}
	if (out_corrected != NULL)
	{
		*out_corrected = sSYNetRollbackEpisodeFsm.corrected_slot;
	}
	if (out_from_peer_symmetric != NULL)
	{
		*out_from_peer_symmetric = syNetRollbackEpisodeFsmIsFromPeerNotify();
	}
}

void syNetRollbackEpisodeSealInputs(u32 mismatch_tick, u32 target_tick, s32 correction_player)
{
#ifdef PORT
	u32 t;
	s32 player;
	u32 span;
	u32 idx;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
	span = (target_tick > mismatch_tick) ? (target_tick - mismatch_tick) : 0U;
	if (span > SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM seal span=%u exceeds max — skip seal\n",
		    (unsigned int)span);
		return;
	}
	syNetInputRollbackReconcileResimSpan(mismatch_tick, target_tick, correction_player);
	{
		u32 promote_t;

		for (promote_t = mismatch_tick; promote_t < target_tick; promote_t++)
		{
			syNetInputPromoteAllRemoteHumanAuthoritySlots(promote_t);
		}
	}
	if (syNetRollbackEpisodeFsmIsFromPeerNotify() != FALSE)
	{
		s32 authority_slot;

		authority_slot = syNetPeerGetLocalSimSlot();
		if (authority_slot < 0)
		{
			authority_slot = correction_player;
		}
		syNetInputRollbackReconcilePeerSymmetricAuthority(authority_slot, mismatch_tick, target_tick);
	}
	memset(sSYNetRollbackEpisodeFsm.sealed_valid, 0, sizeof(sSYNetRollbackEpisodeFsm.sealed_valid));
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo));
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi));
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest = 0U;
	for (t = mismatch_tick; t < target_tick; t++)
	{
		idx = t - mismatch_tick;
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			SYNetInputFrame frame;

			if (syNetRollbackEpisodeSealRowsExchangeEnabled() != FALSE &&
			    syNetRollbackEpisodeSlotRequiresPeerSealRows(player) != FALSE)
			{
				continue;
			}
			if ((syNetRollbackEpisodeSlotIsLocalAuthority(player) != FALSE) &&
			    (syNetInputCopyEpisodeLocalAuthoritySealFrame(player, t, &frame) != FALSE))
			{
				sSYNetRollbackEpisodeFsm.sealed[idx][player] = frame;
				sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] = TRUE;
				continue;
			}
			if ((syNetInputIsRemoteHumanSlot(player) != FALSE) &&
			    (syNetInputCopyEpisodeRemoteAuthoritySealFrame(player, t, &frame) != FALSE))
			{
				syNetRollbackEpisodeNormalizeSealedFrameTick(&frame, t);
				sSYNetRollbackEpisodeFsm.sealed[idx][player] = frame;
				sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] = TRUE;
				continue;
			}
			if (syNetInputGetHistoryFrame(player, t, &frame) != FALSE)
			{
				if ((frame.is_predicted != FALSE) || (frame.source == nSYNetInputSourceRemotePredicted))
				{
					continue;
				}
				syNetRollbackEpisodeNormalizeSealedFrameTick(&frame, t);
				sSYNetRollbackEpisodeFsm.sealed[idx][player] = frame;
				sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] = TRUE;
			}
		}
	}
	sSYNetRollbackEpisodeFsm.inputs_sealed = TRUE;
	syNetRollbackEpisodeFlushPendingPeerSealRows();
	sSYNetRollbackEpisodeFsm.phase = nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline;
	syNetRollbackEpisodeArmSealRowsSend();
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM seal_inputs mismatch=%u target=%u span=%u correction_player=%d\n",
	    mismatch_tick,
	    target_tick,
	    (unsigned int)span,
	    (int)correction_player);
#endif
}

/*
 * Deeper restart widens the seal span backwards (mismatch moves to deeper_load+1) but the peer
 * stays on the original tuple and will never send seal rows for the prefix ticks — its span
 * starts at the original mismatch (soak1 @861: Android reseal 862→861 span=3, Linux tuple 862
 * span=2 → slot0 row 861 missing forever → SEAL_ROWS_TIMEOUT → unilateral self-seal replay).
 * The deepen precondition is "inputs agree through load", so the prefix rows are already
 * cross-peer wire-confirmed: fill them locally at reseal time so the exchange only waits on
 * rows the peer will actually send. Rows that fail strict-confirmed resolve stay missing and
 * the timeout path fails closed.
 * See docs/bugs/netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md.
 */
#ifdef PORT
static void syNetRollbackEpisodeSelfSealPeerPrefixRows(u32 from_tick, u32 to_tick)
{
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 t;

	if ((from_tick >= to_tick) || (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE))
	{
		return;
	}
	if (syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return;
	}
	for (i = 0; i < count; i++)
	{
		s32 player;
		u32 filled;

		player = slots[i];
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		filled = 0U;
		for (t = from_tick; t < to_tick; t++)
		{
			SYNetInputFrame frame;
			u32 idx;

			idx = t - sSYNetRollbackEpisodeFsm.mismatch_tick;
			if (idx >= SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
			{
				continue;
			}
			if (sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] != FALSE)
			{
				syNetRollbackEpisodeMarkPeerSealTick(player, idx);
				continue;
			}
			if (syNetInputGetRemoteHistoryFrame(player, t, &frame) == FALSE)
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_SEAL_ROWS_PREFIX_SKIP slot=%d tick=%u reason=not_wire_confirmed\n",
				    (int)player,
				    t);
				continue;
			}
			syNetRollbackEpisodeNormalizeSealedFrameTick(&frame, t);
			frame.source = nSYNetInputSourceRemoteConfirmed;
			frame.is_predicted = FALSE;
			frame.is_valid = TRUE;
			sSYNetRollbackEpisodeFsm.sealed[idx][player] = frame;
			sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] = TRUE;
			syNetRollbackEpisodeMarkPeerSealTick(player, idx);
			filled++;
		}
		if (filled != 0U)
		{
			port_log(
			    "SSB64 NetRollback: EPISODE_SEAL_ROWS_PREFIX_FILL slot=%d filled=%u from=%u to=%u (deeper reseal; inputs agree through load)\n",
			    (int)player,
			    filled,
			    from_tick,
			    to_tick);
		}
	}
}
#endif

void syNetRollbackEpisodeResealForDeeperLoad(u32 load_tick, u32 mismatch_tick, u32 target_tick, s32 correction_player)
{
#ifdef PORT
	u32 span;
	u32 prior_mismatch;
	SYNetRollbackEpisodeFsmPhase phase;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
	prior_mismatch = sSYNetRollbackEpisodeFsm.mismatch_tick;
	phase = sSYNetRollbackEpisodeFsm.phase;
	if ((phase == nSYNetRollbackEpisodeFsmPhaseReplay) || (phase == nSYNetRollbackEpisodeFsmPhaseVerify) ||
	    (phase == nSYNetRollbackEpisodeFsmPhaseCommit))
	{
		sSYNetRollbackEpisodeFsm.phase = nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline;
	}
	sSYNetRollbackEpisodeFsm.load_tick = load_tick;
	sSYNetRollbackEpisodeFsm.mismatch_tick = mismatch_tick;
	sSYNetRollbackEpisodeFsm.target_tick = target_tick;
	span = (target_tick > mismatch_tick) ? (target_tick - mismatch_tick) : 0U;
	if (span > SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM reseal_deeper span=%u exceeds max %u — clamping target\n",
		    (unsigned int)span,
		    (unsigned int)SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN);
		sSYNetRollbackEpisodeFsm.target_tick = mismatch_tick + SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN;
	}
	sSYNetRollbackEpisodeFsm.replay_log_count = 0U;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest = 0U;
	syNetRollbackEpisodeClearPendingPeerSealRows();
	syNetRollbackEpisodeSealInputs(mismatch_tick, sSYNetRollbackEpisodeFsm.target_tick, correction_player);
	if ((prior_mismatch != 0U) && (mismatch_tick < prior_mismatch))
	{
		syNetRollbackEpisodeSelfSealPeerPrefixRows(mismatch_tick, prior_mismatch);
	}
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM reseal_deeper load=%u mismatch=%u target=%u span=%u correction_player=%d\n",
	    load_tick,
	    mismatch_tick,
	    sSYNetRollbackEpisodeFsm.target_tick,
	    (unsigned int)((sSYNetRollbackEpisodeFsm.target_tick > mismatch_tick)
			       ? (sSYNetRollbackEpisodeFsm.target_tick - mismatch_tick)
			       : 0U),
	    (int)correction_player);
#else
	(void)load_tick;
	(void)mismatch_tick;
	(void)target_tick;
	(void)correction_player;
#endif
}

sb32 syNetRollbackEpisodeInputsSealed(void)
{
	return (sSYNetRollbackEpisodeFsm.inputs_sealed != FALSE) ? TRUE : FALSE;
}

sb32 syNetRollbackEpisodeTickInSealedSpan(u32 tick)
{
	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		return FALSE;
	}
	return ((tick >= sSYNetRollbackEpisodeFsm.mismatch_tick) && (tick < sSYNetRollbackEpisodeFsm.target_tick))
	           ? TRUE
	           : FALSE;
}

sb32 syNetRollbackEpisodeGetSealedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	u32 idx;

	if ((out_frame == NULL) || (player < 0) || (player >= MAXCONTROLLERS) ||
	    (syNetRollbackEpisodeInputsSealed() == FALSE))
	{
		return FALSE;
	}
	idx = syNetRollbackEpisodeFsmSealIndex(tick);
	if (idx >= SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
	{
		return FALSE;
	}
	if (sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] == FALSE)
	{
		return FALSE;
	}
	*out_frame = sSYNetRollbackEpisodeFsm.sealed[idx][player];
	return TRUE;
}

void syNetRollbackEpisodeReplayLogAppend(u32 tick, u32 input_digest, u32 figh, u32 item, u32 rng)
{
#ifdef PORT
	u32 n;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
	n = sSYNetRollbackEpisodeFsm.replay_log_count;
	if (n >= SYNETROLLBACK_EPISODE_REPLAY_LOG_MAX)
	{
		return;
	}
	sSYNetRollbackEpisodeFsm.replay_log[n].tick = tick;
	sSYNetRollbackEpisodeFsm.replay_log[n].input_digest = input_digest;
	sSYNetRollbackEpisodeFsm.replay_log[n].figh = figh;
	sSYNetRollbackEpisodeFsm.replay_log[n].item = item;
	sSYNetRollbackEpisodeFsm.replay_log[n].rng = rng;
	sSYNetRollbackEpisodeFsm.replay_log_count = n + 1U;
#endif
}

sb32 syNetRollbackEpisodeReplayLogGetPostDigest(SYNetRollbackEpisodePostDigest *out_digest)
{
#ifdef PORT
	u32 n;
	u32 last_idx;

	if ((out_digest == NULL) || (sSYNetRollbackEpisodeFsm.replay_log_count == 0U))
	{
		return FALSE;
	}
	memset(out_digest, 0, sizeof(*out_digest));
	last_idx = sSYNetRollbackEpisodeFsm.replay_log_count - 1U;
	out_digest->figh = sSYNetRollbackEpisodeFsm.replay_log[last_idx].figh;
	out_digest->item = sSYNetRollbackEpisodeFsm.replay_log[last_idx].item;
	out_digest->rng = sSYNetRollbackEpisodeFsm.replay_log[last_idx].rng;
	if ((sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid != FALSE) &&
	    (sSYNetRollbackEpisodeFsm.inputs_sealed != FALSE))
	{
		out_digest->input_digest = sSYNetRollbackEpisodeFsm.frozen_post_input_digest;
	}
	else
	{
		out_digest->input_digest = syNetRollbackEpisodeComputeSpanInputDigest(sSYNetRollbackEpisodeFsm.mismatch_tick,
										      sSYNetRollbackEpisodeFsm.target_tick);
	}
	(void)n;
	return TRUE;
#else
	(void)out_digest;
	return FALSE;
#endif
}

u32 syNetRollbackEpisodeReplayLogTickInputDigest(u32 tick)
{
#ifdef PORT
	return syNetRollbackEpisodeComputeTickInputDigest(tick);
#else
	(void)tick;
	return 0U;
#endif
}

void syNetRollbackEpisodeReplayLogCheckInternalDiverge(void)
{
#ifdef PORT
	u32 i;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
	for (i = 1U; i < sSYNetRollbackEpisodeFsm.replay_log_count; i++)
	{
		const SYNetRollbackEpisodeReplayLogEntry *prev;
		const SYNetRollbackEpisodeReplayLogEntry *cur;

		prev = &sSYNetRollbackEpisodeFsm.replay_log[i - 1U];
		cur = &sSYNetRollbackEpisodeFsm.replay_log[i];
		if ((prev->input_digest == cur->input_digest) && (prev->figh != cur->figh))
		{
			port_log(
			    "SSB64 NetRollback: EPISODE_REPLAY_DIVERGE tick=%u prev_tick=%u inp=0x%08X figh_old=0x%08X figh_new=0x%08X\n",
			    cur->tick,
			    prev->tick,
			    cur->input_digest,
			    prev->figh,
			    cur->figh);
		}
	}
#endif
}

void syNetRollbackEpisodeCommitPromoteSealed(void)
{
#ifdef PORT
	u32 t;
	s32 player;

	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		return;
	}
	for (t = sSYNetRollbackEpisodeFsm.mismatch_tick; t < sSYNetRollbackEpisodeFsm.target_tick; t++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			SYNetInputFrame frame;

			if (syNetRollbackEpisodeGetSealedFrame(player, t, &frame) != FALSE)
			{
				syNetInputStorePublishedHistoryFrame(player, &frame);
			}
		}
	}
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM commit_promote mismatch=%u target=%u\n",
	    sSYNetRollbackEpisodeFsm.mismatch_tick,
	    sSYNetRollbackEpisodeFsm.target_tick);
#endif
}

sb32 syNetRollbackEpisodeFsmGetLiveSimCap(u32 *out_cap, u32 *out_cap_source)
{
	u32 cap;
	u32 slack;

	if ((out_cap == NULL) || (syNetRollbackEpisodeFsmEnabled() == FALSE))
	{
		return FALSE;
	}
	cap = ~(u32)0;
	if (syNetRollbackEpisodeFsmIsActive() != FALSE)
	{
		if (sSYNetRollbackEpisodeFsm.target_tick != ~(u32)0)
		{
			cap = sSYNetRollbackEpisodeFsm.target_tick;
		}
		if (out_cap_source != NULL)
		{
			*out_cap_source = 1U;
		}
		*out_cap = cap;
		return (cap != ~(u32)0) ? TRUE : FALSE;
	}
	if (sSYNetRollbackEpisodeFsm.peer_convergence_active != FALSE)
	{
		slack = 1U;
		{
			const char *env;
			int parsed;

			env = getenv("SSB64_NETPLAY_ROLLBACK_EPOCH_CAP_SLACK");
			if ((env != NULL) && (env[0] != '\0'))
			{
				parsed = atoi(env);
				if (parsed >= 0)
				{
					slack = (u32)parsed;
				}
			}
		}
		if (sSYNetRollbackEpisodeFsm.peer_convergence_target > (~(u32)0 - slack))
		{
			cap = ~(u32)0;
		}
		else
		{
			cap = sSYNetRollbackEpisodeFsm.peer_convergence_target + slack;
		}
		if (out_cap_source != NULL)
		{
			*out_cap_source = 2U;
		}
		*out_cap = cap;
		return TRUE;
	}
	return FALSE;
}

void syNetRollbackEpisodeFsmSetPeerConvergence(u32 peer_target)
{
	if (peer_target == 0U)
	{
		return;
	}
	sSYNetRollbackEpisodeFsm.peer_convergence_target = peer_target;
	sSYNetRollbackEpisodeFsm.peer_convergence_active = TRUE;
}

void syNetRollbackEpisodeFsmOnPostDiverge(void)
{
#ifdef PORT
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM post_diverge epoch=%u mismatch=%u target=%u — abort peer convergence\n",
	    sSYNetRollbackEpisodeFsm.epoch_id,
	    sSYNetRollbackEpisodeFsm.mismatch_tick,
	    sSYNetRollbackEpisodeFsm.target_tick);
	sSYNetRollbackEpisodeFsm.peer_convergence_active = FALSE;
	sSYNetRollbackEpisodeFsm.peer_convergence_target = 0U;
	sSYNetRollbackEpisodeFsm.phase = nSYNetRollbackEpisodeFsmPhaseAbort;
#endif
}

void syNetRollbackEpisodeFsmOnPostMatch(void)
{
	sSYNetRollbackEpisodeFsm.peer_convergence_active = FALSE;
	sSYNetRollbackEpisodeFsm.peer_convergence_target = 0U;
}

static void syNetRollbackEpisodeEventQueuePush(const SYNetRollbackEpisodeEvent *event)
{
	u32 next;

	if (event == NULL)
	{
		return;
	}
	next = (sSYNetRollbackEpisodeFsm.event_tail + 1U) % SYNETROLLBACK_EPISODE_EVENT_QUEUE_MAX;
	if (next == sSYNetRollbackEpisodeFsm.event_head)
	{
		port_log("SSB64 NetRollback: EPISODE_FSM event queue full — dropping oldest\n");
		sSYNetRollbackEpisodeFsm.event_head =
		    (sSYNetRollbackEpisodeFsm.event_head + 1U) % SYNETROLLBACK_EPISODE_EVENT_QUEUE_MAX;
	}
	sSYNetRollbackEpisodeFsm.event_queue[sSYNetRollbackEpisodeFsm.event_tail] = *event;
	sSYNetRollbackEpisodeFsm.event_tail = next;
}

void syNetRollbackEpisodeEnqueueEvent(const SYNetRollbackEpisodeEvent *event)
{
#ifdef PORT
	if ((event == NULL) || (event->type == nSYNetRollbackEpisodeEventNone))
	{
		return;
	}
	syNetRollbackEpisodeEventQueuePush(event);
#endif
}

sb32 syNetRollbackEpisodeDrainNextEvent(SYNetRollbackEpisodeEvent *out_event)
{
	if ((out_event == NULL) || (sSYNetRollbackEpisodeFsm.event_head == sSYNetRollbackEpisodeFsm.event_tail))
	{
		return FALSE;
	}
	*out_event = sSYNetRollbackEpisodeFsm.event_queue[sSYNetRollbackEpisodeFsm.event_head];
	sSYNetRollbackEpisodeFsm.event_head =
	    (sSYNetRollbackEpisodeFsm.event_head + 1U) % SYNETROLLBACK_EPISODE_EVENT_QUEUE_MAX;
	return TRUE;
}

sb32 syNetRollbackEpisodeHasPendingEvents(void)
{
	return (sSYNetRollbackEpisodeFsm.event_head != sSYNetRollbackEpisodeFsm.event_tail) ? TRUE : FALSE;
}

sb32 syNetRollbackEpisodeFsmBaselineRequiresAnimMatch(void)
{
	return (syNetRollbackEpisodeFsmEnabled() != FALSE) ? TRUE : FALSE;
}

sb32 syNetRollbackEpisodeSealRowsExchangeEnabled(void)
{
	return syNetRollbackEpisodeFsmEnabled();
}

void syNetRollbackEpisodeResetPeerSealRowsState(void)
{
#ifdef PORT
	s32 player;

	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo));
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi));
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		sSYNetRollbackEpisodeFsm.seal_send_row_begin[player] = nSYNetRollbackEpisodeSealSendDone;
	}
	syNetRollbackEpisodeClearPendingPeerSealRows();
#endif
}

void syNetRollbackEpisodeArmSealRowsSend(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;

	if (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE)
	{
		return;
	}
	memset(sSYNetRollbackEpisodeFsm.seal_send_row_begin, nSYNetRollbackEpisodeSealSendDone,
	       sizeof(sSYNetRollbackEpisodeFsm.seal_send_row_begin));
	if (syNetRollbackEpisodeEnumerateLocalAuthoritySlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return;
	}
	for (i = 0; i < count; i++)
	{
		s32 slot;

		slot = slots[i];
		if ((slot >= 0) && (slot < MAXCONTROLLERS))
		{
			sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot] = 0U;
		}
	}
#endif
}

s32 syNetRollbackEpisodeEnumerateLocalAuthoritySlots(s32 *out_slots, s32 max_slots, s32 *out_count)
{
#ifdef PORT
	s32 local_slot;
	s32 extra_slot;
	s32 n;

	if ((out_slots == NULL) || (max_slots <= 0))
	{
		return FALSE;
	}
	n = 0;
	local_slot = syNetPeerGetLocalSimSlot();
	if ((local_slot >= 0) && (local_slot < MAXCONTROLLERS) && (n < max_slots))
	{
		out_slots[n++] = local_slot;
	}
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	if ((extra_slot >= 0) && (extra_slot < MAXCONTROLLERS) && (extra_slot != local_slot) &&
	    (syNetRollbackEpisodeSlotInList(extra_slot, out_slots, n) == FALSE) && (n < max_slots))
	{
		out_slots[n++] = extra_slot;
	}
	if (out_count != NULL)
	{
		*out_count = n;
	}
	return (n > 0) ? TRUE : FALSE;
#else
	(void)out_slots;
	(void)max_slots;
	(void)out_count;
	return FALSE;
#endif
}

s32 syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(s32 *out_slots, s32 max_slots, s32 *out_count)
{
#ifdef PORT
	s32 local_slots[MAXCONTROLLERS];
	s32 local_count;
	s32 n;
	s32 i;
	s32 slot;

	if ((out_slots == NULL) || (max_slots <= 0))
	{
		return FALSE;
	}
	n = 0;
	(void)syNetRollbackEpisodeEnumerateLocalAuthoritySlots(local_slots, MAXCONTROLLERS, &local_count);
	for (i = 0; i < syNetPeerGetRemoteHumanSlotCount(); i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetRollbackEpisodeSlotInList(slot, local_slots, local_count) != FALSE)
		{
			continue;
		}
		if ((n < max_slots) && (syNetRollbackEpisodeSlotInList(slot, out_slots, n) == FALSE))
		{
			out_slots[n++] = slot;
		}
	}
	if (out_count != NULL)
	{
		*out_count = n;
	}
	return (n > 0) ? TRUE : FALSE;
#else
	(void)out_slots;
	(void)max_slots;
	(void)out_count;
	return FALSE;
#endif
}

sb32 syNetRollbackEpisodeSlotIsLocalAuthority(s32 player)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;

	if (syNetRollbackEpisodeEnumerateLocalAuthoritySlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return FALSE;
	}
	return syNetRollbackEpisodeSlotInList(player, slots, count);
#else
	(void)player;
	return FALSE;
#endif
}

sb32 syNetRollbackEpisodeSlotRequiresPeerSealRows(s32 player)
{
#ifdef PORT
	if (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE)
	{
		return FALSE;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return FALSE;
	}
	return (syNetRollbackEpisodeSlotIsLocalAuthority(player) == FALSE) ? TRUE : FALSE;
#else
	(void)player;
	return FALSE;
#endif
}

sb32 syNetRollbackEpisodeExportSealRowsChunk(s32 slot, u32 row_begin, u32 max_rows, SYNetInputFrame *out_frames,
					    u32 *out_row_count)
{
#ifdef PORT
	u32 span;
	u32 t;
	u32 n;
	u32 i;

	if ((out_frames == NULL) || (out_row_count == NULL) || (syNetRollbackEpisodeInputsSealed() == FALSE) ||
	    (slot < 0) || (slot >= MAXCONTROLLERS) || (max_rows == 0U))
	{
		return FALSE;
	}
	span = syNetRollbackEpisodeSealSpan();
	if (row_begin >= span)
	{
		*out_row_count = 0U;
		return FALSE;
	}
	n = span - row_begin;
	if (n > max_rows)
	{
		n = max_rows;
	}
	if (n > SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX)
	{
		n = SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX;
	}
	for (i = 0; i < n; i++)
	{
		t = sSYNetRollbackEpisodeFsm.mismatch_tick + row_begin + i;
		if (syNetRollbackEpisodeGetSealedFrame(slot, t, &out_frames[i]) == FALSE)
		{
			return FALSE;
		}
	}
	*out_row_count = n;
	return TRUE;
#else
	(void)slot;
	(void)row_begin;
	(void)max_rows;
	(void)out_frames;
	(void)out_row_count;
	return FALSE;
#endif
}

sb32 syNetRollbackEpisodeApplyPeerSealRowsChunk(u32 epoch_id, u32 mismatch_tick, u32 target_tick, s32 slot,
						u32 row_begin, const SYNetInputFrame *rows, u32 row_count)
{
#ifdef PORT
	u32 span;
	u32 i;
	u32 idx;
	u32 t;

	if ((rows == NULL) || (slot < 0) || (slot >= MAXCONTROLLERS) || (row_count == 0U))
	{
		port_log("SSB64 NetRollback: EPISODE_SEAL_ROWS_REJECT reason=bad_args slot=%d count=%u\n", (int)slot,
			 row_count);
		return FALSE;
	}
	if ((syNetRollbackEpisodeFsmIsActive() != FALSE) && (epoch_id == sSYNetRollbackEpisodeFsm.epoch_id))
	{
		sSYNetRollbackEpisodePeerSealChunkSeen = TRUE;
	}
	if (syNetRollbackEpisodeEpisodeTupleMatches(epoch_id, mismatch_tick, target_tick) == FALSE)
	{
		/*
		 * Seals often race ahead of follower FsmBegin (ROLLBACK_SYNC still deferred while
		 * baseline echo retry blocks quiesce). Stash by packet tuple while FSM is Live so
		 * SealInputs can flush after Begin — do not require active_epoch match first.
		 * See docs/bugs/netplay_follower_seal_reject_echo_retry_hang_2026-07-12.md.
		 */
		if (syNetRollbackEpisodeFsmIsActive() == FALSE)
		{
			if (syNetRollbackEpisodeStashPendingPeerSealRowsChunk(epoch_id, mismatch_tick, target_tick, slot,
									      row_begin, rows, row_count) != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_SEAL_ROWS_EARLY_STASH epoch=%u mismatch=%u target=%u slot=%d begin=%u count=%u\n",
				    epoch_id,
				    mismatch_tick,
				    target_tick,
				    (int)slot,
				    row_begin,
				    row_count);
				return TRUE;
			}
			port_log(
			    "SSB64 NetRollback: EPISODE_SEAL_ROWS_REJECT reason=early_stash_full slot=%d pkt_epoch=%u pkt_mismatch=%u pkt_target=%u\n",
			    (int)slot,
			    epoch_id,
			    mismatch_tick,
			    target_tick);
			return FALSE;
		}
		/*
		 * Active FSM, same epoch, partial tuple overlap (mismatch fork or target fork after
		 * tuple_align race): accept overlapping absolute ticks instead of stale reject.
		 */
		if ((epoch_id == sSYNetRollbackEpisodeFsm.epoch_id) &&
		    ((target_tick == sSYNetRollbackEpisodeFsm.target_tick) !=
		     (mismatch_tick == sSYNetRollbackEpisodeFsm.mismatch_tick)))
		{
			if (syNetRollbackEpisodeInputsSealed() != FALSE)
			{
				if (syNetRollbackEpisodeApplyCompatiblePeerSealRowsChunk(epoch_id, mismatch_tick,
											 target_tick, slot, row_begin,
											 rows, row_count) != FALSE)
				{
					return TRUE;
				}
			}
			else if (syNetRollbackEpisodeStashPendingPeerSealRowsChunk(epoch_id, mismatch_tick, target_tick,
										   slot, row_begin, rows,
										   row_count) != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_SEAL_ROWS_COMPATIBLE_STASH epoch=%u pkt_mismatch=%u local_mismatch=%u pkt_target=%u local_target=%u slot=%d begin=%u count=%u\n",
				    epoch_id,
				    mismatch_tick,
				    sSYNetRollbackEpisodeFsm.mismatch_tick,
				    target_tick,
				    sSYNetRollbackEpisodeFsm.target_tick,
				    (int)slot,
				    row_begin,
				    row_count);
				return TRUE;
			}
		}
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_REJECT reason=stale_episode_tuple slot=%d pkt_epoch=%u pkt_mismatch=%u pkt_target=%u active_epoch=%u active_mismatch=%u active_target=%u active_load=%u\n",
		    (int)slot,
		    epoch_id,
		    mismatch_tick,
		    target_tick,
		    sSYNetRollbackEpisodeFsm.epoch_id,
		    sSYNetRollbackEpisodeFsm.mismatch_tick,
		    sSYNetRollbackEpisodeFsm.target_tick,
		    sSYNetRollbackEpisodeFsm.load_tick);
		return FALSE;
	}
	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		if (syNetRollbackEpisodeStashPendingPeerSealRowsChunk(epoch_id, mismatch_tick, target_tick, slot,
								      row_begin, rows, row_count) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: EPISODE_SEAL_ROWS_STASH epoch=%u mismatch=%u target=%u slot=%d begin=%u count=%u\n",
			    epoch_id,
			    mismatch_tick,
			    target_tick,
			    (int)slot,
			    row_begin,
			    row_count);
			return TRUE;
		}
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_REJECT reason=stash_full slot=%d begin=%u count=%u\n",
		    (int)slot,
		    row_begin,
		    row_count);
		return FALSE;
	}
	span = syNetRollbackEpisodeSealSpan();
	if ((row_begin >= span) || (row_begin + row_count > span))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_REJECT reason=span_oob slot=%d begin=%u count=%u span=%u\n",
		    (int)slot,
		    row_begin,
		    row_count,
		    span);
		return FALSE;
	}
	for (i = 0; i < row_count; i++)
	{
		idx = row_begin + i;
		if (idx >= SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN)
		{
			return FALSE;
		}
		t = mismatch_tick + row_begin + i;
		sSYNetRollbackEpisodeFsm.sealed[idx][slot] = rows[i];
		syNetRollbackEpisodeNormalizeSealedFrameTick(&sSYNetRollbackEpisodeFsm.sealed[idx][slot], t);
		sSYNetRollbackEpisodeFsm.sealed_valid[idx][slot] = TRUE;
		syNetRollbackEpisodeMarkPeerSealTick(slot, row_begin + i);
	}
	port_log(
	    "SSB64 NetRollback: EPISODE_SEAL_ROWS_RECV epoch=%u mismatch=%u target=%u slot=%d begin=%u count=%u slot_span_digest=0x%08X\n",
	    epoch_id,
	    mismatch_tick,
	    target_tick,
	    (int)slot,
	    row_begin,
	    row_count,
	    syNetRollbackEpisodeComputeSlotSpanInputDigest(slot, mismatch_tick, target_tick));
	return TRUE;
#else
	(void)epoch_id;
	(void)mismatch_tick;
	(void)target_tick;
	(void)slot;
	(void)row_begin;
	(void)rows;
	(void)row_count;
	return FALSE;
#endif
}

sb32 syNetRollbackEpisodePeerSealRowsComplete(s32 player)
{
#ifdef PORT
	u32 span;

	if (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackEpisodeSlotRequiresPeerSealRows(player) == FALSE)
	{
		return TRUE;
	}
	span = syNetRollbackEpisodeSealSpan();
	return syNetRollbackEpisodePeerSealTickMaskComplete(player, span);
#else
	(void)player;
	return TRUE;
#endif
}

sb32 syNetRollbackEpisodeAllPeerSealRowsComplete(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;

	if (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return TRUE;
	}
	for (i = 0; i < count; i++)
	{
		if (syNetRollbackEpisodePeerSealRowsComplete(slots[i]) == FALSE)
		{
			return FALSE;
		}
	}
	return TRUE;
#else
	return TRUE;
#endif
}

sb32 syNetRollbackEpisodeTrySelfSealMissingPeerRows(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 span;
	u32 j;

	if ((syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE) ||
	    (syNetRollbackEpisodeInputsSealed() == FALSE))
	{
		return FALSE;
	}
	span = syNetRollbackEpisodeSealSpan();
	if ((span == 0U) || (span > SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return TRUE;
	}
	/*
	 * Validate first: every missing row of every incomplete slot must resolve from
	 * wire-CONFIRMED remote input history (never hold-last or predicted). Confirmed wire
	 * frames are byte-identical to what the remote authority would seal for itself, so
	 * self-sealing them is sound; anything weaker would fork the replay from the peer.
	 */
	for (i = 0; i < count; i++)
	{
		s32 player;

		player = slots[i];
		if (syNetRollbackEpisodePeerSealRowsComplete(player) != FALSE)
		{
			continue;
		}
		for (j = 0; j < span; j++)
		{
			SYNetInputFrame frame;

			if (sSYNetRollbackEpisodeFsm.sealed_valid[j][player] != FALSE)
			{
				continue;
			}
			if (syNetInputGetRemoteHistoryFrame(player, sSYNetRollbackEpisodeFsm.mismatch_tick + j,
							    &frame) == FALSE)
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_SEAL_ROWS_SELF_SEAL_SKIP slot=%d row=%u tick=%u reason=not_wire_confirmed\n",
				    (int)player,
				    j,
				    sSYNetRollbackEpisodeFsm.mismatch_tick + j);
				return FALSE;
			}
		}
	}
	for (i = 0; i < count; i++)
	{
		s32 player;
		u32 filled;

		player = slots[i];
		if (syNetRollbackEpisodePeerSealRowsComplete(player) != FALSE)
		{
			continue;
		}
		filled = 0U;
		for (j = 0; j < span; j++)
		{
			SYNetInputFrame frame;
			u32 t;

			t = sSYNetRollbackEpisodeFsm.mismatch_tick + j;
			if (sSYNetRollbackEpisodeFsm.sealed_valid[j][player] == FALSE)
			{
				if (syNetInputGetRemoteHistoryFrame(player, t, &frame) == FALSE)
				{
					return FALSE;
				}
				syNetRollbackEpisodeNormalizeSealedFrameTick(&frame, t);
				frame.source = nSYNetInputSourceRemoteConfirmed;
				frame.is_predicted = FALSE;
				frame.is_valid = TRUE;
				sSYNetRollbackEpisodeFsm.sealed[j][player] = frame;
				sSYNetRollbackEpisodeFsm.sealed_valid[j][player] = TRUE;
				filled++;
			}
			syNetRollbackEpisodeMarkPeerSealTick(player, j);
		}
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_SELF_SEAL slot=%d filled=%u span=%u slot_span_digest=0x%08X\n",
		    (int)player,
		    filled,
		    span,
		    syNetRollbackEpisodeComputeSlotSpanInputDigest(player, sSYNetRollbackEpisodeFsm.mismatch_tick,
								   sSYNetRollbackEpisodeFsm.target_tick));
	}
	return syNetRollbackEpisodeAllPeerSealRowsComplete();
#else
	return FALSE;
#endif
}

sb32 syNetRollbackEpisodeLocalSealRowsSendComplete(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 span;

	if (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		return TRUE;
	}
	span = syNetRollbackEpisodeSealSpan();
	if (span == 0U)
	{
		return TRUE;
	}
	if (syNetRollbackEpisodeEnumerateLocalAuthoritySlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return TRUE;
	}
	for (i = 0; i < count; i++)
	{
		s32 slot;
		u32 begin;

		slot = slots[i];
		if ((slot < 0) || (slot >= MAXCONTROLLERS))
		{
			continue;
		}
		begin = (u32)sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot];
		if ((begin < nSYNetRollbackEpisodeSealSendDone) && (begin < span))
		{
			return FALSE;
		}
	}
	return TRUE;
#else
	return TRUE;
#endif
}

u32 syNetRollbackEpisodeGetLocalSealRowsSendPendingMask(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	u32 mask;
	s32 i;
	u32 span;

	mask = 0U;
	if (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE)
	{
		return 0U;
	}
	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		return 0U;
	}
	span = syNetRollbackEpisodeSealSpan();
	if (span == 0U)
	{
		return 0U;
	}
	if (syNetRollbackEpisodeEnumerateLocalAuthoritySlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return 0U;
	}
	for (i = 0; i < count; i++)
	{
		s32 slot;
		u32 begin;

		slot = slots[i];
		if ((slot < 0) || (slot >= MAXCONTROLLERS))
		{
			continue;
		}
		begin = (u32)sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot];
		if ((begin < nSYNetRollbackEpisodeSealSendDone) && (begin < span))
		{
			mask |= (u32)(1U << slot);
		}
	}
	return mask;
#else
	return 0U;
#endif
}

void syNetRollbackEpisodePumpOutboundSealRows(u32 max_chunks)
{
#ifdef PORT
	u32 i;

	if (max_chunks == 0U)
	{
		max_chunks = 1U;
	}
	if (max_chunks > 8U)
	{
		max_chunks = 8U;
	}
	for (i = 0; i < max_chunks; i++)
	{
		if (syNetRollbackEpisodeLocalSealRowsSendComplete() != FALSE)
		{
			break;
		}
		if (syNetPeerTrySendEpisodeSealRows() == FALSE)
		{
			break;
		}
	}
#else
	(void)max_chunks;
#endif
}

u32 syNetRollbackEpisodeGetSealSpan(void)
{
#ifdef PORT
	return syNetRollbackEpisodeSealSpan();
#else
	return 0U;
#endif
}

void syNetRollbackEpisodeLogSealRowsWaitDetail(u32 load_tick, u32 missing_mask)
{
#ifdef PORT
	static u32 sLastLogLoadTick;
	static u32 sLastLogMissingMask;
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 span;

	if (missing_mask == 0U)
	{
		return;
	}
	if ((load_tick == sLastLogLoadTick) && (missing_mask == sLastLogMissingMask))
	{
		return;
	}
	sLastLogLoadTick = load_tick;
	sLastLogMissingMask = missing_mask;
	span = syNetRollbackEpisodeSealSpan();
	if (syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_WAIT_DETAIL load_tick=%u missing_slots=0x%X span=%u (no required slots)\n",
		    load_tick,
		    missing_mask,
		    span);
		return;
	}
	for (i = 0; i < count; i++)
	{
		s32 player;
		u32 first_invalid;
		u32 j;
		u32 lo_recv;
		u32 hi_recv;

		player = slots[i];
		if (((missing_mask & (u32)(1U << player)) == 0U) || (player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		first_invalid = ~(u32)0;
		for (j = 0; j < span; j++)
		{
			if (sSYNetRollbackEpisodeFsm.sealed_valid[j][player] == FALSE)
			{
				first_invalid = j;
				break;
			}
		}
		lo_recv = 0U;
		hi_recv = 0U;
		for (j = 0; j < span && j < 64U; j++)
		{
			if ((sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_lo[player] & ((u64)1 << j)) != 0U)
			{
				lo_recv++;
			}
		}
		for (j = 64U; j < span; j++)
		{
			if ((sSYNetRollbackEpisodeFsm.peer_seal_tick_mask_hi[player] & ((u64)1 << (j - 64U))) != 0U)
			{
				hi_recv++;
			}
		}
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_WAIT_DETAIL load_tick=%u slot=%d span=%u first_invalid_idx=%u lo_mask_bits=%u hi_mask_bits=%u\n",
		    load_tick,
		    (int)player,
		    span,
		    first_invalid,
		    lo_recv,
		    hi_recv);
	}
#else
	(void)load_tick;
	(void)missing_mask;
#endif
}

u32 syNetRollbackEpisodeGetMissingPeerSealSlotsMask(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	u32 mask;
	s32 i;

	mask = 0U;
	if (syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return 0U;
	}
	for (i = 0; i < count; i++)
	{
		if (syNetRollbackEpisodePeerSealRowsComplete(slots[i]) == FALSE)
		{
			mask |= (u32)(1U << slots[i]);
		}
	}
	return mask;
#else
	return 0U;
#endif
}

sb32 syNetRollbackEpisodeTakeSealRowsChunkForSend(u32 *out_epoch_id, u32 *out_mismatch_tick, u32 *out_target_tick,
						 s32 *out_slot, u32 *out_row_begin, SYNetInputFrame *out_frames,
						 u32 max_frames, u32 *out_row_count)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 span;

	if ((out_epoch_id == NULL) || (out_mismatch_tick == NULL) || (out_target_tick == NULL) || (out_slot == NULL) ||
	    (out_row_begin == NULL) || (out_frames == NULL) || (out_row_count == NULL) ||
	    (syNetRollbackEpisodeInputsSealed() == FALSE) || (syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE))
	{
		return FALSE;
	}
	span = syNetRollbackEpisodeSealSpan();
	if (span == 0U)
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeEnumerateLocalAuthoritySlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return FALSE;
	}
	for (i = 0; i < count; i++)
	{
		s32 slot;
		u32 begin;

		slot = slots[i];
		if ((slot < 0) || (slot >= MAXCONTROLLERS))
		{
			continue;
		}
		begin = (u32)sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot];
		if ((begin >= nSYNetRollbackEpisodeSealSendDone) || (begin >= span))
		{
			continue;
		}
		if (syNetRollbackEpisodeExportSealRowsChunk(slot, begin, max_frames, out_frames, out_row_count) == FALSE)
		{
			continue;
		}
		*out_epoch_id = sSYNetRollbackEpisodeFsm.epoch_id;
		*out_mismatch_tick = sSYNetRollbackEpisodeFsm.mismatch_tick;
		*out_target_tick = sSYNetRollbackEpisodeFsm.target_tick;
		*out_slot = slot;
		*out_row_begin = begin;
		return TRUE;
	}
	return FALSE;
#else
	(void)out_epoch_id;
	(void)out_mismatch_tick;
	(void)out_target_tick;
	(void)out_slot;
	(void)out_row_begin;
	(void)out_frames;
	(void)max_frames;
	(void)out_row_count;
	return FALSE;
#endif
}

void syNetRollbackEpisodeNoteSealRowsChunkSent(s32 slot, u32 row_begin, u32 row_count)
{
#ifdef PORT
	u32 span;

	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (row_count == 0U))
	{
		return;
	}
	span = syNetRollbackEpisodeSealSpan();
	if (sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot] >= nSYNetRollbackEpisodeSealSendDone)
	{
		return;
	}
	sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot] = (u8)(row_begin + row_count);
	if (sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot] >= span)
	{
		sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot] = nSYNetRollbackEpisodeSealSendDone;
	}
#endif
}

void syNetRollbackEpisodePrepareSealRowsRetransmit(void)
{
#ifdef PORT
	s32 slots[MAXCONTROLLERS];
	s32 count;
	s32 i;
	u32 span;

	if ((syNetRollbackEpisodeSealRowsExchangeEnabled() == FALSE) ||
	    (syNetRollbackEpisodeInputsSealed() == FALSE))
	{
		return;
	}
	if ((syNetRollbackEpisodeAllPeerSealRowsComplete() != FALSE) &&
	    (syNetRollbackEpisodeLocalSealRowsSendComplete() != FALSE))
	{
		return;
	}
	span = syNetRollbackEpisodeSealSpan();
	if (span == 0U)
	{
		return;
	}
	if (syNetRollbackEpisodeEnumerateLocalAuthoritySlots(slots, MAXCONTROLLERS, &count) == FALSE)
	{
		return;
	}
	for (i = 0; i < count; i++)
	{
		s32 slot;
		u32 begin;

		slot = slots[i];
		if ((slot < 0) || (slot >= MAXCONTROLLERS))
		{
			continue;
		}
		begin = (u32)sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot];
		if ((begin >= nSYNetRollbackEpisodeSealSendDone) || (begin >= span))
		{
			sSYNetRollbackEpisodeFsm.seal_send_row_begin[slot] = 0U;
		}
	}
#endif
}

u32 syNetRollbackEpisodeComputeSlotSpanInputDigest(s32 player, u32 from_tick, u32 to_tick)
{
#ifdef PORT
	u32 checksum;
	u32 t;
	SYNetInputFrame frame;

	checksum = 2166136261U;
	for (t = from_tick; t < to_tick; t++)
	{
		if (syNetRollbackEpisodeGetSealedFrame(player, t, &frame) != FALSE)
		{
			checksum = syNetInputAccumulateInputChecksum(checksum, player, &frame);
		}
	}
	return checksum;
#else
	(void)player;
	(void)from_tick;
	(void)to_tick;
	return 0U;
#endif
}

void syNetRollbackEpisodeFreezePostInputDigest(void)
{
#ifdef PORT
	u32 digest;

	if (syNetRollbackEpisodeInputsSealed() == FALSE)
	{
		return;
	}
	digest = syNetRollbackEpisodeComputeSpanInputDigest(sSYNetRollbackEpisodeFsm.mismatch_tick,
							    sSYNetRollbackEpisodeFsm.target_tick);
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest = digest;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid = TRUE;
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM freeze_post_inp mismatch=%u target=%u inp=0x%08X\n",
	    sSYNetRollbackEpisodeFsm.mismatch_tick,
	    sSYNetRollbackEpisodeFsm.target_tick,
	    digest);
#endif
}

sb32 syNetRollbackEpisodeGetFrozenPostInputDigest(u32 *out_input_digest)
{
#ifdef PORT
	if ((out_input_digest == NULL) || (sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid == FALSE))
	{
		return FALSE;
	}
	*out_input_digest = sSYNetRollbackEpisodeFsm.frozen_post_input_digest;
	return TRUE;
#else
	(void)out_input_digest;
	return FALSE;
#endif
}

#ifndef PORT
void syNetRollbackEpisodeFsmSessionReset(void)
{
}
void syNetRollbackEpisodeFsmBegin(u32 a, u32 b, u32 c, u32 d, s32 e, sb32 f, sb32 g)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
	(void)f;
	(void)g;
}
void syNetRollbackEpisodeFsmSetPhase(SYNetRollbackEpisodeFsmPhase p)
{
	(void)p;
}
void syNetRollbackEpisodeFsmSyncToLegacy(sb32 *a, sb32 *b, sb32 *c, u32 *d, u32 *e, u32 *u, s32 *s, sb32 *f)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
	(void)u;
	(void)s;
	(void)f;
}
void syNetRollbackEpisodeSealInputs(u32 a, u32 b, s32 c)
{
	(void)a;
	(void)b;
	(void)c;
}
void syNetRollbackEpisodeResealForDeeperLoad(u32 a, u32 b, u32 c, s32 d)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
}
void syNetRollbackEpisodeReplayLogAppend(u32 a, u32 b, u32 c, u32 d, u32 e)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
}
void syNetRollbackEpisodeReplayLogCheckInternalDiverge(void)
{
}
void syNetRollbackEpisodeCommitPromoteSealed(void)
{
}
void syNetRollbackEpisodeFsmSetPeerConvergence(u32 t)
{
	(void)t;
}
void syNetRollbackEpisodeFsmOnPostDiverge(void)
{
}
void syNetRollbackEpisodeFsmOnPostMatch(void)
{
}
void syNetRollbackEpisodeEnqueueEvent(const SYNetRollbackEpisodeEvent *e)
{
	(void)e;
}
void syNetRollbackEpisodePumpPendingPeerSealRows(void)
{
}
u32 syNetRollbackEpisodeComputeSpanInputDigest(u32 from_tick, u32 to_tick)
{
	(void)from_tick;
	(void)to_tick;
	return 0U;
}
void syNetRollbackEpisodeFreezePostInputDigest(void)
{
}
sb32 syNetRollbackEpisodeGetFrozenPostInputDigest(u32 *out_input_digest)
{
	(void)out_input_digest;
	return FALSE;
}
#endif
