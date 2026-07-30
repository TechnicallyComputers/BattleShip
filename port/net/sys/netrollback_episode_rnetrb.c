/*
 * BattleShip bridge into the shared recomp-net rollback episode core (rnet_rb).
 *
 * Opt-in (SSB64_NETPLAY_ROLLBACK_RNETRB=1). When enabled, the
 * shared FSM (tuple, phase, resolved-through watermark, sealed-span commit) runs
 * alongside the proven SSB64 episode logic, which remains authoritative for
 * sealing, peer seal-row exchange, span digests, and replay execution. This is
 * the bisectable adoption step: soak validates the shared core against live
 * BattleShip behavior before the shared core ever becomes the source of truth.
 *
 * Host-owned (stays here, never moves): snapshot save/load, sim advance, state
 * digests, character/status gates, SSB wire transport.
 */

#include <sys/netrollback_episode.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <PR/os.h>
#include <stdlib.h>
#include <string.h>

#include <recomp_net/rollback.h>

#include <sys/netinput.h>
#include <sys/netpeer.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static RNetRbSession *sSYNetRbSession;
static RNetRollbackVTable sSYNetRbVTable;
static int sSYNetRbEnvCache = -999;
/* Bridge feed for host vtable input rows: points at the FSM's sealed frame view. */
static SYNetInputFrame (*sSYNetRbRowProvider)(s32 player, u32 tick);

static int sSYNetRbHostSaveState(void *ctx, u32 tick)
{
	/* SSB snapshot persistence is driven by the rollback engine; the shared core
	 * requests a save only as a watermark — the actual capture is host-owned. */
	(void)ctx;
	(void)tick;
	return 0;
}

static int sSYNetRbHostLoadState(void *ctx, u32 tick)
{
	(void)ctx;
	(void)tick;
	return 0;
}

static int sSYNetRbHostAdvanceSim(void *ctx, u32 tick)
{
	/* Forward replay execution stays in the SSB64 resim path; shared-core replay
	 * is watermark-only in the opt-in bridge. */
	(void)ctx;
	(void)tick;
	return 0;
}

static u32 sSYNetRbHostStateDigest(void *ctx, u32 tick, u32 partition)
{
	(void)ctx;
	(void)tick;
	(void)partition;
	return 0U;
}

static u8 sSYNetRbHostHashConfirmThrough(void *ctx, u32 tick)
{
	(void)ctx;
	(void)tick;
	return 0U;
}

static u8 sSYNetRbHostGetInputRow(void *ctx, s32 slot, u32 tick, RNetRbFrame *out_frame)
{
	SYNetInputFrame frame;

	(void)ctx;
	if (out_frame == NULL)
	{
		return 0U;
	}
	memset(out_frame, 0, sizeof(*out_frame));
	out_frame->tick = tick;
	if (syNetInputGetHistoryFrame(slot, tick, &frame) == FALSE)
	{
		return 0U;
	}
	out_frame->buttons = frame.buttons;
	out_frame->stick_x = frame.stick_x;
	out_frame->stick_y = frame.stick_y;
	out_frame->is_predicted = (frame.is_predicted != FALSE) ? 1U : 0U;
	out_frame->is_valid = (frame.is_valid != FALSE) ? 1U : 0U;
	return out_frame->is_valid;
}

sb32 syNetRollbackEpisodeRnetRbEnabled(void)
{
	const char *env;

	if (sSYNetRbEnvCache == -999)
	{
		env = getenv("SSB64_NETPLAY_ROLLBACK_RNETRB");
		sSYNetRbEnvCache = ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
		if (sSYNetRbEnvCache != 0)
		{
			port_log("SSB64 NetRollback: RNETRB shared-core mirror ENABLED (opt-in)\n");
		}
	}
	return (sSYNetRbEnvCache != 0) ? TRUE : FALSE;
}

void syNetRollbackEpisodeRnetRbSyncFromFsm(void)
{
	RNetRbConfig cfg;

	if (syNetRollbackEpisodeRnetRbEnabled() == FALSE)
	{
		return;
	}
	if (sSYNetRbSession != NULL)
	{
		rnet_rb_session_reset(sSYNetRbSession);
		return;
	}
	memset(&cfg, 0, sizeof(cfg));
	{
		s32 local = syNetPeerGetLocalSimSlot();
		cfg.local_slot = (local >= 0) ? (u32)local : 0U;
	}
	cfg.delay = 0U; /* authoritative D is owned by syNetInput; not needed for mirror */
	cfg.seal_max_span = SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN;
	memset(&sSYNetRbVTable, 0, sizeof(sSYNetRbVTable));
	sSYNetRbVTable.ctx = NULL;
	sSYNetRbVTable.save_state = sSYNetRbHostSaveState;
	sSYNetRbVTable.load_state = sSYNetRbHostLoadState;
	sSYNetRbVTable.advance_sim = sSYNetRbHostAdvanceSim;
	sSYNetRbVTable.state_digest = sSYNetRbHostStateDigest;
	sSYNetRbVTable.hash_confirm_through = sSYNetRbHostHashConfirmThrough;
	sSYNetRbVTable.get_input_row = sSYNetRbHostGetInputRow;
	/* stick_gates left NULL: SSB gates stay bound in netinput.c's own contract
	 * bridge; the mirror only tracks episode FSM, not per-tick stick decisions. */
	sSYNetRbSession = rnet_rb_create(&cfg, &sSYNetRbVTable);
	if (sSYNetRbSession == NULL)
	{
		port_log("SSB64 NetRollback: RNETRB rnet_rb_create failed — mirror disabled\n");
		sSYNetRbEnvCache = 0;
	}
}

