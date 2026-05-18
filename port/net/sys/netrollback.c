#include <sys/netrollback.h>

#include <sys/netinput.h>
#include <sys/netinput_timeline.h>
#include <sys/netpeer.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/taskman.h>

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <if/ifcommon.h>
#include <gm/gmdef.h>
#include <mp/map.h>
#include <sys/controller.h>
#include <sys/utils.h>

/* May already be declared in taskman.h for PORT builds; duplicated here so parsers that see a
 * taskman.h variant without PORT-gated prototypes still diagnose calls under #ifdef PORT correctly. */
extern void syTaskmanSetIntervals(u16 update, u16 framedraw);

#ifdef PORT
#include <stdlib.h>
#include <string.h>
#include <sys/netdesyncclassifier.h>
#include <sys/netpeer_frame_commit.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

#include <sc/sccommon/scvsbattle.h>

/*
 * Rollback control: input mismatch detection + resim pacing.
 * Typed world snapshots live in netrollbacksnapshot.c (ring sized by
 * SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES, independent of input history).
 */
static sb32 sSYNetRollbackModuleEnabled;
static sb32 sSYNetRollbackSessionActive;
static u32 sSYNetRollbackResimDepth;
static u32 sSYNetRollbackRollbackCount;
static sb32 sSYNetRollbackMismatchRemoteWithoutPublished;
#ifdef PORT
static u32 sSYNetRollbackInjectTick;
static sb32 sSYNetRollbackInjectConsumed;
static u32 sSYNetRollbackLastVerifyHash;
static sb32 sSYNetRollbackForceMismatch;
static u32 sSYNetRollbackForceMismatchPendingTick;
static sb32 sSYNetRollbackMismatchDebug;
static sb32 sSYNetRollbackVerifyStrict;
static sb32 sSYNetRollbackLoadHashVerify;
static u32 sSYNetRollbackLoadFailCount;
static u32 sMismatchAsymLogsRemaining;
static s32 sSYNetRollbackForceMismatchPlayerSlot;
static u32 sSYNetRollbackResimTicksPerFrame;
static sb32 sSYNetRollbackResimPending;
static u32 sSYNetRollbackResimStallFrames;
static u32 sSYNetRollbackResimMismatchTick;
static u32 sSYNetRollbackResimTargetTick;
static u32 sSYNetRollbackResimNextTick;
static sb32 sSYNetRollbackForceIdentityPending;
static u32 sSYNetRollbackForceIdentityTick;
static u32 sSYNetRollbackPredictionRecoveryUntilSim;
static u32 sSYNetRollbackPredictionRecoveryLogsRemaining;
static sb32 sSYNetRollbackSymmetricEnabled;
static u32 sSYNetRollbackSymmetricNotifyTick[MAXCONTROLLERS];
static u32 sSYNetRollbackSymmetricNotifyTargetTick[MAXCONTROLLERS];
static u32 sSYNetRollbackSymmetricNotifySendCount[MAXCONTROLLERS];
static u32 sSYNetRollbackPeerSymmetricAppliedTick[MAXCONTROLLERS];
static u32 sSYNetRollbackPendingPeerSymmetricTick;
static u32 sSYNetRollbackPendingPeerSymmetricTargetTick;
static s32 sSYNetRollbackPendingPeerSymmetricSlot;
static u32 sSYNetRollbackPeerSymmetricLogsRemaining;
static sb32 sSYNetRollbackResimFromPeerSymmetric;
static sb32 sSYNetRollbackSymmetricDiagOnly;
static sb32 sSYNetRollbackSynctestEnabled;
static u32 sSYNetRollbackSynctestNextProbeTick;
typedef struct SYNetRollbackHashSet
{
	u32 fighter;
	u32 world;
	u32 item;
	u32 weapon;
	u32 map;
	u32 rng;
	u32 camera;
	u32 animation;

} SYNetRollbackHashSet;
static SYNetRollbackHashSet sSYNetRollbackResimPreHashes;
static sb32 sSYNetRollbackResimPreHashesValid;
static u32 sSYNetRollbackResimLoadTick;
static u32 sSYNetRollbackResimOrdinal;
static sb32 sSYNetRollbackPeerBaselineSendPending;
static u32 sSYNetRollbackPeerBaselineLoadTick;
static u32 sSYNetRollbackPeerBaselineFigh;
static u32 sSYNetRollbackPeerBaselineWorld;
static u32 sSYNetRollbackPeerBaselineItem;
static u32 sSYNetRollbackPeerBaselineRng;
static u32 sSYNetRollbackPeerBaselineAnim;
static u32 sSYNetRollbackPeerBaselineWeapon;
static u32 sSYNetRollbackPeerBaselineMap;
static u32 sSYNetRollbackPeerBaselineCamera;
static sb32 sSYNetRollbackPeerSnapshotAbort;
static u32 sSYNetRollbackDebounceUntilSim;
static u32 sSYNetRollbackLastCommittedMismatchTick;
static sb32 sSYNetRollbackDeferredMismatchPending;
static u32 sSYNetRollbackDeferredMismatchTick;
static u32 sSYNetRollbackDeferredMismatchTargetTick;
static s32 sSYNetRollbackDeferredMismatchPlayer;
static sb32 sSYNetRollbackStateHashRollback;
static u32 sSYNetRollbackStateHashLogsRemaining;
static u32 sSYNetRollbackGgpoCorrectionLogsRemaining;
static sb32 sSYNetRollbackDeferredStateMismatchPending;
static u32 sSYNetRollbackDeferredStateMismatchTick;
static u32 sSYNetRollbackDeferredStateMismatchTargetTick;
static sb32 sSYNetRollbackLoadHashDriftSoft;
#endif

#define SYNETROLLBACK_FRAME_COMMIT_STATE_WINDOW 120U

#define SYNETROLLBACK_DEBOUNCE_FRAMES_DEFAULT 3U
#define SYNETROLLBACK_LOAD_HASH_SOFT_ROLLBACK_THRESHOLD 8U

static sb32 syNetRollbackBeginResim(u32 mismatch_tick, u32 target_tick, s32 peer_symmetric_authority_slot);
static void syNetRollbackAdvanceResimBudget(void);
static u32 syNetRollbackClampResimTargetTick(u32 mismatch_tick, u32 target_tick);
static void syNetRollbackQueueDeferredInputCorrection(s32 player, u32 sim_tick);
static void syNetRollbackArmSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackHistRemoteValueMismatch(const SYNetInputFrame *hist, const SYNetInputFrame *remote);
static sb32 syNetRollbackPublishedSimUsedPrediction(const SYNetInputFrame *published);

