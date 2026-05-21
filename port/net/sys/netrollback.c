#include <sys/netrollback.h>

#include <sys/netrollback_episode.h>
#include <sys/netinput.h>
#include <sys/netinput_timeline.h>
#include <sys/netpeer.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/objman_gcport.h>
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
#include <sc/scdef.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>

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
static u32 sSYNetRollbackSymmetricNotifyLoadTick[MAXCONTROLLERS];
static u32 sSYNetRollbackSymmetricNotifyEpochId[MAXCONTROLLERS];
static u32 sSYNetRollbackSymmetricNotifySendCount[MAXCONTROLLERS];
static u8 sSYNetRollbackSymmetricNotifyFlags[MAXCONTROLLERS];
static u32 sSYNetRollbackPeerSymmetricAppliedTick[MAXCONTROLLERS];
static u32 sSYNetRollbackPendingPeerSymmetricTick;
static u32 sSYNetRollbackPendingPeerSymmetricTargetTick;
static s32 sSYNetRollbackPendingPeerSymmetricSlot;
static sb32 sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth;
static u32 sSYNetRollbackPeerSymmetricLogsRemaining;
static sb32 sSYNetRollbackResimFromPeerSymmetric;
static sb32 sSYNetRollbackSymmetricDiagOnly;
static sb32 sSYNetRollbackResimRngVerifyEnabled;
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
static u32 sSYNetRollbackPeerBaselineSlotFigh;
static u32 sSYNetRollbackPeerBaselineSlotWorld;
static u32 sSYNetRollbackPeerBaselineSlotItem;
static u32 sSYNetRollbackPeerBaselineSlotRng;
static u32 sSYNetRollbackPeerBaselineFighterSlot[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetRollbackPeerBaselineRetransmitCount;
static u32 sSYNetRollbackResimBaselineDeeperAttempts;
static u32 sSYNetRollbackBaselineTimeoutStreak;
static u32 sSYNetRollbackBaselineTimeoutWindowStartTick;
static s32 sSYNetRollbackResimCorrectionPlayer;
static sb32 sSYNetRollbackPeerSnapshotAbort;
static u32 sSYNetRollbackDebounceUntilSim;
static u32 sSYNetRollbackLastCommittedMismatchTick;
static sb32 sSYNetRollbackDeferredMismatchPending;
static u32 sSYNetRollbackDeferredMismatchTick;
static u32 sSYNetRollbackDeferredMismatchTargetTick;
static s32 sSYNetRollbackDeferredMismatchPlayer;
static sb32 sSYNetRollbackDeferredMismatchFromPeerSymmetric;
/* Live cap when peer-symmetric deferred queue is rejected (hold below mismatch until retried). */
static u32 sSYNetRollbackPeerSymmetricRejectLiveCap;
static sb32 sSYNetRollbackStateHashRollback;
static u32 sSYNetRollbackStateHashLogsRemaining;
static u32 sSYNetRollbackGgpoCorrectionLogsRemaining;
static sb32 sSYNetRollbackDeferredStateMismatchPending;
static u32 sSYNetRollbackDeferredStateMismatchTick;
static u32 sSYNetRollbackDeferredStateMismatchTargetTick;
static sb32 sSYNetRollbackDeferredStateMismatchInputAgreed;
/* Frame-commit state diverge with matching input digests (incl. predicted-onset reanchor). */
static sb32 sSYNetRollbackFcStateRecoveryActive;
static u32 sSYNetRollbackFcStateRecoveryMismatchTick;
static u32 sSYNetRollbackFcStateRecoveryTargetTick;
static u32 sSYNetRollbackFcStateRecoverySuppressLogsRemaining;
static u32 sSYNetRollbackFcDeepenLoadTick;
static u32 sSYNetRollbackFcDeepenAttempts;
static sb32 sSYNetRollbackFcDeepenInFlight;
static sb32 sSYNetRollbackFcDeepenStormActive;
static sb32 sSYNetRollbackFcDeepenDetailLogged;
static u32 sSYNetRollbackLastFrameCommitStateAgreedTick;
static sb32 sSYNetRollbackLoadHashDriftSoft;
static SYNetRollbackHashSet sSYNetRollbackLastPeerOutcomeHash;
static u32 sSYNetRollbackLastPeerOutcomeTick;
static sb32 sSYNetRollbackLastPeerOutcomeValid;
static u32 sSYNetRollbackLastOutcomeProbeFrontier;
static u32 sSYNetRollbackOutcomeCorrectionLogsRemaining;
static u32 sSYNetRollbackCoalescedScanLogsRemaining;
static u32 sSYNetRollbackFrontierAheadWarnLogsRemaining;
static u32 sSYNetRollbackEpisodeAnchorMismatch;
static u32 sSYNetRollbackEpisodeAnchorLoadTick;
static u32 sSYNetRollbackEpisodeLastTargetTick;
static u32 sSYNetRollbackEpisodeResolvedThrough;
static u32 sSYNetRollbackEpisodeExtensions;
static u32 sSYNetRollbackLastRollbackBeginSimTick;
static u32 sSYNetRollbackSuppressReloadLoadTick;
static u32 sSYNetRollbackSuppressReloadUntilSim;
static sb32 sSYNetRollbackResimAwaitingPeerBaseline;
static sb32 sSYNetRollbackResimBaselineGateOpen;
static sb32 sSYNetRollbackResimBaselineDigestMatched;
static u32 sSYNetRollbackResimBaselineWaitFrames;
static u32 sSYNetRollbackLastBaselineEchoLoadTick;
static u32 sSYNetRollbackLastBaselineEchoSimTick;
static u32 sSYNetRollbackPeerBaselineResyncSteps;
static u32 sSYNetRollbackPeerBaselineResyncOriginMismatch;
static sb32 sSYNetRollbackPeerBaselineResyncStormActive;
static u32 sSYNetRollbackBaselineEchoRetryLoadTick = ~(u32)0;
static u32 sSYNetRollbackBaselineEchoRetryAttempts;
static sb32 sSYNetRollbackDeferredPeerSymmetricPending;
static u32 sSYNetRollbackDeferredPeerSymmetricTick;
static u32 sSYNetRollbackDeferredPeerSymmetricTargetTick;
static s32 sSYNetRollbackDeferredPeerSymmetricSlot;
static sb32 sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth;
static sb32 sSYNetRollbackDeferredPeerBaselineComparePending;
static u32 sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
static u32 sSYNetRollbackEpochId;
static u32 sSYNetRollbackPeerEpochTargetTick;
static u32 sSYNetRollbackPeerEpochMismatchTick;
static sb32 sSYNetRollbackPeerEpochAwaitingPeerResimPost;
static u32 sSYNetRollbackEpochHoldLogsRemaining;
static int sSYNetRollbackEpochCapSlackCache = -999;
static int sSYNetRollbackPeerDivergeResyncTicksCache = -999;

#define SYNETROLLBACK_EPOCH_CAP_SLACK_DEFAULT 1U
#define SYNETROLLBACK_EPOCH_CAP_SLACK_DISABLE 99U

typedef enum SYNetRollbackEpisodePhase
{
	nSYNetRollbackEpisodePhaseLive = 0,
	nSYNetRollbackEpisodePhaseAwaitingBaseline,
	nSYNetRollbackEpisodePhaseForwardResim
} SYNetRollbackEpisodePhase;

typedef struct SYNetRollbackEpisode
{
	SYNetRollbackEpisodePhase phase;
	u32 mismatch_tick;
	u32 load_tick;
	u32 target_tick;
	s32 corrected_slot;
	sb32 initiator;
	sb32 from_peer_notify;
} SYNetRollbackEpisode;

static SYNetRollbackEpisode sSYNetRollbackEpisode;

typedef struct SYNetRollbackPendingEpisode
{
	sb32 valid;
	s32 slot;
	u32 mismatch_tick;
	u32 target_tick;
	u32 load_tick;
	u32 epoch_id;
	u8 flags;
} SYNetRollbackPendingEpisode;

static SYNetRollbackPendingEpisode sSYNetRollbackPendingEpisode;
static SYNetRollbackPendingEpisode sSYNetRollbackExecutingEpisode;
static sb32 sSYNetRollbackAuthoritativeEpisodeActive;
static sb32 sSYNetRollbackEpisodeAuthorityEnabled = TRUE;
static int sSYNetRollbackEpisodeAuthorityEnvCache = -999;

typedef struct SYNetRollbackResimPostKey
{
	u32 epoch_id;
	u32 load_tick;
	u32 mismatch_tick;
	u32 target_tick;
} SYNetRollbackResimPostKey;

typedef struct SYNetRollbackResimPostDigest
{
	u32 figh;
	u32 world;
	u32 item;
	u32 rng;
	u32 input_digest;
} SYNetRollbackResimPostDigest;

static sb32 sSYNetRollbackResimPostPeerPending;
static SYNetRollbackResimPostKey sSYNetRollbackResimPostPeerKey;
static SYNetRollbackResimPostDigest sSYNetRollbackResimPostPeerDigest;
static sb32 sSYNetRollbackResimPostLocalValid;
static SYNetRollbackResimPostKey sSYNetRollbackResimPostLocalKey;
static SYNetRollbackResimPostDigest sSYNetRollbackResimPostLocalDigest;
static sb32 sSYNetRollbackResimPostCompletedValid;
static SYNetRollbackResimPostKey sSYNetRollbackResimPostCompletedKey;
static SYNetRollbackResimPostDigest sSYNetRollbackResimPostCompletedDigest;
static sb32 sSYNetRollbackResimPostMatchLogged;
static u32 sSYNetRollbackResimPostMatchLoggedEpoch;
static u32 sSYNetRollbackResimPostMatchLoggedMismatch;
static u32 sSYNetRollbackResimPostMatchLoggedTarget;

static void syNetRollbackMaybeClearPeerEpochAfterResimPostMatch(const SYNetRollbackResimPostKey *key);
static sb32 syNetRollbackSpanOverlapsResimPostKey(u32 span_mismatch, u32 span_target, const SYNetRollbackResimPostKey *key);
static void syNetRollbackClearPendingPeerSymmetricNotify(void);
static void syNetRollbackReleaseLiveCapsAfterResimPostMatch(const SYNetRollbackResimPostKey *key);
static sb32 syNetRollbackResimPostKeyValid(const SYNetRollbackResimPostKey *key);
static void syNetRollbackNormalizeResimPostKey(SYNetRollbackResimPostKey *key);
static sb32 syNetRollbackResimPostEpisodeKeysEqual(const SYNetRollbackResimPostKey *a, const SYNetRollbackResimPostKey *b);
static sb32 syNetRollbackResimPostCompletedCoversActiveResim(void);
static void syNetRollbackLogResimPostMatchOnce(const SYNetRollbackResimPostKey *key,
					       const SYNetRollbackResimPostDigest *local);
static void syNetRollbackTryCompleteEpisodeAfterPostMatch(void);
static void syNetRollbackOnResimPostMatchCommitted(const SYNetRollbackResimPostKey *key,
						   const SYNetRollbackResimPostDigest *local);
static void syNetRollbackPendingEpisodeClear(void);
static void syNetRollbackClearDeferredInputMismatch(void);
static void syNetRollbackClearPeerSymmetricRejectLiveCap(void);
static void syNetRollbackFinishForwardResim(void);
static void syNetRollbackClearPeerEpochState(void);
static sb32 syNetRollbackTryRestartResimAtDeeperLoad(u32 deeper_load_tick);

static SYNetRollbackHashSet syNetRollbackCollectHashes(void);

static sb32 syNetRollbackResimPostKeysEqual(const SYNetRollbackResimPostKey *a, const SYNetRollbackResimPostKey *b)
{
	if ((a == NULL) || (b == NULL))
	{
		return FALSE;
	}
	return ((a->epoch_id == b->epoch_id) && (a->load_tick == b->load_tick) && (a->mismatch_tick == b->mismatch_tick) &&
	        (a->target_tick == b->target_tick))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackResimPostKeyValid(const SYNetRollbackResimPostKey *key)
{
	if (key == NULL)
	{
		return FALSE;
	}
	if ((key->load_tick == ~(u32)0) || (key->mismatch_tick == 0U) || (key->target_tick <= key->mismatch_tick))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackNormalizeResimPostKey(SYNetRollbackResimPostKey *key)
{
	if (key == NULL)
	{
		return;
	}
	if ((key->load_tick == ~(u32)0) || (key->load_tick == 0U))
	{
		if ((sSYNetRollbackResimLoadTick != ~(u32)0) && (sSYNetRollbackResimLoadTick != 0U))
		{
			key->load_tick = sSYNetRollbackResimLoadTick;
		}
		else if (syNetRollbackEpisodeFsmEnabled() != FALSE)
		{
			u32 fsm_load;

			fsm_load = syNetRollbackEpisodeFsmGetLoadTick();
			if ((fsm_load != ~(u32)0) && (fsm_load != 0U))
			{
				key->load_tick = fsm_load;
			}
		}
	}
	if ((key->mismatch_tick == 0U) && (sSYNetRollbackResimMismatchTick != ~(u32)0) && (sSYNetRollbackResimMismatchTick != 0U))
	{
		key->mismatch_tick = sSYNetRollbackResimMismatchTick;
	}
	if ((key->target_tick == 0U) && (sSYNetRollbackResimTargetTick != ~(u32)0) && (sSYNetRollbackResimTargetTick != 0U))
	{
		key->target_tick = sSYNetRollbackResimTargetTick;
	}
}

static sb32 syNetRollbackResimPostEpisodeKeysEqual(const SYNetRollbackResimPostKey *a, const SYNetRollbackResimPostKey *b)
{
	if ((a == NULL) || (b == NULL))
	{
		return FALSE;
	}
	if ((a->mismatch_tick == 0U) || (b->mismatch_tick == 0U) || (a->target_tick <= a->mismatch_tick) ||
	    (b->target_tick <= b->mismatch_tick))
	{
		return FALSE;
	}
	return ((a->epoch_id == b->epoch_id) && (a->mismatch_tick == b->mismatch_tick) && (a->target_tick == b->target_tick))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackResimPostCompletedCoversActiveResim(void)
{
	SYNetRollbackResimPostKey active;

	if (sSYNetRollbackResimPostCompletedValid == FALSE)
	{
		return FALSE;
	}
	active.epoch_id = sSYNetRollbackEpochId;
	active.load_tick = sSYNetRollbackResimLoadTick;
	active.mismatch_tick = sSYNetRollbackResimMismatchTick;
	active.target_tick = sSYNetRollbackResimTargetTick;
	if ((active.mismatch_tick == 0U) || (active.target_tick == 0U) || (active.target_tick <= active.mismatch_tick))
	{
		if (syNetRollbackEpisodeFsmEnabled() != FALSE)
		{
			active.mismatch_tick = syNetRollbackEpisodeFsmGetMismatchTick();
			active.target_tick = syNetRollbackEpisodeFsmGetTargetTick();
		}
	}
	return syNetRollbackResimPostEpisodeKeysEqual(&sSYNetRollbackResimPostCompletedKey, &active);
}

static void syNetRollbackLogResimPostMatchOnce(const SYNetRollbackResimPostKey *key,
					       const SYNetRollbackResimPostDigest *local)
{
	if ((key == NULL) || (local == NULL))
	{
		return;
	}
	if ((sSYNetRollbackResimPostMatchLogged != FALSE) &&
	    (sSYNetRollbackResimPostMatchLoggedEpoch == key->epoch_id) &&
	    (sSYNetRollbackResimPostMatchLoggedMismatch == key->mismatch_tick) &&
	    (sSYNetRollbackResimPostMatchLoggedTarget == key->target_tick))
	{
		return;
	}
	sSYNetRollbackResimPostMatchLogged = TRUE;
	sSYNetRollbackResimPostMatchLoggedEpoch = key->epoch_id;
	sSYNetRollbackResimPostMatchLoggedMismatch = key->mismatch_tick;
	sSYNetRollbackResimPostMatchLoggedTarget = key->target_tick;
	port_log(
	    "SSB64 NetRollback: RESIM_POST_MATCH epoch=%u load=%u mismatch=%u target=%u figh=0x%08X item=0x%08X rng=0x%08X inp=0x%08X\n",
	    key->epoch_id,
	    key->load_tick,
	    key->mismatch_tick,
	    key->target_tick,
	    local->figh,
	    local->item,
	    local->rng,
	    local->input_digest);
}

static void syNetRollbackTryCompleteEpisodeAfterPostMatch(void)
{
	if ((syNetRollbackEpisodeFsmEnabled() == FALSE) || (sSYNetRollbackResimPostCompletedValid == FALSE) ||
	    (syNetRollbackEpisodeFsmIsActive() == FALSE))
	{
		return;
	}
	syNetRollbackEpisodeCommitPromoteSealed();
	syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseCommit);
	syNetRollbackFinishForwardResim();
}

static sb32 syNetRollbackSpanOverlapsResimPostKey(u32 span_mismatch, u32 span_target, const SYNetRollbackResimPostKey *key)
{
	if ((key == NULL) || (key->mismatch_tick == 0U) || (key->target_tick <= key->mismatch_tick))
	{
		return FALSE;
	}
	if ((span_mismatch == 0U) || (span_target == 0U) || (span_target == ~(u32)0U) || (span_target <= span_mismatch))
	{
		return FALSE;
	}
	if (span_mismatch > key->target_tick)
	{
		return FALSE;
	}
	if (span_target <= key->mismatch_tick)
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackClearPendingPeerSymmetricNotify(void)
{
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
	syNetRollbackClearPeerSymmetricRejectLiveCap();
}

static void syNetRollbackReleaseLiveCapsAfterResimPostMatch(const SYNetRollbackResimPostKey *key)
{
	if (key == NULL)
	{
		return;
	}
	syNetRollbackEpisodeFsmOnPostMatch();
	if ((sSYNetRollbackDeferredMismatchPending != FALSE) &&
	    (syNetRollbackSpanOverlapsResimPostKey(sSYNetRollbackDeferredMismatchTick,
						   sSYNetRollbackDeferredMismatchTargetTick, key) != FALSE))
	{
		syNetRollbackClearDeferredInputMismatch();
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (syNetRollbackSpanOverlapsResimPostKey(sSYNetRollbackPendingPeerSymmetricTick,
						   sSYNetRollbackPendingPeerSymmetricTargetTick, key) != FALSE))
	{
		syNetRollbackClearPendingPeerSymmetricNotify();
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (syNetRollbackSpanOverlapsResimPostKey(sSYNetRollbackDeferredPeerSymmetricTick,
						   sSYNetRollbackDeferredPeerSymmetricTargetTick, key) != FALSE))
	{
		sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
		sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricSlot = -1;
		sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
		syNetRollbackClearPeerSymmetricRejectLiveCap();
	}
	if ((sSYNetRollbackPendingEpisode.valid != FALSE) &&
	    (syNetRollbackSpanOverlapsResimPostKey(sSYNetRollbackPendingEpisode.mismatch_tick,
						   sSYNetRollbackPendingEpisode.target_tick, key) != FALSE))
	{
		syNetRollbackPendingEpisodeClear();
	}
}

static void syNetRollbackOnResimPostMatchCommitted(const SYNetRollbackResimPostKey *key,
						   const SYNetRollbackResimPostDigest *local)
{
	SYNetRollbackResimPostKey norm;

	if ((key == NULL) || (local == NULL))
	{
		return;
	}
	norm = *key;
	syNetRollbackNormalizeResimPostKey(&norm);
	if (syNetRollbackResimPostKeyValid(&norm) == FALSE)
	{
		return;
	}
	if ((sSYNetRollbackResimPostCompletedValid != FALSE) &&
	    (syNetRollbackResimPostEpisodeKeysEqual(&norm, &sSYNetRollbackResimPostCompletedKey) != FALSE))
	{
		syNetRollbackMaybeClearPeerEpochAfterResimPostMatch(&norm);
		syNetRollbackTryCompleteEpisodeAfterPostMatch();
		return;
	}
	sSYNetRollbackResimPostCompletedValid = TRUE;
	sSYNetRollbackResimPostCompletedKey = norm;
	sSYNetRollbackResimPostCompletedDigest = *local;
	syNetRollbackLogResimPostMatchOnce(&norm, local);
	syNetRollbackReleaseLiveCapsAfterResimPostMatch(&norm);
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		if (syNetRollbackEpisodeFsmIsActive() == FALSE)
		{
			syNetRollbackMaybeClearPeerEpochAfterResimPostMatch(&norm);
			return;
		}
		syNetRollbackEpisodeCommitPromoteSealed();
		syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseCommit);
		syNetRollbackFinishForwardResim();
	}
	syNetRollbackMaybeClearPeerEpochAfterResimPostMatch(&norm);
}

static void syNetRollbackCompareResimPostDigests(const SYNetRollbackResimPostKey *key, const SYNetRollbackResimPostDigest *local,
						 const SYNetRollbackResimPostDigest *peer)
{
	if ((key == NULL) || (local == NULL) || (peer == NULL))
	{
		return;
	}
	if ((local->figh == peer->figh) && (local->item == peer->item) && (local->rng == peer->rng) &&
	    (local->input_digest == peer->input_digest))
	{
		SYNetRollbackResimPostKey norm;

		norm = *key;
		syNetRollbackNormalizeResimPostKey(&norm);
		syNetRollbackOnResimPostMatchCommitted(&norm, local);
		return;
	}
	port_log(
	    "SSB64 NetRollback: RESIM_POST_DIVERGE epoch=%u load=%u mismatch=%u target=%u local figh=0x%08X item=0x%08X rng=0x%08X inp=0x%08X | peer figh=0x%08X item=0x%08X rng=0x%08X inp=0x%08X\n",
	    key->epoch_id,
	    key->load_tick,
	    key->mismatch_tick,
	    key->target_tick,
	    local->figh,
	    local->item,
	    local->rng,
	    local->input_digest,
	    peer->figh,
	    peer->item,
	    peer->rng,
	    peer->input_digest);
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmOnPostDiverge();
		syNetRollbackClearPeerEpochState();
		if ((local->input_digest == peer->input_digest) && (local->rng == peer->rng) && (local->item == peer->item))
		{
			port_log(
			    "SSB64 NetRollback: EPISODE_FSM snapshot_fidelity diverge (matching replay inp) mismatch=%u\n",
			    key->mismatch_tick);
		}
		if (syNetRollbackTryRestartResimAtDeeperLoad(key->load_tick) != FALSE)
		{
			return;
		}
		syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseAbort);
		return;
	}
	if ((syNetRollbackIsActive() == FALSE) || (sSYNetRollbackStateHashRollback == FALSE))
	{
		return;
	}
	if ((sSYNetRollbackDeferredStateMismatchPending == FALSE) ||
	    (key->mismatch_tick < sSYNetRollbackDeferredStateMismatchTick))
	{
		u32 frontier;

		sSYNetRollbackDeferredStateMismatchPending = TRUE;
		syNetPeerFrameCommitDiagNoteDeferredArmed();
		sSYNetRollbackDeferredStateMismatchTick = key->mismatch_tick;
		frontier = syNetInputGetTick();
		if (frontier < ~(u32)0)
		{
			frontier++;
		}
		sSYNetRollbackDeferredStateMismatchTargetTick =
		    (frontier > key->target_tick) ? frontier : key->target_tick;
	}
	syNetPeerArmPostRecoveryConvergenceWatch();
}

static void syNetRollbackCaptureResimPostBoundaryDigest(void)
{
	SYNetRollbackHashSet post;

	if ((sSYNetRollbackResimMismatchTick == 0U) || (sSYNetRollbackResimTargetTick <= sSYNetRollbackResimMismatchTick) ||
	    (sSYNetRollbackResimLoadTick == ~(u32)0))
	{
		sSYNetRollbackResimPostLocalValid = FALSE;
		return;
	}
	sSYNetRollbackResimPostLocalKey.epoch_id = sSYNetRollbackEpochId;
	sSYNetRollbackResimPostLocalKey.load_tick = sSYNetRollbackResimLoadTick;
	sSYNetRollbackResimPostLocalKey.mismatch_tick = sSYNetRollbackResimMismatchTick;
	sSYNetRollbackResimPostLocalKey.target_tick = sSYNetRollbackResimTargetTick;
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		SYNetRollbackEpisodePostDigest replay_post;

		if (syNetRollbackEpisodeReplayLogGetPostDigest(&replay_post) != FALSE)
		{
			post.fighter = replay_post.figh;
			post.item = replay_post.item;
			post.rng = replay_post.rng;
			sSYNetRollbackResimPostLocalDigest.figh = replay_post.figh;
			sSYNetRollbackResimPostLocalDigest.world = post.world;
			sSYNetRollbackResimPostLocalDigest.item = replay_post.item;
			sSYNetRollbackResimPostLocalDigest.rng = replay_post.rng;
			sSYNetRollbackResimPostLocalDigest.input_digest = replay_post.input_digest;
			sSYNetRollbackResimPostLocalValid = TRUE;
			return;
		}
	}
	post = syNetRollbackCollectHashes();
	sSYNetRollbackResimPostLocalDigest.figh = post.fighter;
	sSYNetRollbackResimPostLocalDigest.world = post.world;
	sSYNetRollbackResimPostLocalDigest.item = post.item;
	sSYNetRollbackResimPostLocalDigest.rng = post.rng;
	sSYNetRollbackResimPostLocalDigest.input_digest = 0U;
	sSYNetRollbackResimPostLocalValid = TRUE;
}

static void syNetRollbackFillResimPostInputDigest(const SYNetRollbackResimPostKey *key, u32 *out_input_digest)
{
	u32 sums[MAXCONTROLLERS];
	u32 win_begin;
	u32 win_len;

	if ((key == NULL) || (out_input_digest == NULL))
	{
		return;
	}
	win_begin = key->mismatch_tick;
	win_len = key->target_tick - key->mismatch_tick;
	if (win_len == 0U)
	{
		win_len = 1U;
	}
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		SYNetRollbackEpisodePostDigest replay_post;
		u32 frozen_inp;

		if ((key->mismatch_tick == syNetRollbackEpisodeFsmGetMismatchTick()) &&
		    (key->target_tick == syNetRollbackEpisodeFsmGetTargetTick()) &&
		    (syNetRollbackEpisodeGetFrozenPostInputDigest(&frozen_inp) != FALSE))
		{
			*out_input_digest = frozen_inp;
			return;
		}
		if (syNetRollbackEpisodeReplayLogGetPostDigest(&replay_post) != FALSE)
		{
			*out_input_digest = replay_post.input_digest;
			return;
		}
	}
	if ((sSYNetRollbackResimPostCompletedValid != FALSE) &&
	    (syNetRollbackResimPostKeysEqual(key, &sSYNetRollbackResimPostCompletedKey) != FALSE))
	{
		*out_input_digest = sSYNetRollbackResimPostCompletedDigest.input_digest;
		return;
	}
	syNetInputGetHistoryInputValueChecksumWindow(win_begin, win_len, sums, out_input_digest);
}

static void syNetRollbackClearResimPostBoundaryDigest(void)
{
	sSYNetRollbackResimPostLocalValid = FALSE;
}

static void syNetRollbackFlushPendingResimPostHandshake(void)
{
	SYNetRollbackResimPostDigest local;

	if ((sSYNetRollbackResimPostPeerPending == FALSE) || (syNetRollbackIsActive() == FALSE))
	{
		return;
	}
	if ((sSYNetRollbackResimPostCompletedValid != FALSE) &&
	    (syNetRollbackResimPostEpisodeKeysEqual(&sSYNetRollbackResimPostPeerKey, &sSYNetRollbackResimPostCompletedKey) != FALSE))
	{
		sSYNetRollbackResimPostPeerPending = FALSE;
		return;
	}
	if (sSYNetRollbackResimPostLocalValid != FALSE)
	{
		local = sSYNetRollbackResimPostLocalDigest;
		syNetRollbackFillResimPostInputDigest(&sSYNetRollbackResimPostLocalKey, &local.input_digest);
		if (syNetRollbackResimPostKeysEqual(&sSYNetRollbackResimPostLocalKey, &sSYNetRollbackResimPostPeerKey) != FALSE)
		{
			syNetRollbackCompareResimPostDigests(&sSYNetRollbackResimPostLocalKey, &local,
							     &sSYNetRollbackResimPostPeerDigest);
			sSYNetRollbackResimPostPeerPending = FALSE;
		}
		return;
	}
	if (sSYNetRollbackResimPostCompletedValid == FALSE)
	{
		return;
	}
	local = sSYNetRollbackResimPostCompletedDigest;
	syNetRollbackFillResimPostInputDigest(&sSYNetRollbackResimPostCompletedKey, &local.input_digest);
	if (syNetRollbackResimPostKeysEqual(&sSYNetRollbackResimPostCompletedKey, &sSYNetRollbackResimPostPeerKey) != FALSE)
	{
		syNetRollbackCompareResimPostDigests(&sSYNetRollbackResimPostCompletedKey, &local,
						     &sSYNetRollbackResimPostPeerDigest);
		sSYNetRollbackResimPostPeerPending = FALSE;
	}
}