void syNetRollbackEpisodeRnetRbBegin(u32 epoch_id, u32 mismatch_tick, u32 load_tick, u32 target_tick,
                                     s32 corrected_slot, sb32 from_peer_notify)
{
	RNetRbCorrection corr;

	if ((syNetRollbackEpisodeRnetRbEnabled() == FALSE) || (sSYNetRbSession == NULL))
	{
		return;
	}
	memset(&corr, 0, sizeof(corr));
	corr.epoch_id = epoch_id;
	corr.mismatch_tick = mismatch_tick;
	corr.load_tick = load_tick;
	corr.target_tick = target_tick;
	corr.slot = corrected_slot;
	corr.initiator = (from_peer_notify == FALSE) ? 1U : 0U;
	corr.from_peer_notify = (from_peer_notify != FALSE) ? 1U : 0U;
	rnet_rb_begin_episode(sSYNetRbSession, &corr);
}

void syNetRollbackEpisodeRnetRbSetPhase(SYNetRollbackEpisodeFsmPhase phase)
{
	RNetRbPhase rp;

	if ((syNetRollbackEpisodeRnetRbEnabled() == FALSE) || (sSYNetRbSession == NULL))
	{
		return;
	}
	switch (phase)
	{
	case nSYNetRollbackEpisodeFsmPhaseLive:
		rp = nRNetRbPhaseLive;
		break;
	case nSYNetRollbackEpisodeFsmPhaseSealInputs:
		rp = nRNetRbPhaseSealInputs;
		break;
	case nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline:
		rp = nRNetRbPhaseAwaitingBaseline;
		break;
	case nSYNetRollbackEpisodeFsmPhaseReplay:
		rp = nRNetRbPhaseReplay;
		break;
	case nSYNetRollbackEpisodeFsmPhaseVerify:
		rp = nRNetRbPhaseVerify;
		break;
	case nSYNetRollbackEpisodeFsmPhaseCommit:
		rp = nRNetRbPhaseCommit;
		break;
	case nSYNetRollbackEpisodeFsmPhaseAbort:
		rp = nRNetRbPhaseAbort;
		break;
	default:
		return;
	}
	rnet_rb_set_phase(sSYNetRbSession, rp);
}

void syNetRollbackEpisodeRnetRbSetPeerConvergence(u32 peer_target)
{
	if ((syNetRollbackEpisodeRnetRbEnabled() == FALSE) || (sSYNetRbSession == NULL))
	{
		return;
	}
	rnet_rb_set_peer_convergence(sSYNetRbSession, peer_target);
}

void syNetRollbackEpisodeRnetRbOnPostMatch(void)
{
	if ((syNetRollbackEpisodeRnetRbEnabled() == FALSE) || (sSYNetRbSession == NULL))
	{
		return;
	}
	rnet_rb_on_post_match(sSYNetRbSession);
}

void syNetRollbackEpisodeRnetRbCommitPromoteSealed(void)
{
	if ((syNetRollbackEpisodeRnetRbEnabled() == FALSE) || (sSYNetRbSession == NULL))
	{
		return;
	}
	rnet_rb_commit_promote_sealed(sSYNetRbSession);
}

#else

/* Offline / non-netmenu: shared-core mirror compiled out. */
sb32 syNetRollbackEpisodeRnetRbEnabled(void) { return FALSE; }
void syNetRollbackEpisodeRnetRbSyncFromFsm(void) {}
void syNetRollbackEpisodeRnetRbBegin(u32 a, u32 b, u32 c, u32 d, s32 e, sb32 f)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}
void syNetRollbackEpisodeRnetRbSetPhase(SYNetRollbackEpisodeFsmPhase p) { (void)p; }
void syNetRollbackEpisodeRnetRbSetPeerConvergence(u32 t) { (void)t; }
void syNetRollbackEpisodeRnetRbOnPostMatch(void) {}
void syNetRollbackEpisodeRnetRbCommitPromoteSealed(void) {}

#endif