void syNetRollbackInit(void)
{
#ifdef PORT
	char *env_roll;
	char *env_inj;
	char *env_fm;
	char *env_md;
	char *env_vs;
	char *env_fmp;
	s32 fmp;

	sSYNetRollbackInjectTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = FALSE;
	env_roll = getenv("SSB64_NETPLAY_ROLLBACK");
	sSYNetRollbackModuleEnabled = TRUE;
	if ((env_roll != NULL) && (atoi(env_roll) == 0))
	{
		sSYNetRollbackModuleEnabled = FALSE;
	}
	env_inj = getenv("SSB64_NETPLAY_ROLLBACK_INJECT_TICK");
	if ((env_inj != NULL) && (sSYNetRollbackModuleEnabled != FALSE))
	{
		s32 v = atoi(env_inj);

		if (v >= 0)
		{
			sSYNetRollbackInjectTick = (u32)v;
			port_log("SSB64 NetRollback: debug inject tamper at remote tick %u\n", sSYNetRollbackInjectTick);
		}
	}
	sSYNetRollbackForceMismatch = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	env_fm = getenv("SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH");
	if ((env_fm != NULL) && (atoi(env_fm) != 0))
	{
		sSYNetRollbackForceMismatch = TRUE;
		if ((sSYNetRollbackInjectTick != ~(u32)0) && (sSYNetRollbackModuleEnabled != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH on wire tick %u (after staging: XOR published history if it equals remote)\n",
			    sSYNetRollbackInjectTick);
		}
		else if (sSYNetRollbackModuleEnabled != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH set but SSB64_NETPLAY_ROLLBACK_INJECT_TICK missing — no one-shot scheduled\n");
		}
	}
	sSYNetRollbackMismatchDebug = FALSE;
	env_md = getenv("SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG");
	if ((env_md != NULL) && (atoi(env_md) != 0))
	{
		sSYNetRollbackMismatchDebug = TRUE;
	}
	sSYNetRollbackVerifyStrict = FALSE;
	env_vs = getenv("SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT");
	if ((env_vs != NULL) && (atoi(env_vs) != 0))
	{
		sSYNetRollbackVerifyStrict = TRUE;
	}
	sSYNetRollbackLoadHashVerify = TRUE;
	{
		const char *env_lh;

		env_lh = getenv("SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY");
		if ((env_lh != NULL) && (env_lh[0] != '\0') && (atoi(env_lh) == 0))
		{
			sSYNetRollbackLoadHashVerify = FALSE;
		}
	}
	sSYNetRollbackPeerSnapshotAbort = TRUE;
	{
		const char *env_ps;

		env_ps = getenv("SSB64_NETPLAY_ROLLBACK_PEER_SNAPSHOT_ABORT");
		if ((env_ps != NULL) && (env_ps[0] != '\0') && (atoi(env_ps) == 0))
		{
			sSYNetRollbackPeerSnapshotAbort = FALSE;
		}
	}
	sSYNetRollbackMismatchRemoteWithoutPublished = FALSE;
	{
		const char *env_rp;

		env_rp = getenv("SSB64_NETPLAY_ROLLBACK_MISMATCH_REMOTE_WITHOUT_PUBLISHED");
		if ((env_rp != NULL) && (env_rp[0] != '\0') && (atoi(env_rp) != 0))
		{
			sSYNetRollbackMismatchRemoteWithoutPublished = TRUE;
		}
	}
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackForceMismatchPlayerSlot = -1;
	sSYNetRollbackResimTicksPerFrame = 4U;
	{
		const char *env_rt;

		env_rt = getenv("SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME");
		if ((env_rt != NULL) && (env_rt[0] != '\0'))
		{
			s32 v = atoi(env_rt);

			if (v >= 1)
			{
				sSYNetRollbackResimTicksPerFrame = (u32)v;
			}
		}
		if (sSYNetRollbackResimTicksPerFrame > 32U)
		{
			sSYNetRollbackResimTicksPerFrame = 32U;
		}
	}
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimMismatchTick = ~(u32)0;
	sSYNetRollbackResimTargetTick = ~(u32)0;
	sSYNetRollbackResimNextTick = ~(u32)0;
	sSYNetRollbackForceIdentityPending = FALSE;
	sSYNetRollbackForceIdentityTick = ~(u32)0;
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	sSYNetRollbackPredictionRecoveryLogsRemaining = 8U;
	/* Symmetric peer rollback keeps both sides resimming the same span when multi-tick prediction is on. */
	sSYNetRollbackSymmetricEnabled = FALSE;
	sSYNetRollbackSymmetricDiagOnly = TRUE;
	{
		const char *env_sym;
		const char *env_sym_diag;
		sb32 sym_env_set;

		env_sym = getenv("SSB64_NETPLAY_ROLLBACK_SYMMETRIC");
		sym_env_set = (env_sym != NULL) && (env_sym[0] != '\0');
		if (sym_env_set != FALSE)
		{
			if (atoi(env_sym) != 0)
			{
				sSYNetRollbackSymmetricEnabled = TRUE;
				sSYNetRollbackSymmetricDiagOnly = FALSE;
			}
		}
		else if (syNetPeerGetPhaseLockPredictionWindowTicks() >= 2U)
		{
			sSYNetRollbackSymmetricEnabled = TRUE;
			sSYNetRollbackSymmetricDiagOnly = FALSE;
			port_log(
			    "SSB64 NetRollback: symmetric rollback coupled (PHASE_LOCK_PREDICTION_TICKS=%u; "
			    "SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0 to disable, SYMMETRIC_DIAG=1 for log-only)\n",
			    syNetPeerGetPhaseLockPredictionWindowTicks());
		}
		env_sym_diag = getenv("SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG");
		if ((env_sym_diag != NULL) && (env_sym_diag[0] != '\0') && (atoi(env_sym_diag) != 0))
		{
			sSYNetRollbackSymmetricEnabled = TRUE;
			sSYNetRollbackSymmetricDiagOnly = TRUE;
		}
	}
	sSYNetRollbackSynctestEnabled = FALSE;
	sSYNetRollbackSynctestNextProbeTick = 60U;
	{
		const char *env_st;

		env_st = getenv("SSB64_NETPLAY_ROLLBACK_SYNCTEST");
		if ((env_st != NULL) && (env_st[0] != '\0') && (atoi(env_st) != 0))
		{
			sSYNetRollbackSynctestEnabled = TRUE;
		}
	}
	memset(sSYNetRollbackSymmetricNotifyTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTick));
	memset(sSYNetRollbackSymmetricNotifyTargetTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTargetTick));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackPeerSymmetricAppliedTick, 0, sizeof(sSYNetRollbackPeerSymmetricAppliedTick));
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackPeerSymmetricLogsRemaining = 8U;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	memset(&sSYNetRollbackResimPreHashes, 0, sizeof(sSYNetRollbackResimPreHashes));
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackResimOrdinal = 0;
	sSYNetRollbackDebounceUntilSim = 0U;
	sSYNetRollbackLastCommittedMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPending = FALSE;
	sSYNetRollbackDeferredMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPlayer = -1;
	sSYNetRollbackStateHashRollback = TRUE;
	sSYNetRollbackStateHashLogsRemaining = 16U;
	sSYNetRollbackGgpoCorrectionLogsRemaining = 32U;
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	{
		const char *env_sh;

		env_sh = getenv("SSB64_NETPLAY_ROLLBACK_STATE_HASH");
		if ((env_sh != NULL) && (env_sh[0] != '\0') && (atoi(env_sh) == 0))
		{
			sSYNetRollbackStateHashRollback = FALSE;
		}
	}
	sSYNetRollbackLoadHashDriftSoft = FALSE;
	{
		const char *env_soft;

		env_soft = getenv("SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT");
		if ((env_soft != NULL) && (env_soft[0] != '\0') && (atoi(env_soft) != 0))
		{
			sSYNetRollbackLoadHashDriftSoft = TRUE;
		}
	}
	syNetRbSnapshotInit();
	env_fmp = getenv("SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH_PLAYER");
	if ((env_fmp != NULL) && (env_fmp[0] != '\0'))
	{
		fmp = atoi(env_fmp);
		if ((fmp >= 0) && (fmp < MAXCONTROLLERS))
		{
			sSYNetRollbackForceMismatchPlayerSlot = fmp;
			port_log("SSB64 NetRollback: FORCE_MISMATCH player slot override=%d\n", fmp);
		}
	}
#else
	sSYNetRollbackModuleEnabled = FALSE;
	sSYNetRollbackMismatchRemoteWithoutPublished = FALSE;
#endif
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackLastVerifyHash = 0;
#endif
}

sb32 syNetRollbackIsActive(void)
{
	return (sSYNetRollbackModuleEnabled != FALSE) && (sSYNetRollbackSessionActive != FALSE);
}

sb32 syNetRollbackIsResimulating(void)
{
#ifdef PORT
	if (sSYNetRollbackResimPending != FALSE)
	{
		return TRUE;
	}
#endif
	return sSYNetRollbackResimDepth != 0;
}

void syNetRollbackApplySessionNegotiated(const SYNetSessionParams *params)
{
#ifdef PORT
	const char *env_roll;
	const char *env_sym;
	sb32 sym_env_set;

	if (params == NULL)
	{
		return;
	}
	env_roll = getenv("SSB64_NETPLAY_ROLLBACK");
	if ((env_roll != NULL) && (env_roll[0] != '\0') && (atoi(env_roll) == 0))
	{
		sSYNetRollbackModuleEnabled = FALSE;
	}
	else if ((params->rollback_flags & SYNETSESSION_ROLLBACK_FLAG_ENABLED) != 0U)
	{
		sSYNetRollbackModuleEnabled = TRUE;
	}
	{
		u32 snap_frames;

		snap_frames = (u32)params->rollback_snapshot_frames;
		if (snap_frames < SYNETRB_SNAPSHOT_RING_DEFAULT)
		{
			snap_frames = SYNETRB_SNAPSHOT_RING_DEFAULT;
		}
		syNetRbSnapshotSetRingFramesForSession(snap_frames);
	}
	if (params->rollback_resim_ticks_per_frame >= 1U)
	{
		sSYNetRollbackResimTicksPerFrame = (u32)params->rollback_resim_ticks_per_frame;
	}
	if (sSYNetRollbackResimTicksPerFrame > 32U)
	{
		sSYNetRollbackResimTicksPerFrame = 32U;
	}
	env_sym = getenv("SSB64_NETPLAY_ROLLBACK_SYMMETRIC");
	sym_env_set = (env_sym != NULL) && (env_sym[0] != '\0');
	if (sym_env_set != FALSE)
	{
		sSYNetRollbackSymmetricEnabled = (atoi(env_sym) != 0) ? TRUE : FALSE;
		sSYNetRollbackSymmetricDiagOnly = FALSE;
	}
	else if ((params->rollback_flags & SYNETSESSION_ROLLBACK_FLAG_SYMMETRIC) != 0U)
	{
		sSYNetRollbackSymmetricEnabled = TRUE;
		if (sym_env_set == FALSE)
		{
			sSYNetRollbackSymmetricDiagOnly = FALSE;
		}
	}
	port_log(
	    "SSB64 NetRollback: session_negotiated enabled=%d symmetric=%d snap_frames=%u resim_per_frame=%u "
	    "phase_lock=%u\n",
	    (sSYNetRollbackModuleEnabled != FALSE) ? 1 : 0, (sSYNetRollbackSymmetricEnabled != FALSE) ? 1 : 0,
	    (unsigned int)syNetRbSnapshotRingCapacity(), (unsigned int)sSYNetRollbackResimTicksPerFrame,
	    (unsigned int)syNetPeerGetPhaseLockPredictionWindowTicks());
#else
	(void)params;
#endif
}

void syNetRollbackStartVSSession(void)
{
	if (sSYNetRollbackModuleEnabled == FALSE)
	{
		return;
	}
	syNetRbSnapshotResetSession();
	sSYNetRollbackSessionActive = TRUE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackInjectConsumed = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackForceIdentityPending = FALSE;
	sSYNetRollbackForceIdentityTick = ~(u32)0;
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	sSYNetRollbackPredictionRecoveryLogsRemaining = 8U;
	memset(sSYNetRollbackSymmetricNotifyTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTick));
	memset(sSYNetRollbackSymmetricNotifyTargetTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTargetTick));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackPeerSymmetricAppliedTick, 0, sizeof(sSYNetRollbackPeerSymmetricAppliedTick));
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackResimOrdinal = 0;
	sSYNetRollbackDebounceUntilSim = 0U;
	sSYNetRollbackLastCommittedMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPending = FALSE;
	sSYNetRollbackDeferredMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPlayer = -1;
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackPeerSnapshotAbort = TRUE;
	syNetSyncResetNetplayBattleClock();
	syUtilsResetCosmeticRandomSeed(syUtilsRandSeed());