void syNetRollbackTryEmitResimPostHandshake(void)
{
	SYNetRollbackResimPostKey key;
	SYNetRollbackResimPostDigest local;
	SYNetRollbackHashSet h;
	SYNetRollbackResimPostKey probe;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (sSYNetRollbackResimPostCompletedValid != FALSE)
	{
		probe.epoch_id = sSYNetRollbackEpochId;
		probe.load_tick = sSYNetRollbackResimLoadTick;
		probe.mismatch_tick = sSYNetRollbackResimMismatchTick;
		probe.target_tick = sSYNetRollbackResimTargetTick;
		syNetRollbackNormalizeResimPostKey(&probe);
		if (syNetRollbackResimPostEpisodeKeysEqual(&probe, &sSYNetRollbackResimPostCompletedKey) != FALSE)
		{
			return;
		}
	}
	if ((sSYNetRollbackResimPostLocalValid != FALSE) &&
	    (sSYNetRollbackResimPostLocalKey.mismatch_tick != 0U) &&
	    (sSYNetRollbackResimPostLocalKey.target_tick > sSYNetRollbackResimPostLocalKey.mismatch_tick))
	{
		key = sSYNetRollbackResimPostLocalKey;
		local = sSYNetRollbackResimPostLocalDigest;
	}
	else
	{
		key.epoch_id = sSYNetRollbackEpochId;
		key.load_tick = sSYNetRollbackResimLoadTick;
		key.mismatch_tick = sSYNetRollbackResimMismatchTick;
		key.target_tick = sSYNetRollbackResimTargetTick;
		if ((key.mismatch_tick == 0U) || (key.target_tick <= key.mismatch_tick))
		{
			return;
		}
		h = syNetRollbackCollectHashes();
		local.figh = h.fighter;
		local.world = h.world;
		local.item = h.item;
		local.rng = h.rng;
	}
	syNetRollbackFillResimPostInputDigest(&key, &local.input_digest);
	if ((sSYNetRollbackResimPostPeerPending != FALSE) &&
	    (syNetRollbackResimPostKeysEqual(&key, &sSYNetRollbackResimPostPeerKey) != FALSE))
	{
		syNetRollbackCompareResimPostDigests(&key, &local, &sSYNetRollbackResimPostPeerDigest);
		sSYNetRollbackResimPostPeerPending = FALSE;
	}
	syNetPeerTrySendResimPostDigest(key.epoch_id, key.load_tick, key.mismatch_tick, key.target_tick, local.figh,
					local.world, local.item, local.rng, local.input_digest);
}

void syNetRollbackOnPeerResimPostDigest(u32 epoch_id, u32 load_tick, u32 mismatch_tick, u32 target_tick, u32 figh,
					u32 world, u32 item, u32 rng, u32 input_digest)
{
	SYNetRollbackResimPostKey key;
	SYNetRollbackResimPostDigest peer;
	SYNetRollbackResimPostDigest local;
	SYNetRollbackHashSet h;

	(void)world;
	key.epoch_id = epoch_id;
	key.load_tick = load_tick;
	key.mismatch_tick = mismatch_tick;
	key.target_tick = target_tick;
	peer.figh = figh;
	peer.world = world;
	peer.item = item;
	peer.rng = rng;
	peer.input_digest = input_digest;
	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if ((syNetRollbackIsResimulating() != FALSE) || (sSYNetRollbackResimPending != FALSE))
	{
		sSYNetRollbackResimPostPeerKey = key;
		sSYNetRollbackResimPostPeerDigest = peer;
		sSYNetRollbackResimPostPeerPending = TRUE;
		return;
	}
	if ((sSYNetRollbackResimPostLocalValid != FALSE) &&
	    (syNetRollbackResimPostKeysEqual(&key, &sSYNetRollbackResimPostLocalKey) != FALSE))
	{
		local = sSYNetRollbackResimPostLocalDigest;
		syNetRollbackFillResimPostInputDigest(&key, &local.input_digest);
		syNetRollbackCompareResimPostDigests(&key, &local, &peer);
		return;
	}
	if ((sSYNetRollbackResimPostCompletedValid != FALSE) &&
	    (syNetRollbackResimPostEpisodeKeysEqual(&key, &sSYNetRollbackResimPostCompletedKey) != FALSE))
	{
		local = sSYNetRollbackResimPostCompletedDigest;
		syNetRollbackFillResimPostInputDigest(&sSYNetRollbackResimPostCompletedKey, &local.input_digest);
		syNetRollbackOnResimPostMatchCommitted(&sSYNetRollbackResimPostCompletedKey, &local);
		return;
	}
	if ((key.epoch_id != sSYNetRollbackEpochId) || (key.load_tick != sSYNetRollbackResimLoadTick) ||
	    (key.mismatch_tick != sSYNetRollbackResimMismatchTick) ||
	    (key.target_tick != sSYNetRollbackResimTargetTick))
	{
		sSYNetRollbackResimPostPeerKey = key;
		sSYNetRollbackResimPostPeerDigest = peer;
		sSYNetRollbackResimPostPeerPending = TRUE;
		return;
	}
	h = syNetRollbackCollectHashes();
	local.figh = h.fighter;
	local.world = h.world;
	local.item = h.item;
	local.rng = h.rng;
	syNetRollbackFillResimPostInputDigest(&key, &local.input_digest);
	syNetRollbackCompareResimPostDigests(&key, &local, &peer);
}
#endif

#define SYNETROLLBACK_FRAME_COMMIT_STATE_WINDOW 120U
#define SYNETROLLBACK_BASELINE_GATE_TIMEOUT_FRAMES 10U
#define SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS 2U
#define SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_MAX 3U
#define SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_WINDOW_TICKS 300U
#define SYNETROLLBACK_OUTCOME_PROBE_INTERVAL_TICKS 120U
#define SYNETROLLBACK_MAX_EPISODE_EXTENSIONS 3U
#define SYNETROLLBACK_SYMMETRIC_FC_DEFER_TOLERANCE 2U
#define SYNETROLLBACK_PEER_BASELINE_ECHO_MIN_ADVANCE_TICKS 30U
#define SYNETROLLBACK_PEER_BASELINE_RESYNC_MAX_STEPS 8U
#define SYNETROLLBACK_BASELINE_SNAPSHOT_RETRY_MAX 4U
/* Max deepen retries per load_tick during frame-commit state recovery (baseline packet storm guard). */
#define SYNETROLLBACK_FC_DEEPEN_MAX_PER_LOAD 4U
#define SYNETROLLBACK_LOAD_TICK_REWIND_MAX 16U
#define SYNETROLLBACK_ROLLBACK_COOLDOWN_FRAMES_DEFAULT 2U

#define SYNETROLLBACK_DEBOUNCE_FRAMES_DEFAULT 3U
#define SYNETROLLBACK_LOAD_HASH_SOFT_ROLLBACK_THRESHOLD 8U

static sb32 syNetRollbackBeginResim(u32 mismatch_tick, u32 target_tick, s32 correction_player);
static void syNetRollbackAdvanceResimBudget(void);
static u32 syNetRollbackClampResimTargetTick(u32 mismatch_tick, u32 target_tick);
static u32 syNetRollbackClampResimTargetTickEx(u32 mismatch_tick, u32 target_tick, u32 frontier, sb32 wire_locked);
static u32 syNetRollbackClampResimTargetTickAuthoritative(u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackEpisodeAuthorityEnabled(void);
static void syNetRollbackPendingEpisodeClear(void);
static void syNetRollbackPendingEpisodeSet(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick, u32 epoch_id,
					   u8 flags);
static void syNetRollbackMaybeLogEpisodeExec(sb32 from_peer_notify);
static u32 syNetRollbackComputeRemoteSimResimCap(void);
static u32 syNetRollbackComputeSharedResimTarget(u32 mismatch_tick, u32 validation_tick);
static void syNetRollbackMaybeLogFrontierAheadWarn(u32 local_sim, u32 remote_cap);
static sb32 syNetRollbackSymmetricWireLockActive(void);
static sb32 syNetRollbackSymmetricLocalAuthorityDeferredPending(void);
static void syNetRollbackArmPeerBaselineResync(u32 load_tick);
static void syNetRollbackCollectFighterSlotHashes(u32 out_slot_hash[GMCOMMON_PLAYERS_MAX]);
static sb32 syNetRollbackBaselineProceedOnTimeoutEnabled(void);
static void syNetRollbackPumpResimBaselineSend(void);
static void syNetRollbackOnBaselineGateTimeout(void);
static sb32 syNetRollbackTryRestartResimAtDeeperLoad(u32 deeper_load_tick);
static sb32 syNetRollbackTryAlignActiveEpisodeTuple(s32 slot, u32 load_tick, u32 mismatch_tick, u32 target_tick,
						    sb32 follower_local_auth);
static sb32 syNetRollbackTryDeeperLoadBeforeResim(u32 *io_load_tick, u32 *io_mismatch_tick);
static void syNetRollbackTryOpenResimBaselineGateFromPeerDigest(u32 load_tick, const SYNetRollbackHashSet *peer,
							       const u32 *peer_fighter_slot);
static sb32 syNetRollbackTryEchoBaselineResponse(u32 load_tick);
static sb32 syNetRollbackBaselineEchoAllowed(u32 load_tick);
static void syNetRollbackQueuePeerSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick,
						  sb32 follower_local_auth);
static void syNetRollbackResetPeerBaselineResyncStorm(void);
static sb32 syNetRollbackPeerBaselineResyncStormLimitReached(u32 load_tick);
static void syNetRollbackOnPeerBaselineResyncStormLimit(u32 load_tick);
static u32 syNetRollbackClampLoadTickForPeerSend(u32 load_tick);
static u32 syNetRollbackGetPeerDivergeResyncTicks(void);
static u32 syNetRollbackReanchorMismatchTick(u32 mismatch_tick, u32 frontier);
static u32 syNetRollbackLoadTickMinBound(u32 sim_tick);
static u32 syNetRollbackResolveStateMismatchLoadTick(u32 validation_tick, u32 min_load);
static sb32 syNetRollbackTryNegotiateResimLoadTickWithPeer(u32 peer_load_tick);
static void syNetRollbackResetBaselineResimState(void);
static sb32 syNetRollbackPeerDigestUniverseMismatch(const SYNetRollbackHashSet *peer);
static void syNetRollbackAbortToInputCorrectionFromUniverseMismatch(u32 load_tick);
static void syNetRollbackAbortPendingResimForBaselineMismatch(u32 failed_load_tick);
static void syNetRollbackClearFcStateRecovery(void);
static void syNetRollbackClearFcDeepenGuard(void);
static void syNetRollbackOnFcDeepenStormLimit(u32 load_tick);
static sb32 syNetRollbackTryFcStateRecoveryDeepen(u32 load_tick);
static sb32 syNetRollbackFcStateRecoveryCoversSpan(u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackPeerSymmetricSuppressedByFcStateRecovery(u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackLocalEpisodeConflictsWithPeerNotify(u32 peer_mismatch, u32 peer_target);
static void syNetRollbackAbortInFlightResimForPeerEpisode(void);
static u32 syNetRollbackComputeAuthoritativeFcTarget(u32 mismatch_tick, u32 validation_tick);
static sb32 syNetRollbackUniverseMismatchPreferStateRecovery(u32 load_tick);
static void syNetRollbackComparePeerBaselineToLocal(u32 load_tick, const SYNetRollbackHashSet *peer,
						    const u32 *peer_fighter_slot);
static void syNetRollbackPumpBaselineEchoRetry(void);
static sb32 syNetRollbackBaselineCompareQuiesced(void);
static void syNetRollbackFlushDeferredPeerSymmetric(void);
static sb32 syNetRollbackSnapshotReadyForBaselineCompare(u32 load_tick);
static sb32 syNetRollbackResolveLoadTickForSnapshot(u32 *io_load_tick, u32 *io_mismatch_tick);
static sb32 syNetRollbackPeerBaselineDriftIsAnimOnly(const SYNetRollbackHashSet *peer,
						     const SYNetRollbackHashSet *local);
static sb32 syNetRollbackPeerBaselineDriftIsGameplayOnlyMap(const SYNetRollbackHashSet *peer,
							    const SYNetRollbackHashSet *local);
static void syNetRollbackRunDeferredPeerBaselineCompare(void);
static u32 syNetRollbackGetEpochCapSlack(void);
static u32 syNetRollbackComputePeerEpochLiveCap(void);
static void syNetRollbackNotePeerEpochTarget(s32 slot, u32 mismatch_tick, u32 target_tick);
static void syNetRollbackArmPeerEpochForStateResim(u32 mismatch_tick, u32 target_tick);
static void syNetRollbackClearPeerEpochState(void);
static sb32 syNetRollbackRetainPeerEpochAfterLocalResim(void);
static void syNetRollbackClearPeerEpochAfterEpisodeFsmClose(u32 mismatch_tick, u32 target_tick);
static void syNetRollbackMaybeLogEpochHold(u32 sim_tick, u32 cap, u32 cap_source, u32 local_resim,
					    u32 peer_target);
static void syNetRollbackFailPeerSnapshotDiverge(u32 load_tick, const SYNetRollbackHashSet *peer,
						   const SYNetRollbackHashSet *local, const u32 *peer_fighter_slot);
static void syNetRollbackEpisodeReset(void);
static void syNetRollbackEpisodeSyncToLegacy(void);
static void syNetRollbackEpisodeBegin(u32 mismatch_tick, u32 load_tick, u32 target_tick, s32 corrected_slot,
				      sb32 initiator, sb32 from_peer_notify);
static void syNetRollbackEpisodeSetPhase(SYNetRollbackEpisodePhase phase);
static void syNetRollbackQueueDeferredInputCorrection(s32 player, u32 sim_tick);
static void syNetRollbackQueueDeferredInputCorrectionEx(s32 player, u32 sim_tick, u32 target_tick_override);
static void syNetRollbackArmSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick, sb32 follower_local_auth);
static void syNetRollbackArmSymmetricNotifyEx(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick,
					      u32 epoch_id, sb32 follower_local_auth);
static void syNetRollbackClearSymmetricNotifyAll(void);
static void syNetRollbackOnResimCompleted(void);
static void syNetRollbackArmPredictionRecovery(u32 mismatch_tick, u32 frontier_tick, const SYNetInputFrame *hist);
static void syNetRollbackTryOutcomeAwareCorrection(u32 probe_frontier);
static u32 syNetRollbackFindEarliestPredictedRemoteTick(u32 from_tick, u32 to_tick, s32 *out_player);
static void syNetRollbackCoalesceScanResimSpan(u32 *io_mismatch, u32 *io_target, u32 frontier);
static void syNetRollbackResetCorrectionEpisode(void);
static void syNetRollbackNoteEpisodeResimCompleted(void);
typedef struct SYNetRollbackCorrectionCommitSnap
{
	u32 episode_anchor_mismatch;
	u32 episode_anchor_load_tick;
	u32 episode_last_target_tick;
	u32 episode_extensions;
	u32 last_rollback_begin_sim_tick;
} SYNetRollbackCorrectionCommitSnap;
static void syNetRollbackCorrectionCommitSnapSave(SYNetRollbackCorrectionCommitSnap *snap);
static void syNetRollbackAbortCorrectionCommit(const SYNetRollbackCorrectionCommitSnap *snap);
static sb32 syNetRollbackTryCommitCorrectionBegin(u32 mismatch_tick, u32 load_tick, u32 target_tick,
						  SYNetRollbackCorrectionCommitSnap *out_revert_on_begin_fail);
static void syNetRollbackTryArmSymmetricNotifyForLocalCorrection(s32 player, u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackGlobalCooldownAllows(u32 mismatch_tick);
static sb32 syNetRollbackHistRemoteValueMismatch(const SYNetInputFrame *hist, const SYNetInputFrame *remote);
static sb32 syNetRollbackTickHasValueMismatch(u32 sim_tick, s32 player);
static void syNetRollbackClearTimelineForCompletedResim(void);
static sb32 syNetRollbackPublishedSimUsedPrediction(const SYNetInputFrame *published);
#ifdef PORT
static void syNetRollbackApplySymmetricFollowerPolicy(sb32 rollback_module_active);
static SYNetRollbackHashSet syNetRollbackCollectHashes(void);
static u32 syNetRollbackFindEarliestInputMismatch(u32 frontier_tick, s32 *out_mismatch_player);
#endif

#ifdef PORT
static void syNetRollbackApplySymmetricFollowerPolicy(sb32 rollback_module_active)
{
	const char *env_sym;
	const char *env_sym_diag;

	if (rollback_module_active == FALSE)
	{
		sSYNetRollbackSymmetricEnabled = FALSE;
		sSYNetRollbackSymmetricDiagOnly = TRUE;
		return;
	}
	env_sym = getenv("SSB64_NETPLAY_ROLLBACK_SYMMETRIC");
	if ((env_sym != NULL) && (env_sym[0] != '\0') && (atoi(env_sym) == 0))
	{
		sSYNetRollbackSymmetricEnabled = FALSE;
		sSYNetRollbackSymmetricDiagOnly = TRUE;
		return;
	}
	sSYNetRollbackSymmetricEnabled = TRUE;
	sSYNetRollbackSymmetricDiagOnly = FALSE;
	env_sym_diag = getenv("SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG");
	if ((env_sym_diag != NULL) && (env_sym_diag[0] != '\0') && (atoi(env_sym_diag) != 0))
	{
		sSYNetRollbackSymmetricDiagOnly = TRUE;
	}
}
#endif

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
	sSYNetRollbackResimRngVerifyEnabled = TRUE;
	{
		const char *env_rng;

		env_rng = getenv("SSB64_NETPLAY_RESIM_RNG_VERIFY");
		if ((env_rng != NULL) && (env_rng[0] != '\0') && (atoi(env_rng) == 0))
		{
			sSYNetRollbackResimRngVerifyEnabled = FALSE;
		}
	}
	syNetRollbackApplySymmetricFollowerPolicy(sSYNetRollbackModuleEnabled);
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
	memset(sSYNetRollbackSymmetricNotifyLoadTick, 0, sizeof(sSYNetRollbackSymmetricNotifyLoadTick));
	memset(sSYNetRollbackSymmetricNotifyEpochId, 0, sizeof(sSYNetRollbackSymmetricNotifyEpochId));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackSymmetricNotifyFlags, 0, sizeof(sSYNetRollbackSymmetricNotifyFlags));
	syNetRollbackPendingEpisodeClear();
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	{
		const char *env_ea;

		sSYNetRollbackEpisodeAuthorityEnvCache = -999;
		sSYNetRollbackEpisodeAuthorityEnabled = TRUE;
		env_ea = getenv("SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY");
		if ((env_ea != NULL) && (env_ea[0] != '\0') && (atoi(env_ea) == 0))
		{
			sSYNetRollbackEpisodeAuthorityEnabled = FALSE;
		}
	}
	memset(sSYNetRollbackPeerSymmetricAppliedTick, 0, sizeof(sSYNetRollbackPeerSymmetricAppliedTick));
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
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
	sSYNetRollbackDeferredMismatchFromPeerSymmetric = FALSE;
	sSYNetRollbackPeerSymmetricRejectLiveCap = ~(u32)0;
	sSYNetRollbackStateHashRollback = TRUE;
	sSYNetRollbackStateHashLogsRemaining = 16U;
	sSYNetRollbackFcStateRecoverySuppressLogsRemaining = 8U;
	sSYNetRollbackGgpoCorrectionLogsRemaining = 32U;
	sSYNetRollbackLastPeerOutcomeValid = FALSE;
	sSYNetRollbackLastPeerOutcomeTick = ~(u32)0;
	sSYNetRollbackLastOutcomeProbeFrontier = ~(u32)0;
	sSYNetRollbackOutcomeCorrectionLogsRemaining = 16U;
	sSYNetRollbackCoalescedScanLogsRemaining = 16U;
	syNetRollbackResetCorrectionEpisode();
	sSYNetRollbackLastRollbackBeginSimTick = ~(u32)0;
	sSYNetRollbackSuppressReloadLoadTick = ~(u32)0;
	sSYNetRollbackSuppressReloadUntilSim = 0U;
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
	syNetRollbackClearFcStateRecovery();
	syNetRollbackClearFcDeepenGuard();
	sSYNetRollbackLastBaselineEchoLoadTick = ~(u32)0;
	sSYNetRollbackLastBaselineEchoSimTick = 0U;
	sSYNetRollbackPeerBaselineResyncSteps = 0U;
	sSYNetRollbackPeerBaselineResyncOriginMismatch = ~(u32)0;
	sSYNetRollbackPeerBaselineResyncStormActive = FALSE;
	sSYNetRollbackBaselineEchoRetryLoadTick = ~(u32)0;
	sSYNetRollbackBaselineEchoRetryAttempts = 0U;
	sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
	sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricSlot = -1;
	sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackDeferredPeerBaselineComparePending = FALSE;
	sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
	sSYNetRollbackEpochId = 0U;
	syNetRollbackClearPeerEpochState();
	sSYNetRollbackEpochHoldLogsRemaining = 16U;
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
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		if (syNetRollbackEpisodeFsmIsResimulating() != FALSE)
		{
			return TRUE;
		}
	}
	else if (sSYNetRollbackResimPending != FALSE)
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
	syNetRollbackApplySymmetricFollowerPolicy(sSYNetRollbackModuleEnabled);
	port_log(
	    "SSB64 NetRollback: session_negotiated enabled=%d symmetric=%d symmetric_diag_only=%d resim_rng_verify=%d "
	    "snap_frames=%u resim_per_frame=%u phase_lock=%u\n",
	    (sSYNetRollbackModuleEnabled != FALSE) ? 1 : 0, (sSYNetRollbackSymmetricEnabled != FALSE) ? 1 : 0,
	    (sSYNetRollbackSymmetricDiagOnly != FALSE) ? 1 : 0, (sSYNetRollbackResimRngVerifyEnabled != FALSE) ? 1 : 0,
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
	memset(sSYNetRollbackSymmetricNotifyLoadTick, 0, sizeof(sSYNetRollbackSymmetricNotifyLoadTick));
	memset(sSYNetRollbackSymmetricNotifyEpochId, 0, sizeof(sSYNetRollbackSymmetricNotifyEpochId));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackSymmetricNotifyFlags, 0, sizeof(sSYNetRollbackSymmetricNotifyFlags));
	syNetRollbackPendingEpisodeClear();
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	{
		const char *env_ea;

		sSYNetRollbackEpisodeAuthorityEnvCache = -999;
		sSYNetRollbackEpisodeAuthorityEnabled = TRUE;
		env_ea = getenv("SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY");
		if ((env_ea != NULL) && (env_ea[0] != '\0') && (atoi(env_ea) == 0))
		{
			sSYNetRollbackEpisodeAuthorityEnabled = FALSE;
		}
	}
	memset(sSYNetRollbackPeerSymmetricAppliedTick, 0, sizeof(sSYNetRollbackPeerSymmetricAppliedTick));
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
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
	sSYNetRollbackDeferredMismatchFromPeerSymmetric = FALSE;
	sSYNetRollbackPeerSymmetricRejectLiveCap = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
	syNetRollbackClearFcStateRecovery();
	syNetRollbackClearFcDeepenGuard();
	sSYNetRollbackFrontierAheadWarnLogsRemaining = 8U;
	sSYNetRollbackLastFrameCommitStateAgreedTick = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackPeerBaselineRetransmitCount = 0U;
	sSYNetRollbackResimBaselineDeeperAttempts = 0U;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackBaselineTimeoutWindowStartTick = 0U;
	sSYNetRollbackResimCorrectionPlayer = -1;
	sSYNetRollbackLastPeerOutcomeValid = FALSE;
	sSYNetRollbackLastPeerOutcomeTick = ~(u32)0;
	sSYNetRollbackLastOutcomeProbeFrontier = ~(u32)0;
	syNetRollbackResetCorrectionEpisode();
	sSYNetRollbackLastRollbackBeginSimTick = ~(u32)0;
	sSYNetRollbackSuppressReloadLoadTick = ~(u32)0;
	sSYNetRollbackSuppressReloadUntilSim = 0U;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineDigestMatched = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackLastBaselineEchoLoadTick = ~(u32)0;
	sSYNetRollbackLastBaselineEchoSimTick = 0U;
	sSYNetRollbackPeerBaselineResyncSteps = 0U;
	sSYNetRollbackPeerBaselineResyncOriginMismatch = ~(u32)0;
	sSYNetRollbackPeerBaselineResyncStormActive = FALSE;
	sSYNetRollbackBaselineEchoRetryLoadTick = ~(u32)0;
	sSYNetRollbackBaselineEchoRetryAttempts = 0U;
	sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
	sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricSlot = -1;
	sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackDeferredPeerBaselineComparePending = FALSE;
	sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
	syNetRollbackClearPeerEpochState();
	syNetRollbackEpisodeReset();
	syNetRollbackEpisodeFsmSessionReset();
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
	memset(sSYNetRollbackSymmetricNotifyLoadTick, 0, sizeof(sSYNetRollbackSymmetricNotifyLoadTick));
	memset(sSYNetRollbackSymmetricNotifyEpochId, 0, sizeof(sSYNetRollbackSymmetricNotifyEpochId));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackSymmetricNotifyFlags, 0, sizeof(sSYNetRollbackSymmetricNotifyFlags));
	syNetRollbackPendingEpisodeClear();
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	{
		const char *env_ea;

		sSYNetRollbackEpisodeAuthorityEnvCache = -999;
		sSYNetRollbackEpisodeAuthorityEnabled = TRUE;
		env_ea = getenv("SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY");
		if ((env_ea != NULL) && (env_ea[0] != '\0') && (atoi(env_ea) == 0))
		{
			sSYNetRollbackEpisodeAuthorityEnabled = FALSE;
		}
	}
	memset(sSYNetRollbackPeerSymmetricAppliedTick, 0, sizeof(sSYNetRollbackPeerSymmetricAppliedTick));
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
	sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricSlot = -1;
	sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackDeferredPeerBaselineComparePending = FALSE;
	sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
	syNetRollbackClearPeerEpochState();
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackPeerBaselineRetransmitCount = 0U;
	sSYNetRollbackResimBaselineDeeperAttempts = 0U;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackLastBaselineEchoLoadTick = ~(u32)0;
	sSYNetRollbackLastBaselineEchoSimTick = 0U;
	sSYNetRollbackPeerBaselineResyncSteps = 0U;
	sSYNetRollbackPeerBaselineResyncOriginMismatch = ~(u32)0;
	sSYNetRollbackPeerBaselineResyncStormActive = FALSE;
	syNetRollbackEpisodeReset();
	syNetRollbackEpisodeFsmSessionReset();
	sSYNetRollbackResimCorrectionPlayer = -1;
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
	syNetRollbackClearFcStateRecovery();
	syNetRollbackClearFcDeepenGuard();
	sSYNetRollbackResimPostPeerPending = FALSE;
	sSYNetRollbackResimPostLocalValid = FALSE;
	sSYNetRollbackResimPostCompletedValid = FALSE;
	sSYNetRollbackResimPostMatchLogged = FALSE;
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

