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
	u64 peer_seal_tick_mask[MAXCONTROLLERS];
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

	if ((player < 0) || (player >= MAXCONTROLLERS) || (row_offset >= 64U))
	{
		return;
	}
	bit = (u64)1 << row_offset;
	sSYNetRollbackEpisodeFsm.peer_seal_tick_mask[player] |= bit;
}

static sb32 syNetRollbackEpisodePeerSealTickMaskComplete(s32 player, u32 span)
{
	u64 need;
	u32 i;

	if ((player < 0) || (player >= MAXCONTROLLERS) || (span == 0U) || (span > 64U))
	{
		return FALSE;
	}
	need = (span >= 64U) ? ~(u64)0 : (((u64)1 << span) - 1U);
	for (i = 0; i < span; i++)
	{
		u32 idx;

		idx = i;
		if (sSYNetRollbackEpisodeFsm.sealed_valid[idx][player] == FALSE)
		{
			return FALSE;
		}
	}
	return ((sSYNetRollbackEpisodeFsm.peer_seal_tick_mask[player] & need) == need) ? TRUE : FALSE;
}

static void syNetRollbackEpisodeClearPendingPeerSealRows(void)
{
	memset(sSYNetRollbackEpisodePendingSeal, 0, sizeof(sSYNetRollbackEpisodePendingSeal));
}

static sb32 syNetRollbackEpisodeEpisodeTupleMatches(u32 epoch_id, u32 mismatch_tick, u32 target_tick)
{
	if (sSYNetRollbackEpisodeFsm.phase == nSYNetRollbackEpisodeFsmPhaseLive)
	{
		return FALSE;
	}
	return ((epoch_id == sSYNetRollbackEpisodeFsm.epoch_id) &&
		(mismatch_tick == sSYNetRollbackEpisodeFsm.mismatch_tick) &&
		(target_tick == sSYNetRollbackEpisodeFsm.target_tick))
	           ? TRUE
	           : FALSE;
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
#endif

sb32 syNetRollbackEpisodeFsmEnabled(void)
{
#ifdef PORT
	const char *env;

	if (sSYNetRollbackEpisodeFsmEnvCache == -999)
	{
		env = getenv("SSB64_NETPLAY_ROLLBACK_EPISODE_FSM");
		sSYNetRollbackEpisodeFsmEnvCache =
		    ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
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
	syNetRollbackEpisodeClearPendingPeerSealRows();
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
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask));
	memset(sSYNetRollbackEpisodeFsm.seal_send_row_begin, nSYNetRollbackEpisodeSealSendDone,
	       sizeof(sSYNetRollbackEpisodeFsm.seal_send_row_begin));
	sSYNetRollbackEpisodeFsm.replay_log_count = 0U;
	sSYNetRollbackEpisodeFsm.inputs_sealed = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest_valid = FALSE;
	sSYNetRollbackEpisodeFsm.frozen_post_input_digest = 0U;
	sSYNetRollbackEpisodeFsm.peer_convergence_target = 0U;
	sSYNetRollbackEpisodeFsm.peer_convergence_active = FALSE;
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
	syNetRollbackEpisodeClearPendingPeerSealRows();
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
	if (phase == nSYNetRollbackEpisodeFsmPhaseLive)
	{
		syNetRollbackEpisodeFsmSessionReset();
	}
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
	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask));
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
			    (syNetInputCopyEpisodeRemoteHumanSealFrame(player, t, &frame) != FALSE))
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

void syNetRollbackEpisodeResealForDeeperLoad(u32 load_tick, u32 mismatch_tick, u32 target_tick, s32 correction_player)
{
#ifdef PORT
	u32 span;
	SYNetRollbackEpisodeFsmPhase phase;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
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

	memset(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask, 0, sizeof(sSYNetRollbackEpisodeFsm.peer_seal_tick_mask));
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
	if (syNetRollbackEpisodeEpisodeTupleMatches(epoch_id, mismatch_tick, target_tick) == FALSE)
	{
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
	    (syNetRollbackEpisodeInputsSealed() == FALSE) || (syNetRollbackEpisodeAllPeerSealRowsComplete() != FALSE))
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