#endif
}

void syNetRollbackStopVSSession(void)
{
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
#ifdef PORT
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackForceIdentityPending = FALSE;
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	memset(sSYNetRollbackSymmetricNotifyTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTick));
	memset(sSYNetRollbackSymmetricNotifyTargetTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTargetTick));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackPeerSymmetricAppliedTick, 0, sizeof(sSYNetRollbackPeerSymmetricAppliedTick));
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	syNetSyncResetNetplayBattleClock();
#endif
}

#ifdef PORT
#define SYNETROLLBACK_SYMMETRIC_NOTIFY_HOLD_TICKS 32U
#define SYNETROLLBACK_SYMMETRIC_NOTIFY_MIN_SENDS 3U

static sb32 syNetRollbackPlayerIsRemoteHuman(s32 player)
{
	s32 i;
	s32 slot;

	for (i = 0; i < syNetPeerGetRemoteHumanSlotCount(); i++)
	{
		if ((syNetPeerGetRemoteHumanSlotByIndex(i, &slot) != FALSE) && (slot == player))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static u32 syNetRollbackDebounceFrames(void)
{
	const char *env;
	s32 parsed;
	u32 frames;

	frames = SYNETROLLBACK_DEBOUNCE_FRAMES_DEFAULT;
	env = getenv("SSB64_NETPLAY_ROLLBACK_DEBOUNCE_FRAMES");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
		if (parsed >= 0)
		{
			frames = (u32)parsed;
		}
	}
	return frames;
}

static sb32 syNetRollbackLoadHashDriftIsSoft(void)
{
	if (sSYNetRollbackLoadHashDriftSoft != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackRollbackCount >= SYNETROLLBACK_LOAD_HASH_SOFT_ROLLBACK_THRESHOLD)
	{
		return TRUE;
	}
	return FALSE;
}

#ifdef PORT
/*
 * Snapshot load can advance figatree/AObj one frame before verify (status transitions during restore).
 * Fighter/world/RNG/item/wpn/map/cam are authoritative for sim; anim is visual and is rebuilt on resim forward.
 */
static sb32 syNetRollbackLoadHashDriftIsAnimOnly(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp, u32 live_m,
                                                  u32 live_r, u32 live_c, u32 live_a)
{
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)) ||
	    (live_c != syNetRbSnapshotGetSlotHashCamera(tick)))
	{
		return FALSE;
	}
	if (live_a == syNetRbSnapshotGetSlotHashAnimation(tick))
	{
		return FALSE;
	}
	return TRUE;
}
#endif

static void syNetRollbackArmDebounceAfterResim(void)
{
	u32 sim_tick;
	u32 debounce_frames;

	sim_tick = syNetInputGetTick();
	debounce_frames = syNetRollbackDebounceFrames();
	if (debounce_frames == 0U)
	{
		sSYNetRollbackDebounceUntilSim = 0U;
		return;
	}
	if (sim_tick > (~(u32)0) - debounce_frames)
	{
		sSYNetRollbackDebounceUntilSim = ~(u32)0;
	}
	else
	{
		sSYNetRollbackDebounceUntilSim = sim_tick + debounce_frames;
	}
}

static sb32 syNetRollbackMismatchAllowedDuringDebounce(u32 mismatch_tick)
{
	u32 sim_tick;

	if (mismatch_tick == ~(u32)0)
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if (sim_tick >= sSYNetRollbackDebounceUntilSim)
	{
		return TRUE;
	}
	if ((sSYNetRollbackLastCommittedMismatchTick != ~(u32)0) &&
	    (mismatch_tick < sSYNetRollbackLastCommittedMismatchTick))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRollbackQueueDeferredInputCorrection(s32 player, u32 sim_tick)
{
	u32 frontier;
	u32 target_tick;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if (sim_tick >= frontier)
	{
		return;
	}
	target_tick = frontier;
	if (target_tick <= sim_tick)
	{
		target_tick = sim_tick + 1U;
	}
	target_tick = syNetRollbackClampResimTargetTick(sim_tick, target_tick);
	if (sSYNetRollbackDeferredMismatchPending != FALSE)
	{
		if (sim_tick >= sSYNetRollbackDeferredMismatchTick)
		{
			if (target_tick > sSYNetRollbackDeferredMismatchTargetTick)
			{
				sSYNetRollbackDeferredMismatchTargetTick = target_tick;
			}
			return;
		}
		if (target_tick > sSYNetRollbackDeferredMismatchTargetTick)
		{
			sSYNetRollbackDeferredMismatchTargetTick = target_tick;
		}
		sSYNetRollbackDeferredMismatchTick = sim_tick;
		sSYNetRollbackDeferredMismatchPlayer = player;
		return;
	}
	sSYNetRollbackDeferredMismatchPending = TRUE;
	sSYNetRollbackDeferredMismatchTick = sim_tick;
	sSYNetRollbackDeferredMismatchTargetTick = target_tick;
	sSYNetRollbackDeferredMismatchPlayer = player;
}

void syNetRollbackRequestInputCorrection(s32 player, u32 sim_tick)
{
	SYNetInputFrame published;
	SYNetInputFrame remote;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	if ((syNetInputGetHistoryFrame(player, sim_tick, &published) == FALSE) ||
	    (syNetInputGetRemoteHistoryFrame(player, sim_tick, &remote) == FALSE))
	{
		return;
	}
	if (syNetRollbackHistRemoteValueMismatch(&published, &remote) == FALSE)
	{
		return;
	}
	if (syNetRollbackPublishedSimUsedPrediction(&published) == FALSE)
	{
		return;
	}
	if (sSYNetRollbackGgpoCorrectionLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: GGPO input correction queued player=%d sim_tick=%u frontier=%u published btn=0x%04X sx=%d sy=%d pred=%u | remote btn=0x%04X sx=%d sy=%d\n",
		    (int)player,
		    sim_tick,
		    syNetInputGetTick(),
		    (unsigned int)published.buttons,
		    published.stick_x,
		    published.stick_y,
		    (unsigned int)published.is_predicted,
		    (unsigned int)remote.buttons,
		    remote.stick_x,
		    remote.stick_y);
		sSYNetRollbackGgpoCorrectionLogsRemaining--;
	}
	syNetInputTimelineNotePublishedRemoteMismatch(player, sim_tick);
	syNetRollbackQueueDeferredInputCorrection(player, sim_tick);
}

void syNetRollbackDeferRemoteInputCorrection(s32 player, u32 sim_tick)
{
	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U) || (sSYNetRollbackResimPending == FALSE))
	{
		return;
	}
	if ((sim_tick < sSYNetRollbackResimMismatchTick) || (sim_tick >= sSYNetRollbackResimTargetTick))
	{
		return;
	}
	syNetRollbackQueueDeferredInputCorrection(player, sim_tick);
}