/* Debounce bypass for continued GGPO / scan corrections in the same prediction episode. */
static sb32 syNetRollbackCorrectionAllowedAtTick(u32 mismatch_tick)
{
	u32 prediction_window;
	u32 episode_end;

	if (syNetRollbackSymmetricLocalAuthorityDeferredPending() != FALSE)
	{
		if ((mismatch_tick >= sSYNetRollbackDeferredMismatchTick) &&
		    (mismatch_tick <= sSYNetRollbackDeferredMismatchTargetTick))
		{
			return TRUE;
		}
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (sSYNetRollbackPendingPeerSymmetricTargetTick != ~(u32)0) &&
	    (mismatch_tick >= sSYNetRollbackPendingPeerSymmetricTick) &&
	    (mismatch_tick <= sSYNetRollbackPendingPeerSymmetricTargetTick))
	{
		return TRUE;
	}
	if ((sSYNetRollbackPeerEpochMismatchTick != 0U) && (sSYNetRollbackPeerEpochTargetTick != 0U) &&
	    (mismatch_tick >= sSYNetRollbackPeerEpochMismatchTick) &&
	    (mismatch_tick <= sSYNetRollbackPeerEpochTargetTick))
	{
		return TRUE;
	}
	if (syNetRollbackMismatchAllowedDuringDebounce(mismatch_tick) != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackLastCommittedMismatchTick == ~(u32)0)
	{
		return FALSE;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	if (sSYNetRollbackLastCommittedMismatchTick > (~(u32)0) - prediction_window)
	{
		episode_end = ~(u32)0;
	}
	else
	{
		episode_end = sSYNetRollbackLastCommittedMismatchTick + prediction_window;
	}
	if ((mismatch_tick >= sSYNetRollbackLastCommittedMismatchTick) && (mismatch_tick <= episode_end))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRollbackResetCorrectionEpisode(void)
{
	sSYNetRollbackEpisodeAnchorMismatch = ~(u32)0;
	sSYNetRollbackEpisodeAnchorLoadTick = ~(u32)0;
	sSYNetRollbackEpisodeLastTargetTick = 0U;
	sSYNetRollbackEpisodeResolvedThrough = 0U;
	sSYNetRollbackEpisodeExtensions = 0U;
}

static void syNetRollbackCloseCorrectionEpisode(u32 completed_target)
{
	if ((completed_target != 0U) && (completed_target != ~(u32)0))
	{
		if (completed_target > sSYNetRollbackEpisodeResolvedThrough)
		{
			sSYNetRollbackEpisodeResolvedThrough = completed_target;
		}
		if ((sSYNetRollbackPeerEpochTargetTick != 0U) && (completed_target >= sSYNetRollbackPeerEpochTargetTick) &&
		    (syNetRollbackRetainPeerEpochAfterLocalResim() == FALSE))
		{
			syNetRollbackClearPeerEpochState();
		}
	}
	sSYNetRollbackEpisodeAnchorMismatch = ~(u32)0;
	sSYNetRollbackEpisodeAnchorLoadTick = ~(u32)0;
	sSYNetRollbackEpisodeLastTargetTick = 0U;
	sSYNetRollbackEpisodeExtensions = 0U;
}

static sb32 syNetRollbackDeferDiagEnabled(void)
{
	const char *env;

	env = getenv("SSB64_NETPLAY_ROLLBACK_DEFER_DIAG");
	return ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? TRUE : FALSE;
}

static void syNetRollbackLogDeferDiag(const char *stage, u32 mismatch, u32 target, s32 player)
{
	if (syNetRollbackDeferDiagEnabled() == FALSE)
	{
		return;
	}
	port_log(
	    "SSB64 NetRollback: defer_diag stage=%s mismatch=%u target=%u player=%d resim_pending=%d anchor=%u resolved_through=%u extensions=%u\n",
	    (stage != NULL) ? stage : "?",
	    mismatch,
	    target,
	    (int)player,
	    (int)sSYNetRollbackResimPending,
	    (unsigned int)sSYNetRollbackEpisodeAnchorMismatch,
	    (unsigned int)sSYNetRollbackEpisodeResolvedThrough,
	    (unsigned int)sSYNetRollbackEpisodeExtensions);
}

sb32 syNetRollbackPredictionRecoveryEnabled(void)
{
	const char *env;

	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	env = getenv("SSB64_NETPLAY_PREDICTION_RECOVERY");
	return ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? TRUE : FALSE;
}

sb32 syNetRollbackStickMismatchRecoveryEnabled(void)
{
	const char *env;
	static sb32 sCached = -1;

	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	if (sCached >= 0)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_STICK_MISMATCH_RECOVERY");
	if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
	{
		sCached = 0;
	}
	else
	{
		sCached = 1;
	}
	return (sCached != 0) ? TRUE : FALSE;
}

static u32 syNetRollbackRollbackCooldownFrames(void)
{
	const char *env;
	s32 parsed;
	u32 frames;

	frames = SYNETROLLBACK_ROLLBACK_COOLDOWN_FRAMES_DEFAULT;
	env = getenv("SSB64_NETPLAY_ROLLBACK_COOLDOWN_FRAMES");
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

static sb32 syNetRollbackGlobalCooldownAllows(u32 mismatch_tick)
{
	u32 sim_tick;
	u32 cooldown;
	u32 earliest_allowed;

	if (sSYNetRollbackLastRollbackBeginSimTick == ~(u32)0)
	{
		return TRUE;
	}
	sim_tick = syNetInputGetTick();
	cooldown = syNetRollbackRollbackCooldownFrames();
	if (cooldown == 0U)
	{
		return TRUE;
	}
	if (sim_tick >= sSYNetRollbackLastRollbackBeginSimTick + cooldown)
	{
		return TRUE;
	}
	if (mismatch_tick < sSYNetRollbackLastCommittedMismatchTick)
	{
		return TRUE;
	}
	if (mismatch_tick == ~(u32)0)
	{
		return FALSE;
	}
	earliest_allowed = mismatch_tick;
	if (sSYNetRollbackLastCommittedMismatchTick != ~(u32)0)
	{
		earliest_allowed = sSYNetRollbackLastCommittedMismatchTick;
	}
	if (mismatch_tick < earliest_allowed)
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRollbackCorrectionCommitSnapSave(SYNetRollbackCorrectionCommitSnap *snap)
{
	snap->episode_anchor_mismatch = sSYNetRollbackEpisodeAnchorMismatch;
	snap->episode_anchor_load_tick = sSYNetRollbackEpisodeAnchorLoadTick;
	snap->episode_last_target_tick = sSYNetRollbackEpisodeLastTargetTick;
	snap->episode_extensions = sSYNetRollbackEpisodeExtensions;
	snap->last_rollback_begin_sim_tick = sSYNetRollbackLastRollbackBeginSimTick;
}

static void syNetRollbackAbortCorrectionCommit(const SYNetRollbackCorrectionCommitSnap *snap)
{
	if (snap != NULL)
	{
		sSYNetRollbackEpisodeAnchorMismatch = snap->episode_anchor_mismatch;
		sSYNetRollbackEpisodeAnchorLoadTick = snap->episode_anchor_load_tick;
		sSYNetRollbackEpisodeLastTargetTick = snap->episode_last_target_tick;
		sSYNetRollbackEpisodeExtensions = snap->episode_extensions;
		sSYNetRollbackLastRollbackBeginSimTick = snap->last_rollback_begin_sim_tick;
	}
	syNetRollbackClearSymmetricNotifyAll();
}

static sb32 syNetRollbackTryCommitCorrectionBegin(u32 mismatch_tick, u32 load_tick, u32 target_tick,
						  SYNetRollbackCorrectionCommitSnap *out_revert_on_begin_fail)
{
	u32 sim_tick;
	SYNetRollbackCorrectionCommitSnap snap;

	syNetRollbackCorrectionCommitSnapSave(&snap);

	if ((mismatch_tick == 0U) || (target_tick <= mismatch_tick) || (load_tick == ~(u32)0))
	{
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	if (syNetRollbackGlobalCooldownAllows(mismatch_tick) == FALSE)
	{
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if ((load_tick == sSYNetRollbackSuppressReloadLoadTick) && (sim_tick <= sSYNetRollbackSuppressReloadUntilSim))
	{
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch_tick < sSYNetRollbackEpisodeResolvedThrough))
	{
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	if (sSYNetRollbackEpisodeAnchorMismatch == ~(u32)0)
	{
		sSYNetRollbackEpisodeAnchorMismatch = mismatch_tick;
		sSYNetRollbackEpisodeAnchorLoadTick = load_tick;
		sSYNetRollbackEpisodeLastTargetTick = 0U;
		sSYNetRollbackEpisodeExtensions = 0U;
	}
	else if (mismatch_tick < sSYNetRollbackEpisodeAnchorMismatch)
	{
		if (sSYNetRollbackPeerBaselineResyncStormActive != FALSE)
		{
			sSYNetRollbackEpisodeAnchorMismatch = mismatch_tick;
			sSYNetRollbackEpisodeAnchorLoadTick = load_tick;
			sSYNetRollbackEpisodeExtensions++;
			if (sSYNetRollbackEpisodeExtensions >= SYNETROLLBACK_MAX_EPISODE_EXTENSIONS)
			{
				syNetRollbackAbortCorrectionCommit(&snap);
				return FALSE;
			}
		}
		else
		{
			syNetRollbackResetCorrectionEpisode();
			sSYNetRollbackEpisodeAnchorMismatch = mismatch_tick;
			sSYNetRollbackEpisodeAnchorLoadTick = load_tick;
			sSYNetRollbackEpisodeExtensions = 0U;
		}
	}
	else if (load_tick == sSYNetRollbackEpisodeAnchorLoadTick)
	{
		if (target_tick <= sSYNetRollbackEpisodeLastTargetTick)
		{
			syNetRollbackAbortCorrectionCommit(&snap);
			return FALSE;
		}
		if (sSYNetRollbackEpisodeExtensions >= SYNETROLLBACK_MAX_EPISODE_EXTENSIONS)
		{
			syNetRollbackAbortCorrectionCommit(&snap);
			return FALSE;
		}
		sSYNetRollbackEpisodeExtensions++;
	}
	sSYNetRollbackEpisodeLastTargetTick = target_tick;
	sSYNetRollbackLastRollbackBeginSimTick = sim_tick;
	if (out_revert_on_begin_fail != NULL)
	{
		*out_revert_on_begin_fail = snap;
	}
	return TRUE;
}

static void syNetRollbackNoteEpisodeResimCompleted(void)
{
	u32 sim_tick;
	u32 cooldown;

	sim_tick = syNetInputGetTick();
	if (sSYNetRollbackResimLoadTick != ~(u32)0)
	{
		sSYNetRollbackSuppressReloadLoadTick = sSYNetRollbackResimLoadTick;
		cooldown = syNetRollbackRollbackCooldownFrames();
		if (cooldown < 1U)
		{
			cooldown = 1U;
		}
		if (sim_tick > (~(u32)0) - cooldown)
		{
			sSYNetRollbackSuppressReloadUntilSim = ~(u32)0;
		}
		else
		{
			sSYNetRollbackSuppressReloadUntilSim = sim_tick + cooldown;
		}
	}
	if (sSYNetRollbackResimTargetTick > sSYNetRollbackEpisodeResolvedThrough)
	{
		sSYNetRollbackEpisodeResolvedThrough = sSYNetRollbackResimTargetTick;
	}
	if (sSYNetRollbackEpisodeAnchorMismatch == ~(u32)0)
	{
		sSYNetRollbackEpisodeAnchorMismatch = sSYNetRollbackResimMismatchTick;
		sSYNetRollbackEpisodeAnchorLoadTick = sSYNetRollbackResimLoadTick;
	}
	if (sSYNetRollbackResimTargetTick > sSYNetRollbackEpisodeLastTargetTick)
	{
		sSYNetRollbackEpisodeLastTargetTick = sSYNetRollbackResimTargetTick;
	}
}

static void syNetRollbackQueueDeferredInputCorrection(s32 player, u32 sim_tick)
{
	syNetRollbackQueueDeferredInputCorrectionEx(player, sim_tick, 0U);
}

static void syNetRollbackClearPeerSymmetricRejectLiveCap(void)
{
	sSYNetRollbackPeerSymmetricRejectLiveCap = ~(u32)0;
}

static void syNetRollbackArmPeerSymmetricRejectLiveCap(u32 mismatch_tick)
{
	u32 cap;

	cap = (mismatch_tick > 0U) ? (mismatch_tick - 1U) : 0U;
	sSYNetRollbackPeerSymmetricRejectLiveCap = cap;
	if (syNetRollbackDeferDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRollback: defer_diag symmetric_reject_live_cap cap=%u mismatch=%u\n", cap,
		         mismatch_tick);
	}
}

static sb32 syNetRollbackPeerSymmetricRejectBlocksLiveAdvance(u32 *out_cap)
{
	if ((out_cap == NULL) || (sSYNetRollbackPeerSymmetricRejectLiveCap == ~(u32)0))
	{
		return FALSE;
	}
	*out_cap = sSYNetRollbackPeerSymmetricRejectLiveCap;
	return TRUE;
}

/* Hold live sim below peer-symmetric mismatch until snapshot load (follower pre-load figatree freeze). */
static sb32 syNetRollbackPeerSymmetricNotifyBlocksLiveAdvance(u32 *out_cap)
{
	u32 mismatch_tick;

	if (out_cap == NULL)
	{
		return FALSE;
	}
	mismatch_tick = ~(u32)0;
	if (sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0)
	{
		mismatch_tick = sSYNetRollbackPendingPeerSymmetricTick;
	}
	else if (sSYNetRollbackDeferredPeerSymmetricPending != FALSE)
	{
		mismatch_tick = sSYNetRollbackDeferredPeerSymmetricTick;
	}
	else
	{
		return FALSE;
	}
	if ((mismatch_tick == 0U) || (mismatch_tick == ~(u32)0))
	{
		return FALSE;
	}
	*out_cap = mismatch_tick - 1U;
	return TRUE;
}

static void syNetRollbackQueueDeferredInputCorrectionEx(s32 player, u32 sim_tick, u32 target_tick_override)
{
	u32 frontier;
	u32 target_tick;
	u32 prediction_window;
	u32 extended_target;
	sb32 from_peer_symmetric;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	from_peer_symmetric = sSYNetRollbackDeferredMismatchFromPeerSymmetric;
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if (from_peer_symmetric != FALSE)
	{
		if (sim_tick > frontier)
		{
			syNetRollbackLogDeferDiag("symmetric_queue_past_frontier", sim_tick, frontier, player);
			return;
		}
		if ((sim_tick == frontier) && (syNetRollbackDeferDiagEnabled() != FALSE))
		{
			syNetRollbackLogDeferDiag("symmetric_queue_at_frontier", sim_tick, frontier, player);
		}
	}
	else if (sim_tick >= frontier)
	{
		syNetRollbackLogDeferDiag("queue_past_frontier", sim_tick, frontier, player);
		return;
	}
	target_tick = frontier;
	if (target_tick <= sim_tick)
	{
		target_tick = sim_tick + 1U;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	if (sim_tick <= ~(u32)0 - prediction_window)
	{
		extended_target = sim_tick + prediction_window;
		if (extended_target > target_tick)
		{
			target_tick = (extended_target <= frontier) ? extended_target : frontier;
		}
	}
	if ((target_tick_override != 0U) && (target_tick_override > target_tick))
	{
		target_tick = target_tick_override;
		if ((from_peer_symmetric != FALSE) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
		{
			target_tick = syNetRollbackClampResimTargetTickAuthoritative(sim_tick, target_tick);
		}
		else
		{
			if ((target_tick > frontier) && (syNetRollbackSymmetricWireLockActive() == FALSE))
			{
				target_tick = frontier;
			}
			target_tick = syNetRollbackClampResimTargetTick(sim_tick, target_tick);
		}
	}
	else
	{
		target_tick = syNetRollbackClampResimTargetTick(sim_tick, target_tick);
	}
	if (sSYNetRollbackDeferredMismatchPending != FALSE)
	{
		if (sim_tick < sSYNetRollbackDeferredMismatchTick)
		{
			sSYNetRollbackDeferredMismatchTick = sim_tick;
			sSYNetRollbackDeferredMismatchPlayer = player;
		}
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
	if (from_peer_symmetric != FALSE)
	{
		syNetRollbackClearPeerSymmetricRejectLiveCap();
		syNetRollbackLogDeferDiag("symmetric_queue_ok", sim_tick, target_tick, player);
	}
}

sb32 syNetRollbackShouldQueueGgpoCorrection(u32 sim_tick)
{
	return syNetRollbackMismatchAllowedDuringDebounce(sim_tick);
}

void syNetRollbackRequestInputCorrection(s32 player, u32 sim_tick)
{
	SYNetInputFrame published;
	SYNetInputFrame remote;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		syNetRollbackDeferRemoteInputCorrection(player, sim_tick);
		return;
	}
	if (syNetRollbackShouldQueueGgpoCorrection(sim_tick) == FALSE)
	{
		return;
	}
	if ((syNetInputGetHistoryFrame(player, sim_tick, &published) == FALSE) ||
	    (syNetInputGetRemoteHistoryFrame(player, sim_tick, &remote) == FALSE))
	{
		return;
	}
	if (syNetRollbackPublishedSimUsedPrediction(&published) == FALSE)
	{
		return;
	}
	if (syNetInputShouldDeferPredictedAnalogCorrection(player, sim_tick, &published, &remote) != FALSE)
	{
		return;
	}
	if (syNetInputGameplayCorrectionIsSignificantEx(&published, &remote, TRUE) == FALSE)
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
	if ((syNetRollbackStickMismatchRecoveryEnabled() != FALSE) &&
	    (syNetInputGgpoStickNeutralAnalogFlip(&published, &remote) != FALSE))
	{
		syNetRollbackArmPredictionRecoveryForStickMismatch(sim_tick, syNetInputGetTick());
	}
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		SYNetRollbackEpisodeEvent ev;
		u32 frontier;

		memset(&ev, 0, sizeof(ev));
		ev.type = nSYNetRollbackEpisodeEventInputMismatch;
		ev.slot = player;
		ev.mismatch_tick = sim_tick;
		frontier = syNetInputGetTick();
		if (frontier < ~(u32)0)
		{
			frontier++;
		}
		ev.target_tick = (frontier > sim_tick) ? frontier : (sim_tick + 1U);
		syNetRollbackEpisodeEnqueueEvent(&ev);
		return;
	}
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

static u32 syNetRollbackFindEarliestPredictedRemoteTick(u32 from_tick, u32 to_tick, s32 *out_player)
{
	u32 t;
	s32 ri;
	s32 remote_player;
	SYNetInputFrame published;

	if (out_player != NULL)
	{
		*out_player = -1;
	}
	if ((from_tick >= to_tick) || (to_tick == 0U))
	{
		return ~(u32)0;
	}
	if (from_tick == 0U)
	{
		from_tick = 1U;
	}
	for (t = from_tick; t < to_tick; t++)
	{
		for (ri = 0; ri < syNetPeerGetRemoteHumanSlotCount(); ri++)
		{
			if (syNetPeerGetRemoteHumanSlotByIndex(ri, &remote_player) == FALSE)
			{
				continue;
			}
			if ((remote_player < 0) || (remote_player >= MAXCONTROLLERS))
			{
				continue;
			}
			if (syNetInputGetHistoryFrame(remote_player, t, &published) == FALSE)
			{
				continue;
			}
			if (syNetRollbackPublishedSimUsedPrediction(&published) == FALSE)
			{
				continue;
			}
			if (out_player != NULL)
			{
				*out_player = remote_player;
			}
			return t;
		}
	}
	return ~(u32)0;
}

static sb32 syNetRollbackOutcomeCorrectionEnabled(void)
{
	const char *env;

	env = getenv("SSB64_NETPLAY_ROLLBACK_OUTCOME_CORRECT");
	if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
	{
		return FALSE;
	}
	return TRUE;
}

static s32 syNetRollbackPeerSymmetricAuthoritySlotForPlayer(s32 player)
{
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return -1;
	}
	if ((player == syNetPeerGetLocalSimSlot()) || (player == syNetPeerGetExtraLocalSenderSimSlot()))
	{
		return player;
	}
	return -1;
}

static sb32 syNetRollbackOutcomeCorrectionSuppressedDuringEpisode(u32 probe_frontier)
{
	u32 prediction_window;
	u32 suppress_until;

	if (sSYNetRollbackEpisodeAnchorMismatch == ~(u32)0)
	{
		return FALSE;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	if (sSYNetRollbackEpisodeLastTargetTick > (~(u32)0) - (prediction_window * 2U))
	{
		suppress_until = ~(u32)0;
	}
	else
	{
		suppress_until = sSYNetRollbackEpisodeLastTargetTick + (prediction_window * 2U);
	}
	return (probe_frontier <= suppress_until) ? TRUE : FALSE;
}

void syNetRollbackNotifyLocalAuthorityTransmitRevision(s32 player, u32 sim_tick)
{
	u32 frontier;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	if (syNetRollbackPeerSymmetricAuthoritySlotForPlayer(player) < 0)
	{
		return;
	}
	if (syNetRollbackShouldQueueGgpoCorrection(sim_tick) == FALSE)
	{
		return;
	}
	if (syNetRollbackCorrectionAllowedAtTick(sim_tick) == FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if (frontier <= sim_tick)
	{
		frontier = sim_tick + 1U;
	}
	syNetRollbackQueueDeferredInputCorrectionEx(player, sim_tick, frontier);
}

static void syNetRollbackTryOutcomeAwareCorrection(u32 probe_frontier)
{
	SYNetRollbackHashSet live;
	u32 mismatch;
	u32 target_tick;
	u32 begin;
	u32 prediction_window;
	s32 player;

	if ((syNetRollbackOutcomeCorrectionEnabled() == FALSE) || (syNetRollbackIsActive() == FALSE) ||
	    (probe_frontier == 0U) || (sSYNetRollbackLastPeerOutcomeValid == FALSE))
	{
		return;
	}
	if (syNetRollbackOutcomeCorrectionSuppressedDuringEpisode(probe_frontier) != FALSE)
	{
		return;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE) ||
	    (sSYNetRollbackDeferredMismatchPending != FALSE))
	{
		return;
	}
	if (probe_frontier <= sSYNetRollbackLastPeerOutcomeTick)
	{
		return;
	}
	if ((probe_frontier - sSYNetRollbackLastPeerOutcomeTick) > 16U)
	{
		return;
	}
	if (syNetRollbackFindEarliestInputMismatch(probe_frontier, NULL) != ~(u32)0)
	{
		return;
	}
	live = syNetRollbackCollectHashes();
	if ((live.fighter == sSYNetRollbackLastPeerOutcomeHash.fighter) &&
	    (live.map == sSYNetRollbackLastPeerOutcomeHash.map))
	{
		return;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	begin = 0U;
	if (probe_frontier > prediction_window)
	{
		begin = probe_frontier - prediction_window;
	}
	mismatch = syNetRollbackFindEarliestPredictedRemoteTick(begin, probe_frontier, &player);
	if (mismatch == ~(u32)0)
	{
		if (sSYNetRollbackLastPeerOutcomeTick > 0U)
		{
			mismatch = sSYNetRollbackLastPeerOutcomeTick;
		}
		else
		{
			return;
		}
	}
	if (player < 0)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(0, &player) == FALSE)
		{
			return;
		}
	}
	target_tick = probe_frontier;
	if (target_tick <= mismatch)
	{
		target_tick = mismatch + 1U;
	}
	if (syNetRollbackTryCommitCorrectionBegin(mismatch, mismatch - 1U, target_tick, NULL) == FALSE)
	{
		return;
	}
	if (sSYNetRollbackOutcomeCorrectionLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: outcome-aware correction queued mismatch=%u target=%u live_figh=0x%08X peer_figh=0x%08X live_mph=0x%08X peer_mph=0x%08X\n",
		    mismatch,
		    target_tick,
		    live.fighter,
		    sSYNetRollbackLastPeerOutcomeHash.fighter,
		    live.map,
		    sSYNetRollbackLastPeerOutcomeHash.map);
		sSYNetRollbackOutcomeCorrectionLogsRemaining--;
	}
	syNetRollbackQueueDeferredInputCorrectionEx(player, mismatch, target_tick);
}

static void syNetRollbackSchedulePostResimInputFollowup(void)
{
	u32 frontier;
	u32 mismatch;
	u32 target_tick;
	u32 prediction_window;
	s32 player;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, &player);
	if (mismatch == ~(u32)0)
	{
		if (syNetRollbackOutcomeCorrectionSuppressedDuringEpisode(frontier) == FALSE)
		{
			syNetRollbackTryOutcomeAwareCorrection(frontier);
		}
		return;
	}
	if (syNetRollbackTickHasValueMismatch(mismatch, player) == FALSE)
	{
		if (syNetRollbackOutcomeCorrectionSuppressedDuringEpisode(frontier) == FALSE)
		{
			syNetRollbackTryOutcomeAwareCorrection(frontier);
		}
		return;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	if (mismatch > (sSYNetRollbackResimTargetTick + prediction_window))
	{
		return;
	}
	if ((sSYNetRollbackResimMismatchTick != ~(u32)0) && (sSYNetRollbackResimTargetTick != ~(u32)0) &&
	    (mismatch >= sSYNetRollbackResimMismatchTick) && (mismatch <= (sSYNetRollbackResimTargetTick + 1U)))
	{
		return;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch <= sSYNetRollbackEpisodeResolvedThrough) &&
	    (syNetRollbackTickHasValueMismatch(mismatch, player) == FALSE))
	{
		return;
	}
	if (player < 0)
	{
		player = sSYNetRollbackDeferredMismatchPlayer;
	}
	if (player < 0)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(0, &player) == FALSE)
		{
			return;
		}
	}
	target_tick = frontier;
	if (target_tick <= mismatch)
	{
		target_tick = mismatch + 1U;
	}
	if (target_tick < sSYNetRollbackResimTargetTick)
	{
		target_tick = sSYNetRollbackResimTargetTick;
	}
	if (syNetRollbackTryCommitCorrectionBegin(mismatch, mismatch - 1U, target_tick, NULL) == FALSE)
	{
		return;
	}
	if (sSYNetRollbackCoalescedScanLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: post-resim input followup deferred mismatch=%u target=%u (prior resim %u->%u)\n",
		    mismatch,
		    target_tick,
		    sSYNetRollbackResimMismatchTick,
		    sSYNetRollbackResimTargetTick);
		sSYNetRollbackCoalescedScanLogsRemaining--;
	}
	syNetRollbackQueueDeferredInputCorrectionEx(player, mismatch, target_tick);
}

static void syNetRollbackOnResimCompleted(void)
{
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick != ~(u32)0) &&
	    (sSYNetRollbackResimTargetTick >= sSYNetRollbackFcStateRecoveryTargetTick) &&
	    (sSYNetRollbackResimMismatchTick != ~(u32)0) &&
	    (sSYNetRollbackResimMismatchTick >= sSYNetRollbackFcStateRecoveryMismatchTick))
	{
		syNetRollbackClearFcStateRecovery();
		syNetRollbackClearFcDeepenGuard();
	}
	syNetRollbackNoteEpisodeResimCompleted();
	syNetRollbackClearTimelineForCompletedResim();
	if ((sSYNetRollbackResimRngVerifyEnabled != FALSE) && (syNetRollbackIsActive() != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: resim_rng_verify mismatch=%u target=%u rng=0x%08X\n",
		    sSYNetRollbackResimMismatchTick,
		    sSYNetRollbackResimTargetTick,
		    syNetSyncHashRNGSeed());
	}
	syNetRollbackTryEmitResimPostHandshake();
	syNetRollbackFlushPendingResimPostHandshake();
	syNetRollbackTryCompleteEpisodeAfterPostMatch();
	syNetRollbackClearResimPostBoundaryDigest();
	sSYNetRollbackResimLoadTick = ~(u32)0;
	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		syNetRollbackSchedulePostResimInputFollowup();
	}
}