static sb32 syNetRollbackTryBeginDeferredMismatch(void)
{
	u32 mismatch;
	u32 target;
	s32 player;

	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return FALSE;
	}
	mismatch = sSYNetRollbackDeferredMismatchTick;
	target = sSYNetRollbackDeferredMismatchTargetTick;
	player = sSYNetRollbackDeferredMismatchPlayer;
	if ((mismatch == ~(u32)0) || (target <= mismatch))
	{
		sSYNetRollbackDeferredMismatchPending = FALSE;
		sSYNetRollbackDeferredMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredMismatchTargetTick = ~(u32)0;
		sSYNetRollbackDeferredMismatchPlayer = -1;
		return FALSE;
	}
	if (syNetRollbackMismatchAllowedDuringDebounce(mismatch) == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackDeferredMismatchPending = FALSE;
	sSYNetRollbackDeferredMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPlayer = -1;
	target = syNetRollbackClampResimTargetTick(mismatch, target);
	port_log(
	    "SSB64 NetRollback: GGPO deferred input correction resim mismatch_tick=%u target_tick=%u slot=%d\n",
	    mismatch,
	    target,
	    (int)player);
	if ((player >= 0) && (sSYNetRollbackSymmetricEnabled != FALSE))
	{
		syNetRollbackArmSymmetricNotify(player, mismatch, target);
	}
	/* Remote slot correction: peer symmetric notify carries `player`; resim uses remote-confirmed reconcile only. */
	if (syNetRollbackBeginResim(mismatch, target, -1) == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackRollbackCount++;
	sSYNetRollbackResimOrdinal = sSYNetRollbackRollbackCount;
	sSYNetRollbackLastCommittedMismatchTick = mismatch;
	syNetRollbackAdvanceResimBudget();
	return TRUE;
}

static sb32 syNetRollbackTryBeginDeferredStateMismatch(void)
{
	u32 mismatch;
	u32 target;

	if (sSYNetRollbackDeferredStateMismatchPending == FALSE)
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return FALSE;
	}
	mismatch = sSYNetRollbackDeferredStateMismatchTick;
	target = sSYNetRollbackDeferredStateMismatchTargetTick;
	if ((mismatch == ~(u32)0) || (target <= mismatch))
	{
		sSYNetRollbackDeferredStateMismatchPending = FALSE;
		sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
		return FALSE;
	}
	if (syNetRollbackMismatchAllowedDuringDebounce(mismatch) == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	target = syNetRollbackClampResimTargetTick(mismatch, target);
	port_log("SSB64 NetRollback: deferred frame-commit state resim mismatch_tick=%u target_tick=%u\n", mismatch,
	         target);
	if (syNetRollbackBeginResim(mismatch, target, -1) == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackRollbackCount++;
	sSYNetRollbackResimOrdinal = sSYNetRollbackRollbackCount;
	sSYNetRollbackLastCommittedMismatchTick = mismatch;
	syNetRollbackAdvanceResimBudget();
	return TRUE;
}

void syNetRollbackOnPeerFrameCommitStateMismatch(u32 validation_tick, const SYNetFrameCommitToken *local,
						 const SYNetFrameCommitToken *peer)
{
	u32 mismatch_tick;
	u32 frontier;
	u32 target_tick;

	if ((local == NULL) || (peer == NULL))
	{
		return;
	}
	if ((syNetRollbackIsActive() == FALSE) || (sSYNetRollbackStateHashRollback == FALSE))
	{
		return;
	}
	if (syNetFrameCommitStateDigestsDiverge(local, peer) == FALSE)
	{
		return;
	}
	if (sSYNetRollbackStateHashLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: FRAME_COMMIT_STATE_DIVERGE validation=%u local figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X | peer figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X inp_local=0x%08X inp_peer=0x%08X\n",
		    validation_tick,
		    local->fighter_digest,
		    local->world_digest,
		    local->item_digest,
		    local->rng_digest,
		    peer->fighter_digest,
		    peer->world_digest,
		    peer->item_digest,
		    peer->rng_digest,
		    local->input_digest,
		    peer->input_digest);
		sSYNetRollbackStateHashLogsRemaining--;
	}
	if (validation_tick >= SYNETROLLBACK_FRAME_COMMIT_STATE_WINDOW)
	{
		mismatch_tick = validation_tick - SYNETROLLBACK_FRAME_COMMIT_STATE_WINDOW;
	}
	else
	{
		mismatch_tick = 0U;
	}
	if (mismatch_tick == 0U)
	{
		mismatch_tick = 1U;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	target_tick = frontier;
	if (target_tick <= mismatch_tick)
	{
		target_tick = mismatch_tick + 1U;
	}
	if ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	    (mismatch_tick >= sSYNetRollbackDeferredStateMismatchTick))
	{
		return;
	}
	sSYNetRollbackDeferredStateMismatchPending = TRUE;
	sSYNetRollbackDeferredStateMismatchTick = mismatch_tick;
	sSYNetRollbackDeferredStateMismatchTargetTick = target_tick;
}

static void syNetRollbackArmSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick)
{
	u32 active_tick;

	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (sSYNetRollbackResimFromPeerSymmetric != FALSE))
	{
		return;
	}
	if (syNetRollbackPlayerIsRemoteHuman(slot) == FALSE)
	{
		return;
	}
	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (mismatch_tick == 0U) || (target_tick <= mismatch_tick))
	{
		return;
	}
	active_tick = sSYNetRollbackSymmetricNotifyTick[slot];
	if ((active_tick == 0U) || (mismatch_tick < active_tick) ||
	    ((mismatch_tick > active_tick) &&
	     (sSYNetRollbackSymmetricNotifySendCount[slot] >= SYNETROLLBACK_SYMMETRIC_NOTIFY_MIN_SENDS)))
	{
		sSYNetRollbackSymmetricNotifyTick[slot] = mismatch_tick;
		sSYNetRollbackSymmetricNotifyTargetTick[slot] = target_tick;
		sSYNetRollbackSymmetricNotifySendCount[slot] = 0U;
	}
}

static void syNetRollbackExpireSymmetricNotify(void)
{
	u32 sim_tick;
	s32 slot;

	sim_tick = syNetInputGetTick();
	for (slot = 0; slot < MAXCONTROLLERS; slot++)
	{
		u32 notify_tick;

		notify_tick = sSYNetRollbackSymmetricNotifyTick[slot];
		if (notify_tick == 0U)
		{
			continue;
		}
		if (sim_tick > notify_tick + SYNETROLLBACK_SYMMETRIC_NOTIFY_HOLD_TICKS)
		{
			sSYNetRollbackSymmetricNotifyTick[slot] = 0U;
			sSYNetRollbackSymmetricNotifyTargetTick[slot] = 0U;
			sSYNetRollbackSymmetricNotifySendCount[slot] = 0U;
		}
	}
}

void syNetRollbackExportPeerSymmetricNotify(s32 *out_tick_per_slot, s32 *out_target_tick_per_slot, s32 count)
{
	s32 i;
	s32 n;

	if (out_tick_per_slot == NULL)
	{
		return;
	}
	n = count;
	if (n <= 0)
	{
		return;
	}
	if (n > MAXCONTROLLERS)
	{
		n = MAXCONTROLLERS;
	}
	syNetRollbackExpireSymmetricNotify();
	for (i = 0; i < n; i++)
	{
		if (sSYNetRollbackSymmetricNotifyTick[i] != 0U)
		{
			out_tick_per_slot[i] = (s32)sSYNetRollbackSymmetricNotifyTick[i];
			if (out_target_tick_per_slot != NULL)
			{
				out_target_tick_per_slot[i] = (s32)sSYNetRollbackSymmetricNotifyTargetTick[i];
			}
			if (sSYNetRollbackSymmetricNotifySendCount[i] != ~(u32)0)
			{
				sSYNetRollbackSymmetricNotifySendCount[i]++;
			}
		}
		else
		{
			out_tick_per_slot[i] = -1;
			if (out_target_tick_per_slot != NULL)
			{
				out_target_tick_per_slot[i] = -1;
			}
		}
	}
}

void syNetRollbackOnPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick)
{
	u32 min_target_tick;

	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (syNetRollbackIsActive() == FALSE))
	{
		return;
	}
	if (sSYNetRollbackSymmetricDiagOnly != FALSE)
	{
		if (sSYNetRollbackPeerSymmetricLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: peer symmetric notice (diag-only) slot=%d mismatch_tick=%u target_tick=%u sim=%u\n",
			    (int)slot,
			    mismatch_tick,
			    target_tick,
			    syNetInputGetTick());
			sSYNetRollbackPeerSymmetricLogsRemaining--;
		}
		return;
	}
	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (mismatch_tick == 0U))
	{
		return;
	}
	if ((sSYNetRollbackPeerSymmetricAppliedTick[slot] != 0U) &&
	    (mismatch_tick <= sSYNetRollbackPeerSymmetricAppliedTick[slot]))
	{
		return;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick >= sSYNetRollbackPendingPeerSymmetricTick))
	{
		return;
	}
	min_target_tick = ((~(u32)0 - mismatch_tick) >= 2U) ? (mismatch_tick + 2U) : ~(u32)0;
	if ((target_tick == 0U) || (target_tick <= mismatch_tick))
	{
		target_tick = min_target_tick;
	}
	else if (target_tick < min_target_tick)
	{
		target_tick = min_target_tick;
	}
	sSYNetRollbackPendingPeerSymmetricTick = mismatch_tick;
	sSYNetRollbackPendingPeerSymmetricTargetTick = target_tick;
	sSYNetRollbackPendingPeerSymmetricSlot = slot;
	if (sSYNetRollbackPeerSymmetricLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback queued slot=%d mismatch_tick=%u target_tick=%u sim=%u\n",
		    (int)slot,
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
		sSYNetRollbackPeerSymmetricLogsRemaining--;
	}
}
#endif

static sb32 syNetRollbackSavePostTick(u32 tick)
{
	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	return syNetRbSnapshotSave(tick);
}

#ifdef PORT
static SYNetRollbackHashSet syNetRollbackCollectHashes(void)
{
	SYNetRollbackHashSet hashes;

	hashes.fighter = syNetSyncHashBattleFightersFull();
	hashes.world = syNetSyncHashRollbackWorld();
	hashes.item = syNetSyncHashActiveItemsForRollback();
	hashes.weapon = syNetSyncHashActiveWeaponsForRollback();
	hashes.map = syNetSyncHashMapCollisionKinematics();
	hashes.rng = syNetSyncHashRNGSeed();
	hashes.camera = syNetSyncHashGMCamera();
	hashes.animation = syNetSyncHashFighterAnimationStateForRollback();
	return hashes;
}

static sb32 syNetRollbackHashesEqual(const SYNetRollbackHashSet *a, const SYNetRollbackHashSet *b)
{
	return ((a->fighter == b->fighter) && (a->world == b->world) && (a->item == b->item) &&
	        (a->weapon == b->weapon) && (a->map == b->map) && (a->rng == b->rng) && (a->camera == b->camera) &&
	        (a->animation == b->animation))
	           ? TRUE
	           : FALSE;
}
#endif

static sb32 syNetRollbackVerifyLoadedSlot(u32 tick)
{
#ifdef PORT
	u32 live_f;
	u32 live_w;
	u32 live_i;
	u32 live_wp;
	u32 live_m;
	u32 live_r;
	u32 live_c;
	u32 live_a;

	if (sSYNetRollbackLoadHashVerify == FALSE)
	{
		return TRUE;
	}
	live_f = syNetSyncHashBattleFightersFull();
	live_w = syNetSyncHashRollbackWorld();
	live_i = syNetSyncHashActiveItemsForRollback();
	live_wp = syNetSyncHashActiveWeaponsForRollback();
	live_m = syNetSyncHashMapCollisionKinematics();
	live_r = syNetSyncHashRNGSeed();
	live_c = syNetSyncHashGMCamera();
	live_a = syNetSyncHashFighterAnimationStateForRollback();
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)) ||
	    (live_c != syNetRbSnapshotGetSlotHashCamera(tick)) || (live_a != syNetRbSnapshotGetSlotHashAnimation(tick)))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT tick=%u figh=0x%08X/0x%08X world=0x%08X/0x%08X item=0x%08X/0x%08X "
		    "wpn=0x%08X/0x%08X map=0x%08X/0x%08X rng=0x%08X/0x%08X cam=0x%08X/0x%08X anim=0x%08X/0x%08X\n",
		    tick,
		    syNetRbSnapshotGetSlotHashFighter(tick),
		    live_f,
		    syNetRbSnapshotGetSlotHashWorld(tick),
		    live_w,
		    syNetRbSnapshotGetSlotHashItem(tick),
		    live_i,
		    syNetRbSnapshotGetSlotHashWeapon(tick),
		    live_wp,
		    syNetRbSnapshotGetSlotHashMap(tick),
		    live_m,
		    syNetRbSnapshotGetSlotHashRng(tick),
		    live_r,
		    syNetRbSnapshotGetSlotHashCamera(tick),
		    live_c,
		    syNetRbSnapshotGetSlotHashAnimation(tick),
		    live_a);
		syNetDesyncClassifierOnLoadHashDrift(tick);
		if (syNetRollbackLoadHashDriftIsAnimOnly(tick, live_f, live_w, live_i, live_wp, live_m, live_r, live_c,
		                                        live_a) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT anim-only — continuing resim tick=%u snap=0x%08X live=0x%08X\n",
			    tick,
			    syNetRbSnapshotGetSlotHashAnimation(tick),
			    live_a);
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsSoft() != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-continue tick=%u rollbacks=%u (set "
			    "SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=0 to hard-abort)\n",
			    tick,
			    sSYNetRollbackRollbackCount);
			return TRUE;
		}
		return FALSE;
	}
	return TRUE;
#else
	(void)tick;
	return TRUE;
#endif
}

static sb32 syNetRollbackLoadPostTick(u32 tick)
{
#ifdef PORT
	sb32 emergency_valid;
#endif

#ifdef PORT
	emergency_valid = syNetRbSnapshotCaptureLiveEmergency();
#endif
	if (syNetRbSnapshotLoad(tick) == FALSE)
	{
#ifdef PORT
		if (emergency_valid != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
#endif
		return FALSE;
	}
#ifdef PORT
	syNetSyncReconcileBattleTimePassedForSimTick(tick);
#endif
	if (syNetRollbackVerifyLoadedSlot(tick) == FALSE)
	{
#ifdef PORT
		sSYNetRollbackLoadFailCount++;
		if (syNetRollbackLoadHashDriftIsSoft() != FALSE)
		{
			port_log("SSB64 NetRollback: LOAD_HASH_DRIFT soft — continuing resim without session stop (tick %u)\n",
			         tick);
			return TRUE;
		}
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT — restoring live world and stopping VS session (tick %u)\n", tick);
		if (emergency_valid != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetRollbackStopVSSession();
		syNetPeerStopVSSession();
#endif
		return FALSE;
	}
	return TRUE;
}

#ifdef PORT
sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick)
{
	return syNetRollbackLoadPostTick(completed_sim_tick);
}

sb32 syNetRollbackPredictionRecoveryRequiresConfirmed(u32 sim_tick)
{
	if ((syNetRollbackIsActive() == FALSE) || (sSYNetRollbackPredictionRecoveryUntilSim == 0U))
	{
		return FALSE;
	}
	if (sim_tick < sSYNetRollbackPredictionRecoveryUntilSim)
	{
		return TRUE;
	}
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	return FALSE;
}
#endif

/* Once per frame after battle sim: persist the world that finished `syNetInputGetTick()` (completed sim index). */
void syNetRollbackAfterBattleUpdate(void)
{
	u32 completed_tick;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	completed_tick = syNetInputGetTick();
	syNetRollbackSavePostTick(completed_tick);
#ifdef PORT
	if ((sSYNetRollbackSynctestEnabled != FALSE) && (completed_tick >= sSYNetRollbackSynctestNextProbeTick) &&
	    (completed_tick > 0U))
	{
		u32 probe_tick;
		sb32 emergency_ok;
		sb32 verify_ok;

		probe_tick = completed_tick - 1U;
		emergency_ok = syNetRbSnapshotCaptureLiveEmergency();
		verify_ok = FALSE;
		if ((emergency_ok != FALSE) && (syNetRbSnapshotLoad(probe_tick) != FALSE))
		{
			verify_ok = syNetRollbackVerifyLoadedSlot(probe_tick);
		}
		if (emergency_ok != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		if (verify_ok == FALSE)
		{
			port_log("SSB64 NetRollback: SYNCTEST_FAIL tick=%u\n", probe_tick);
		}
		else
		{
			port_log("SSB64 NetRollback: SYNCTEST_OK tick=%u\n", probe_tick);
		}
		sSYNetRollbackSynctestNextProbeTick = completed_tick + 120U;
	}
#endif
}

/*
 * Input mismatch search uses **netinput sim tick** indices only (`syNetInputGetHistoryFrame` / `GetRemoteHistoryFrame`),
 * not taskman update/frame counts. `frontier_tick` is exclusive: `syNetInputGetTick() + 1` after the latest completed sim step
 * (`syNetRollbackUpdate`), so tick `frontier_tick - 1` is the newest completed index included in the scan.
 *
 * Compares gameplay fields only (tick, buttons, sticks) when both rows exist — same contract as
 * `syNetInputGetHistoryInputChecksum` / replay validation. `source`, `is_predicted`, and `is_valid` are
 * diagnostic metadata; predicted-vs-confirmed tags must not trigger resim when buttons/sticks match.
 * Treats **remote ring present without published history** as a mismatch (wire ahead of local commit).
 */
/* TRUE when both rows exist and disagree on gameplay input used during sim. */
static sb32 syNetRollbackHistRemoteValueMismatch(const SYNetInputFrame *hist, const SYNetInputFrame *remote)
{
	if ((hist->tick != remote->tick) || (hist->buttons != remote->buttons) || (hist->stick_x != remote->stick_x) ||
	    (hist->stick_y != remote->stick_y))
	{
		return TRUE;
	}
	return FALSE;
}

/* TRUE when published history for a completed sim tick reflects speculative remote input. */
static sb32 syNetRollbackPublishedSimUsedPrediction(const SYNetInputFrame *published)
{
	if (published == NULL)
	{
		return FALSE;
	}
	if (published->is_predicted != FALSE)
	{
		return TRUE;
	}
	if (published->source == nSYNetInputSourceRemotePredicted)
	{
		return TRUE;
	}
	return FALSE;
}

#ifdef PORT
static void syNetRollbackArmPredictionRecovery(u32 mismatch_tick, u32 frontier_tick, const SYNetInputFrame *hist)
{
	u32 prediction_window;
	u32 until_sim;

	if ((hist == NULL) || (hist->is_predicted == FALSE))
	{
		return;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window == 0U)
	{
		return;
	}
	if (frontier_tick > (~(u32)0 - prediction_window))
	{
		until_sim = ~(u32)0;
	}
	else until_sim = frontier_tick + prediction_window;
	if (until_sim > sSYNetRollbackPredictionRecoveryUntilSim)
	{
		sSYNetRollbackPredictionRecoveryUntilSim = until_sim;
		if (sSYNetRollbackPredictionRecoveryLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: prediction recovery armed mismatch=%u frontier=%u require_confirmed_until_sim=%u "
			    "window=%u\n",
			    mismatch_tick,
			    frontier_tick,
			    until_sim,
			    prediction_window);
			sSYNetRollbackPredictionRecoveryLogsRemaining--;
		}
	}
}
#endif

/* Walk backward from `frontier_tick` up to `SYNETROLLBACK_SCAN_WINDOW` comparing published vs remote history. */
static u32 syNetRollbackFindEarliestInputMismatch(u32 frontier_tick, s32 *out_mismatch_player)
{
	SYNetInputFrame hist;
	SYNetInputFrame remote;
	u32 begin;
	u32 t;
	s32 ri;
	s32 remote_player;

	if (out_mismatch_player != NULL)
	{
		*out_mismatch_player = -1;
	}
	if (frontier_tick == 0)
	{
		return ~(u32)0;
	}
#ifdef PORT
	{
		u32 timeline_tick;
		s32 timeline_player;

		timeline_tick = syNetInputTimelineFindEarliestValidatedMismatch(frontier_tick, &timeline_player);
		if (timeline_tick != ~(u32)0)
		{
			if (out_mismatch_player != NULL)
			{
				*out_mismatch_player = timeline_player;
			}
			return timeline_tick;
		}
	}
#endif
	begin = 0;
	if (frontier_tick > SYNETROLLBACK_SCAN_WINDOW)
	{
		begin = frontier_tick - SYNETROLLBACK_SCAN_WINDOW;
	}
	for (t = begin; t < frontier_tick; t++)
	{
		for (ri = 0; ri < syNetPeerGetRemoteHumanSlotCount(); ri++)
		{
			sb32 has_hist;
			sb32 has_remote;

			if (syNetPeerGetRemoteHumanSlotByIndex(ri, &remote_player) == FALSE)
			{
				continue;
			}
			if ((remote_player < 0) || (remote_player >= MAXCONTROLLERS))
			{
				continue;
			}
			has_hist = syNetInputGetHistoryFrame(remote_player, t, &hist);
			has_remote = syNetInputGetRemoteHistoryFrame(remote_player, t, &remote);
#ifdef PORT
			if ((sSYNetRollbackMismatchDebug != FALSE) && (has_hist != has_remote))
			{
				if (sMismatchAsymLogsRemaining > 0U)
				{
					port_log(
					    "SSB64 NetRollback: MISMATCH_DEBUG asym tick=%u slot=%d hist=%d remote=%d frontier=%u\n",
					    t,
					    remote_player,
					    (has_hist != FALSE) ? 1 : 0,
					    (has_remote != FALSE) ? 1 : 0,
					    frontier_tick);
					sMismatchAsymLogsRemaining--;
				}
			}
#endif
			if (has_hist == FALSE)
			{
				if ((has_remote != FALSE) && (sSYNetRollbackMismatchRemoteWithoutPublished != FALSE))
				{
					if (out_mismatch_player != NULL)
					{
						*out_mismatch_player = remote_player;
					}
					return t;
				}
				continue;
			}
			if (has_remote == FALSE)
			{
				continue;
			}
			if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) != FALSE)
			{
#ifdef PORT
				syNetInputTimelineNotePublishedRemoteMismatch(remote_player, t);
#endif
				if (out_mismatch_player != NULL)
				{
					*out_mismatch_player = remote_player;
				}
				return t;
			}
		}
	}
	return ~(u32)0;
}