static void syNetRollbackCoalesceScanResimSpan(u32 *io_mismatch, u32 *io_target, u32 frontier)
{
	u32 prediction_window;
	u32 merged_mismatch;
	u32 merged_target;

	if ((io_mismatch == NULL) || (io_target == NULL) || (*io_mismatch == ~(u32)0))
	{
		return;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (*io_mismatch < sSYNetRollbackEpisodeResolvedThrough))
	{
		return;
	}
	if (sSYNetRollbackEpisodeExtensions >= SYNETROLLBACK_MAX_EPISODE_EXTENSIONS)
	{
		return;
	}
	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	if ((sSYNetRollbackLastCommittedMismatchTick != ~(u32)0) &&
	    (*io_mismatch > (sSYNetRollbackLastCommittedMismatchTick + prediction_window)))
	{
		return;
	}
	merged_mismatch = *io_mismatch;
	if ((sSYNetRollbackLastCommittedMismatchTick != ~(u32)0) &&
	    (merged_mismatch > sSYNetRollbackLastCommittedMismatchTick) &&
	    (syNetRollbackTickHasValueMismatch(sSYNetRollbackLastCommittedMismatchTick, -1) != FALSE))
	{
		merged_mismatch = sSYNetRollbackLastCommittedMismatchTick;
	}
	merged_target = *io_target;
	if ((merged_target < frontier) && (syNetRollbackTickHasValueMismatch(merged_mismatch, -1) != FALSE))
	{
		merged_target = frontier;
	}
	if (merged_target <= merged_mismatch)
	{
		return;
	}
	if (merged_target <= sSYNetRollbackEpisodeLastTargetTick)
	{
		return;
	}
	if ((merged_mismatch != *io_mismatch) || (merged_target > *io_target))
	{
		if (sSYNetRollbackCoalescedScanLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: coalesced scan resim mismatch=%u->%u target=%u->%u frontier=%u ext=%u\n",
			    *io_mismatch,
			    merged_mismatch,
			    *io_target,
			    merged_target,
			    frontier,
			    sSYNetRollbackEpisodeExtensions + 1U);
			sSYNetRollbackCoalescedScanLogsRemaining--;
		}
	}
	*io_mismatch = merged_mismatch;
	*io_target = merged_target;
}

static void syNetRollbackClearDeferredInputMismatch(void)
{
	sSYNetRollbackDeferredMismatchPending = FALSE;
	sSYNetRollbackDeferredMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPlayer = -1;
	sSYNetRollbackDeferredMismatchFromPeerSymmetric = FALSE;
	syNetRollbackClearPeerSymmetricRejectLiveCap();
}

static sb32 syNetRollbackSymmetricLocalAuthorityDeferredPending(void)
{
	if ((sSYNetRollbackDeferredMismatchPending == FALSE) ||
	    (sSYNetRollbackDeferredMismatchFromPeerSymmetric == FALSE))
	{
		return FALSE;
	}
	if ((sSYNetRollbackDeferredMismatchTick == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick <= sSYNetRollbackDeferredMismatchTick))
	{
		return FALSE;
	}
	return TRUE;
}

/* Hold live sim below mismatch tick until deferred GGPO/symmetric resim loads snapshot (figatree freeze). */
static sb32 syNetRollbackDeferredCorrectionBlocksLiveAdvance(u32 *out_cap)
{
	if (out_cap == NULL)
	{
		return FALSE;
	}
	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return FALSE;
	}
	if ((sSYNetRollbackDeferredMismatchTick == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick <= sSYNetRollbackDeferredMismatchTick))
	{
		return FALSE;
	}
	*out_cap = (sSYNetRollbackDeferredMismatchTick > 0U) ? (sSYNetRollbackDeferredMismatchTick - 1U) : 0U;
	return TRUE;
}

static sb32 syNetRollbackSymmetricDeferredCommitRetriable(u32 mismatch, u32 target)
{
	u32 sim_tick;

	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch < sSYNetRollbackEpisodeResolvedThrough))
	{
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeAnchorMismatch != ~(u32)0) &&
	    (sSYNetRollbackEpisodeExtensions >= SYNETROLLBACK_MAX_EPISODE_EXTENSIONS) &&
	    (mismatch >= sSYNetRollbackEpisodeAnchorMismatch))
	{
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeAnchorMismatch != ~(u32)0) && (target <= sSYNetRollbackEpisodeLastTargetTick) &&
	    (mismatch >= sSYNetRollbackEpisodeAnchorMismatch))
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if ((mismatch == sSYNetRollbackSuppressReloadLoadTick) && (sim_tick <= sSYNetRollbackSuppressReloadUntilSim))
	{
		return TRUE;
	}
	return TRUE;
}

static sb32 syNetRollbackDeferFrameCommitForSymmetric(u32 fc_mismatch)
{
	u32 defer_tick;
	u32 defer_target;

	if ((sSYNetRollbackResimFromPeerSymmetric != FALSE) &&
	    ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE)))
	{
		if ((sSYNetRollbackResimMismatchTick != ~(u32)0) && (fc_mismatch >= sSYNetRollbackResimMismatchTick) &&
		    (fc_mismatch < sSYNetRollbackResimTargetTick))
		{
			return TRUE;
		}
	}
	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return FALSE;
	}
	if (sSYNetRollbackDeferredMismatchFromPeerSymmetric == FALSE)
	{
		return FALSE;
	}
	defer_tick = sSYNetRollbackDeferredMismatchTick;
	defer_target = sSYNetRollbackDeferredMismatchTargetTick;
	if (defer_tick == ~(u32)0)
	{
		return FALSE;
	}
	if (fc_mismatch + SYNETROLLBACK_SYMMETRIC_FC_DEFER_TOLERANCE < defer_tick)
	{
		return FALSE;
	}
	if ((defer_target != ~(u32)0) &&
	    (fc_mismatch > defer_target + SYNETROLLBACK_SYMMETRIC_FC_DEFER_TOLERANCE))
	{
		return FALSE;
	}
	return TRUE;
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
		syNetRollbackClearDeferredInputMismatch();
		return FALSE;
	}
	if (syNetRollbackCorrectionAllowedAtTick(mismatch) == FALSE)
	{
		syNetRollbackLogDeferDiag("correction_not_allowed", mismatch, target, player);
		return FALSE;
	}
	{
		u32 frontier;
		sb32 wire_lock;

		frontier = syNetInputGetTick();
		if (frontier < ~(u32)0)
		{
			frontier++;
		}
		wire_lock = syNetRollbackSymmetricWireLockActive();
		if ((sSYNetRollbackDeferredMismatchFromPeerSymmetric != FALSE) &&
		    (syNetRollbackEpisodeAuthorityEnabled() != FALSE) && (sSYNetRollbackPendingEpisode.valid != FALSE))
		{
			sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
			target = syNetRollbackClampResimTargetTickAuthoritative(mismatch,
									       sSYNetRollbackPendingEpisode.target_tick);
		}
		else
		{
			target = syNetRollbackClampResimTargetTickEx(mismatch, target, frontier, wire_lock);
		}
	}
	{
		SYNetRollbackCorrectionCommitSnap commit_snap;

		sb32 deferred_from_peer_symmetric;

		deferred_from_peer_symmetric = sSYNetRollbackDeferredMismatchFromPeerSymmetric;
		if (syNetRollbackTryCommitCorrectionBegin(mismatch, mismatch - 1U, target, &commit_snap) == FALSE)
		{
			if ((deferred_from_peer_symmetric != FALSE) &&
			    (syNetRollbackSymmetricDeferredCommitRetriable(mismatch, target) == FALSE))
			{
				port_log(
				    "SSB64 NetRollback: symmetric deferred abandon mismatch_tick=%u target_tick=%u slot=%d\n",
				    mismatch,
				    target,
				    (int)player);
				syNetRollbackClearDeferredInputMismatch();
				syNetRollbackLogDeferDiag("symmetric_deferred_abandon", mismatch, target, player);
			}
			else
			{
				syNetRollbackLogDeferDiag("commit_begin_failed", mismatch, target, player);
			}
			return FALSE;
		}
		syNetRollbackClearDeferredInputMismatch();
		if (deferred_from_peer_symmetric != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: symmetric local authority resim mismatch_tick=%u target_tick=%u slot=%d\n",
			    mismatch,
			    target,
			    (int)player);
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: GGPO deferred input correction resim mismatch_tick=%u target_tick=%u slot=%d\n",
			    mismatch,
			    target,
			    (int)player);
		}
		sSYNetRollbackResimFromPeerSymmetric = deferred_from_peer_symmetric;
		if (syNetRollbackBeginResim(mismatch, target, player) == FALSE)
		{
			syNetRollbackAbortCorrectionCommit(&commit_snap);
			if (deferred_from_peer_symmetric != FALSE)
			{
				sSYNetRollbackDeferredMismatchPending = TRUE;
				sSYNetRollbackDeferredMismatchTick = mismatch;
				sSYNetRollbackDeferredMismatchTargetTick = target;
				sSYNetRollbackDeferredMismatchPlayer = player;
				sSYNetRollbackDeferredMismatchFromPeerSymmetric = TRUE;
			}
			syNetRollbackLogDeferDiag("begin_resim_failed", mismatch, target, player);
			return FALSE;
		}
	}
	sSYNetRollbackRollbackCount++;
	sSYNetRollbackResimOrdinal = sSYNetRollbackRollbackCount;
	sSYNetRollbackLastCommittedMismatchTick = mismatch;
	syNetRollbackAdvanceResimBudget();
	return TRUE;
}

#ifdef PORT
static u32 syNetRollbackReanchorMismatchTick(u32 mismatch_tick, u32 frontier)
{
	u32 input_mismatch;
	u32 last_safe;

	if (mismatch_tick == ~(u32)0)
	{
		return mismatch_tick;
	}
	input_mismatch = syNetRollbackFindEarliestInputMismatch(frontier, NULL);
	if ((input_mismatch != ~(u32)0) && (input_mismatch < mismatch_tick))
	{
		mismatch_tick = input_mismatch;
	}
	last_safe = syNetRbSnapshotGetLastLoadSafeTick();
	if ((last_safe != ~(u32)0) && (mismatch_tick > (last_safe + 1U)))
	{
		mismatch_tick = last_safe + 1U;
	}
	return mismatch_tick;
}

static u32 syNetRollbackLoadTickMinBound(u32 sim_tick)
{
	u32 min_load;
	u32 ring_cap;
	u32 episode_floor;

	min_load = 0U;
	ring_cap = syNetRbSnapshotRingCapacity();
	if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
	{
		min_load = sim_tick - (ring_cap - 2U);
	}
	if (sSYNetRollbackEpisodeResolvedThrough > 1U)
	{
		episode_floor = sSYNetRollbackEpisodeResolvedThrough - 1U;
		if (episode_floor > min_load)
		{
			min_load = episode_floor;
		}
	}
	return min_load;
}

static u32 syNetRollbackResolveStateMismatchLoadTick(u32 validation_tick, u32 min_load)
{
	u32 probe;
	u32 resolved;
	u32 sim_floor;

	sim_floor = syNetRollbackLoadTickMinBound(syNetInputGetTick());
	if (sim_floor > min_load)
	{
		min_load = sim_floor;
	}
	probe = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
	resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(probe, min_load);
	if ((resolved == ~(u32)0) && (sSYNetRollbackLastFrameCommitStateAgreedTick != 0U))
	{
		resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(sSYNetRollbackLastFrameCommitStateAgreedTick,
									min_load);
	}
	if (resolved == ~(u32)0)
	{
		resolved = syNetRbSnapshotFindLatestValidTickAtOrBefore(probe, min_load);
	}
	return resolved;
}

void syNetRollbackNoteFrameCommitStateAgreed(u32 validation_tick)
{
	if (validation_tick > sSYNetRollbackLastFrameCommitStateAgreedTick)
	{
		sSYNetRollbackLastFrameCommitStateAgreedTick = validation_tick;
	}
}

u32 syNetRollbackGetLastFrameCommitStateAgreedTick(void)
{
	return sSYNetRollbackLastFrameCommitStateAgreedTick;
}
#endif

static sb32 syNetRollbackTryBeginDeferredStateMismatch(void)
{
	u32 mismatch;
	u32 target;
	u32 load_tick;

	if (sSYNetRollbackDeferredStateMismatchPending == FALSE)
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
	{
		if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) ||
		    (sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0))
		{
			syNetRollbackLogDeferDiag("fc_waiting_peer_episode", sSYNetRollbackDeferredStateMismatchTick,
						  sSYNetRollbackDeferredStateMismatchTargetTick, -1);
			return FALSE;
		}
	}
	mismatch = sSYNetRollbackDeferredStateMismatchTick;
	target = sSYNetRollbackDeferredStateMismatchTargetTick;
	if (syNetRollbackDeferFrameCommitForSymmetric(mismatch) != FALSE)
	{
		syNetRollbackLogDeferDiag("symmetric_defers_frame_commit", mismatch, target, -1);
		return FALSE;
	}
	if ((mismatch == ~(u32)0) || (target <= mismatch))
	{
		sSYNetRollbackDeferredStateMismatchPending = FALSE;
		sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
		syNetRollbackResetPeerBaselineResyncStorm();
		return FALSE;
	}
#ifdef PORT
	{
		u32 frontier;

		frontier = syNetInputGetTick();
		if (frontier < ~(u32)0)
		{
			frontier++;
		}
		if (sSYNetRollbackDeferredStateMismatchInputAgreed == FALSE)
		{
			mismatch = syNetRollbackReanchorMismatchTick(mismatch, frontier);
		}
		if (target <= mismatch)
		{
			target = mismatch + 1U;
		}
		sSYNetRollbackDeferredStateMismatchTick = mismatch;
		sSYNetRollbackDeferredStateMismatchTargetTick = target;
	}
#endif
	if (syNetRollbackMismatchAllowedDuringDebounce(mismatch) == FALSE)
	{
		return FALSE;
	}
	if (mismatch == 0U)
	{
		load_tick = 0U;
	}
	else
	{
		load_tick = mismatch - 1U;
#ifdef PORT
		{
			u32 min_load;
			u32 ring_cap;
			u32 sim_tick;
			u32 resolved;
			u32 requested_load;

			requested_load = load_tick;
			sim_tick = syNetInputGetTick();
			ring_cap = syNetRbSnapshotRingCapacity();
			min_load = 0U;
			if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
			{
				min_load = sim_tick - (ring_cap - 2U);
			}
			resolved = syNetRollbackResolveStateMismatchLoadTick(mismatch, min_load);
			if (resolved != ~(u32)0)
			{
				load_tick = resolved;
				if (load_tick != requested_load)
				{
					port_log(
					    "SSB64 NetRollback: deferred recovery load_tick=%u (requested=%u mismatch=%u ring_cap=%u)\n",
					    load_tick,
					    requested_load,
					    mismatch,
					    (unsigned int)ring_cap);
				}
				if (mismatch > (resolved + 1U))
				{
					mismatch = resolved + 1U;
					sSYNetRollbackDeferredStateMismatchTick = mismatch;
					if (target <= mismatch)
					{
						target = mismatch + 1U;
						sSYNetRollbackDeferredStateMismatchTargetTick = target;
					}
				}
			}
		}
#endif
	}
	if (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) != FALSE)
	{
		syNetRollbackOnPeerBaselineResyncStormLimit(load_tick);
		return FALSE;
	}
	{
		u32 probe_f;
		u32 probe_w;
		u32 probe_i;
		u32 probe_r;

		if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &probe_f, &probe_w, &probe_i, &probe_r) == FALSE)
		{
			syNetPeerFrameCommitDiagNoteRecoverySkippedNoSnap();
			port_log(
			    "SSB64 NetRollback: peer baseline resync skipped — no load_safe snapshot at load_tick=%u "
			    "mismatch=%u ring_cap=%u sim=%u\n",
			    load_tick,
			    mismatch,
			    (unsigned int)syNetRbSnapshotRingCapacity(),
			    (unsigned int)syNetInputGetTick());
			sSYNetRollbackDeferredStateMismatchPending = FALSE;
			sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
			sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
			syNetRollbackResetPeerBaselineResyncStorm();
			return FALSE;
		}
	}
	{
		sb32 input_agreed;

		input_agreed = sSYNetRollbackDeferredStateMismatchInputAgreed;
		sSYNetRollbackDeferredStateMismatchPending = FALSE;
		sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
		if ((input_agreed != FALSE) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
		{
			target = syNetRollbackClampResimTargetTickAuthoritative(mismatch, target);
		}
		else
		{
			target = syNetRollbackClampResimTargetTick(mismatch, target);
		}
	}
	{
		SYNetRollbackCorrectionCommitSnap commit_snap;

		if (syNetRollbackTryCommitCorrectionBegin(mismatch, load_tick, target, &commit_snap) == FALSE)
		{
			syNetRollbackLogDeferDiag("state_resync_commit_failed", mismatch, target, -1);
			syNetRollbackResetPeerBaselineResyncStorm();
			return FALSE;
		}
		syNetPeerFrameCommitDiagNoteRecoveryStarted();
		syNetPeerArmPostRecoveryConvergenceWatch();
		port_log("SSB64 NetRollback: deferred frame-commit state resim mismatch_tick=%u target_tick=%u\n", mismatch,
		         target);
		if (syNetRollbackBeginResim(mismatch, target, -1) == FALSE)
		{
			syNetRollbackAbortCorrectionCommit(&commit_snap);
			syNetRollbackResetPeerBaselineResyncStorm();
			return FALSE;
		}
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
	syNetPeerFrameCommitDiagNoteDeferredArmed();
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
	{
		u32 probe;
		u32 min_load;
		u32 ring_cap;
		u32 sim_tick;
		u32 resolved;

		if (local->input_digest == peer->input_digest)
		{
			if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
			{
				/* Bilateral FC episode: anchor at validation tick (no per-peer predicted-onset reanchor). */
				mismatch_tick = validation_tick;
				sSYNetRollbackDeferredStateMismatchInputAgreed = TRUE;
			}
			else
			{
				u32 scan_begin;
				u32 predicted_onset;

				scan_begin = 1U;
				if (sSYNetRollbackLastFrameCommitStateAgreedTick > 0U)
				{
					scan_begin = sSYNetRollbackLastFrameCommitStateAgreedTick;
				}
				predicted_onset = syNetInputFindEarliestPredictedRemoteUsageInSpan(scan_begin, validation_tick);
				if (predicted_onset != ~(u32)0)
				{
					if (sSYNetRollbackStateHashLogsRemaining > 0U)
					{
						port_log(
						    "SSB64 NetRollback: FRAME_COMMIT_INPUT_AGREE_PREDICTED_ONSET validation=%u onset=%u scan_begin=%u\n",
						    validation_tick,
						    predicted_onset,
						    scan_begin);
						sSYNetRollbackStateHashLogsRemaining--;
					}
					sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
					mismatch_tick = predicted_onset;
					sim_tick = syNetInputGetTick();
					ring_cap = syNetRbSnapshotRingCapacity();
					min_load = 0U;
					if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
					{
						min_load = sim_tick - (ring_cap - 2U);
					}
					if (predicted_onset > 0U)
					{
						resolved = syNetRollbackResolveStateMismatchLoadTick(predicted_onset - 1U, min_load);
					}
					else
					{
						resolved = syNetRollbackResolveStateMismatchLoadTick(validation_tick, min_load);
					}
					if ((resolved != ~(u32)0) && ((resolved + 1U) < mismatch_tick))
					{
						mismatch_tick = resolved + 1U;
					}
					probe = syNetInputGetTick();
					if (probe < ~(u32)0)
					{
						probe++;
					}
					mismatch_tick = syNetRollbackReanchorMismatchTick(mismatch_tick, probe);
				}
				else
				{
					/* Inputs agree: anchor resim at the validation tick (skip load_safe reanchor). */
					mismatch_tick = validation_tick;
					sSYNetRollbackDeferredStateMismatchInputAgreed = TRUE;
				}
			}
		}
		else
		{
			sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
			sim_tick = syNetInputGetTick();
			ring_cap = syNetRbSnapshotRingCapacity();
			min_load = 0U;
			if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
			{
				min_load = sim_tick - (ring_cap - 2U);
			}
			resolved = syNetRollbackResolveStateMismatchLoadTick(validation_tick, min_load);
			if (resolved != ~(u32)0)
			{
				mismatch_tick = resolved + 1U;
			}
			else if (validation_tick > 1U)
			{
				mismatch_tick = validation_tick - 1U;
			}
			else
			{
				mismatch_tick = 1U;
			}
			probe = syNetInputGetTick();
			if (probe < ~(u32)0)
			{
				probe++;
			}
			mismatch_tick = syNetRollbackReanchorMismatchTick(mismatch_tick, probe);
		}
	}
	if ((local->input_digest == peer->input_digest) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
	{
		target_tick = syNetRollbackComputeAuthoritativeFcTarget(mismatch_tick, validation_tick);
	}
	else
	{
		target_tick = syNetRollbackComputeSharedResimTarget(mismatch_tick, validation_tick);
	}
	if ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	    (mismatch_tick >= sSYNetRollbackDeferredStateMismatchTick))
	{
		return;
	}
	if (local->input_digest == peer->input_digest)
	{
		sSYNetRollbackFcStateRecoveryActive = TRUE;
		sSYNetRollbackFcStateRecoveryMismatchTick = mismatch_tick;
		sSYNetRollbackFcStateRecoveryTargetTick = target_tick;
	}
	sSYNetRollbackDeferredStateMismatchPending = TRUE;
	sSYNetRollbackDeferredStateMismatchTick = mismatch_tick;
	sSYNetRollbackDeferredStateMismatchTargetTick = target_tick;
	syNetRollbackArmPeerEpochForStateResim(mismatch_tick, target_tick);
}

static sb32 syNetRollbackPeerSymmetricUseFollowerLocalAuthority(s32 notify_slot, sb32 follower_local_auth_flag)
{
	if (follower_local_auth_flag != FALSE)
	{
		return TRUE;
	}
	return (syNetRollbackPeerSymmetricAuthoritySlotForPlayer(notify_slot) >= 0) ? TRUE : FALSE;
}

static void syNetRollbackArmSymmetricNotifyEx(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick,
					      u32 epoch_id, sb32 follower_local_auth)
{
	u32 active_tick;
	u8 notify_flags;

	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (sSYNetRollbackResimFromPeerSymmetric != FALSE) ||
	    (sSYNetRollbackResimPending != FALSE))
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
	if (load_tick == 0U)
	{
		load_tick = (mismatch_tick > 0U) ? (mismatch_tick - 1U) : 0U;
	}
	if (epoch_id == 0U)
	{
		epoch_id = sSYNetRollbackEpochId;
	}
	notify_flags = 0U;
	if (follower_local_auth != FALSE)
	{
		notify_flags |= SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH;
	}
	active_tick = sSYNetRollbackSymmetricNotifyTick[slot];
	if ((active_tick == 0U) || (mismatch_tick < active_tick) ||
	    ((mismatch_tick > active_tick) &&
	     (sSYNetRollbackSymmetricNotifySendCount[slot] >= SYNETROLLBACK_SYMMETRIC_NOTIFY_MIN_SENDS)))
	{
		sSYNetRollbackSymmetricNotifyTick[slot] = mismatch_tick;
		sSYNetRollbackSymmetricNotifyTargetTick[slot] = target_tick;
		sSYNetRollbackSymmetricNotifyLoadTick[slot] = load_tick;
		sSYNetRollbackSymmetricNotifyEpochId[slot] = epoch_id;
		sSYNetRollbackSymmetricNotifySendCount[slot] = 0U;
		sSYNetRollbackSymmetricNotifyFlags[slot] = notify_flags;
	}
	else if ((active_tick == mismatch_tick) && (target_tick > sSYNetRollbackSymmetricNotifyTargetTick[slot]))
	{
		sSYNetRollbackSymmetricNotifyTargetTick[slot] = target_tick;
		sSYNetRollbackSymmetricNotifyLoadTick[slot] = load_tick;
		sSYNetRollbackSymmetricNotifyEpochId[slot] = epoch_id;
		if (follower_local_auth != FALSE)
		{
			sSYNetRollbackSymmetricNotifyFlags[slot] |= SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH;
		}
	}
	syNetRollbackNotePeerEpochTarget(slot, mismatch_tick, target_tick);
	sSYNetRollbackPeerEpochAwaitingPeerResimPost = TRUE;
	syNetPeerTrySendRollbackSyncNotice();
}

static void syNetRollbackArmSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick, sb32 follower_local_auth)
{
	syNetRollbackArmSymmetricNotifyEx(slot, mismatch_tick, target_tick, 0U, 0U, follower_local_auth);
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
			sSYNetRollbackSymmetricNotifyLoadTick[slot] = 0U;
			sSYNetRollbackSymmetricNotifyEpochId[slot] = 0U;
			sSYNetRollbackSymmetricNotifySendCount[slot] = 0U;
			sSYNetRollbackSymmetricNotifyFlags[slot] = 0U;
		}
	}
}

void syNetRollbackExportPeerSymmetricEpisode(s32 slot, u32 *out_load_tick, u32 *out_epoch_id)
{
	u32 load_tick;
	u32 epoch_id;

	if ((slot < 0) || (slot >= MAXCONTROLLERS))
	{
		return;
	}
	load_tick = sSYNetRollbackSymmetricNotifyLoadTick[slot];
	epoch_id = sSYNetRollbackSymmetricNotifyEpochId[slot];
	if (load_tick == 0U)
	{
		u32 mismatch;

		mismatch = sSYNetRollbackSymmetricNotifyTick[slot];
		if (mismatch > 0U)
		{
			load_tick = mismatch - 1U;
		}
	}
	if (epoch_id == 0U)
	{
		epoch_id = sSYNetRollbackEpochId;
	}
	if (out_load_tick != NULL)
	{
		*out_load_tick = load_tick;
	}
	if (out_epoch_id != NULL)
	{
		*out_epoch_id = epoch_id;
	}
}

void syNetRollbackExportPeerSymmetricNotify(s32 *out_tick_per_slot, s32 *out_target_tick_per_slot, u8 *out_flags_per_slot,
					    s32 count)
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
			if (out_flags_per_slot != NULL)
			{
				out_flags_per_slot[i] = sSYNetRollbackSymmetricNotifyFlags[i];
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
			if (out_flags_per_slot != NULL)
			{
				out_flags_per_slot[i] = 0U;
			}
		}
	}
}

static u32 syNetRollbackGetEpochCapSlack(void)
{
	const char *env;
	int v;

	if (sSYNetRollbackEpochCapSlackCache != -999)
	{
		v = sSYNetRollbackEpochCapSlackCache;
	}
	else
	{
		v = (int)SYNETROLLBACK_EPOCH_CAP_SLACK_DEFAULT;
		env = getenv("SSB64_NETPLAY_ROLLBACK_EPOCH_CAP_SLACK");
		if ((env != NULL) && (env[0] != '\0'))
		{
			v = atoi(env);
		}
		sSYNetRollbackEpochCapSlackCache = v;
	}
	if (v < 0)
	{
		v = 0;
	}
	return (u32)v;
}

static void syNetRollbackClearPeerEpochState(void)
{
	sSYNetRollbackPeerEpochTargetTick = 0U;
	sSYNetRollbackPeerEpochMismatchTick = 0U;
	sSYNetRollbackPeerEpochAwaitingPeerResimPost = FALSE;
}

static sb32 syNetRollbackRetainPeerEpochAfterLocalResim(void)
{
	if (sSYNetRollbackPeerEpochAwaitingPeerResimPost == FALSE)
	{
		return FALSE;
	}
	if (sSYNetRollbackPeerEpochTargetTick == 0U)
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackClearPeerEpochAfterEpisodeFsmClose(u32 mismatch_tick, u32 target_tick)
{
	SYNetRollbackResimPostKey key;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
	if ((mismatch_tick == 0U) || (target_tick == 0U) || (target_tick <= mismatch_tick))
	{
		syNetRollbackClearPeerEpochState();
		return;
	}
	key.epoch_id = sSYNetRollbackEpochId;
	key.load_tick = sSYNetRollbackResimLoadTick;
	key.mismatch_tick = mismatch_tick;
	key.target_tick = target_tick;
	syNetRollbackNormalizeResimPostKey(&key);
	syNetRollbackReleaseLiveCapsAfterResimPostMatch(&key);
	syNetRollbackClearPeerEpochState();
}

static void syNetRollbackMaybeClearPeerEpochAfterResimPostMatch(const SYNetRollbackResimPostKey *key)
{
	if (key == NULL)
	{
		return;
	}
	if ((sSYNetRollbackResimPostCompletedValid != FALSE) &&
	    (syNetRollbackResimPostKeysEqual(key, &sSYNetRollbackResimPostCompletedKey) == FALSE))
	{
		return;
	}
	if (sSYNetRollbackPeerEpochAwaitingPeerResimPost != FALSE)
	{
		if ((sSYNetRollbackPeerEpochTargetTick == 0U) || (sSYNetRollbackPeerEpochMismatchTick == 0U))
		{
			return;
		}
		if ((key->mismatch_tick != sSYNetRollbackPeerEpochMismatchTick) ||
		    (key->target_tick != sSYNetRollbackPeerEpochTargetTick))
		{
			return;
		}
	}
	else if (syNetRollbackSpanOverlapsResimPostKey(sSYNetRollbackPeerEpochMismatchTick,
						       sSYNetRollbackPeerEpochTargetTick, key) == FALSE)
	{
		return;
	}
	syNetRollbackClearPeerEpochState();
}

static void syNetRollbackNotePeerEpochTarget(s32 slot, u32 mismatch_tick, u32 target_tick)
{
	(void)slot;
	if ((mismatch_tick == 0U) || (target_tick == 0U))
	{
		return;
	}
	if ((sSYNetRollbackPeerEpochTargetTick == 0U) || (target_tick > sSYNetRollbackPeerEpochTargetTick) ||
	    ((target_tick == sSYNetRollbackPeerEpochTargetTick) && (mismatch_tick < sSYNetRollbackPeerEpochMismatchTick)))
	{
		sSYNetRollbackPeerEpochTargetTick = target_tick;
		sSYNetRollbackPeerEpochMismatchTick = mismatch_tick;
	}
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmSetPeerConvergence(target_tick);
	}
}