#ifdef PORT
static s32 syNetRollbackResolveForceMismatchTargetPlayer(void)
{
	s32 want;
	s32 i;
	s32 slot;

	want = sSYNetRollbackForceMismatchPlayerSlot;
	if (want >= 0)
	{
		for (i = 0; i < syNetPeerGetRemoteHumanSlotCount(); i++)
		{
			if ((syNetPeerGetRemoteHumanSlotByIndex(i, &slot) != FALSE) && (slot == want))
			{
				return want;
			}
		}
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH_PLAYER=%d not in remote receive slot list; using first remote slot\n",
		    want);
	}
	if (syNetPeerGetRemoteHumanSlotByIndex(0, &slot) != FALSE)
	{
		return slot;
	}
	return -1;
}

static void syNetRollbackDebugTryApplyPendingForceMismatch(void)
{
	s32 player;
	u32 tick;
	u32 frontier;
	SYNetInputFrame hist;
	SYNetInputFrame remote;

	if (sSYNetRollbackForceMismatch == FALSE)
	{
		return;
	}
	if (sSYNetRollbackForceMismatchPendingTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetRollbackInjectConsumed != FALSE)
	{
		return;
	}
	player = syNetRollbackResolveForceMismatchTargetPlayer();
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return;
	}
	tick = sSYNetRollbackForceMismatchPendingTick;
	frontier = syNetInputGetTick();

	if (frontier <= tick)
	{
		return;
	}
	if (syNetInputGetHistoryFrame(player, tick, &hist) == FALSE)
	{
		if (frontier > tick + (u32)SYNETINPUT_HISTORY_LENGTH)
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH gave up: no published history at tick %u (frontier=%u)\n",
			    tick,
			    frontier);
			sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
			sSYNetRollbackInjectConsumed = TRUE;
		}
		return;
	}
	if (syNetInputGetRemoteHistoryFrame(player, tick, &remote) == FALSE)
	{
		port_log("SSB64 NetRollback: FORCE_MISMATCH gave up: no remote history at tick %u\n", tick);
		sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
		sSYNetRollbackInjectConsumed = TRUE;
		return;
	}
	if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH detected published history already differs from remote at tick %u (no XOR)\n",
		    tick);
		sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
		sSYNetRollbackInjectConsumed = TRUE;
		return;
	}

	port_log(
	    "SSB64 NetRollback: FORCE_MISMATCH detected published history == remote at tick %u; XOR 0x1000 into published history only\n",
	    tick);
	syNetInputDebugXorPublishedHistoryButtons(player, tick, 0x1000);
	sSYNetRollbackForceIdentityPending = TRUE;
	sSYNetRollbackForceIdentityTick = tick;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = TRUE;
}
#endif

#ifdef PORT
static void syNetRollbackArmResimBaselineAfterLoad(u32 load_tick)
{
	SYNetRollbackHashSet live;

	live = syNetRollbackCollectHashes();
	sSYNetRollbackResimLoadTick = load_tick;
	sSYNetRollbackResimPreHashes = live;
	sSYNetRollbackResimPreHashesValid = TRUE;
	sSYNetRollbackPeerBaselineSendPending = TRUE;
	sSYNetRollbackPeerBaselineLoadTick = load_tick;
	sSYNetRollbackPeerBaselineFigh = live.fighter;
	sSYNetRollbackPeerBaselineWorld = live.world;
	sSYNetRollbackPeerBaselineItem = live.item;
	sSYNetRollbackPeerBaselineRng = live.rng;
	sSYNetRollbackPeerBaselineAnim = live.animation;
	sSYNetRollbackPeerBaselineWeapon = live.weapon;
	sSYNetRollbackPeerBaselineMap = live.map;
	sSYNetRollbackPeerBaselineCamera = live.camera;
	port_log(
	    "SSB64 NetRollback: resim baseline (post-load tick=%u) figh=0x%08X world=0x%08X item=0x%08X wpn=0x%08X mph=0x%08X rng=0x%08X cam=0x%08X anim=0x%08X slot_figh=0x%08X slot_world=0x%08X\n",
	    load_tick,
	    live.fighter,
	    live.world,
	    live.item,
	    live.weapon,
	    live.map,
	    live.rng,
	    live.camera,
	    live.animation,
	    syNetRbSnapshotGetSlotHashFighter(load_tick),
	    syNetRbSnapshotGetSlotHashWorld(load_tick));
	syNetPeerTrySendRollbackBaselineDigest();
}

static u32 syNetRollbackClampResimTargetTick(u32 mismatch_tick, u32 target_tick)
{
	u32 hr;
	u32 bound;
	u32 min_target;

	min_target = mismatch_tick + 2U;
	hr = syNetPeerGetHighestRemoteTick();
	if (hr > 0U)
	{
		bound = hr + syNetPeerGetCommittedInputDelay() + 1U;
		if (target_tick > bound)
		{
			target_tick = bound;
		}
	}
	if (target_tick < min_target)
	{
		target_tick = min_target;
	}
	return target_tick;
}

static void syNetRollbackFailPeerSnapshotDiverge(u32 load_tick, const SYNetRollbackHashSet *peer,
						 const SYNetRollbackHashSet *local)
{
	port_log(
	    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE load_tick=%u peer figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X | local figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X\n",
	    load_tick,
	    peer->fighter,
	    peer->world,
	    peer->item,
	    peer->rng,
	    peer->animation,
	    local->fighter,
	    local->world,
	    local->item,
	    local->rng,
	    local->animation);
	syNetDesyncClassifierOnPeerSnapshotDiverge(load_tick);
	if (sSYNetRollbackPeerSnapshotAbort != FALSE)
	{
		port_log("SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE — stopping VS session (load_tick %u)\n", load_tick);
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetRollbackStopVSSession();
		syNetPeerStopVSSession();
	}
}

void syNetRollbackOnPeerBaselineDigest(u32 load_tick, u32 figh, u32 world, u32 item, u32 rng, u32 anim, u32 weapon,
				     u32 map, u32 camera)
{
	SYNetRollbackHashSet peer;
	SYNetRollbackHashSet local;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	peer.fighter = figh;
	peer.world = world;
	peer.item = item;
	peer.rng = rng;
	peer.animation = anim;
	peer.weapon = weapon;
	peer.map = map;
	peer.camera = camera;
	if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &local.fighter, &local.world, &local.item, &local.rng) ==
	    FALSE)
	{
		local = syNetRollbackCollectHashes();
	}
	else
	{
		local.weapon = syNetRbSnapshotGetSlotHashWeapon(load_tick);
		local.map = syNetRbSnapshotGetSlotHashMap(load_tick);
		local.camera = syNetRbSnapshotGetSlotHashCamera(load_tick);
		local.animation = syNetRbSnapshotGetSlotHashAnimation(load_tick);
	}
	if ((peer.fighter != local.fighter) || (peer.world != local.world) || (peer.item != local.item) ||
	    (peer.rng != local.rng) || (peer.animation != local.animation) || (peer.weapon != local.weapon) ||
	    (peer.map != local.map) || (peer.camera != local.camera))
	{
		syNetRollbackFailPeerSnapshotDiverge(load_tick, &peer, &local);
	}
}

void syNetRollbackNotePeerBaselineDigestSent(void)
{
	sSYNetRollbackPeerBaselineSendPending = FALSE;
}

sb32 syNetRollbackTakePeerBaselineDigestForSend(u32 *out_load_tick, u32 *out_figh, u32 *out_world, u32 *out_item,
					      u32 *out_rng, u32 *out_anim, u32 *out_weapon, u32 *out_map,
					      u32 *out_camera)
{
	if ((sSYNetRollbackPeerBaselineSendPending == FALSE) || (out_load_tick == NULL) || (out_figh == NULL) ||
	    (out_world == NULL) || (out_item == NULL) || (out_rng == NULL) || (out_anim == NULL) ||
	    (out_weapon == NULL) || (out_map == NULL) || (out_camera == NULL))
	{
		return FALSE;
	}
	*out_load_tick = sSYNetRollbackPeerBaselineLoadTick;
	*out_figh = sSYNetRollbackPeerBaselineFigh;
	*out_world = sSYNetRollbackPeerBaselineWorld;
	*out_item = sSYNetRollbackPeerBaselineItem;
	*out_rng = sSYNetRollbackPeerBaselineRng;
	*out_anim = sSYNetRollbackPeerBaselineAnim;
	*out_weapon = sSYNetRollbackPeerBaselineWeapon;
	*out_map = sSYNetRollbackPeerBaselineMap;
	*out_camera = sSYNetRollbackPeerBaselineCamera;
	return TRUE;
}
#endif

static sb32 syNetRollbackBeginResim(u32 mismatch_tick, u32 target_tick, s32 peer_symmetric_authority_slot)
{
	u32 load_tick;

	if ((mismatch_tick >= target_tick) || (mismatch_tick == 0))
	{
		return FALSE;
	}
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	load_tick = mismatch_tick - 1U;
	if (syNetRollbackLoadPostTick(load_tick) == FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetRollback: load post tick %u failed (need earlier snapshots; ring=%u scan=%u)\n",
		    load_tick,
		    (unsigned int)syNetRbSnapshotRingCapacity(),
		    (unsigned int)SYNETROLLBACK_SCAN_WINDOW);
		sSYNetRollbackLoadFailCount++;
#endif
		return FALSE;
	}
#ifdef PORT
	syNetSyncLogRollbackWorldDetail("rollback_load", load_tick);
	syNetSyncLogFighterDetail("rollback_load", load_tick);
	syNetRollbackArmResimBaselineAfterLoad(load_tick);
#endif
	syNetInputRollbackPrepareForResim(mismatch_tick);
#ifdef PORT
	syNetInputRollbackReconcilePublishedFromRemote(mismatch_tick, target_tick);
	if (peer_symmetric_authority_slot >= 0)
	{
		syNetInputRollbackReconcilePeerSymmetricAuthority(peer_symmetric_authority_slot, mismatch_tick,
								  target_tick);
	}
	port_log(
	    "SSB64 NetRollback: resim begin mismatch_tick=%u load_tick=%u target_tick=%u span=%u budget=%u/frame\n",
	    mismatch_tick,
	    load_tick,
	    target_tick,
	    (unsigned int)(target_tick - mismatch_tick),
	    (unsigned int)sSYNetRollbackResimTicksPerFrame);
	sSYNetRollbackResimPending = TRUE;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimMismatchTick = mismatch_tick;
	sSYNetRollbackResimTargetTick = target_tick;
	sSYNetRollbackResimNextTick = mismatch_tick;
	sSYNetRollbackResimDepth = 1U;
#endif
	return TRUE;
}

#ifdef PORT
static void syNetRollbackLogResimTickTrace(u32 tick)
{
	char *rt = getenv("SSB64_NETPLAY_RESIM_TICK_TRACE");
	SYNetRollbackHashSet h;

	if ((rt == NULL) || (rt[0] == '\0') || (atoi(rt) == 0))
	{
		return;
	}
	h = syNetRollbackCollectHashes();
	port_log(
	    "SSB64 NetRollback: resim_tick t=%u figh=0x%08X world=0x%08X item=0x%08X wpn=0x%08X mph=0x%08X rng=0x%08X cam=0x%08X anim=0x%08X\n",
	    tick,
	    h.fighter,
	    h.world,
	    h.item,
	    h.weapon,
	    h.map,
	    h.rng,
	    h.camera,
	    h.animation);
}

static void syNetRollbackLogResimComplete(void)
{
	SYNetRollbackHashSet post;

	if (sSYNetRollbackResimPreHashesValid == FALSE)
	{
		return;
	}
	post = syNetRollbackCollectHashes();
	port_log(
	    "SSB64 NetRollback: resim complete baseline/post figh=0x%08X/0x%08X world=0x%08X/0x%08X item=0x%08X/0x%08X wpn=0x%08X/0x%08X mph=0x%08X/0x%08X rng=0x%08X/0x%08X cam=0x%08X/0x%08X anim=0x%08X/0x%08X load_tick=%u mismatch_tick=%u rollbacks=%u\n",
	    sSYNetRollbackResimPreHashes.fighter,
	    post.fighter,
	    sSYNetRollbackResimPreHashes.world,
	    post.world,
	    sSYNetRollbackResimPreHashes.item,
	    post.item,
	    sSYNetRollbackResimPreHashes.weapon,
	    post.weapon,
	    sSYNetRollbackResimPreHashes.map,
	    post.map,
	    sSYNetRollbackResimPreHashes.rng,
	    post.rng,
	    sSYNetRollbackResimPreHashes.camera,
	    post.camera,
	    sSYNetRollbackResimPreHashes.animation,
	    post.animation,
	    sSYNetRollbackResimLoadTick,
	    sSYNetRollbackResimMismatchTick,
	    sSYNetRollbackResimOrdinal);
	if (syNetRollbackHashesEqual(&sSYNetRollbackResimPreHashes, &post) == FALSE)
	{
		port_log(
		    "SSB64 NetRollback: RESIM_STATE_DELTA (expected after input correction) load_tick=%u mismatch_tick=%u target_tick=%u\n",
		    sSYNetRollbackResimLoadTick,
		    sSYNetRollbackResimMismatchTick,
		    sSYNetRollbackResimTargetTick);
	}
	if ((sSYNetRollbackForceIdentityPending != FALSE) &&
	    (sSYNetRollbackForceIdentityTick == sSYNetRollbackResimMismatchTick))
	{
		if (syNetRollbackHashesEqual(&sSYNetRollbackResimPreHashes, &post) == FALSE)
		{
			port_log(
			    "SSB64 NetRollback: ROLLBACK_IDENTITY_DRIFT tick=%u figh=0x%08X/0x%08X world=0x%08X/0x%08X item=0x%08X/0x%08X wpn=0x%08X/0x%08X mph=0x%08X/0x%08X rng=0x%08X/0x%08X cam=0x%08X/0x%08X anim=0x%08X/0x%08X\n",
			    sSYNetRollbackForceIdentityTick,
			    sSYNetRollbackResimPreHashes.fighter,
			    post.fighter,
			    sSYNetRollbackResimPreHashes.world,
			    post.world,
			    sSYNetRollbackResimPreHashes.item,
			    post.item,
			    sSYNetRollbackResimPreHashes.weapon,
			    post.weapon,
			    sSYNetRollbackResimPreHashes.map,
			    post.map,
			    sSYNetRollbackResimPreHashes.rng,
			    post.rng,
			    sSYNetRollbackResimPreHashes.camera,
			    post.camera,
			    sSYNetRollbackResimPreHashes.animation,
			    post.animation);
		}
		sSYNetRollbackForceIdentityPending = FALSE;
	}
	if ((sSYNetRollbackVerifyStrict != FALSE) && (post.fighter == sSYNetRollbackResimPreHashes.fighter))
	{
		port_log(
		    "SSB64 NetRollback: VERIFY_STRICT warning: figh unchanged after resim (mismatch_tick=%u frontier=%u)\n",
		    sSYNetRollbackResimMismatchTick,
		    sSYNetRollbackResimTargetTick);
		syNetDesyncClassifierOnVerifyStrictUnchanged(sSYNetRollbackResimMismatchTick);
	}
	if (sSYNetRollbackLastVerifyHash != 0)
	{
		port_log("SSB64 NetRollback: verify delta vs prior rollback figh ref=0x%08X\n", sSYNetRollbackLastVerifyHash);
	}
	sSYNetRollbackLastVerifyHash = post.fighter;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
}
#endif

#define SYNETROLLBACK_RESIM_STALL_ABORT_FRAMES_DEFAULT 300U

static sb32 syNetRollbackResimStallAbortIfNeeded(void)
{
	u32 limit;
	const char *env;
	s32 parsed;

	if (sSYNetRollbackResimPending == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackResimStallFrames++;
	limit = SYNETROLLBACK_RESIM_STALL_ABORT_FRAMES_DEFAULT;
	env = getenv("SSB64_NETPLAY_RESIM_STALL_ABORT_FRAMES");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
		if (parsed > 0)
		{
			limit = (u32)parsed;
		}
	}
	if (sSYNetRollbackResimStallFrames < limit)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: resim stall abort frames=%u next=%u target=%u mismatch=%u — ending VS session\n",
	    (unsigned int)sSYNetRollbackResimStallFrames,
	    (unsigned int)sSYNetRollbackResimNextTick,
	    (unsigned int)sSYNetRollbackResimTargetTick,
	    (unsigned int)sSYNetRollbackResimMismatchTick);
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackResimStallFrames = 0U;
	syNetPeerSendVsSessionEndNotifyPeer();
	syNetRollbackStopVSSession();
	syNetPeerStopVSSession();
	return TRUE;
}

static void syNetRollbackAdvanceResimBudget(void)
{
#ifdef PORT
	u32 ran;
	u32 t;

	if (sSYNetRollbackResimPending == FALSE)
	{
		return;
	}
	ran = 0;
	t = sSYNetRollbackResimNextTick;
	while ((t < sSYNetRollbackResimTargetTick) && (ran < sSYNetRollbackResimTicksPerFrame))
	{
		syNetInputSetTick(t);
		syNetInputPublishSynchronizedTick(t);
		scVSBattleFuncUpdateBattleSimOnly();
		syNetRollbackLogResimTickTrace(t);
		t++;
		ran++;
	}
	sSYNetRollbackResimNextTick = t;
	if (t >= sSYNetRollbackResimTargetTick)
	{
		sSYNetRollbackResimPending = FALSE;
		sSYNetRollbackResimStallFrames = 0U;
		sSYNetRollbackResimDepth = 0;
		sSYNetRollbackResimFromPeerSymmetric = FALSE;
		syNetSyncLogRollbackWorldDetail("rollback_post", sSYNetRollbackResimMismatchTick);
		syNetSyncLogFighterDetail("rollback_post", sSYNetRollbackResimMismatchTick);
		ifCommonItemArrowPruneStaleInterfaces();
		syNetRollbackLogResimComplete();
		syNetRollbackArmDebounceAfterResim();
	}
#else
	(void)0;
#endif
}