static void syNetRollbackArmPeerEpochForStateResim(u32 mismatch_tick, u32 target_tick)
{
	s32 slot;

	syNetRollbackNotePeerEpochTarget(-1, mismatch_tick, target_tick);
	sSYNetRollbackPeerEpochAwaitingPeerResimPost = TRUE;
	if (syNetPeerGetRemoteHumanSlotByIndex(0, &slot) != FALSE)
	{
		syNetRollbackArmSymmetricNotify(slot, mismatch_tick, target_tick, FALSE);
	}
	else
	{
		syNetPeerTrySendRollbackSyncNotice();
	}
}

static u32 syNetRollbackComputePeerEpochLiveCap(void)
{
	u32 peer_target;
	u32 slack;

	if (syNetRollbackGetEpochCapSlack() >= SYNETROLLBACK_EPOCH_CAP_SLACK_DISABLE)
	{
		return ~(u32)0;
	}
	peer_target = 0U;
	if (sSYNetRollbackPeerEpochTargetTick != 0U)
	{
		peer_target = sSYNetRollbackPeerEpochTargetTick;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (sSYNetRollbackPendingPeerSymmetricTargetTick != ~(u32)0) &&
	    (sSYNetRollbackPendingPeerSymmetricTargetTick > peer_target))
	{
		peer_target = sSYNetRollbackPendingPeerSymmetricTargetTick;
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (sSYNetRollbackDeferredPeerSymmetricTargetTick > peer_target))
	{
		peer_target = sSYNetRollbackDeferredPeerSymmetricTargetTick;
	}
	if ((sSYNetRollbackDeferredMismatchPending != FALSE) &&
	    (sSYNetRollbackDeferredMismatchTargetTick != ~(u32)0) &&
	    (sSYNetRollbackDeferredMismatchTargetTick > peer_target))
	{
		peer_target = sSYNetRollbackDeferredMismatchTargetTick;
	}
	if ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchTargetTick != ~(u32)0) &&
	    (sSYNetRollbackDeferredStateMismatchTargetTick > peer_target))
	{
		peer_target = sSYNetRollbackDeferredStateMismatchTargetTick;
	}
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick != ~(u32)0) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick > peer_target))
	{
		peer_target = sSYNetRollbackFcStateRecoveryTargetTick;
	}
	if ((sSYNetRollbackPendingEpisode.valid != FALSE) &&
	    (sSYNetRollbackPendingEpisode.target_tick > peer_target))
	{
		peer_target = sSYNetRollbackPendingEpisode.target_tick;
	}
	if (peer_target == 0U)
	{
		return ~(u32)0;
	}
	slack = syNetRollbackGetEpochCapSlack();
	if (peer_target > (~(u32)0 - slack))
	{
		return ~(u32)0;
	}
	return peer_target + slack;
}

static void syNetRollbackMaybeLogEpochHold(u32 sim_tick, u32 cap, u32 cap_source, u32 local_resim, u32 peer_target)
{
	const char *owner;
	const char *source;

	if (sSYNetRollbackEpochHoldLogsRemaining == 0U)
	{
		return;
	}
	owner = "live";
	if (sSYNetRollbackResimPending != FALSE)
	{
		owner = sSYNetRollbackResimFromPeerSymmetric != FALSE ? "peer_follower" : "local_initiator";
	}
	else if (sSYNetRollbackPeerEpochTargetTick != 0U)
	{
		owner = "peer_epoch";
	}
	source = "none";
	if ((cap_source & 1U) != 0U)
	{
		source = "local_resim";
	}
	else if ((cap_source & 2U) != 0U)
	{
		source = "peer_target";
	}
	else if ((cap_source & 8U) != 0U)
	{
		source = "deferred_correction";
	}
	else if ((cap_source & 16U) != 0U)
	{
		source = "sym_reject_cap";
	}
	else if ((cap_source & 32U) != 0U)
	{
		source = "sym_notify_cap";
	}
	else if ((cap_source & 4U) != 0U)
	{
		source = "hr_cap";
	}
	port_log(
	    "SSB64 NetRollback: rollback_epoch_hold epoch=%u owner=%s sim=%u cap=%u source=%s local_resim=%u peer_target=%u slack=%u\n",
	    sSYNetRollbackEpochId,
	    owner,
	    sim_tick,
	    cap,
	    source,
	    local_resim,
	    peer_target,
	    syNetRollbackGetEpochCapSlack());
	sSYNetRollbackEpochHoldLogsRemaining--;
}

sb32 syNetRollbackGetLiveSimCap(u32 *out_max_live_sim, u32 *out_cap_source)
{
	u32 cap;
	u32 source;

	if (out_max_live_sim == NULL)
	{
		return FALSE;
	}
	if ((syNetRollbackIsActive() == FALSE) || (syNetSessionParamsRollbackEnabled() == FALSE))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		u32 fsm_cap;
		u32 fsm_source;

		if (syNetRollbackEpisodeFsmGetLiveSimCap(&fsm_cap, &fsm_source) != FALSE)
		{
			if (out_cap_source != NULL)
			{
				*out_cap_source = fsm_source;
			}
			*out_max_live_sim = fsm_cap;
			return TRUE;
		}
	}
	cap = ~(u32)0;
	source = 0U;
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimTargetTick != ~(u32)0))
	{
		cap = sSYNetRollbackResimTargetTick;
		source = 1U;
	}
	{
		u32 peer_cap;

		peer_cap = syNetRollbackComputePeerEpochLiveCap();
		if (peer_cap != ~(u32)0)
		{
			if ((cap == ~(u32)0) || (peer_cap < cap))
			{
				cap = peer_cap;
			}
			source |= 2U;
		}
	}
	{
		u32 defer_cap;

		if (syNetRollbackDeferredCorrectionBlocksLiveAdvance(&defer_cap) != FALSE)
		{
			if ((cap == ~(u32)0) || (defer_cap < cap))
			{
				cap = defer_cap;
			}
			source |= 8U;
		}
		if (syNetRollbackPeerSymmetricRejectBlocksLiveAdvance(&defer_cap) != FALSE)
		{
			if ((cap == ~(u32)0) || (defer_cap < cap))
			{
				cap = defer_cap;
			}
			source |= 16U;
		}
		if (syNetRollbackPeerSymmetricNotifyBlocksLiveAdvance(&defer_cap) != FALSE)
		{
			if ((cap == ~(u32)0) || (defer_cap < cap))
			{
				cap = defer_cap;
			}
			source |= 32U;
		}
	}
	if (out_cap_source != NULL)
	{
		*out_cap_source = source;
	}
	*out_max_live_sim = cap;
	return (cap != ~(u32)0) ? TRUE : FALSE;
}

static void syNetRollbackEpisodeFsmDrainEvents(void)
{
	SYNetRollbackEpisodeEvent ev;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return;
	}
	while (syNetRollbackEpisodeDrainNextEvent(&ev) != FALSE)
	{
		switch (ev.type)
		{
		case nSYNetRollbackEpisodeEventInputMismatch:
			if ((syNetRollbackIsActive() != FALSE) && (syNetInputIsRemoteHumanSlot(ev.slot) != FALSE) &&
			    (ev.mismatch_tick != 0U))
			{
				syNetRollbackQueueDeferredInputCorrection(ev.slot, ev.mismatch_tick);
			}
			break;
		case nSYNetRollbackEpisodeEventPeerSymmetric:
			syNetRollbackOnPeerSymmetricRollbackNotifyEx(ev.slot, ev.mismatch_tick, ev.target_tick, ev.load_tick,
								     ev.epoch_id, ev.follower_local_auth);
			break;
		case nSYNetRollbackEpisodeEventStateDiverge:
			syNetRollbackArmPeerEpochForStateResim(ev.mismatch_tick, ev.target_tick);
			break;
		case nSYNetRollbackEpisodeEventFrameCommit:
		default:
			break;
		}
	}
}

void syNetRollbackPumpCorrectionBeforeBattleSim(void)
{
	if ((syNetRollbackIsActive() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	syNetRollbackEpisodeFsmDrainEvents();
	(void)syNetRollbackTryBeginDeferredMismatch();
	syNetRollbackFlushDeferredPeerSymmetric();
	(void)syNetRollbackTryBeginDeferredStateMismatch();
	syNetRollbackFlushDeferredPeerSymmetric();
	/*
	 * Follower may receive ROLLBACK_SYNC during ingress (FuncRead) while sim is already past
	 * mismatch-1. Kick syNetRollbackUpdate before battle sim so snapshot load runs before
	 * gcRunAll can mutate figatree (same class as GGPO initiator pre-load freeze).
	 */
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) ||
	    (sSYNetRollbackDeferredPeerSymmetricPending != FALSE))
	{
		syNetRollbackUpdate();
	}
}

sb32 syNetRollbackShouldBlockLiveBattleAdvance(u32 sim_tick)
{
	u32 cap;
	u32 cap_source;
	u32 local_resim;
	u32 peer_target;

	if ((syNetRollbackIsActive() == FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return FALSE;
	}
	if (syNetRollbackGetLiveSimCap(&cap, &cap_source) == FALSE)
	{
		return FALSE;
	}
	if (cap == ~(u32)0)
	{
		return FALSE;
	}
	if (sim_tick <= cap)
	{
		return FALSE;
	}
	local_resim =
	    (sSYNetRollbackResimPending != FALSE) ? sSYNetRollbackResimTargetTick : 0U;
	peer_target = sSYNetRollbackPeerEpochTargetTick;
	syNetRollbackMaybeLogEpochHold(sim_tick, cap, cap_source, local_resim, peer_target);
	return TRUE;
}

u32 syNetRollbackGetEpochId(void)
{
	return sSYNetRollbackEpochId;
}

sb32 syNetRollbackAcceptPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick)
{
	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (syNetRollbackIsActive() == FALSE))
	{
		return FALSE;
	}
	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (mismatch_tick == 0U))
	{
		return FALSE;
	}
	if ((sSYNetRollbackPeerSymmetricAppliedTick[slot] != 0U) &&
	    (mismatch_tick <= sSYNetRollbackPeerSymmetricAppliedTick[slot]))
	{
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch_tick < sSYNetRollbackEpisodeResolvedThrough))
	{
		return FALSE;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick == sSYNetRollbackPendingPeerSymmetricTick) &&
	    (target_tick != 0U) && (target_tick <= sSYNetRollbackPendingPeerSymmetricTargetTick))
	{
		return FALSE;
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (mismatch_tick == sSYNetRollbackDeferredPeerSymmetricTick) &&
	    (target_tick != 0U) && (target_tick <= sSYNetRollbackDeferredPeerSymmetricTargetTick))
	{
		return FALSE;
	}
	if (syNetRollbackPeerSymmetricSuppressedByFcStateRecovery(mismatch_tick, target_tick) != FALSE)
	{
		if (sSYNetRollbackFcStateRecoverySuppressLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: peer symmetric suppressed by frame-commit state recovery notify_mismatch=%u notify_target=%u fc_mismatch=%u fc_target=%u sim=%u\n",
			    mismatch_tick,
			    target_tick,
			    sSYNetRollbackFcStateRecoveryMismatchTick,
			    sSYNetRollbackFcStateRecoveryTargetTick,
			    syNetInputGetTick());
			sSYNetRollbackFcStateRecoverySuppressLogsRemaining--;
		}
		return FALSE;
	}
	return TRUE;
}

void syNetRollbackOnPeerSymmetricRollbackNotifyEx(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick,
						  u32 epoch_id, sb32 follower_local_auth)
{
	u32 frontier;

	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (syNetRollbackIsActive() == FALSE))
	{
		return;
	}
	if (syNetRollbackTryAlignActiveEpisodeTuple(slot, load_tick, mismatch_tick, target_tick, follower_local_auth) !=
	    FALSE)
	{
		return;
	}
	if ((syNetRollbackEpisodeFsmIsActive() != FALSE) && (mismatch_tick == syNetRollbackEpisodeFsmGetMismatchTick()) &&
	    (target_tick == syNetRollbackEpisodeFsmGetTargetTick()))
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
	if (syNetRollbackAcceptPeerSymmetricRollbackNotify(slot, mismatch_tick, target_tick) == FALSE)
	{
		return;
	}
	if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
	    (syNetRollbackLocalEpisodeConflictsWithPeerNotify(mismatch_tick, target_tick) != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_YIELD local resim/FC superseded by peer notify mismatch=%u target=%u sim=%u\n",
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
		syNetRollbackAbortInFlightResimForPeerEpisode();
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if ((target_tick == 0U) || (target_tick <= mismatch_tick))
	{
		target_tick = frontier;
	}
	if (target_tick <= mismatch_tick)
	{
		target_tick = mismatch_tick + 1U;
	}
	if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
	{
		u8 flags;

		flags = (follower_local_auth != FALSE) ? (u8)SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH : 0U;
		syNetRollbackPendingEpisodeSet(slot, mismatch_tick, target_tick, load_tick, epoch_id, flags);
	}
	syNetRollbackNotePeerEpochTarget(slot, mismatch_tick, target_tick);
	if (syNetRollbackBaselineCompareQuiesced() == FALSE)
	{
		if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
		    (mismatch_tick == sSYNetRollbackDeferredPeerSymmetricTick) &&
		    (target_tick > sSYNetRollbackDeferredPeerSymmetricTargetTick))
		{
			sSYNetRollbackDeferredPeerSymmetricTargetTick = target_tick;
		}
		else if ((sSYNetRollbackDeferredPeerSymmetricPending == FALSE) ||
		         (mismatch_tick <= sSYNetRollbackDeferredPeerSymmetricTick))
		{
			sSYNetRollbackDeferredPeerSymmetricPending = TRUE;
			sSYNetRollbackDeferredPeerSymmetricTick = mismatch_tick;
			sSYNetRollbackDeferredPeerSymmetricTargetTick = target_tick;
			sSYNetRollbackDeferredPeerSymmetricSlot = slot;
			sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = follower_local_auth;
			syNetRollbackArmPeerSymmetricRejectLiveCap(mismatch_tick);
		}
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback deferred mismatch_tick=%u target_tick=%u sim=%u\n",
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
		return;
	}
	syNetRollbackQueuePeerSymmetricNotify(slot, mismatch_tick, target_tick, follower_local_auth);
}

void syNetRollbackOnPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick,
						sb32 follower_local_auth)
{
	syNetRollbackOnPeerSymmetricRollbackNotifyEx(slot, mismatch_tick, target_tick, 0U, 0U, follower_local_auth);
}
#endif

static sb32 syNetRollbackSavePostTick(u32 tick)
{
#ifdef PORT
	const char *env_assert;
	static sb32 sSaveAssertLogged;

	env_assert = getenv("SSB64_NETPLAY_SNAPSHOT_SAVE_ASSERT");
	if ((env_assert != NULL) && (env_assert[0] != '\0') && (atoi(env_assert) != 0) &&
	    (sSYNetRollbackResimPending != FALSE) && (sSaveAssertLogged == FALSE))
	{
		port_log("SSB64 NetRollback: SNAPSHOT_SAVE_ASSERT tick=%u during resim_pending\n", tick);
		sSaveAssertLogged = TRUE;
	}
#endif
	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
#ifdef PORT
	{
		sb32 load_safe;

		load_safe = (syNetInputSimTickUsedPredictedRemote(tick) != FALSE) ? FALSE : TRUE;
		return syNetRbSnapshotSaveMarked(tick, load_safe);
	}
#else
	return syNetRbSnapshotSave(tick);
#endif
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
		syNetRbSnapshotLogFighterLoadVerifyDiag(tick,
		                                        live_f,
		                                        syNetRbSnapshotGetSlotHashFighter(tick),
		                                        live_a,
		                                        syNetRbSnapshotGetSlotHashAnimation(tick));
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
		/* Mid-baseline negotiation: slot/live drift is expected on poisoned history (~1033 soak). */
		if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
		    (sSYNetRollbackResimBaselineGateOpen == FALSE))
		{
			syNetRbSnapshotMarkLoadUnsafe(tick);
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-ignored (baseline storm) tick=%u\n",
			    tick);
			syNetRbSnapshotRebindAllFighters();
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsSoft() != FALSE)
		{
			port_log("SSB64 NetRollback: LOAD_HASH_DRIFT soft — continuing resim without session stop (tick %u)\n",
			         tick);
			syNetRbSnapshotRebindAllFighters();
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
	syNetRbSnapshotRebindAllFighters();
	return TRUE;
}

#ifdef PORT
sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick)
{
	return syNetRollbackLoadPostTick(completed_sim_tick);
}

void syNetRollbackArmPredictionRecoveryForStickMismatch(u32 sim_tick, u32 frontier_tick)
{
	SYNetInputFrame hist;

	if (syNetRollbackStickMismatchRecoveryEnabled() == FALSE)
	{
		return;
	}
	memset(&hist, 0, sizeof(hist));
	hist.is_predicted = TRUE;
	syNetRollbackArmPredictionRecovery(sim_tick, frontier_tick, &hist);
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
#ifdef PORT
	if (sSYNetRollbackResimPending != FALSE)
	{
		return;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (completed_tick <= sSYNetRollbackEpisodeResolvedThrough))
	{
		return;
	}
#endif
	syNetRollbackSavePostTick(completed_tick);
#ifdef PORT
	syNetRollbackFlushDeferredPeerSymmetric();
	syNetRollbackPumpBaselineEchoRetry();
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

/* TRUE when published history and remote ring disagree on gameplay input at `sim_tick`. */
static sb32 syNetRollbackTickHasValueMismatch(u32 sim_tick, s32 player)
{
	SYNetInputFrame hist;
	SYNetInputFrame remote;
	s32 ri;
	s32 remote_player;

	if (sim_tick == 0U)
	{
		return FALSE;
	}
	if ((player >= 0) && (player < MAXCONTROLLERS))
	{
		if ((syNetInputGetHistoryFrame(player, sim_tick, &hist) == FALSE) ||
		    (syNetInputGetRemoteHistoryFrame(player, sim_tick, &remote) == FALSE))
		{
			return FALSE;
		}
		if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) == FALSE)
		{
			return FALSE;
		}
		if (syNetInputShouldDeferPredictedAnalogCorrection(player, sim_tick, &hist, &remote) != FALSE)
		{
			return FALSE;
		}
		return TRUE;
	}
	for (ri = 0; ri < syNetPeerGetRemoteHumanSlotCount(); ri++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(ri, &remote_player) == FALSE)
		{
			continue;
		}
		if ((remote_player < 0) || (remote_player >= MAXCONTROLLERS))
		{
			continue;
		}
		if ((syNetInputGetHistoryFrame(remote_player, sim_tick, &hist) == FALSE) ||
		    (syNetInputGetRemoteHistoryFrame(remote_player, sim_tick, &remote) == FALSE))
		{
			continue;
		}
		if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) != FALSE)
		{
			if (syNetInputShouldDeferPredictedAnalogCorrection(remote_player, sim_tick, &hist, &remote) != FALSE)
			{
				continue;
			}
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRollbackClearTimelineForCompletedResim(void)
{
	u32 from_tick;
	u32 to_tick;

	if ((sSYNetRollbackResimMismatchTick == 0U) || (sSYNetRollbackResimMismatchTick == ~(u32)0) ||
	    (sSYNetRollbackResimTargetTick <= sSYNetRollbackResimMismatchTick))
	{
		return;
	}
	from_tick = sSYNetRollbackResimMismatchTick;
	to_tick = sSYNetRollbackResimTargetTick;
	syNetInputTimelineClearResolvedSpan(from_tick, to_tick);
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

	if (syNetRollbackPredictionRecoveryEnabled() == FALSE)
	{
		return;
	}
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
		if ((timeline_tick != ~(u32)0) &&
		    (syNetRollbackTickHasValueMismatch(timeline_tick, timeline_player) != FALSE))
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
			/* Presence-only (ring vs published timing) is diagnostic — do not drive rollback. */
			if (has_hist == FALSE)
			{
				continue;
			}
			if (has_remote == FALSE)
			{
				continue;
			}
			if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) != FALSE)
			{
				if (syNetInputShouldDeferPredictedAnalogCorrection(remote_player, t, &hist, &remote) != FALSE)
				{
					continue;
				}
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
static void syNetRollbackCollectFighterSlotHashes(u32 out_slot_hash[GMCOMMON_PLAYERS_MAX])
{
	GObj *fighter_gobj;
	s32 si;

	if (out_slot_hash == NULL)
	{
		return;
	}
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		out_slot_hash[si] = 2166136261U;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		slot = fp->player;
		if ((slot >= 0) && (slot < GMCOMMON_PLAYERS_MAX))
		{
			out_slot_hash[slot] = syNetSyncHashFighterStructLight(fp);
		}
	}
}

static sb32 syNetRollbackBaselineProceedOnTimeoutEnabled(void)
{
	const char *env;

	env = getenv("SSB64_NETPLAY_RESIM_BASELINE_PROCEED_ON_TIMEOUT");
	return ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? TRUE : FALSE;
}

static void syNetRollbackEpisodeReset(void)
{
	memset(&sSYNetRollbackEpisode, 0, sizeof(sSYNetRollbackEpisode));
}

static void syNetRollbackEpisodeSyncToLegacy(void)
{
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmSyncToLegacy(&sSYNetRollbackResimPending, &sSYNetRollbackResimAwaitingPeerBaseline,
						  &sSYNetRollbackResimBaselineGateOpen, &sSYNetRollbackResimMismatchTick,
						  &sSYNetRollbackResimLoadTick, &sSYNetRollbackResimTargetTick,
						  &sSYNetRollbackResimCorrectionPlayer, &sSYNetRollbackResimFromPeerSymmetric);
		return;
	}
	if (sSYNetRollbackEpisode.phase == nSYNetRollbackEpisodePhaseLive)
	{
		sSYNetRollbackResimPending = FALSE;
		sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
		sSYNetRollbackResimBaselineGateOpen = FALSE;
		return;
	}
	sSYNetRollbackResimPending = TRUE;
	sSYNetRollbackResimMismatchTick = sSYNetRollbackEpisode.mismatch_tick;
	sSYNetRollbackResimLoadTick = sSYNetRollbackEpisode.load_tick;
	sSYNetRollbackResimTargetTick = sSYNetRollbackEpisode.target_tick;
	sSYNetRollbackResimCorrectionPlayer = sSYNetRollbackEpisode.corrected_slot;
	sSYNetRollbackResimFromPeerSymmetric = sSYNetRollbackEpisode.from_peer_notify;
	sSYNetRollbackResimAwaitingPeerBaseline =
	    (sSYNetRollbackEpisode.phase == nSYNetRollbackEpisodePhaseAwaitingBaseline) ? TRUE : FALSE;
	sSYNetRollbackResimBaselineGateOpen =
	    (sSYNetRollbackEpisode.phase == nSYNetRollbackEpisodePhaseForwardResim) ? TRUE : FALSE;
}

static void syNetRollbackEpisodeBegin(u32 mismatch_tick, u32 load_tick, u32 target_tick, s32 corrected_slot,
				      sb32 initiator, sb32 from_peer_notify)
{
	if (sSYNetRollbackEpochId < ~(u32)0)
	{
		sSYNetRollbackEpochId++;
	}
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmBegin(sSYNetRollbackEpochId, mismatch_tick, load_tick, target_tick, corrected_slot,
					     initiator, from_peer_notify);
		syNetRollbackEpisodeSealInputs(mismatch_tick, target_tick, corrected_slot);
		sSYNetRollbackEpisode.mismatch_tick = mismatch_tick;
		sSYNetRollbackEpisode.load_tick = load_tick;
		sSYNetRollbackEpisode.target_tick = syNetRollbackEpisodeFsmGetTargetTick();
		sSYNetRollbackEpisode.corrected_slot = corrected_slot;
		sSYNetRollbackEpisode.initiator = initiator;
		sSYNetRollbackEpisode.from_peer_notify = from_peer_notify;
		sSYNetRollbackEpisode.phase = nSYNetRollbackEpisodePhaseAwaitingBaseline;
		syNetRollbackEpisodeSyncToLegacy();
		return;
	}
	sSYNetRollbackEpisode.phase = nSYNetRollbackEpisodePhaseAwaitingBaseline;
	sSYNetRollbackEpisode.mismatch_tick = mismatch_tick;
	sSYNetRollbackEpisode.load_tick = load_tick;
	sSYNetRollbackEpisode.target_tick = target_tick;
	sSYNetRollbackEpisode.corrected_slot = corrected_slot;
	sSYNetRollbackEpisode.initiator = initiator;
	sSYNetRollbackEpisode.from_peer_notify = from_peer_notify;
	syNetRollbackEpisodeSyncToLegacy();
}

static void syNetRollbackEpisodeSetPhase(SYNetRollbackEpisodePhase phase)
{
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		SYNetRollbackEpisodeFsmPhase fsm_phase;

		sSYNetRollbackEpisode.phase = phase;
		switch (phase)
		{
		case nSYNetRollbackEpisodePhaseLive:
			fsm_phase = nSYNetRollbackEpisodeFsmPhaseLive;
			break;
		case nSYNetRollbackEpisodePhaseAwaitingBaseline:
			fsm_phase = nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline;
			break;
		case nSYNetRollbackEpisodePhaseForwardResim:
			fsm_phase = nSYNetRollbackEpisodeFsmPhaseReplay;
			break;
		default:
			fsm_phase = syNetRollbackEpisodeFsmGetPhase();
			break;
		}
		syNetRollbackEpisodeFsmSetPhase(fsm_phase);
		syNetRollbackEpisodeSyncToLegacy();
		return;
	}
	sSYNetRollbackEpisode.phase = phase;
	syNetRollbackEpisodeSyncToLegacy();
}

static void syNetRollbackClearSymmetricNotifyAll(void)
{
	s32 slot;

	for (slot = 0; slot < MAXCONTROLLERS; slot++)
	{
		sSYNetRollbackSymmetricNotifyTick[slot] = 0U;
		sSYNetRollbackSymmetricNotifyTargetTick[slot] = 0U;
		sSYNetRollbackSymmetricNotifyLoadTick[slot] = 0U;
		sSYNetRollbackSymmetricNotifyEpochId[slot] = 0U;
		sSYNetRollbackSymmetricNotifySendCount[slot] = 0U;
		sSYNetRollbackSymmetricNotifyFlags[slot] = 0U;
	}
}

static void syNetRollbackFinishForwardResim(void)
{
	u32 completed_target;
	u32 mismatch_tick;
	s32 correction_player;

	completed_target = sSYNetRollbackResimTargetTick;
	mismatch_tick = sSYNetRollbackResimMismatchTick;
	correction_player = sSYNetRollbackResimCorrectionPlayer;
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) &&
	    (syNetRollbackEpisodeFsmGetPhase() != nSYNetRollbackEpisodeFsmPhaseCommit) &&
	    (syNetRollbackEpisodeFsmGetPhase() != nSYNetRollbackEpisodeFsmPhaseLive) &&
	    (syNetRollbackEpisodeFsmGetPhase() != nSYNetRollbackEpisodeFsmPhaseAbort))
	{
		if ((sSYNetRollbackResimPostCompletedValid != FALSE) &&
		    (syNetRollbackResimPostCompletedCoversActiveResim() != FALSE))
		{
			syNetRollbackEpisodeCommitPromoteSealed();
			syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseCommit);
		}
		else
		{
			syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseVerify);
			syNetRollbackCaptureResimPostBoundaryDigest();
			if (sSYNetRollbackResimPostLocalValid != FALSE)
			{
				sSYNetRollbackResimPostCompletedValid = TRUE;
				sSYNetRollbackResimPostCompletedKey = sSYNetRollbackResimPostLocalKey;
				sSYNetRollbackResimPostCompletedDigest = sSYNetRollbackResimPostLocalDigest;
			}
			syNetRollbackTryEmitResimPostHandshake();
			syNetRollbackEpisodeSyncToLegacy();
			return;
		}
	}
	if ((syNetRollbackEpisodeFsmEnabled() == FALSE) && (mismatch_tick != 0U) && (mismatch_tick != ~(u32)0U) &&
	    (completed_target != 0U) && (completed_target != ~(u32)0U) && (completed_target > mismatch_tick))
	{
		syNetInputRollbackReconcileAfterResimCompleted(mismatch_tick, completed_target, correction_player);
	}
	syNetRollbackCloseCorrectionEpisode(completed_target);
	syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseLive);
	syNetRollbackEpisodeReset();
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	sSYNetRollbackResimNextTick = ~(u32)0;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	syNetRollbackClearSymmetricNotifyAll();
	syNetRollbackClearPeerSymmetricRejectLiveCap();
	syNetRollbackResetPeerBaselineResyncStorm();
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackClearPeerEpochAfterEpisodeFsmClose(mismatch_tick, completed_target);
	}
	else if (syNetRollbackRetainPeerEpochAfterLocalResim() == FALSE)
	{
		syNetRollbackClearPeerEpochState();
	}
	syNetRollbackRunDeferredPeerBaselineCompare();
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmSessionReset();
	}
}

void syNetRollbackPumpResimBaselineIfAwaiting(void)
{
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) ||
	    (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return;
	}
	syNetRollbackPumpResimBaselineSend();
}

static void syNetRollbackPumpResimBaselineSend(void)
{
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) ||
	    (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return;
	}
	/* Rate-limit retransmits so baseline packets are not drowned by INPUT bundles. */
	if ((sSYNetRollbackPeerBaselineRetransmitCount > 0U) &&
	    ((sSYNetRollbackResimBaselineWaitFrames & 1U) != 0U))
	{
		return;
	}
	sSYNetRollbackPeerBaselineSendPending = TRUE;
	syNetPeerTrySendRollbackBaselineDigest();
	syNetRollbackEpisodePrepareSealRowsRetransmit();
	syNetPeerTrySendEpisodeSealRows();
}

static sb32 syNetRollbackBaselineFighterSlotsMatch(const u32 *peer_fighter_slot, const u32 *local_fighter_slot)
{
	s32 si;

	if ((peer_fighter_slot == NULL) || (local_fighter_slot == NULL))
	{
		return TRUE;
	}
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		if (peer_fighter_slot[si] != local_fighter_slot[si])
		{
			return FALSE;
		}
	}
	return TRUE;
}

static void syNetRollbackArmResimBaselineAfterLoad(u32 load_tick)
{
	SYNetRollbackHashSet live;

	load_tick = syNetRollbackClampLoadTickForPeerSend(load_tick);
	live = syNetRollbackCollectHashes();
	sSYNetRollbackResimLoadTick = load_tick;
	sSYNetRollbackResimPreHashes = live;
	sSYNetRollbackResimPreHashesValid = TRUE;
	sSYNetRollbackPeerBaselineSendPending = TRUE;
	sSYNetRollbackResimBaselineDigestMatched = FALSE;
	sSYNetRollbackPeerBaselineLoadTick = load_tick;
	sSYNetRollbackPeerBaselineFigh = live.fighter;
	sSYNetRollbackPeerBaselineWorld = live.world;
	sSYNetRollbackPeerBaselineItem = live.item;
	sSYNetRollbackPeerBaselineRng = live.rng;
	sSYNetRollbackPeerBaselineAnim = live.animation;
	sSYNetRollbackPeerBaselineWeapon = live.weapon;
	sSYNetRollbackPeerBaselineMap = live.map;
	sSYNetRollbackPeerBaselineCamera = live.camera;
	sSYNetRollbackPeerBaselineSlotFigh = syNetRbSnapshotGetSlotHashFighter(load_tick);
	sSYNetRollbackPeerBaselineSlotWorld = syNetRbSnapshotGetSlotHashWorld(load_tick);
	sSYNetRollbackPeerBaselineSlotItem = syNetRbSnapshotGetSlotHashItem(load_tick);
	sSYNetRollbackPeerBaselineSlotRng = syNetRbSnapshotGetSlotHashRng(load_tick);
	syNetRollbackCollectFighterSlotHashes(sSYNetRollbackPeerBaselineFighterSlot);
	{
		s32 live_yaku_n = gMPCollisionYakumonosNum;
		s32 stored_yaku_n = syNetRbSnapshotGetSlotMapYakumonoCount(load_tick);

		if (live_yaku_n < 0)
		{
			live_yaku_n = 0;
		}
		port_log(
		    "SSB64 NetRollback: map_yaku post-load tick=%u live_n=%d stored_n=%d mph=0x%08X\n",
		    load_tick,
		    (int)live_yaku_n,
		    (int)stored_yaku_n,
		    live.map);
	}
	{
		u32 slot_anim = syNetRbSnapshotGetSlotHashAnimation(load_tick);

		port_log(
		    "SSB64 NetRollback: fighter_anim post-load tick=%u live_anim=0x%08X slot_anim=0x%08X\n",
		    load_tick,
		    live.animation,
		    slot_anim);
	}
	port_log(
	    "SSB64 NetRollback: resim baseline (post-load tick=%u) live figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X | slot figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X\n",
	    load_tick,
	    live.fighter,
	    live.world,
	    live.item,
	    live.rng,
	    live.animation,
	    sSYNetRollbackPeerBaselineSlotFigh,
	    sSYNetRollbackPeerBaselineSlotWorld,
	    sSYNetRollbackPeerBaselineSlotItem,
	    sSYNetRollbackPeerBaselineSlotRng);
	if ((live.fighter != sSYNetRollbackPeerBaselineSlotFigh) ||
	    (live.world != sSYNetRollbackPeerBaselineSlotWorld) || (live.item != sSYNetRollbackPeerBaselineSlotItem) ||
	    (live.rng != sSYNetRollbackPeerBaselineSlotRng))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_SLOT_LIVE_DRIFT load_tick=%u live figh=0x%08X slot figh=0x%08X live world=0x%08X slot world=0x%08X\n",
		    load_tick,
		    live.fighter,
		    sSYNetRollbackPeerBaselineSlotFigh,
		    live.world,
		    sSYNetRollbackPeerBaselineSlotWorld);
	}
	syNetPeerTrySendRollbackBaselineDigest();
	syNetPeerTrySendEpisodeSealRows();
}

static void syNetRollbackResetPeerBaselineResyncStorm(void)
{
	sSYNetRollbackBaselineEchoRetryLoadTick = ~(u32)0;
	sSYNetRollbackBaselineEchoRetryAttempts = 0U;
	sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
	sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricSlot = -1;
	sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackPeerBaselineResyncSteps = 0U;
	sSYNetRollbackPeerBaselineResyncOriginMismatch = ~(u32)0;
	sSYNetRollbackPeerBaselineResyncStormActive = FALSE;
}

static sb32 syNetRollbackPeerBaselineResyncStormLimitReached(u32 load_tick)
{
	u32 sim_tick;
	u32 ring_cap;
	u32 min_load;

	sim_tick = syNetInputGetTick();
	ring_cap = syNetRbSnapshotRingCapacity();
	if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
	{
		min_load = sim_tick - (ring_cap - 2U);
		if (load_tick < min_load)
		{
			return TRUE;
		}
	}
	if (sSYNetRollbackPeerBaselineResyncSteps >= SYNETROLLBACK_PEER_BASELINE_RESYNC_MAX_STEPS)
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRollbackOnPeerBaselineResyncStormLimit(u32 load_tick)
{
	port_log(
	    "SSB64 NetRollback: PEER_BASELINE_RESYNC_STORM load_tick=%u steps=%u origin=%u sim=%u ring=%u — aborting resync loop\n",
	    load_tick,
	    (unsigned int)sSYNetRollbackPeerBaselineResyncSteps,
	    (unsigned int)sSYNetRollbackPeerBaselineResyncOriginMismatch,
	    syNetInputGetTick(),
	    (unsigned int)syNetRbSnapshotRingCapacity());
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
	syNetRollbackClearFcStateRecovery();
	syNetRollbackClearFcDeepenGuard();
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	syNetRollbackResetPeerBaselineResyncStorm();
	if (sSYNetRollbackPeerSnapshotAbort != FALSE)
	{
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetRollbackStopVSSession();
		syNetPeerStopVSSession();
	}
}

static sb32 syNetRollbackBaselineEchoAllowed(u32 load_tick)
{
	u32 sim_tick;
	u32 ring_cap;
	u32 min_load;

	if (load_tick == 0U)
	{
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (load_tick <= sSYNetRollbackEpisodeResolvedThrough))
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if ((sSYNetRollbackSuppressReloadLoadTick != ~(u32)0) && (load_tick == sSYNetRollbackSuppressReloadLoadTick) &&
	    (sim_tick <= sSYNetRollbackSuppressReloadUntilSim))
	{
		return FALSE;
	}
	if (sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0)
	{
		u32 cap;

		cap = (sSYNetRollbackPendingPeerSymmetricTick > 0U) ? (sSYNetRollbackPendingPeerSymmetricTick - 1U) : 0U;
		if (sim_tick > cap)
		{
			return FALSE;
		}
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (sSYNetRollbackDeferredPeerSymmetricTick != ~(u32)0))
	{
		u32 cap;

		cap = (sSYNetRollbackDeferredPeerSymmetricTick > 0U) ? (sSYNetRollbackDeferredPeerSymmetricTick - 1U) : 0U;
		if (sim_tick > cap)
		{
			return FALSE;
		}
	}
	if ((load_tick == sSYNetRollbackLastBaselineEchoLoadTick) && (sim_tick >= sSYNetRollbackLastBaselineEchoSimTick) &&
	    ((sim_tick - sSYNetRollbackLastBaselineEchoSimTick) < SYNETROLLBACK_PEER_BASELINE_ECHO_MIN_ADVANCE_TICKS))
	{
		return FALSE;
	}
	ring_cap = syNetRbSnapshotRingCapacity();
	if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
	{
		min_load = sim_tick - (ring_cap - 2U);
		if (load_tick < min_load)
		{
			return FALSE;
		}
	}
	return TRUE;
}

static sb32 syNetRollbackTryEchoBaselineResponse(u32 load_tick)
{
	u32 slot_figh;
	u32 slot_world;
	u32 slot_item;
	u32 slot_rng;
	sb32 emergency_valid;

	if (load_tick == 0U)
	{
		return FALSE;
	}
	if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &slot_figh, &slot_world, &slot_item, &slot_rng) == FALSE)
	{
		port_log("SSB64 NetRollback: RESIM_BASELINE_ECHO_SKIP load_tick=%u reason=no_snapshot\n", load_tick);
		return FALSE;
	}
	emergency_valid = syNetRbSnapshotCaptureLiveEmergency();
	if (syNetRbSnapshotLoad(load_tick) == FALSE)
	{
		if (emergency_valid != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		port_log("SSB64 NetRollback: RESIM_BASELINE_ECHO_FAIL load_tick=%u reason=load\n", load_tick);
		return FALSE;
	}
	syNetSyncReconcileBattleTimePassedForSimTick(load_tick);
	syNetRollbackArmResimBaselineAfterLoad(load_tick);
	if (emergency_valid != FALSE)
	{
		(void)syNetRbSnapshotRestoreLiveEmergency();
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_ECHO load_tick=%u slot figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X\n",
	    load_tick,
	    slot_figh,
	    slot_world,
	    slot_item,
	    slot_rng);
	sSYNetRollbackLastBaselineEchoLoadTick = load_tick;
	sSYNetRollbackLastBaselineEchoSimTick = syNetInputGetTick();
	return TRUE;
}

static sb32 syNetRollbackSymmetricWireLockActive(void)
{
	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (sSYNetRollbackSymmetricDiagOnly != FALSE))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackEpisodeAuthorityEnabled(void)
{
	const char *env;

	if (sSYNetRollbackEpisodeAuthorityEnvCache == -999)
	{
		sSYNetRollbackEpisodeAuthorityEnabled = TRUE;
		env = getenv("SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY");
		if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
		{
			sSYNetRollbackEpisodeAuthorityEnabled = FALSE;
		}
		sSYNetRollbackEpisodeAuthorityEnvCache = sSYNetRollbackEpisodeAuthorityEnabled ? 1 : 0;
	}
	return sSYNetRollbackEpisodeAuthorityEnabled;
}

static void syNetRollbackPendingEpisodeClear(void)
{
	memset(&sSYNetRollbackPendingEpisode, 0, sizeof(sSYNetRollbackPendingEpisode));
}

static void syNetRollbackPendingEpisodeSet(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick, u32 epoch_id,
					   u8 flags)
{
	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (mismatch_tick == 0U) || (target_tick <= mismatch_tick))
	{
		return;
	}
	if (load_tick == 0U)
	{
		load_tick = (mismatch_tick > 0U) ? (mismatch_tick - 1U) : 0U;
	}
	sSYNetRollbackPendingEpisode.valid = TRUE;
	sSYNetRollbackPendingEpisode.slot = slot;
	sSYNetRollbackPendingEpisode.mismatch_tick = mismatch_tick;
	sSYNetRollbackPendingEpisode.target_tick = target_tick;
	sSYNetRollbackPendingEpisode.load_tick = load_tick;
	sSYNetRollbackPendingEpisode.epoch_id = epoch_id;
	sSYNetRollbackPendingEpisode.flags = flags;
}

static u32 syNetRollbackClampResimTargetTickAuthoritative(u32 mismatch_tick, u32 target_tick)
{
	u32 min_target;

	min_target = mismatch_tick + 2U;
	if (target_tick < min_target)
	{
		target_tick = min_target;
	}
	return target_tick;
}

static u32 syNetRollbackComputeAuthoritativeFcTarget(u32 mismatch_tick, u32 validation_tick)
{
	u32 target;

	target = validation_tick + 1U;
	if (target <= mismatch_tick)
	{
		target = mismatch_tick + 1U;
	}
	return syNetRollbackClampResimTargetTickAuthoritative(mismatch_tick, target);
}

static sb32 syNetRollbackLocalEpisodeConflictsWithPeerNotify(u32 peer_mismatch, u32 peer_target)
{
	if (peer_mismatch == 0U)
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		if ((sSYNetRollbackResimMismatchTick != peer_mismatch) || (sSYNetRollbackResimTargetTick != peer_target))
		{
			return TRUE;
		}
	}
	if (sSYNetRollbackDeferredStateMismatchPending != FALSE)
	{
		if (sSYNetRollbackDeferredStateMismatchTick != peer_mismatch)
		{
			return TRUE;
		}
		if ((sSYNetRollbackDeferredStateMismatchTargetTick != ~(u32)0) &&
		    (sSYNetRollbackDeferredStateMismatchTargetTick != peer_target))
		{
			return TRUE;
		}
	}
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		if (sSYNetRollbackFcStateRecoveryMismatchTick != peer_mismatch)
		{
			return TRUE;
		}
		if ((sSYNetRollbackFcStateRecoveryTargetTick != ~(u32)0) &&
		    (sSYNetRollbackFcStateRecoveryTargetTick != peer_target))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRollbackAbortInFlightResimForPeerEpisode(void)
{
	syNetRollbackClearFcStateRecovery();
	sSYNetRollbackDeferredStateMismatchPending = FALSE;
	sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
	syNetRollbackResetBaselineResimState();
	syNetRollbackEpisodeReset();
	sSYNetRollbackEpisode.phase = nSYNetRollbackEpisodePhaseLive;
	syNetRollbackEpisodeSyncToLegacy();
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	sSYNetRollbackResimNextTick = ~(u32)0;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	syNetRollbackClearFcDeepenGuard();
}

static void syNetRollbackMaybeLogEpisodeExec(sb32 from_peer_notify)
{
	const char *owner;
	SYNetRollbackPendingEpisode *req;

	if (syNetRollbackEpisodeAuthorityEnabled() == FALSE)
	{
		return;
	}
	req = &sSYNetRollbackExecutingEpisode;
	if (req->valid == FALSE)
	{
		return;
	}
	owner = from_peer_notify != FALSE ? "peer_follower" : "local_initiator";
	port_log(
	    "SSB64 NetRollback: EPISODE_EXEC owner=%s req_load=%u req_mismatch=%u req_target=%u exec_load=%u exec_mismatch=%u exec_target=%u sim=%u epoch=%u\n",
	    owner,
	    req->load_tick,
	    req->mismatch_tick,
	    req->target_tick,
	    sSYNetRollbackResimLoadTick,
	    sSYNetRollbackResimMismatchTick,
	    sSYNetRollbackResimTargetTick,
	    syNetInputGetTick(),
	    req->epoch_id);
}

#define SYNETROLLBACK_FRONTIER_AHEAD_WARN_TOLERANCE 3U

/* Same cap as syNetPeerEvaluateBattleAdvance rollback_sim_cap (remote sim + D + phase_lock). */
static u32 syNetRollbackComputeRemoteSimResimCap(void)
{
	u32 hr;

	hr = syNetPeerGetHighestRemoteTick();
	if (hr == 0U)
	{
		return ~(u32)0;
	}
	return syNetPeerDelaySimTickFromWire(hr) + syNetPeerGetCommittedInputDelay() +
	       syNetPeerGetPhaseLockPredictionWindowTicks();
}

static void syNetRollbackMaybeLogFrontierAheadWarn(u32 local_sim, u32 remote_cap)
{
	if (remote_cap == ~(u32)0)
	{
		return;
	}
	if (local_sim <= remote_cap + SYNETROLLBACK_FRONTIER_AHEAD_WARN_TOLERANCE)
	{
		return;
	}
	if ((u32)gSCManagerSceneData.scene_curr != (u32)nSCKindVSBattle)
	{
		return;
	}
	if (sSYNetRollbackFrontierAheadWarnLogsRemaining == 0U)
	{
		return;
	}
	{
		u32 hr;
		u32 remote_sim;

		hr = syNetPeerGetHighestRemoteTick();
		remote_sim = syNetPeerDelaySimTickFromWire(hr);
		sSYNetRollbackFrontierAheadWarnLogsRemaining--;
		port_log(
		    "SSB64 NetRollback: FRONTIER_AHEAD_WARN local_sim=%u remote_cap=%u hr=%u remote_sim=%u ahead=%u\n",
		    local_sim,
		    remote_cap,
		    hr,
		    remote_sim,
		    local_sim - remote_cap);
	}
}

/*
 * Paired resim target for frame-commit recovery: do not resim past validation boundary, local frontier,
 * or remote-confirmed sim cap (matches battle advance prediction runway).
 */
static u32 syNetRollbackComputeSharedResimTarget(u32 mismatch_tick, u32 validation_tick)
{
	u32 local_frontier;
	u32 target;
	u32 remote_cap;
	u32 validation_floor;

	local_frontier = syNetInputGetTick();
	if (local_frontier < ~(u32)0)
	{
		local_frontier++;
	}
	validation_floor = validation_tick + 1U;
	target = mismatch_tick + 1U;
	if (validation_floor > target)
	{
		target = validation_floor;
	}
	if (target > local_frontier)
	{
		target = local_frontier;
	}
	remote_cap = syNetRollbackComputeRemoteSimResimCap();
	if (remote_cap != ~(u32)0)
	{
		if (target > remote_cap)
		{
			target = remote_cap;
		}
		syNetRollbackMaybeLogFrontierAheadWarn(local_frontier, remote_cap);
	}
	if (target <= mismatch_tick)
	{
		target = mismatch_tick + 1U;
	}
	return target;
}

static u32 syNetRollbackClampResimTargetTickEx(u32 mismatch_tick, u32 target_tick, u32 frontier, sb32 wire_locked)
{
	if ((wire_locked != FALSE) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
	    (sSYNetRollbackAuthoritativeEpisodeActive != FALSE))
	{
		return syNetRollbackClampResimTargetTickAuthoritative(mismatch_tick, target_tick);
	}
	{
		u32 min_target;
		u32 remote_cap;

		min_target = mismatch_tick + 2U;
		if (wire_locked == FALSE)
		{
			remote_cap = syNetRollbackComputeRemoteSimResimCap();
			if (remote_cap != ~(u32)0)
			{
				if (target_tick > remote_cap)
				{
					target_tick = remote_cap;
				}
			}
		}
		else if ((frontier > 0U) && (target_tick > frontier))
		{
			target_tick = frontier;
		}
		if (target_tick < min_target)
		{
			target_tick = min_target;
		}
		return target_tick;
	}
}

static u32 syNetRollbackClampResimTargetTick(u32 mismatch_tick, u32 target_tick)
{
	u32 frontier;

	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	return syNetRollbackClampResimTargetTickEx(mismatch_tick, target_tick, frontier, FALSE);
}

static void syNetRollbackResetBaselineResimState(void)
{
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseLive);
}

static sb32 syNetRollbackPeerDigestUniverseMismatch(const SYNetRollbackHashSet *peer)
{
	if (peer == NULL)
	{
		return FALSE;
	}
	if ((peer->rng != sSYNetRollbackPeerBaselineRng) || (peer->fighter != sSYNetRollbackPeerBaselineFigh) ||
	    (peer->world != sSYNetRollbackPeerBaselineWorld))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRollbackClearFcDeepenGuard(void)
{
	sSYNetRollbackFcDeepenLoadTick = ~(u32)0;
	sSYNetRollbackFcDeepenAttempts = 0U;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	sSYNetRollbackFcDeepenStormActive = FALSE;
	sSYNetRollbackFcDeepenDetailLogged = FALSE;
}

static void syNetRollbackClearFcStateRecovery(void)
{
	sSYNetRollbackFcStateRecoveryActive = FALSE;
	sSYNetRollbackFcStateRecoveryMismatchTick = ~(u32)0;
	sSYNetRollbackFcStateRecoveryTargetTick = ~(u32)0;
}

static void syNetRollbackOnFcDeepenStormLimit(u32 load_tick)
{
	port_log(
	    "SSB64 NetRollback: FC_DEEPEN_STORM load_tick=%u attempts=%u fc_mismatch=%u sim=%u — aborting baseline deepen loop\n",
	    load_tick,
	    (unsigned int)sSYNetRollbackFcDeepenAttempts,
	    sSYNetRollbackFcStateRecoveryMismatchTick,
	    syNetInputGetTick());
	syNetRollbackClearFcStateRecovery();
	syNetRollbackResetBaselineResimState();
	sSYNetRollbackFcDeepenStormActive = TRUE;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	sSYNetRollbackFcDeepenAttempts = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	if (sSYNetRollbackPeerSnapshotAbort != FALSE)
	{
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetRollbackStopVSSession();
		syNetPeerStopVSSession();
	}
}

static sb32 syNetRollbackTryFcStateRecoveryDeepen(u32 load_tick)
{
	if (syNetRollbackUniverseMismatchPreferStateRecovery(load_tick) == FALSE)
	{
		return FALSE;
	}
	if (sSYNetRollbackFcDeepenStormActive != FALSE)
	{
		return TRUE;
	}
	if ((sSYNetRollbackFcDeepenInFlight != FALSE) && (load_tick == sSYNetRollbackFcDeepenLoadTick))
	{
		return TRUE;
	}
	if (load_tick != sSYNetRollbackFcDeepenLoadTick)
	{
		sSYNetRollbackFcDeepenLoadTick = load_tick;
		sSYNetRollbackFcDeepenAttempts = 0U;
		sSYNetRollbackFcDeepenDetailLogged = FALSE;
	}
	sSYNetRollbackFcDeepenAttempts++;
	if (sSYNetRollbackFcDeepenAttempts > SYNETROLLBACK_FC_DEEPEN_MAX_PER_LOAD)
	{
		syNetRollbackOnFcDeepenStormLimit(load_tick);
		return TRUE;
	}
	if (sSYNetRollbackFcDeepenAttempts == 1U)
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_UNIVERSE_MISMATCH load_tick=%u → state recovery deepen (fc_recovery=%d deferred_state=%d storm=%d) sim=%u\n",
		    load_tick,
		    (int)sSYNetRollbackFcStateRecoveryActive,
		    (int)sSYNetRollbackDeferredStateMismatchPending,
		    (int)sSYNetRollbackPeerBaselineResyncStormActive,
		    syNetInputGetTick());
	}
	sSYNetRollbackFcDeepenInFlight = TRUE;
	syNetRollbackAbortPendingResimForBaselineMismatch(load_tick);
	return TRUE;
}

static sb32 syNetRollbackFcStateRecoveryCoversSpan(u32 mismatch_tick, u32 target_tick)
{
	u32 fc_mismatch;
	u32 fc_target;

	if (sSYNetRollbackFcStateRecoveryActive == FALSE)
	{
		return FALSE;
	}
	fc_mismatch = sSYNetRollbackFcStateRecoveryMismatchTick;
	fc_target = sSYNetRollbackFcStateRecoveryTargetTick;
	if (fc_mismatch == ~(u32)0)
	{
		return FALSE;
	}
	if (mismatch_tick < fc_mismatch)
	{
		return FALSE;
	}
	if ((fc_target != ~(u32)0) && (target_tick != 0U) && (target_tick > fc_target))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackPeerSymmetricSuppressedByFcStateRecovery(u32 mismatch_tick, u32 target_tick)
{
	u32 defer_mismatch;
	u32 defer_target;

	if (syNetRollbackFcStateRecoveryCoversSpan(mismatch_tick, target_tick) != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		u32 fc_mismatch;

		fc_mismatch = sSYNetRollbackFcStateRecoveryMismatchTick;
		if ((fc_mismatch != ~(u32)0) && (mismatch_tick < fc_mismatch))
		{
			return FALSE;
		}
	}
	if (sSYNetRollbackDeferredStateMismatchPending == FALSE)
	{
		return FALSE;
	}
	defer_mismatch = sSYNetRollbackDeferredStateMismatchTick;
	defer_target = sSYNetRollbackDeferredStateMismatchTargetTick;
	if (defer_mismatch == ~(u32)0)
	{
		return FALSE;
	}
	if (mismatch_tick < defer_mismatch)
	{
		return FALSE;
	}
	if ((defer_target != ~(u32)0) && (target_tick != 0U) && (target_tick > defer_target))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackUniverseMismatchPreferStateRecovery(u32 load_tick)
{
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackDeferredStateMismatchPending != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackPeerBaselineResyncStormActive != FALSE)
	{
		return TRUE;
	}
	if ((sSYNetRollbackResimFromPeerSymmetric != FALSE) && (sSYNetRollbackFcStateRecoveryMismatchTick != ~(u32)0) &&
	    (load_tick >= sSYNetRollbackFcStateRecoveryMismatchTick))
	{
		return TRUE;
	}
	return FALSE;
}

/* Peers agreed on load_tick but rng/figh/world differ — poisoned snapshot band; input fork upstream (~1032 soak). */
static void syNetRollbackAbortToInputCorrectionFromUniverseMismatch(u32 load_tick)
{
	u32 frontier;
	u32 mismatch;
	s32 player;

	if (sSYNetRollbackResimPending == FALSE)
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_UNIVERSE_MISMATCH load_tick=%u ignored (no pending resim) sim=%u\n",
		    load_tick,
		    syNetInputGetTick());
		return;
	}
	if (syNetRollbackTryFcStateRecoveryDeepen(load_tick) != FALSE)
	{
		return;
	}
	syNetRollbackResetBaselineResimState();
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, &player);
	if (mismatch == ~(u32)0)
	{
		mismatch = (load_tick < ~(u32)0) ? (load_tick + 1U) : 1U;
	}
	if (player < 0)
	{
		(void)syNetPeerGetRemoteHumanSlotByIndex(0, &player);
	}
	port_log(
	    "SSB64 NetRollback: BASELINE_UNIVERSE_MISMATCH load_tick=%u → input correction mismatch=%u player=%d sim=%u\n",
	    load_tick,
	    mismatch,
	    (int)player,
	    syNetInputGetTick());
	syNetRollbackQueueDeferredInputCorrectionEx(player, mismatch, frontier);
}