/* Transport-time hook: if inputs diverge, resim (nested calls short-circuit via `IsResimulating`). */
void syNetRollbackUpdate(void)
{
	u32 frontier;
	u32 mismatch;
	u32 resim_target_tick;
#ifdef PORT
	SYNetInputFrame mismatch_hist;
	SYNetInputFrame mismatch_remote;
	sb32 mismatch_detail_ready;
	sb32 mismatch_from_peer_symmetric;
	s32 mismatch_player;
#endif

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
#ifdef PORT
	if (sSYNetRollbackResimPending != FALSE)
	{
		if (syNetRollbackResimStallAbortIfNeeded() != FALSE)
		{
			return;
		}
		syNetRollbackAdvanceResimBudget();
		return;
	}
#endif
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
#ifdef PORT
	syNetRollbackDebugTryApplyPendingForceMismatch();
	syNetRollbackExpireSymmetricNotify();
	if (syNetRollbackTryBeginDeferredStateMismatch() != FALSE)
	{
		return;
	}
	if (syNetRollbackTryBeginDeferredMismatch() != FALSE)
	{
		return;
	}
	if (syNetTickCommitAllowsBattleSimFromLastFuncReadEvaluate() == FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	resim_target_tick = frontier;
	mismatch_player = -1;
	mismatch_from_peer_symmetric = FALSE;
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, &mismatch_player);
	if ((sSYNetRollbackSymmetricEnabled != FALSE) && (sSYNetRollbackSymmetricDiagOnly == FALSE) &&
	    (sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0))
	{
		u32 peer_tick;
		u32 peer_target_tick;

		peer_tick = sSYNetRollbackPendingPeerSymmetricTick;
		peer_target_tick = sSYNetRollbackPendingPeerSymmetricTargetTick;
		if (peer_target_tick == ~(u32)0)
		{
			peer_target_tick = frontier;
		}
		if (peer_target_tick <= peer_tick)
		{
			peer_target_tick = peer_tick + 1U;
		}
		if ((peer_tick != 0U) && (peer_tick < frontier) && ((mismatch == ~(u32)0) || (peer_tick < mismatch)))
		{
			mismatch = peer_tick;
			resim_target_tick = peer_target_tick;
			mismatch_player = -1;
			mismatch_from_peer_symmetric = TRUE;
		}
	}
#else
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	resim_target_tick = frontier;
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, NULL);
#endif

	if (mismatch == ~(u32)0)
	{
#ifdef PORT
		{
			char *sd = getenv("SSB64_NETPLAY_ROLLBACK_SCAN_DIAG");

			if ((sd != NULL) && (sd[0] != '\0') && (atoi(sd) != 0))
			{
				static u32 sLastRollbackCleanLogFrontier = ~(u32)0;
				u32 fr = frontier;

				if ((sLastRollbackCleanLogFrontier == ~(u32)0) || (fr >= sLastRollbackCleanLogFrontier + 120U))
				{
					port_log(
					    "SSB64 NetRollback: input scan clean frontier=%u (no mismatch in rollback scan window)\n",
					    fr);
					sLastRollbackCleanLogFrontier = fr;
				}
			}
		}
#endif
		return;
	}
	if (syNetRollbackMismatchAllowedDuringDebounce(mismatch) == FALSE)
	{
		return;
	}
#ifdef PORT
	if (mismatch_from_peer_symmetric != FALSE)
	{
		if (sSYNetRollbackPeerSymmetricLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: peer symmetric rollback at tick %u target_tick=%u frontier=%u rollbacks=%u\n",
			    mismatch,
			    resim_target_tick,
			    frontier,
			    sSYNetRollbackRollbackCount + 1);
			sSYNetRollbackPeerSymmetricLogsRemaining--;
		}
	}
	else
	{
		port_log(
		    "SSB64 NetRollback: input mismatch at tick %u frontier=%u sim_slot=%d rollbacks=%u\n",
		    mismatch,
		    frontier,
		    (int)mismatch_player,
		    sSYNetRollbackRollbackCount + 1);
	}
	syNetDesyncClassifierOnRollbackInputMismatch(mismatch);
	mismatch_detail_ready = FALSE;
	if ((mismatch_from_peer_symmetric == FALSE) && (mismatch_player >= 0) && (mismatch_player < MAXCONTROLLERS))
	{
		if ((syNetInputGetHistoryFrame(mismatch_player, mismatch, &mismatch_hist) != FALSE) &&
		    (syNetInputGetRemoteHistoryFrame(mismatch_player, mismatch, &mismatch_remote) != FALSE))
		{
			mismatch_detail_ready = TRUE;
			port_log(
			    "SSB64 NetRollback: INPUT_MISMATCH_DETAIL tick=%u slot=%d hist t=%u btn=0x%04X sx=%d sy=%d src=%u "
			    "pred=%u valid=%u | ring t=%u btn=0x%04X sx=%d sy=%d src=%u pred=%u valid=%u\n",
			    mismatch,
			    (int)mismatch_player,
			    (unsigned int)mismatch_hist.tick,
			    (unsigned int)mismatch_hist.buttons,
			    mismatch_hist.stick_x,
			    mismatch_hist.stick_y,
			    (unsigned int)mismatch_hist.source,
			    (unsigned int)mismatch_hist.is_predicted,
			    (unsigned int)mismatch_hist.is_valid,
			    (unsigned int)mismatch_remote.tick,
			    (unsigned int)mismatch_remote.buttons,
			    mismatch_remote.stick_x,
			    mismatch_remote.stick_y,
			    (unsigned int)mismatch_remote.source,
			    (unsigned int)mismatch_remote.is_predicted,
			    (unsigned int)mismatch_remote.is_valid);
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: INPUT_MISMATCH_DETAIL tick=%u slot=%d (missing hist or ring row for detail)\n",
			    mismatch,
			    (int)mismatch_player);
		}
	}
	if ((mismatch_from_peer_symmetric == FALSE) && (mismatch_detail_ready != FALSE))
	{
		syNetRollbackArmPredictionRecovery(mismatch, frontier, &mismatch_hist);
	}
	if ((syNetInputGetAbortOnInputMismatchMask() & 2) != 0)
	{
		port_log(
		    "SSB64 NetRollback: ABORT_ON_INPUT_MISMATCH (bit2) before resim tick=%u frontier=%u slot=%d — %s\n",
		    mismatch,
		    frontier,
		    (int)mismatch_player,
		    (syNetInputGetAbortOnInputMismatchFatal() != FALSE)
		        ? "hard-abort (SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL)"
		        : "soft (unset mask or set SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL=1 to abort)");
		if (syNetPeerShouldHardAbortOnNetplayInputMismatch() != FALSE)
		{
			abort();
		}
		else if (syNetInputGetAbortOnInputMismatchFatal() != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: ABORT_ON_INPUT_MISMATCH_FATAL skipped (sync pipeline phase=%d; hard-abort only "
			    "in Running)\n",
			    (int)syNetPeerGetSyncPipelinePhase());
		}
	}
#endif
	resim_target_tick = syNetRollbackClampResimTargetTick(mismatch, resim_target_tick);
#ifdef PORT
	if ((mismatch_from_peer_symmetric == FALSE) && (mismatch_player >= 0) &&
	    (sSYNetRollbackSymmetricEnabled != FALSE))
	{
		syNetRollbackArmSymmetricNotify(mismatch_player, mismatch, resim_target_tick);
	}
#endif
	if (syNetRollbackBeginResim(
	        mismatch,
	        resim_target_tick,
	        (mismatch_from_peer_symmetric != FALSE) ? sSYNetRollbackPendingPeerSymmetricSlot : -1) == FALSE)
	{
		return;
	}
#ifdef PORT
	if (mismatch_from_peer_symmetric != FALSE)
	{
		sSYNetRollbackResimFromPeerSymmetric = TRUE;
		if ((sSYNetRollbackPendingPeerSymmetricSlot >= 0) &&
		    (sSYNetRollbackPendingPeerSymmetricSlot < MAXCONTROLLERS))
		{
			sSYNetRollbackPeerSymmetricAppliedTick[sSYNetRollbackPendingPeerSymmetricSlot] = mismatch;
		}
		sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
		sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
		sSYNetRollbackPendingPeerSymmetricSlot = -1;
	}
#endif
	sSYNetRollbackRollbackCount++;
#ifdef PORT
	sSYNetRollbackResimOrdinal = sSYNetRollbackRollbackCount;
	sSYNetRollbackLastCommittedMismatchTick = mismatch;
#endif
	syNetRollbackAdvanceResimBudget();
}

#ifdef PORT
u32 syNetRollbackGetAppliedResimCount(void)
{
	return sSYNetRollbackRollbackCount;
}

u32 syNetRollbackGetLoadFailCount(void)
{
	return sSYNetRollbackLoadFailCount;
}
#endif

void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y)
{
#ifdef PORT
	if (sSYNetRollbackInjectTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetRollbackInjectConsumed != FALSE)
	{
		return;
	}
	if (*tick != sSYNetRollbackInjectTick)
	{
		return;
	}

	if (sSYNetRollbackForceMismatch != FALSE)
	{
		if (sSYNetRollbackForceMismatchPendingTick == *tick)
		{
			return;
		}
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH armed at wire tick %u (patch published history after staging if it matches remote)\n",
		    *tick);
		sSYNetRollbackForceMismatchPendingTick = *tick;
		return;
	}

	*buttons ^= 0x1000;
	sSYNetRollbackInjectConsumed = TRUE;
	port_log("SSB64 NetRollback: injected button tamper at tick %u\n", *tick);
#else
	(void)tick;
	(void)buttons;
	(void)stick_x;
	(void)stick_y;
#endif
}

void syNetRollbackApplyPortSimPacing(u32 refresh_hz)
{
#ifdef PORT
	u32 sim_hz;
	char *hz_env;
	u32 K;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		syTaskmanSetIntervals(1, 1);
		return;
	}
	sim_hz = 60;
	hz_env = getenv("SSB64_NETPLAY_SIM_HZ");

	if ((hz_env != NULL) && (atoi(hz_env) > 0))
	{
		sim_hz = (u32)atoi(hz_env);
	}
	if (sim_hz < 1)
	{
		sim_hz = 1;
	}
	if (refresh_hz == 0)
	{
		refresh_hz = 60;
	}
	K = (refresh_hz + sim_hz / 2) / sim_hz;

	if (K < 1)
	{
		K = 1;
	}
	if (K > 16)
	{
		K = 16;
	}
	syTaskmanSetIntervals((u16)K, 1);
#else
	(void)refresh_hz;
#endif
}