static void syNetRollbackAbortPendingResimForBaselineMismatch(u32 failed_load_tick)
{
	u32 probe;
	u32 min_load;
	u32 local_deeper;
	u32 restart_load;
	u32 peer_ann;
	u32 resim_load;

	resim_load = sSYNetRollbackResimLoadTick;
	if ((sSYNetRollbackLastPeerOutcomeValid != FALSE) && (sSYNetRollbackLastPeerOutcomeTick == resim_load) &&
	    (resim_load == failed_load_tick) &&
	    (syNetRollbackPeerDigestUniverseMismatch(&sSYNetRollbackLastPeerOutcomeHash) != FALSE))
	{
		if (syNetRollbackTryFcStateRecoveryDeepen(failed_load_tick) != FALSE)
		{
			return;
		}
		syNetRollbackAbortToInputCorrectionFromUniverseMismatch(failed_load_tick);
		return;
	}
	syNetRollbackResetBaselineResimState();
	min_load = syNetRollbackLoadTickMinBound(syNetInputGetTick());
	probe = (failed_load_tick > 0U) ? (failed_load_tick - 1U) : 0U;
	local_deeper = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(probe, min_load);
	restart_load = local_deeper;
	peer_ann = 0U;
	if ((sSYNetRollbackLastPeerOutcomeValid != FALSE) && (sSYNetRollbackLastPeerOutcomeTick > 0U) &&
	    (sSYNetRollbackLastPeerOutcomeTick < failed_load_tick))
	{
		peer_ann = sSYNetRollbackLastPeerOutcomeTick;
		if (restart_load == ~(u32)0)
		{
			restart_load = peer_ann;
		}
		else if (peer_ann < restart_load)
		{
			restart_load = peer_ann;
		}
	}
	if ((restart_load != ~(u32)0) && (restart_load < failed_load_tick))
	{
		sSYNetRollbackResimBaselineDeeperAttempts++;
		if (syNetRollbackTryRestartResimAtDeeperLoad(restart_load) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH negotiated restart load_tick=%u failed_load=%u peer_ann=%u local_deeper=%u\n",
			    restart_load,
			    failed_load_tick,
			    peer_ann,
			    (unsigned int)((local_deeper != ~(u32)0) ? local_deeper : 0U));
			return;
		}
	}
	if ((local_deeper != ~(u32)0) && (local_deeper < failed_load_tick))
	{
		syNetRollbackArmPeerBaselineResync(local_deeper);
		return;
	}
	syNetRollbackArmPeerBaselineResync(failed_load_tick);
}

static sb32 syNetRollbackTryNegotiateResimLoadTickWithPeer(u32 peer_load_tick)
{
	u32 local_load;

	if ((peer_load_tick == 0U) || (sSYNetRollbackResimPending == FALSE) ||
	    (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) || (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return FALSE;
	}
	if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) && (sSYNetRollbackAuthoritativeEpisodeActive != FALSE))
	{
		return FALSE;
	}
	local_load = sSYNetRollbackResimLoadTick;
	if ((local_load == ~(u32)0) || (peer_load_tick >= local_load))
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_TICK_NEGOTIATE local=%u peer=%u negotiated=%u sim=%u\n",
	    local_load,
	    peer_load_tick,
	    peer_load_tick,
	    syNetInputGetTick());
	sSYNetRollbackResimBaselineDeeperAttempts++;
	return syNetRollbackTryRestartResimAtDeeperLoad(peer_load_tick);
}

static void syNetRollbackTryOpenResimBaselineGateFromPeerDigest(u32 load_tick, const SYNetRollbackHashSet *peer,
							       const u32 *peer_fighter_slot)
{
	sb32 slot_ok;

	if ((peer == NULL) || (sSYNetRollbackResimPending == FALSE) || (load_tick != sSYNetRollbackResimLoadTick) ||
	    (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) || (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return;
	}
	if (syNetRollbackPeerDigestUniverseMismatch(peer) != FALSE)
	{
		if ((sSYNetRollbackFcDeepenDetailLogged == FALSE) &&
		    (syNetRollbackUniverseMismatchPreferStateRecovery(load_tick) != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: BASELINE_UNIVERSE_MISMATCH load_tick=%u peer rng=0x%08X figh=0x%08X world=0x%08X | local rng=0x%08X figh=0x%08X world=0x%08X\n",
			    load_tick,
			    peer->rng,
			    peer->fighter,
			    peer->world,
			    sSYNetRollbackPeerBaselineRng,
			    sSYNetRollbackPeerBaselineFigh,
			    sSYNetRollbackPeerBaselineWorld);
			sSYNetRollbackFcDeepenDetailLogged = TRUE;
		}
		syNetRollbackAbortToInputCorrectionFromUniverseMismatch(load_tick);
		return;
	}
	slot_ok = ((peer->fighter == sSYNetRollbackPeerBaselineSlotFigh) &&
	           (peer->world == sSYNetRollbackPeerBaselineSlotWorld) &&
	           (peer->item == sSYNetRollbackPeerBaselineSlotItem) &&
	           (peer->rng == sSYNetRollbackPeerBaselineSlotRng))
	              ? TRUE
	              : FALSE;
	if ((slot_ok != FALSE) &&
	    (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, sSYNetRollbackPeerBaselineFighterSlot) != FALSE) &&
	    (peer->fighter == sSYNetRollbackPeerBaselineFigh) && (peer->world == sSYNetRollbackPeerBaselineWorld) &&
	    (peer->item == sSYNetRollbackPeerBaselineItem) && (peer->rng == sSYNetRollbackPeerBaselineRng) &&
	    ((syNetRollbackEpisodeFsmBaselineRequiresAnimMatch() == FALSE) ||
	     (peer->animation == sSYNetRollbackPeerBaselineAnim)))
	{
		port_log(
		    "SSB64 NetRollback: resim baseline digest matched load_tick=%u figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X\n",
		    load_tick,
		    peer->fighter,
		    peer->world,
		    peer->item,
		    peer->rng,
		    peer->animation);
		sSYNetRollbackResimBaselineDigestMatched = TRUE;
		sSYNetRollbackResimBaselineWaitFrames = 0U;
		sSYNetRollbackPeerBaselineSendPending = FALSE;
		sSYNetRollbackBaselineTimeoutStreak = 0U;
		sSYNetRollbackFcDeepenInFlight = FALSE;
		syNetRollbackTryOpenResimReplayGate();
		return;
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH load_tick=%u peer figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X | local live figh=0x%08X slot figh=0x%08X world=0x%08X slot world=0x%08X\n",
	    load_tick,
	    peer->fighter,
	    peer->world,
	    peer->item,
	    peer->rng,
	    peer->animation,
	    sSYNetRollbackPeerBaselineFigh,
	    sSYNetRollbackPeerBaselineSlotFigh,
	    sSYNetRollbackPeerBaselineWorld,
	    sSYNetRollbackPeerBaselineSlotWorld);
	syNetRollbackAbortPendingResimForBaselineMismatch(load_tick);
}

void syNetRollbackTryOpenResimReplayGate(void)
{
	u32 missing;

	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) ||
	    (sSYNetRollbackResimBaselineDigestMatched == FALSE) || (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return;
	}
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) &&
	    (syNetRollbackEpisodeAllPeerSealRowsComplete() == FALSE))
	{
		missing = syNetRollbackEpisodeGetMissingPeerSealSlotsMask();
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_WAIT load_tick=%u missing_slots=0x%X\n",
		    sSYNetRollbackResimLoadTick,
		    missing);
		return;
	}
	port_log(
	    "SSB64 NetRollback: resim replay gate open load_tick=%u mismatch=%u target=%u\n",
	    sSYNetRollbackResimLoadTick,
	    sSYNetRollbackResimMismatchTick,
	    sSYNetRollbackResimTargetTick);
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFreezePostInputDigest();
	}
	syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseForwardResim);
}

static sb32 syNetRollbackTryRestartResimAtDeeperLoad(u32 deeper_load_tick)
{
	u32 mismatch_tick;
	u32 target_tick;
	s32 correction_player;

	if (deeper_load_tick == 0U)
	{
		return FALSE;
	}
	if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) && (sSYNetRollbackAuthoritativeEpisodeActive != FALSE))
	{
		return FALSE;
	}
	mismatch_tick = deeper_load_tick + 1U;
	if (syNetRollbackResolveLoadTickForSnapshot(&deeper_load_tick, &mismatch_tick) == FALSE)
	{
		return FALSE;
	}
	if (syNetRollbackLoadPostTick(deeper_load_tick) == FALSE)
	{
		return FALSE;
	}
	target_tick = sSYNetRollbackResimTargetTick;
	correction_player = sSYNetRollbackResimCorrectionPlayer;
	syNetSyncLogRollbackWorldDetail("rollback_load_deeper", deeper_load_tick);
	syNetSyncLogFighterDetail("rollback_load_deeper", deeper_load_tick);
	syNetInputRollbackPrepareForResim(mismatch_tick);
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeResealForDeeperLoad(deeper_load_tick, mismatch_tick, target_tick, correction_player);
		syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline);
	}
	else
	{
		syNetInputRollbackReconcileResimSpan(mismatch_tick, target_tick, correction_player);
	}
	sSYNetRollbackEpisode.mismatch_tick = mismatch_tick;
	sSYNetRollbackEpisode.load_tick = deeper_load_tick;
	sSYNetRollbackEpisode.target_tick = target_tick;
	syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseAwaitingBaseline);
	sSYNetRollbackResimNextTick = mismatch_tick;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackResimBaselineDigestMatched = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackPeerBaselineRetransmitCount = 0U;
	syNetRollbackEpisodeSyncToLegacy();
	syNetRollbackArmSymmetricNotifyEx(correction_player, mismatch_tick, target_tick, deeper_load_tick,
					sSYNetRollbackEpochId, FALSE);
	syNetPeerTrySendRollbackSyncNotice();
	syNetRollbackArmResimBaselineAfterLoad(deeper_load_tick);
	port_log(
	    "SSB64 NetRollback: resim baseline deeper restart load_tick=%u mismatch_tick=%u target_tick=%u attempt=%u\n",
	    deeper_load_tick,
	    mismatch_tick,
	    target_tick,
	    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
	return TRUE;
}

static sb32 syNetRollbackTryAlignActiveEpisodeTuple(s32 slot, u32 load_tick, u32 mismatch_tick, u32 target_tick,
						    sb32 follower_local_auth)
{
	u32 cur_load;
	u32 cur_mismatch;
	u32 cur_target;
	s32 correction_player;
	u32 align_load;

	if ((syNetRollbackEpisodeFsmEnabled() == FALSE) || (syNetRollbackEpisodeFsmIsActive() == FALSE) ||
	    (mismatch_tick == 0U) || (target_tick <= mismatch_tick) || (sSYNetRollbackResimPending == FALSE))
	{
		return FALSE;
	}
	cur_load = syNetRollbackEpisodeFsmGetLoadTick();
	cur_mismatch = syNetRollbackEpisodeFsmGetMismatchTick();
	cur_target = syNetRollbackEpisodeFsmGetTargetTick();
	if ((mismatch_tick == cur_mismatch) && (target_tick == cur_target) &&
	    ((load_tick == 0U) || (load_tick == cur_load)))
	{
		return FALSE;
	}
	correction_player = syNetRollbackEpisodeFsmGetCorrectedSlot();
	if ((correction_player < 0) || (correction_player >= MAXCONTROLLERS))
	{
		correction_player = slot;
	}
	align_load = (load_tick != 0U) ? load_tick : cur_load;
	if ((align_load != 0U) && (align_load < cur_load) && (syNetRollbackTryRestartResimAtDeeperLoad(align_load) != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM tuple_align deeper_load=%u peer_mismatch=%u peer_target=%u (was load=%u mismatch=%u)\n",
		    align_load,
		    mismatch_tick,
		    target_tick,
		    cur_load,
		    cur_mismatch);
		return TRUE;
	}
	if ((align_load == 0U) || (align_load != cur_load))
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: EPISODE_FSM tuple_align load=%u mismatch=%u->%u target=%u->%u sim=%u follower_auth=%d\n",
	    align_load,
	    cur_mismatch,
	    mismatch_tick,
	    cur_target,
	    target_tick,
	    syNetInputGetTick(),
	    (int)follower_local_auth);
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimNextTick = mismatch_tick;
	syNetRollbackEpisodeResealForDeeperLoad(align_load, mismatch_tick, target_tick, correction_player);
	sSYNetRollbackEpisode.mismatch_tick = mismatch_tick;
	sSYNetRollbackEpisode.load_tick = align_load;
	sSYNetRollbackEpisode.target_tick = syNetRollbackEpisodeFsmGetTargetTick();
	syNetRollbackEpisodeSyncToLegacy();
	if (follower_local_auth != FALSE)
	{
		syNetInputRollbackReconcilePeerSymmetricAuthority(syNetPeerGetLocalSimSlot(), mismatch_tick,
								  syNetRollbackEpisodeFsmGetTargetTick());
	}
	return TRUE;
}

static void syNetRollbackOnBaselineGateTimeout(void)
{
	u32 load_tick;
	u32 sim_tick;

	load_tick = sSYNetRollbackResimLoadTick;
	sim_tick = syNetInputGetTick();
	if ((sSYNetRollbackBaselineTimeoutWindowStartTick == 0U) ||
	    ((sim_tick > sSYNetRollbackBaselineTimeoutWindowStartTick) &&
	     ((sim_tick - sSYNetRollbackBaselineTimeoutWindowStartTick) > SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_WINDOW_TICKS)))
	{
		sSYNetRollbackBaselineTimeoutWindowStartTick = sim_tick;
		sSYNetRollbackBaselineTimeoutStreak = 0U;
	}
	sSYNetRollbackBaselineTimeoutStreak++;
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_TIMEOUT load_tick=%u wait_frames=%u retransmits=%u deeper_attempts=%u streak=%u baseline_matched=%d seal_rows_missing=0x%X\n",
	    load_tick,
	    (unsigned int)sSYNetRollbackResimBaselineWaitFrames,
	    (unsigned int)sSYNetRollbackPeerBaselineRetransmitCount,
	    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts,
	    (unsigned int)sSYNetRollbackBaselineTimeoutStreak,
	    (int)sSYNetRollbackResimBaselineDigestMatched,
	    syNetRollbackEpisodeGetMissingPeerSealSlotsMask());
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) &&
	    (syNetRollbackEpisodeAllPeerSealRowsComplete() == FALSE))
	{
		port_log(
		    "SSB64 NetRollback: RESIM_SEAL_ROWS_TIMEOUT load_tick=%u missing_slots=0x%X\n",
		    load_tick,
		    syNetRollbackEpisodeGetMissingPeerSealSlotsMask());
	}
	if ((syNetRollbackBaselineProceedOnTimeoutEnabled() != FALSE) &&
	    (syNetRollbackSymmetricWireLockActive() == FALSE))
	{
		port_log(
		    "SSB64 NetRollback: RESIM_BASELINE_TIMEOUT — proceeding without peer agreement (env proceed)\n");
		sSYNetRollbackResimBaselineDigestMatched = TRUE;
		sSYNetRollbackResimBaselineWaitFrames = 0U;
		sSYNetRollbackPeerBaselineSendPending = FALSE;
		syNetRollbackTryOpenResimReplayGate();
		return;
	}
	if (sSYNetRollbackBaselineTimeoutStreak >= SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_MAX)
	{
		port_log(
		    "SSB64 NetRollback: RESIM_BASELINE_TIMEOUT streak — hard desync recovery (load_tick=%u)\n",
		    load_tick);
		sSYNetRollbackResimPending = FALSE;
		sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
		sSYNetRollbackResimBaselineGateOpen = FALSE;
		sSYNetRollbackPeerBaselineSendPending = FALSE;
		if (sSYNetRollbackPeerSnapshotAbort != FALSE)
		{
			syNetPeerSendVsSessionEndNotifyPeer();
			syNetRollbackStopVSSession();
			syNetPeerStopVSSession();
		}
		else
		{
			syNetRollbackArmPeerBaselineResync(load_tick);
		}
		return;
	}
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackResimPending = FALSE;
	if ((sSYNetRollbackResimBaselineDeeperAttempts < SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS) && (load_tick > 0U))
	{
		sSYNetRollbackResimBaselineDeeperAttempts++;
		if (syNetRollbackTryRestartResimAtDeeperLoad(load_tick - 1U) != FALSE)
		{
			return;
		}
	}
	syNetRollbackArmPeerBaselineResync(load_tick);
}

static sb32 syNetRollbackTryDeeperLoadBeforeResim(u32 *io_load_tick, u32 *io_mismatch_tick)
{
	SYNetRollbackHashSet live;
	u32 load_tick;
	u32 slot_f;
	u32 slot_w;
	u32 slot_i;
	u32 slot_r;
	u32 slot_a;
	u32 slot_wp;
	u32 slot_m;
	u32 slot_c;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL))
	{
		return FALSE;
	}
	load_tick = *io_load_tick;
	live = syNetRollbackCollectHashes();
	slot_f = syNetRbSnapshotGetSlotHashFighter(load_tick);
	slot_w = syNetRbSnapshotGetSlotHashWorld(load_tick);
	slot_i = syNetRbSnapshotGetSlotHashItem(load_tick);
	slot_r = syNetRbSnapshotGetSlotHashRng(load_tick);
	slot_wp = syNetRbSnapshotGetSlotHashWeapon(load_tick);
	slot_m = syNetRbSnapshotGetSlotHashMap(load_tick);
	slot_c = syNetRbSnapshotGetSlotHashCamera(load_tick);
	slot_a = syNetRbSnapshotGetSlotHashAnimation(load_tick);
	if (syNetRollbackLoadHashDriftIsAnimOnly(load_tick, live.fighter, live.world, live.item, slot_wp, slot_m,
						 slot_r, slot_c, live.animation) != FALSE)
	{
		return FALSE;
	}
	if ((live.fighter == slot_f) && (live.world == slot_w) && (live.item == slot_i) && (live.rng == slot_r))
	{
		return FALSE;
	}
	if (load_tick == 0U)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_SLOT_LIVE_DRIFT pre-resim — trying deeper load_tick=%u (was %u)\n",
	    load_tick - 1U,
	    load_tick);
	if (syNetRollbackLoadPostTick(load_tick - 1U) == FALSE)
	{
		return FALSE;
	}
	*io_load_tick = load_tick - 1U;
	*io_mismatch_tick = load_tick;
	return TRUE;
}

static void syNetRollbackArmPeerBaselineResync(u32 load_tick)
{
	u32 mismatch_tick;
	u32 frontier;
	u32 target_tick;

	if (load_tick == 0U)
	{
		mismatch_tick = 1U;
	}
	else
	{
		mismatch_tick = load_tick;
	}
	if (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) != FALSE)
	{
		syNetRollbackOnPeerBaselineResyncStormLimit(load_tick);
		return;
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
	if (sSYNetRollbackPeerBaselineResyncStormActive == FALSE)
	{
		sSYNetRollbackPeerBaselineResyncStormActive = TRUE;
		sSYNetRollbackPeerBaselineResyncOriginMismatch = mismatch_tick;
		sSYNetRollbackPeerBaselineResyncSteps = 0U;
	}
	else if ((sSYNetRollbackDeferredStateMismatchTick != ~(u32)0) &&
	         (mismatch_tick < sSYNetRollbackDeferredStateMismatchTick))
	{
		sSYNetRollbackPeerBaselineResyncSteps++;
	}
	sSYNetRollbackDeferredStateMismatchPending = TRUE;
	sSYNetRollbackDeferredStateMismatchTick = mismatch_tick;
	sSYNetRollbackDeferredStateMismatchTargetTick = target_tick;
	port_log(
	    "SSB64 NetRollback: peer baseline resync armed load_tick=%u mismatch_tick=%u target_tick=%u sim=%u\n",
	    load_tick,
	    mismatch_tick,
	    target_tick,
	    syNetInputGetTick());
}

static u32 syNetRollbackGetPeerDivergeResyncTicks(void)
{
	const char *e;
	int v;

	if (sSYNetRollbackPeerDivergeResyncTicksCache != -999)
	{
		return (u32)sSYNetRollbackPeerDivergeResyncTicksCache;
	}
	e = getenv("SSB64_NETPLAY_PEER_DIVERGE_RESYNC_TICKS");
	v = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 2;
	if (v < 2)
	{
		v = 2;
	}
	if (v > 16)
	{
		v = 16;
	}
	sSYNetRollbackPeerDivergeResyncTicksCache = v;
	return (u32)v;
}

static sb32 syNetRollbackBaselineCompareQuiesced(void)
{
	if (sSYNetRollbackBaselineEchoRetryLoadTick != ~(u32)0)
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
	    (sSYNetRollbackResimBaselineGateOpen == FALSE))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackQueuePeerSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick,
						  sb32 follower_local_auth)
{
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick == sSYNetRollbackPendingPeerSymmetricTick))
	{
		if (target_tick > sSYNetRollbackPendingPeerSymmetricTargetTick)
		{
			sSYNetRollbackPendingPeerSymmetricTargetTick = target_tick;
		}
		if (follower_local_auth != FALSE)
		{
			sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = TRUE;
		}
		return;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick > sSYNetRollbackPendingPeerSymmetricTick))
	{
		return;
	}
	sSYNetRollbackPendingPeerSymmetricTick = mismatch_tick;
	sSYNetRollbackPendingPeerSymmetricTargetTick = target_tick;
	sSYNetRollbackPendingPeerSymmetricSlot = slot;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = follower_local_auth;
	syNetRollbackArmPeerSymmetricRejectLiveCap(mismatch_tick);
	if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
	{
		if (sSYNetRollbackPendingEpisode.valid == FALSE)
		{
			syNetRollbackPendingEpisodeSet(slot, mismatch_tick, target_tick, 0U, 0U, 0U);
		}
		else if (target_tick > sSYNetRollbackPendingEpisode.target_tick)
		{
			sSYNetRollbackPendingEpisode.target_tick = target_tick;
		}
	}
	if (sSYNetRollbackPeerSymmetricLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback queued slot=%d mismatch_tick=%u target_tick=%u follower_local_auth=%d sim=%u\n",
		    (int)slot,
		    mismatch_tick,
		    target_tick,
		    (int)follower_local_auth,
		    syNetInputGetTick());
		sSYNetRollbackPeerSymmetricLogsRemaining--;
	}
}

static void syNetRollbackFlushDeferredPeerSymmetric(void)
{
	u32 mismatch_tick;
	u32 target_tick;
	s32 slot;

	if (sSYNetRollbackDeferredPeerSymmetricPending == FALSE)
	{
		return;
	}
	if (syNetRollbackBaselineCompareQuiesced() == FALSE)
	{
		return;
	}
	mismatch_tick = sSYNetRollbackDeferredPeerSymmetricTick;
	target_tick = sSYNetRollbackDeferredPeerSymmetricTargetTick;
	slot = sSYNetRollbackDeferredPeerSymmetricSlot;
	{
		sb32 follower_local_auth;

		follower_local_auth = sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth;
		sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
		sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricSlot = -1;
		sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback flush deferred mismatch_tick=%u target_tick=%u sim=%u\n",
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
		syNetRollbackQueuePeerSymmetricNotify(slot, mismatch_tick, target_tick, follower_local_auth);
	}
	syNetRollbackRunDeferredPeerBaselineCompare();
}

static sb32 syNetRollbackSnapshotReadyForBaselineCompare(u32 load_tick)
{
	u32 sim_tick;

	if (load_tick == 0U)
	{
		return FALSE;
	}
	/* Sealed ticks only after at least one completed episode; load anchor stays comparable. */
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (load_tick > sSYNetRollbackEpisodeResolvedThrough))
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick > sSYNetRollbackResimLoadTick) &&
	    (load_tick < sSYNetRollbackResimTargetTick))
	{
		return FALSE;
	}
	if ((sSYNetRollbackPeerEpochMismatchTick != 0U) && (sSYNetRollbackPeerEpochTargetTick != 0U) &&
	    (load_tick >= sSYNetRollbackPeerEpochMismatchTick) && (load_tick < sSYNetRollbackPeerEpochTargetTick))
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if (load_tick >= sim_tick)
	{
		return FALSE;
	}
	return syNetRbSnapshotIsTickCommitted(load_tick);
}

static sb32 syNetRollbackResolveLoadTickForSnapshot(u32 *io_load_tick, u32 *io_mismatch_tick)
{
	u32 min_load;
	u32 sim_tick;
	u32 resolved;
	u32 load_tick;
	u32 rewind_limit;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL))
	{
		return FALSE;
	}
	load_tick = *io_load_tick;
	if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, NULL, NULL, NULL, NULL) != FALSE)
	{
		return TRUE;
	}
	sim_tick = syNetInputGetTick();
	min_load = syNetRollbackLoadTickMinBound(sim_tick);
	if (load_tick < min_load)
	{
		return FALSE;
	}
	rewind_limit = SYNETROLLBACK_LOAD_TICK_REWIND_MAX;
	if ((load_tick > min_load) && ((load_tick - min_load) < rewind_limit))
	{
		rewind_limit = load_tick - min_load;
	}
	if (load_tick > rewind_limit)
	{
		min_load = load_tick - rewind_limit;
	}
	resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(load_tick, min_load);
	if (resolved == ~(u32)0)
	{
		resolved = syNetRbSnapshotFindLatestValidTickAtOrBefore(load_tick, min_load);
	}
	if (resolved == ~(u32)0)
	{
		return FALSE;
	}
	if (resolved != load_tick)
	{
		if ((sSYNetRollbackAuthoritativeEpisodeActive != FALSE) &&
		    (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
		{
			u32 locked_load;

			locked_load = sSYNetRollbackExecutingEpisode.load_tick;
			if ((locked_load > 0U) && (resolved < locked_load) &&
			    ((locked_load - resolved) <= SYNETROLLBACK_LOAD_TICK_REWIND_MAX))
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_LOAD_REWIND locked_load=%u resolved=%u mismatch=%u sim=%u\n",
				    locked_load,
				    resolved,
				    *io_mismatch_tick,
				    sim_tick);
				*io_load_tick = resolved;
			}
			else
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_LOAD_FAIL requested=%u resolved=%u locked_load=%u mismatch=%u sim=%u\n",
				    load_tick,
				    resolved,
				    locked_load,
				    *io_mismatch_tick,
				    sim_tick);
				return FALSE;
			}
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: LOAD_TICK_ADJUST requested=%u resolved=%u mismatch=%u->%u sim=%u\n",
			    load_tick,
			    resolved,
			    *io_mismatch_tick,
			    resolved + 1U,
			    sim_tick);
			*io_load_tick = resolved;
			if (*io_mismatch_tick > (resolved + 1U))
			{
				*io_mismatch_tick = resolved + 1U;
			}
		}
	}
	return TRUE;
}

static sb32 syNetRollbackPeerBaselineDriftIsAnimOnly(const SYNetRollbackHashSet *peer, const SYNetRollbackHashSet *local)
{
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	if ((peer->fighter != local->fighter) || (peer->world != local->world) || (peer->item != local->item) ||
	    (peer->rng != local->rng) || (peer->weapon != local->weapon) || (peer->map != local->map) ||
	    (peer->camera != local->camera))
	{
		return FALSE;
	}
	if (peer->animation == local->animation)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackPeerBaselineDriftIsGameplayOnlyMap(const SYNetRollbackHashSet *peer,
							    const SYNetRollbackHashSet *local)
{
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	if (peer->map == local->map)
	{
		return FALSE;
	}
	if ((peer->fighter != local->fighter) || (peer->world != local->world) || (peer->item != local->item) ||
	    (peer->rng != local->rng) || (peer->animation != local->animation) || (peer->weapon != local->weapon) ||
	    (peer->camera != local->camera))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackRunDeferredPeerBaselineCompare(void)
{
	u32 load_tick;

	if (sSYNetRollbackDeferredPeerBaselineComparePending == FALSE)
	{
		return;
	}
	if (sSYNetRollbackLastPeerOutcomeValid == FALSE)
	{
		sSYNetRollbackDeferredPeerBaselineComparePending = FALSE;
		sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
		return;
	}
	load_tick = sSYNetRollbackDeferredPeerBaselineCompareLoadTick;
	if (load_tick != sSYNetRollbackLastPeerOutcomeTick)
	{
		return;
	}
	sSYNetRollbackDeferredPeerBaselineComparePending = FALSE;
	sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
	syNetRollbackComparePeerBaselineToLocal(load_tick, &sSYNetRollbackLastPeerOutcomeHash, NULL);
}

static u32 syNetRollbackClampLoadTickForPeerSend(u32 load_tick)
{
	u32 remote_hr;

	if (load_tick == 0U)
	{
		return load_tick;
	}
	remote_hr = syNetPeerGetHighestRemoteTick();
	if ((remote_hr == 0U) || (load_tick <= remote_hr))
	{
		return load_tick;
	}
	port_log(
	    "SSB64 NetRollback: BASELINE_LOAD_CLAMP orig_load_tick=%u effective_load_tick=%u remote_hr=%u sim=%u\n",
	    load_tick,
	    remote_hr,
	    remote_hr,
	    syNetInputGetTick());
	return remote_hr;
}

static void syNetRollbackComparePeerBaselineToLocal(u32 load_tick, const SYNetRollbackHashSet *peer,
						    const u32 *peer_fighter_slot)
{
	SYNetRollbackHashSet local;
	u32 sim_tick;
	u32 resync_ticks;

	if (peer == NULL)
	{
		return;
	}
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
	if ((peer->fighter == local.fighter) && (peer->world == local.world) && (peer->item == local.item) &&
	    (peer->rng == local.rng) && (peer->animation == local.animation) && (peer->weapon == local.weapon) &&
	    (peer->map == local.map) && (peer->camera == local.camera))
	{
		return;
	}
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackEpisodeInputsSealed() != FALSE) &&
	    (syNetRollbackEpisodeAllPeerSealRowsComplete() != FALSE) && (peer->world == local.world) &&
	    (peer->item == local.item) && (peer->rng == local.rng) && (peer->fighter != local.fighter))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM seal_authority_mismatch load_tick=%u peer figh=0x%08X local figh=0x%08X — input correction\n",
		    load_tick,
		    peer->fighter,
		    local.fighter);
		syNetRollbackAbortToInputCorrectionFromUniverseMismatch(load_tick);
		return;
	}
	if (syNetRollbackPeerBaselineDriftIsAnimOnly(peer, &local) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_ANIM_ONLY load_tick=%u peer anim=0x%08X local anim=0x%08X — continuing\n",
		    load_tick,
		    peer->animation,
		    local.animation);
		return;
	}
	if (syNetRollbackPeerBaselineDriftIsGameplayOnlyMap(peer, &local) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_MAP_DRIFT load_tick=%u peer map=0x%08X local map=0x%08X — arming resync\n",
		    load_tick,
		    peer->map,
		    local.map);
		if (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) == FALSE)
		{
			syNetRollbackArmPeerBaselineResync(load_tick);
		}
		return;
	}
	sim_tick = syNetInputGetTick();
	resync_ticks = syNetRollbackGetPeerDivergeResyncTicks();
	if ((sim_tick > load_tick) && ((sim_tick - load_tick) > resync_ticks))
	{
		if (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) == FALSE)
		{
			syNetRollbackArmPeerBaselineResync(load_tick);
		}
	}
	else
	{
		syNetRollbackFailPeerSnapshotDiverge(load_tick, peer, &local, peer_fighter_slot);
	}
}

static void syNetRollbackPumpBaselineEchoRetry(void)
{
	u32 load_tick;

	if (sSYNetRollbackBaselineEchoRetryLoadTick == ~(u32)0)
	{
		return;
	}
	load_tick = sSYNetRollbackBaselineEchoRetryLoadTick;
	{
		u32 probe_f;
		u32 probe_w;
		u32 probe_i;
		u32 probe_r;

		if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &probe_f, &probe_w, &probe_i, &probe_r) == FALSE)
		{
			return;
		}
	}
	if ((sSYNetRollbackLastPeerOutcomeValid == FALSE) || (sSYNetRollbackLastPeerOutcomeTick != load_tick))
	{
		return;
	}
	if (syNetRollbackBaselineEchoAllowed(load_tick) != FALSE)
	{
		(void)syNetRollbackTryEchoBaselineResponse(load_tick);
	}
	port_log(
	    "SSB64 NetRollback: BASELINE_ECHO_RETRY load_tick=%u attempt=%u sim=%u\n",
	    load_tick,
	    sSYNetRollbackBaselineEchoRetryAttempts,
	    syNetInputGetTick());
	syNetRollbackComparePeerBaselineToLocal(load_tick, &sSYNetRollbackLastPeerOutcomeHash, NULL);
	sSYNetRollbackBaselineEchoRetryLoadTick = ~(u32)0;
	sSYNetRollbackBaselineEchoRetryAttempts = 0U;
	syNetRollbackFlushDeferredPeerSymmetric();
}

static void syNetRollbackFailPeerSnapshotDiverge(u32 load_tick, const SYNetRollbackHashSet *peer,
						 const SYNetRollbackHashSet *local, const u32 *peer_fighter_slot)
{
	s32 si;
	u32 local_slot[GMCOMMON_PLAYERS_MAX];
	SYNetSyncRollbackWorldComponents local_world;

	port_log(
	    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE load_tick=%u peer figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X wpn=0x%08X map=0x%08X cam=0x%08X | local figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X wpn=0x%08X map=0x%08X cam=0x%08X\n",
	    load_tick,
	    peer->fighter,
	    peer->world,
	    peer->item,
	    peer->rng,
	    peer->animation,
	    peer->weapon,
	    peer->map,
	    peer->camera,
	    local->fighter,
	    local->world,
	    local->item,
	    local->rng,
	    local->animation,
	    local->weapon,
	    local->map,
	    local->camera);
	if (syNetSyncPeerDivergeDetailEnabled() != FALSE)
	{
		if (peer->fighter != local->fighter)
		{
			port_log("SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=figh peer=0x%08X local=0x%08X\n",
				 load_tick,
				 peer->fighter,
				 local->fighter);
		}
		if (peer->item != local->item)
		{
			port_log("SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=item peer=0x%08X local=0x%08X\n",
				 load_tick,
				 peer->item,
				 local->item);
		}
		if (peer->rng != local->rng)
		{
			port_log("SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=rng peer=0x%08X local=0x%08X\n",
				 load_tick,
				 peer->rng,
				 local->rng);
		}
		if (peer->animation != local->animation)
		{
			port_log(
			    "SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=anim peer=0x%08X local=0x%08X\n",
			    load_tick,
			    peer->animation,
			    local->animation);
		}
		if (peer->weapon != local->weapon)
		{
			port_log(
			    "SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=weapon peer=0x%08X local=0x%08X\n",
			    load_tick,
			    peer->weapon,
			    local->weapon);
		}
		if (peer->map != local->map)
		{
			port_log("SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=map peer=0x%08X local=0x%08X\n",
				 load_tick,
				 peer->map,
				 local->map);
		}
		if (peer->camera != local->camera)
		{
			port_log(
			    "SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=camera peer=0x%08X local=0x%08X\n",
			    load_tick,
			    peer->camera,
			    local->camera);
		}
		if (peer->world != local->world)
		{
			port_log(
			    "SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=world peer=0x%08X local=0x%08X\n",
			    load_tick,
			    peer->world,
			    local->world);
			syNetSyncCollectRollbackWorldComponents(&local_world);
			syNetSyncLogWorldHashDiff("peer_diverge", load_tick, NULL, &local_world);
		}
		syNetRollbackCollectFighterSlotHashes(local_slot);
		if (peer_fighter_slot != NULL)
		{
			for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
			{
				if (peer_fighter_slot[si] != local_slot[si])
				{
					port_log(
					    "SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=%u partition=fighter_slot player=%d peer=0x%08X local=0x%08X\n",
					    load_tick,
					    (int)si,
					    peer_fighter_slot[si],
					    local_slot[si]);
				}
			}
		}
		syNetSyncLogRollbackWorldDetail("peer_diverge", load_tick);
		syNetSyncLogFighterDetail("peer_diverge", load_tick);
		gcPortDumpGObjEjectRing("peer_diverge", load_tick);
	}
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
				     u32 map, u32 camera, const u32 *fighter_slot)
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
	sSYNetRollbackLastPeerOutcomeHash = peer;
	sSYNetRollbackLastPeerOutcomeTick = load_tick;
	sSYNetRollbackLastPeerOutcomeValid = TRUE;
	if (syNetRollbackTryNegotiateResimLoadTickWithPeer(load_tick) != FALSE)
	{
		return;
	}
	syNetRollbackTryOpenResimBaselineGateFromPeerDigest(load_tick, &peer, fighter_slot);
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick != sSYNetRollbackResimLoadTick))
	{
		/* Ignore foreign baseline traffic while a local resim episode is in flight. */
		return;
	}
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick == sSYNetRollbackResimLoadTick) &&
	    (sSYNetRollbackResimAwaitingPeerBaseline != FALSE))
	{
		return;
	}
	if ((sSYNetRollbackResimPending == FALSE) && (syNetRollbackBaselineEchoAllowed(load_tick) != FALSE))
	{
		(void)syNetRollbackTryEchoBaselineResponse(load_tick);
	}
	if (syNetRollbackSnapshotReadyForBaselineCompare(load_tick) == FALSE)
	{
		u32 sim_tick;

		sim_tick = syNetInputGetTick();
		if (sSYNetRollbackBaselineEchoRetryAttempts < SYNETROLLBACK_BASELINE_SNAPSHOT_RETRY_MAX)
		{
			sSYNetRollbackBaselineEchoRetryLoadTick = load_tick;
			sSYNetRollbackBaselineEchoRetryAttempts++;
			port_log(
			    "SSB64 NetRollback: BASELINE_ECHO_RETRY_DEFER load_tick=%u sim=%u attempt=%u (snapshot_not_ready)\n",
			    load_tick,
			    sim_tick,
			    sSYNetRollbackBaselineEchoRetryAttempts);
			return;
		}
	}
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
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick > sSYNetRollbackResimLoadTick) &&
	    (load_tick < sSYNetRollbackResimTargetTick))
	{
		sSYNetRollbackDeferredPeerBaselineComparePending = TRUE;
		sSYNetRollbackDeferredPeerBaselineCompareLoadTick = load_tick;
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_COMPARE_DEFER load_tick=%u resim_span=%u..%u sim=%u\n",
		    load_tick,
		    sSYNetRollbackResimLoadTick,
		    sSYNetRollbackResimTargetTick,
		    syNetInputGetTick());
		return;
	}
	syNetRollbackComparePeerBaselineToLocal(load_tick, &peer, fighter_slot);
}

void syNetRollbackNotePeerBaselineDigestSent(void)
{
	sSYNetRollbackPeerBaselineRetransmitCount++;
	if ((sSYNetRollbackResimAwaitingPeerBaseline == FALSE) || (sSYNetRollbackResimBaselineGateOpen != FALSE) ||
	    (sSYNetRollbackResimPending == FALSE))
	{
		sSYNetRollbackPeerBaselineSendPending = FALSE;
	}
}

sb32 syNetRollbackTakePeerBaselineDigestForSend(u32 *out_load_tick, u32 *out_figh, u32 *out_world, u32 *out_item,
					      u32 *out_rng, u32 *out_anim, u32 *out_weapon, u32 *out_map,
					      u32 *out_camera, u32 *out_fighter_slot, s32 fighter_slot_count)
{
	s32 si;

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
	if ((out_fighter_slot != NULL) && (fighter_slot_count >= GMCOMMON_PLAYERS_MAX))
	{
		for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
		{
			out_fighter_slot[si] = sSYNetRollbackPeerBaselineFighterSlot[si];
		}
	}
	return TRUE;
}
#endif

static sb32 syNetRollbackBeginResim(u32 mismatch_tick, u32 target_tick, s32 correction_player)
{
	u32 load_tick;
	sb32 from_peer_notify;

	if ((mismatch_tick >= target_tick) || (mismatch_tick == 0))
	{
		return FALSE;
	}
	from_peer_notify = sSYNetRollbackResimFromPeerSymmetric;
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	if ((from_peer_notify != FALSE) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
	    (sSYNetRollbackPendingEpisode.valid != FALSE))
	{
		sSYNetRollbackExecutingEpisode = sSYNetRollbackPendingEpisode;
		sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
		mismatch_tick = sSYNetRollbackExecutingEpisode.mismatch_tick;
		target_tick = sSYNetRollbackExecutingEpisode.target_tick;
		load_tick = sSYNetRollbackExecutingEpisode.load_tick;
		syNetRollbackPendingEpisodeClear();
	}
	else
	{
		sSYNetRollbackExecutingEpisode.valid = TRUE;
		sSYNetRollbackExecutingEpisode.slot = correction_player;
		sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
		sSYNetRollbackExecutingEpisode.target_tick = target_tick;
		sSYNetRollbackExecutingEpisode.load_tick =
		    (mismatch_tick > 0U) ? (mismatch_tick - 1U) : 0U;
		sSYNetRollbackExecutingEpisode.epoch_id = sSYNetRollbackEpochId;
		load_tick = sSYNetRollbackExecutingEpisode.load_tick;
		if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
		    (sSYNetRollbackFcStateRecoveryActive != FALSE))
		{
			sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
		}
	}
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	sSYNetRollbackResimCorrectionPlayer = correction_player;
	sSYNetRollbackResimBaselineDeeperAttempts = 0U;
	sSYNetRollbackPeerBaselineRetransmitCount = 0U;
	if (load_tick == 0U)
	{
		load_tick = mismatch_tick - 1U;
	}
	{
		u32 clamped_load = syNetRollbackClampLoadTickForPeerSend(load_tick);

		if ((clamped_load != load_tick) && (clamped_load > 0U))
		{
			load_tick = clamped_load;
			if (mismatch_tick > (load_tick + 1U))
			{
				mismatch_tick = load_tick + 1U;
			}
		}
	}
	if (syNetRollbackResolveLoadTickForSnapshot(&load_tick, &mismatch_tick) == FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetRollback: load post tick %u failed (no snapshot in ring; ring=%u scan=%u)\n",
		    load_tick,
		    (unsigned int)syNetRbSnapshotRingCapacity(),
		    (unsigned int)SYNETROLLBACK_SCAN_WINDOW);
		sSYNetRollbackLoadFailCount++;
		syNetRollbackResetCorrectionEpisode();
#endif
		return FALSE;
	}
	if (syNetRollbackLoadPostTick(load_tick) == FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetRollback: load post tick %u failed (need earlier snapshots; ring=%u scan=%u)\n",
		    load_tick,
		    (unsigned int)syNetRbSnapshotRingCapacity(),
		    (unsigned int)SYNETROLLBACK_SCAN_WINDOW);
		sSYNetRollbackLoadFailCount++;
		syNetRollbackResetCorrectionEpisode();
#endif
		return FALSE;
	}
#ifdef PORT
	if (syNetRollbackTryDeeperLoadBeforeResim(&load_tick, &mismatch_tick) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: resim begin after deeper pre-load mismatch_tick=%u load_tick=%u target_tick=%u\n",
		    mismatch_tick,
		    load_tick,
		    target_tick);
	}
	syNetSyncLogRollbackWorldDetail("rollback_load", load_tick);
	syNetSyncLogFighterDetail("rollback_load", load_tick);
	syNetRollbackArmResimBaselineAfterLoad(load_tick);
#endif
	syNetInputRollbackPrepareForResim(mismatch_tick);
#ifdef PORT
	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		syNetInputRollbackReconcileResimSpan(mismatch_tick, target_tick, correction_player);
		if ((from_peer_notify != FALSE) || (syNetRollbackPeerSymmetricAuthoritySlotForPlayer(correction_player) >= 0))
		{
			s32 authority_slot;

			authority_slot = correction_player;
			if (from_peer_notify != FALSE)
			{
				authority_slot = syNetPeerGetLocalSimSlot();
				if (authority_slot < 0)
				{
					authority_slot = correction_player;
				}
			}
			syNetInputRollbackReconcilePeerSymmetricAuthority(authority_slot, mismatch_tick, target_tick);
		}
	}
	port_log(
	    "SSB64 NetRollback: resim begin epoch=%u mismatch_tick=%u load_tick=%u target_tick=%u span=%u budget=%u/frame owner=%s\n",
	    sSYNetRollbackEpochId,
	    mismatch_tick,
	    load_tick,
	    target_tick,
	    (unsigned int)(target_tick - mismatch_tick),
	    (unsigned int)sSYNetRollbackResimTicksPerFrame,
	    from_peer_notify != FALSE ? "peer_follower" : "local_initiator");
	if ((from_peer_notify == FALSE) && (syNetRollbackSymmetricWireLockActive() != FALSE))
	{
		sb32 follower_local_auth;
		s32 notify_slot;

		notify_slot = correction_player;
		if (notify_slot < 0)
		{
			if (syNetPeerGetRemoteHumanSlotByIndex(0, &notify_slot) == FALSE)
			{
				notify_slot = 0;
			}
		}
		follower_local_auth = syNetRollbackPlayerIsRemoteHuman(notify_slot) ? TRUE : FALSE;
		syNetRollbackArmSymmetricNotifyEx(notify_slot, mismatch_tick, target_tick, load_tick,
						sSYNetRollbackExecutingEpisode.epoch_id, follower_local_auth);
	}
	syNetRollbackEpisodeBegin(mismatch_tick,
	                          load_tick,
	                          target_tick,
	                          correction_player,
	                          (from_peer_notify == FALSE) ? TRUE : FALSE,
	                          from_peer_notify);
	syNetRollbackMaybeLogEpisodeExec(from_peer_notify);
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimNextTick = mismatch_tick;
	sSYNetRollbackResimDepth = 1U;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	syNetPeerSendLocalInput();
	syNetPeerTrySendRollbackSyncNotice();
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
	syNetSyncLogItemHashWalkTrace(tick);
}

static void syNetRollbackLogResimComplete(void)
{
	SYNetRollbackHashSet post;

	if (sSYNetRollbackResimPreHashesValid == FALSE)
	{
		return;
	}
	post = syNetRollbackCollectHashes();
	syNetSyncLogItemHashWalkTrace(syNetInputGetTick());
	port_log(
	    "SSB64 NetRollback: resim complete epoch=%u baseline/post figh=0x%08X/0x%08X world=0x%08X/0x%08X item=0x%08X/0x%08X wpn=0x%08X/0x%08X mph=0x%08X/0x%08X rng=0x%08X/0x%08X cam=0x%08X/0x%08X anim=0x%08X/0x%08X load_tick=%u mismatch_tick=%u rollbacks=%u\n",
	    sSYNetRollbackEpochId,
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
	syNetRollbackCaptureResimPostBoundaryDigest();
	if (sSYNetRollbackResimPostLocalValid != FALSE)
	{
		sSYNetRollbackResimPostCompletedValid = TRUE;
		sSYNetRollbackResimPostCompletedKey = sSYNetRollbackResimPostLocalKey;
		sSYNetRollbackResimPostCompletedDigest = sSYNetRollbackResimPostLocalDigest;
	}
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

static sb32 syNetRollbackResimSnapshotRefreshEnabled(void)
{
	const char *env;
	static sb32 sCached = -1;

	if (sCached >= 0)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_RESIM_SNAPSHOT_REFRESH");
	if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
	{
		sCached = 0;
	}
	else
	{
		sCached = 1;
	}
	return (sCached != 0) ? TRUE : FALSE;
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
	if ((sSYNetRollbackResimAwaitingPeerBaseline != FALSE) && (sSYNetRollbackResimBaselineGateOpen == FALSE))
	{
		syNetRollbackPumpResimBaselineSend();
		sSYNetRollbackResimBaselineWaitFrames++;
		if (sSYNetRollbackResimBaselineWaitFrames >= SYNETROLLBACK_BASELINE_GATE_TIMEOUT_FRAMES)
		{
			syNetRollbackOnBaselineGateTimeout();
			if ((sSYNetRollbackResimPending == FALSE) ||
			    (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) ||
			    (sSYNetRollbackResimBaselineGateOpen == FALSE))
			{
				return;
			}
		}
		else
		{
			return;
		}
	}
	ran = 0;
	t = sSYNetRollbackResimNextTick;
	while ((t < sSYNetRollbackResimTargetTick) && (ran < sSYNetRollbackResimTicksPerFrame))
	{
		syNetInputSetTick(t);
		syNetInputPublishSynchronizedTick(t);
		scVSBattleFuncUpdateBattleSimOnly();
		if (syNetRollbackResimSnapshotRefreshEnabled() != FALSE)
		{
			(void)syNetRbSnapshotSave(t);
		}
		if (syNetRollbackEpisodeFsmEnabled() != FALSE)
		{
			SYNetRollbackHashSet tick_h;
			u32 tick_inp;

			tick_h = syNetRollbackCollectHashes();
			tick_inp = syNetRollbackEpisodeReplayLogTickInputDigest(t);
			syNetRollbackEpisodeReplayLogAppend(t, tick_inp, tick_h.fighter, tick_h.item, tick_h.rng);
		}
		syNetRollbackLogResimTickTrace(t);
		t++;
		ran++;
	}
	sSYNetRollbackResimNextTick = t;
	if (t >= sSYNetRollbackResimTargetTick)
	{
		u32 completed_mismatch;

		completed_mismatch = sSYNetRollbackResimMismatchTick;
		if (completed_mismatch == 0U)
		{
			completed_mismatch = sSYNetRollbackEpisode.mismatch_tick;
		}
		syNetRollbackFinishForwardResim();
		syNetSyncLogRollbackWorldDetail("rollback_post", completed_mismatch);
		syNetSyncLogFighterDetail("rollback_post", completed_mismatch);
		ifCommonItemArrowPruneStaleInterfaces();
		syNetRollbackLogResimComplete();
		syNetRollbackOnResimCompleted();
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
	u32 peer_symmetric_target_tick;
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
	syNetRollbackFlushPendingResimPostHandshake();
	syNetRollbackDebugTryApplyPendingForceMismatch();
	syNetRollbackExpireSymmetricNotify();
	if (syNetRollbackTryBeginDeferredMismatch() != FALSE)
	{
		return;
	}
	syNetRollbackFlushDeferredPeerSymmetric();
	if (syNetRollbackTryBeginDeferredStateMismatch() != FALSE)
	{
		return;
	}
	syNetRollbackFlushDeferredPeerSymmetric();
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
	peer_symmetric_target_tick = ~(u32)0;
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
		if ((peer_tick != 0U) && (peer_tick <= frontier) &&
		    ((mismatch == ~(u32)0) || (peer_tick <= mismatch)))
		{
			s32 sym_slot;

			sym_slot = sSYNetRollbackPendingPeerSymmetricSlot;
			if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
			    (sSYNetRollbackPendingEpisode.valid != FALSE))
			{
				peer_tick = sSYNetRollbackPendingEpisode.mismatch_tick;
				peer_target_tick = sSYNetRollbackPendingEpisode.target_tick;
			}
			else if (syNetRollbackPeerSymmetricUseFollowerLocalAuthority(
				     sym_slot, sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth) != FALSE)
			{
				u32 local_mismatch;
				u32 eff_mismatch;
				u32 eff_target;
				s32 authority_slot;
				u32 scan_to;

				authority_slot = syNetPeerGetLocalSimSlot();
				if (authority_slot < 0)
				{
					authority_slot = sym_slot;
				}
				eff_target = peer_target_tick;
				scan_to = eff_target;
				if (scan_to > frontier)
				{
					scan_to = frontier;
				}
				if (scan_to <= peer_tick)
				{
					scan_to = peer_tick + 1U;
				}
				local_mismatch =
				    syNetInputFindEarliestLocalAuthorityMismatch(authority_slot, peer_tick, scan_to);
				eff_mismatch = peer_tick;
				if ((local_mismatch != ~(u32)0) && (local_mismatch >= peer_tick))
				{
					eff_mismatch = local_mismatch;
				}
				if (sSYNetRollbackPeerSymmetricLogsRemaining > 0U)
				{
					port_log(
					    "SSB64 NetRollback: peer symmetric local authority queued notify_slot=%d authority_slot=%d mismatch_tick=%u local_mismatch=%u target_tick=%u frontier=%u\n",
					    (int)sym_slot,
					    (int)authority_slot,
					    peer_tick,
					    eff_mismatch,
					    eff_target,
					    frontier);
					sSYNetRollbackPeerSymmetricLogsRemaining--;
				}
				sSYNetRollbackPeerSymmetricAppliedTick[sym_slot] = peer_tick;
				sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
				sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
				sSYNetRollbackPendingPeerSymmetricSlot = -1;
				sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
				sSYNetRollbackDeferredMismatchFromPeerSymmetric = TRUE;
				syNetRollbackQueueDeferredInputCorrectionEx(authority_slot, eff_mismatch, eff_target);
				if (sSYNetRollbackDeferredMismatchPending == FALSE)
				{
					sSYNetRollbackDeferredMismatchFromPeerSymmetric = FALSE;
					syNetRollbackArmPeerSymmetricRejectLiveCap(eff_mismatch);
					syNetRollbackLogDeferDiag("symmetric_queue_failed", eff_mismatch, eff_target,
					                           authority_slot);
				}
				if (syNetRollbackTryBeginDeferredMismatch() != FALSE)
				{
					return;
				}
				return;
			}
			mismatch = peer_tick;
			resim_target_tick = peer_target_tick;
			peer_symmetric_target_tick = peer_target_tick;
			mismatch_player = sym_slot;
			mismatch_from_peer_symmetric = TRUE;
			sSYNetRollbackResimFromPeerSymmetric = TRUE;
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
		if ((sSYNetRollbackLastOutcomeProbeFrontier == ~(u32)0) ||
		    (frontier >= sSYNetRollbackLastOutcomeProbeFrontier + SYNETROLLBACK_OUTCOME_PROBE_INTERVAL_TICKS))
		{
			sSYNetRollbackLastOutcomeProbeFrontier = frontier;
			if (syNetRollbackOutcomeCorrectionSuppressedDuringEpisode(frontier) == FALSE)
			{
				syNetRollbackTryOutcomeAwareCorrection(frontier);
			}
		}
#endif
		return;
	}
	if (mismatch_from_peer_symmetric == FALSE)
	{
		syNetRollbackCoalesceScanResimSpan(&mismatch, &resim_target_tick, frontier);
	}
	else if (peer_symmetric_target_tick != ~(u32)0)
	{
		resim_target_tick = peer_symmetric_target_tick;
	}
	if (syNetRollbackCorrectionAllowedAtTick(mismatch) == FALSE)
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
	if (mismatch_from_peer_symmetric != FALSE)
	{
		if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
		    (sSYNetRollbackPendingEpisode.valid != FALSE))
		{
			sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
			resim_target_tick =
			    syNetRollbackClampResimTargetTickAuthoritative(mismatch,
									   sSYNetRollbackPendingEpisode.target_tick);
		}
		else
		{
			resim_target_tick = syNetRollbackClampResimTargetTickEx(mismatch, resim_target_tick, frontier, TRUE);
		}
	}
	else
	{
		sb32 wire_lock;

		wire_lock = syNetRollbackSymmetricWireLockActive();
		resim_target_tick = syNetRollbackClampResimTargetTickEx(mismatch, resim_target_tick, frontier, wire_lock);
	}
	{
		SYNetRollbackCorrectionCommitSnap commit_snap;
		s32 correction_player;

		correction_player = mismatch_player;
		if (mismatch_from_peer_symmetric != FALSE)
		{
			if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) ||
			    (syNetRollbackPeerSymmetricUseFollowerLocalAuthority(
				 sSYNetRollbackPendingPeerSymmetricSlot,
				 sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth) == FALSE))
			{
				s32 local_slot;

				local_slot = syNetPeerGetLocalSimSlot();
				correction_player = (local_slot >= 0) ? local_slot : sSYNetRollbackPendingPeerSymmetricSlot;
			}
			else if (sSYNetRollbackPendingPeerSymmetricSlot >= 0)
			{
				correction_player = sSYNetRollbackPendingPeerSymmetricSlot;
			}
		}
		if (syNetRollbackTryCommitCorrectionBegin(mismatch, mismatch - 1U, resim_target_tick, &commit_snap) == FALSE)
		{
			return;
		}
		if (syNetRollbackBeginResim(mismatch, resim_target_tick, correction_player) == FALSE)
		{
			syNetRollbackAbortCorrectionCommit(&commit_snap);
			return;
		}
	}
#ifdef PORT
	if (mismatch_from_peer_symmetric != FALSE)
	{
		if ((sSYNetRollbackPendingPeerSymmetricSlot >= 0) &&
		    (sSYNetRollbackPendingPeerSymmetricSlot < MAXCONTROLLERS))
		{
			sSYNetRollbackPeerSymmetricAppliedTick[sSYNetRollbackPendingPeerSymmetricSlot] = mismatch;
		}
		sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
		sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
		sSYNetRollbackPendingPeerSymmetricSlot = -1;
		sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
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
