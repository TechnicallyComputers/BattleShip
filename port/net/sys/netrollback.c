#include <sys/netrollback.h>
#include <sys/netsched_rbe.h>

#include <sys/netrollback_episode.h>
#include <sys/netinput.h>
#include <sys/netreconnect.h>
#include <sys/netinput_timeline.h>
#include <sys/netpeer.h>
#include <sys/netreplay.h>
#include <sys/netrollbacksnapshot.h>
#if defined(PORT) && defined(SSB64_NETMENU)
#include <sys/netplay_statusvars_bank.h>
#endif
#include <sys/netsync.h>
#include <sys/netsync_rng_trace.h>
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
#if defined(SSB64_NETMENU)
#include <gr/grdef.h>
#include <gr/grcommon/grpupupu.h>
#include <gr/ground.h>
#include <gm/gmcamera.h>
#include <sys/netplay_ness_pkthunder_gate.h>
#include <sys/netplay_pikachu_quickattack_gate.h>
#include <sys/netplay_fox_firefox_gate.h>
#include <sys/netplay_rebirth_gate.h>
#include <sys/netplay_resim_replay_hang_diag.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netplay_branch_predict.h>
#endif

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
static sb32 sSYNetRollbackDiagnosticResimConsumed;
static u32 sSYNetRollbackLastVerifyHash;
static sb32 sSYNetRollbackForceMismatch;
static u32 sSYNetRollbackForceMismatchPendingTick;
static sb32 sSYNetRollbackMismatchDebug;
static sb32 sSYNetRollbackVerifyStrict;
static sb32 sSYNetRollbackLoadHashVerify;
static sb32 sSYNetRollbackVerifyEffectHash;
static u32 sSYNetRollbackLoadFailCount;
static sb32 sSYNetRollbackBattleSimHoldAfterLoadFail;
static sb32 sSYNetRollbackLoadFailBattleExitPending;
static sb32 sSYNetRollbackLoadFailSceneRetargeted;
/* BATTLE_SIM_HOLD escape watchdog: the hold blocks the battle sim, so nothing
 * inside the sim can end the match. Frames counted by PumpLoadFailBattleExit
 * (the one thing scvsbattle still runs while held). */
static u32 sSYNetRollbackBattleSimHoldLoadTick;
static u32 sSYNetRollbackBattleSimHoldFrames;
static sb32 sSYNetRollbackBattleSimHoldTeardownSent;
#define SYNETROLLBACK_BATTLE_SIM_HOLD_TEARDOWN_FRAMES 120U /* ~2 s @ 60 Hz */
/*
 * Live-wedge escape: the cap branch of ShouldBlockLiveBattleAdvance had no watchdog at
 * all — soak 2026-08-30 wedged both peers at tick 3851 (FC validation never passed, the
 * live cap never rose) with inputs flowing and frames pumping for ~15 s until manual
 * shutdown, BATTLE_SIM_HOLD never armed. Track the LIVE FRONTIER (highest tick reached
 * outside resim — resim episodes rewind syNetInputGetTick, so a naive tick-unchanged
 * counter resets on every failed recovery episode and never fires) plus proof the commit
 * gate actually refused a tick at/past it, and after the threshold escalate into the
 * proven BATTLE_SIM_HOLD escape (emergency restore → watchdog → VS teardown → clean
 * exit). See docs/bugs/netplay_live_cap_wedge_escape_2026-08-30.md.
 */
static u32 sSYNetRollbackLiveWedgeFrontierTick;
static u32 sSYNetRollbackLiveWedgeFrames;
#define SYNETROLLBACK_LIVE_WEDGE_ESCAPE_FRAMES_DEFAULT 600U /* ~10 s @ 60 Hz */
static sb32 sSYNetRollbackResimAnchorProbeLastMismatch;
static sb32 sSYNetRollbackResimAnchorProbeActive;
static u32 sMismatchAsymLogsRemaining;
static s32 sSYNetRollbackForceMismatchPlayerSlot;
static u32 sSYNetRollbackResimTicksPerFrame;
static u32 sSYNetRollbackResimMaxBurstTicks;
static sb32 sSYNetRollbackResimBudgetedCatchUpLogged;
static sb32 sSYNetRollbackResimPending;
/*
 * Armed when FinishForwardResim closes to Live (exclusive frontier). Cleared on the first
 * subsequent live FuncUpdate that actually ran gcRunAll for GetTick before save/commit/advance.
 * Prevents PeerUpdate completing resim mid-FuncUpdate from SavePostTick(target) with
 * post-(target-1) map/fighter state (silent Whispy phase skew).
 * See docs/bugs/netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md.
 *
 * Boundary Finish (soak 1903062639): never close to Live mid-FuncUpdate after live interface
 * already ran — that path used SamePass DEFER + emergency restore (peer-asymmetric) and forked
 * JumpAerial TopN.x. Defer Finish to the next OnLiveFuncUpdateBegin instead. SamePassBlock only
 * arms while Finish is deferred so a stale live_battle_sim_ran cannot clear Await; no restore.
 */
static sb32 sSYNetRollbackAwaitLiveSimAfterResim;
static sb32 sSYNetRollbackAwaitLiveSimSamePassBlock;
static sb32 sSYNetRollbackLiveInterfaceRanThisPass;
static sb32 sSYNetRollbackFinishLiveDeferred;
static sb32 sSYNetRollbackFinishLiveFlushing;
static sb32 sSYNetRollbackResimCompleteLogPending;
static u32 sSYNetRollbackResimCompleteLogMismatch;
static sb32 sSYNetRollbackBeginResimInitialLoad;
static sb32 sSYNetRollbackResimDeeperLoadActive;
static u32 sSYNetRollbackResimStallFrames;
static u32 sSYNetRollbackResimMismatchTick;
static u32 sSYNetRollbackResimTargetTick;
static u32 sSYNetRollbackResimNextTick;
#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Highest sim tick for which a post-load BattleSimOnly completed (GetTick advanced).
 * SavePostTick(T) for T > load requires proven_through >= T — otherwise the ring can
 * store pre-advance load world under the mismatch index (+1 Pupupu phase skew).
 * See docs/bugs/netplay_deepen_resim_map_save_phase_skew_peer_2026-07-28.md.
 */
static u32 sSYNetRollbackResimBattleProvenThrough;
#endif
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
static u8 sSYNetRollbackPendingPeerSymmetricFlags;
static u32 sSYNetRollbackPeerSymmetricLogsRemaining;
static sb32 sSYNetRollbackResimFromPeerSymmetric;
static sb32 sSYNetRollbackSymmetricDiagOnly;
static sb32 sSYNetRollbackResimRngVerifyEnabled;
static sb32 sSYNetRollbackSynctestEnabled;
static u32 sSYNetRollbackSynctestNextProbeTick;
/* After intro Wait skips, keep +1 probe cadence this many times so early Go / first-stick ticks are covered before the normal 120-tick interval. */
static u32 sSYNetRollbackSynctestPostWaitEarlyRemaining;
#define SYNETROLLBACK_SYNCTEST_POST_WAIT_EARLY_PROBES 2U
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
	u32 effect;

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
static u32 sSYNetRollbackPeerBaselineEffect;
static sb32 sSYNetRollbackLastPeerOutcomeEffectValid;
static u32 sSYNetRollbackPeerBaselineSlotFigh;
static u32 sSYNetRollbackPeerBaselineSlotWorld;
static u32 sSYNetRollbackPeerBaselineSlotItem;
static u32 sSYNetRollbackPeerBaselineSlotRng;
static u32 sSYNetRollbackPeerBaselineSlotWeapon;
static u32 sSYNetRollbackPeerBaselineSlotMap;
static u32 sSYNetRollbackPeerBaselineSlotCamera;
static u32 sSYNetRollbackPeerBaselineSlotEffect;
static u32 sSYNetRollbackPeerBaselineFighterSlot[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetRollbackPeerBaselineRetransmitCount;
#define SYNETROLLBACK_BASELINE_UNIVERSE_REPEAT_CAP 12U
static u32 sSYNetRollbackBaselineUniverseRepeatLoad;
static u32 sSYNetRollbackBaselineUniverseRepeatPeerFigh;
static u32 sSYNetRollbackBaselineUniverseRepeatLocalFigh;
static u32 sSYNetRollbackBaselineUniverseRepeatCount;
static u32 sSYNetRollbackResimBaselineDeeperAttempts;
static sb32 sSYNetRollbackPreResimDeeperLoadUsed;
static u32 sSYNetRollbackBaselineTimeoutStreak;
static u32 sSYNetRollbackBaselineTimeoutWindowStartTick;
/*
 * Highest sim tick whose snapshot has been retroactively promoted to load-safe after the
 * predicted remote inputs it was captured under were strict-confirmed and matched published.
 * Prevents EPISODE_LOAD_REWIND load-tick divergence when most live ticks sim under prediction.
 * See docs/bugs/netplay_divergent_load_tick_baseline_stall_2026-07-12.md.
 */
static u32 sSYNetRollbackLoadSafePromotedThrough;
static u32 sSYNetRollbackLoadSafePromoteLogBudget;
/* Peer baseline observed for a different load_tick while we awaited ours (divergent-load stall). */
static u32 sSYNetRollbackPeerBaselineForeignLoadTick;
/*
 * load_tick at which this peer could not reproduce its own saved item hash during resim baseline
 * post-load (figh/world/rng/weapon/map all agreed — item-only self load-fidelity drift, e.g. the
 * Peach's Castle GBumper folding to a resting value on one ISA after a Firefox collision). Signals
 * the baseline gate timeout to prefer a bounded deeper-load resync over a VS_SESSION_END teardown.
 */
static u32 sSYNetRollbackBaselineItemOnlySelfDriftLoadTick = ~(u32)0;
static s32 sSYNetRollbackResimCorrectionPlayer;
static sb32 sSYNetRollbackPeerSnapshotAbort;
static u32 sSYNetRollbackDebounceUntilSim;
static u32 sSYNetRollbackLastCommittedMismatchTick;
static sb32 sSYNetRollbackDeferredMismatchPending;
static u32 sSYNetRollbackDeferredMismatchTick;
static u32 sSYNetRollbackDeferredMismatchTargetTick;
static s32 sSYNetRollbackDeferredMismatchPlayer;
/* Bitmask of remote slots with pending REPLACE in the union deferred span. */
static u32 sSYNetRollbackDeferredMismatchSlotMask;
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
/* Join attempts for FC divergent-load negotiate (not shared with baseline deepen). */
static u32 sSYNetRollbackFcRecoveryJoinAttempts;
#if defined(SSB64_NETMENU)
/* TryBegin pumps spent waiting for light wire-ready to widen past span-1. */
static u32 sSYNetRollbackLightWireCoalesceWaits;
#endif
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
static u32 sSYNetRollbackLastPeerOutcomeFighterSlot[GMCOMMON_PLAYERS_MAX];
static sb32 sSYNetRollbackLastPeerOutcomeFighterSlotsValid;
static u32 sSYNetRollbackLastOutcomeProbeFrontier;
static u32 sSYNetRollbackOutcomeCorrectionLogsRemaining;
static u32 sSYNetRollbackCoalescedScanLogsRemaining;
static u32 sSYNetRollbackFrontierAheadWarnLogsRemaining;
static u32 sSYNetRollbackEpisodeAnchorMismatch;
static u32 sSYNetRollbackEpisodeAnchorLoadTick;
static u32 sSYNetRollbackEpisodeLastTargetTick;
static u32 sSYNetRollbackEpisodeResolvedThrough;
#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Durable exclusive-end coverage: raised with EpisodeResolvedThrough on Commit/close, NOT cleared
 * by ResetCorrectionEpisode (FC deepen). Live ignore / StaleAggregate soft-continue use this so
 * SoftLip cannot hard-kill after deepen clears EpisodeResolvedThrough (soak 2064608876).
 * Begin must NOT refuse behind this frontier — that hung inputs_agree FC recovery when soft-continue
 * had papered a real Hold/physics fork (soak1 session 313936707: mismatch=1264 < healed=1269,
 * recovery_started=0). See docs/bugs/netplay_fc_healed_frontier_honest_fc_2026-07-28.md.
 */
static u32 sSYNetRollbackHealedThrough;
#endif
/* Peer-advertised correction frontier (ROLLBACK_SYNC resolved_through); monotonic max. */
static u32 sSYNetRollbackPeerResolvedThrough;
static u32 sSYNetRollbackEpisodeExtensions;
/* After episode complete: stick REPLACE through this sim tick coalesces into deferred (no ep spam). */
static u32 sSYNetRollbackStickAbsorbUntilSim;
static s32 sSYNetRollbackStickAbsorbPlayer;
#if defined(SSB64_NETMENU)
/* Hard polarity: allow Begin for this slot only when a single slot is dirty. */
static s32 sSYNetRollbackStickAbsorbExemptPlayer;
/* Sim tick when current absorb window was armed (sticky multi-stick refresh cap). */
static u32 sSYNetRollbackStickAbsorbArmedFromSim;
/*
 * Multi-stick mash absorb: window = clamp(phase_lock + K*(pressure-1), …, MAX).
 * pressure = hot_remote_count, or 2 when MultiStickHot (local+1 remote). Scales to N.
 * See docs/bugs/netplay_multistick_correction_union_2026-07-27.md.
 */
#define SYNETROLLBACK_MULTI_STICK_ABSORB_K 4U
#define SYNETROLLBACK_MULTI_STICK_ABSORB_MAX 12U
#define SYNETROLLBACK_MULTI_STICK_ABSORB_CAP 16U
/*
 * Light input episodes: pure GGPO input corrections resim locally — no symmetric notify
 * (no peer_follower mirror), no baseline digest gate, no seal-rows exchange. Peer-coupled
 * episode protocol stays for state recovery (FC diverge / BASELINE_UNIVERSE / peer-sym).
 * Merge-only absorb window while enabled. Scales per-peer O(own mispredictions) for N
 * joysticks. See docs/bugs/netplay_light_input_episodes_2026-07-27.md.
 */
#define SYNETROLLBACK_LIGHT_EPISODE_ABSORB_WINDOW 2U
/*
 * Light episodes that would invent hold_last past the contiguous wire frontier
 * (target > wire_ready exclusive) SoftLip-fork vs the owner's LOCAL stick ramp.
 * Cap / widen deferred + light Begin targets to wire-ready exclusive end.
 * See docs/bugs/netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md.
 */
#define SYNETROLLBACK_LIGHT_WIRE_READY_SCAN_SLACK 16U
/*
 * PEER_SNAPSHOT_DIVERGE fail-closed may be deferred by context waits (resim seal wait,
 * stick absorb) only this many times per load_tick before the abort proceeds anyway.
 * Progress criterion: waits must terminate; they may not re-suppress the exit forever
 * (soak 891751718 @960: seal-wait on host / absorb on guest re-suppressed every peer
 * baseline until session timeout). replay_determinism class never defers at all.
 * See docs/bugs/netplay_close_wire_cap_diverge_hang_exit_2026-07-27.md.
 */
#define SYNETROLLBACK_PEER_DIVERGE_SUPPRESS_BUDGET 8U
static s32 sSYNetRollbackLightInputEpisodesEnvCache = -999;
/* TRUE while the in-flight resim episode is a light (local-only) input episode. */
static sb32 sSYNetRollbackLightEpisodeActive;
#endif
static u32 sSYNetRollbackLastRollbackBeginSimTick;
static u32 sSYNetRollbackSuppressReloadLoadTick;
static u32 sSYNetRollbackSuppressReloadUntilSim;
static sb32 sSYNetRollbackResimAwaitingPeerBaseline;
static sb32 sSYNetRollbackResimBaselineGateOpen;
static sb32 sSYNetRollbackResimBaselineDigestMatched;
static u32 sSYNetRollbackResimBaselineWaitFrames;
static u32 sSYNetRollbackResimSealRowsTimeoutRetries;
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
static u8 sSYNetRollbackDeferredPeerSymmetricFlags;
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

static SYNetRollbackPendingEpisode sSYNetRollbackPendingEpisodeBySlot[MAXCONTROLLERS];
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
static void syNetRollbackPendingEpisodeClearAll(void);
static void syNetRollbackPendingEpisodeClearSlot(s32 slot);
static void syNetRollbackClearDeferredInputMismatch(void);
static void syNetRollbackDeferredMismatchNoteSlot(s32 player);
static void syNetRollbackDeferredMismatchSyncPlayerFromMask(void);
static u32 syNetRollbackDeferredMismatchSlotCount(void);
#if defined(SSB64_NETMENU)
static u32 syNetRollbackMultiStickAbsorbWindow(u32 sim_tick);
static sb32 syNetRollbackLightInputEpisodesEnabled(void);
static u32 syNetRollbackLightWireReadyTargetExclusive(u32 mismatch_tick, s32 player);
static void syNetRollbackAlignTargetToWireReady(u32 mismatch_tick, s32 player, u32 *inout_target,
					       const char *tag);
static void syNetRollbackAlignDeferredTargetToWireReady(const char *tag);
#endif
static void syNetRollbackClearPeerSymmetricRejectLiveCap(void);
static sb32 syNetRollbackFinishForwardResim(void);
static void syNetRollbackEmitResimCompleteAfterFinish(u32 completed_mismatch);
static void syNetRollbackLogResimComplete(void);
static void syNetRollbackClearPeerEpochState(void);
static sb32 syNetRollbackTryRestartResimAtDeeperLoad(u32 deeper_load_tick);
static u32 syNetRollbackResolveDeeperLoadForFidelity(u32 failed_load_tick);
static void syNetRollbackLogEpisodeProofDiverge(u32 load_tick, const char *reason, const char *class_name);

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
	sSYNetRollbackPendingPeerSymmetricFlags = 0U;
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
#if defined(SSB64_NETMENU)
		/*
		 * Overlap-clear assumes the completed resim replayed the deferred span with
		 * authoritative inputs (heavy seal contract). Light episodes replay past the wire
		 * frontier with hold_last guesses; a wire REPLACE that armed deferred mid-resim for
		 * one of those ticks must survive completion or the guess becomes permanent
		 * promote-only input loss (soak 2026-07-27 tick 397: deferred armed @397, cleared
		 * here, ep2 began @400 -> tap-counter fork -> FC diverge @721).
		 *
		 * Follow-on (JumpAerial SoftLip soak 2026-07-27 @740): mid-resim wire armed
		 * deferred@740 then CORRECTION_MERGE_DEEPEN pulled mismatch back to 739; keep
		 * required mismatch>=watermark so 739 was cleared while hold_last@740 stayed
		 * baked. Raise mismatch up to the watermark when the deferred target still
		 * covers predicted-replay ticks.
		 */
		u32 watermark;

		watermark = syNetInputResimLowestPredictedReplayTick();
		if ((watermark != 0U) && (sSYNetRollbackDeferredMismatchTick >= watermark))
		{
			port_log(
			    "SSB64 NetRollback: DEFERRED_KEEP_PRED_SPAN mismatch=%u target=%u watermark=%u (hold_last replay not resolved)\n",
			    sSYNetRollbackDeferredMismatchTick,
			    sSYNetRollbackDeferredMismatchTargetTick,
			    watermark);
			/*
			 * Clamp/widen to contiguous wire so the follow-up light ep does not
			 * invent hold_last ahead of sticks already on the ledger (soak
			 * 2026-07-27: KEEP target=405 with wire only through 401 → SoftLip cascade).
			 */
			syNetRollbackAlignDeferredTargetToWireReady("KEEP_PRED");
		}
		else if ((watermark != 0U) &&
			 (sSYNetRollbackDeferredMismatchTargetTick > watermark) &&
			 (sSYNetRollbackDeferredMismatchTick < watermark))
		{
			port_log(
			    "SSB64 NetRollback: DEFERRED_RAISE_PRED_SPAN mismatch=%u->%u target=%u watermark=%u "
			    "(merge_deepen undercut hold_last tick)\n",
			    sSYNetRollbackDeferredMismatchTick,
			    watermark,
			    sSYNetRollbackDeferredMismatchTargetTick,
			    watermark);
			sSYNetRollbackDeferredMismatchTick = watermark;
			syNetRollbackAlignDeferredTargetToWireReady("RAISE_PRED");
		}
		else
		{
			syNetRollbackClearDeferredInputMismatch();
		}
#else
		syNetRollbackClearDeferredInputMismatch();
#endif
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
		sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
		syNetRollbackClearPeerSymmetricRejectLiveCap();
	}
	{
		s32 ep_slot;

		for (ep_slot = 0; ep_slot < MAXCONTROLLERS; ep_slot++)
		{
			SYNetRollbackPendingEpisode *ep;

			ep = &sSYNetRollbackPendingEpisodeBySlot[ep_slot];
			if ((ep->valid != FALSE) &&
			    (syNetRollbackSpanOverlapsResimPostKey(ep->mismatch_tick, ep->target_tick, key) != FALSE))
			{
				syNetRollbackPendingEpisodeClearSlot(ep_slot);
			}
		}
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
	    "SSB64 NetRollback: RESIM_POST_DIVERGE epoch=%u load=%u mismatch=%u target=%u local figh=0x%08X item=0x%08X rng=0x%08X inp=0x%08X | peer figh=0x%08X item=0x%08X rng=0x%08X inp=0x%08X class=%s\n",
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
	    peer->input_digest,
	    (local->input_digest == peer->input_digest) ? "replay_determinism" : "protocol");
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmOnPostDiverge();
		syNetRollbackClearPeerEpochState();
		if ((local->input_digest == peer->input_digest) && (local->rng == peer->rng) && (local->item == peer->item))
		{
			u32 deeper_load;

			port_log(
			    "SSB64 NetRollback: EPISODE_FSM snapshot_fidelity diverge (matching replay inp) mismatch=%u\n",
			    key->mismatch_tick);
			syNetRbSnapshotMarkLoadUnsafe(key->load_tick);
			deeper_load = syNetRollbackResolveDeeperLoadForFidelity(key->load_tick);
			if ((deeper_load != 0U) && (deeper_load < key->load_tick) &&
			    (syNetRollbackTryRestartResimAtDeeperLoad(deeper_load) != FALSE))
			{
				return;
			}
		}
		if (syNetRollbackTryRestartResimAtDeeperLoad(key->load_tick) != FALSE)
		{
			return;
		}
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM Abort(snapshot_fidelity) mismatch=%u load=%u target=%u (deeper load exhausted)\n",
		    key->mismatch_tick,
		    key->load_tick,
		    key->target_tick);
		syNetRollbackLogEpisodeProofDiverge(
		    key->load_tick,
		    "resim_post_diverge_exhausted",
		    (local->input_digest == peer->input_digest) ? "replay_determinism" : "protocol");
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

		syNetRollbackEpisodeReplayLogCheckInternalDiverge();
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

/* Frame-commit validation cadence is RTT-tiered via syNetSessionParamsGetEffectiveFrameCommitValidationTicks(). */
#define SYNETROLLBACK_BASELINE_GATE_TIMEOUT_FRAMES 10U
#define SYNETROLLBACK_BASELINE_GATE_TIMEOUT_SPAN_CHUNK_FRAMES 4U
#define SYNETROLLBACK_BASELINE_GATE_TIMEOUT_MAX_FRAMES 96U
#define SYNETROLLBACK_SEAL_ROWS_TIMEOUT_MAX_RETRIES 2U
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
/*
 * FC input-agree recovery join budget — independent of BASELINE_DEEPER_MAX.
 * Soak 3849468025: baseline deepen burned attempts=2 before the first foreign
 * baseline arrived, so LOAD_TICK_NEGOTIATE 817→803 forever "deeper exhausted"
 * and never joined. See docs/bugs/netplay_fc_join_budget_light_wire_coalesce_2026-07-27.md.
 */
#define SYNETROLLBACK_FC_RECOVERY_JOIN_MAX_ATTEMPTS 4U
/*
 * Light episodes: when wire-ready exclusive end is only mismatch+1 (span-1), defer
 * Begin up to this many TryBegin pumps so contiguous wire can widen the burst.
 * Live-cap at mismatch-1 stays armed (unlike stick_absorb_coalesce which lifts it).
 */
#define SYNETROLLBACK_LIGHT_WIRE_COALESCE_MAX_WAITS 4U
#define SYNETROLLBACK_LOAD_TICK_REWIND_MAX 16U
#define SYNETROLLBACK_ROLLBACK_COOLDOWN_FRAMES_DEFAULT 2U

#define SYNETROLLBACK_DEBOUNCE_FRAMES_DEFAULT 3U
#define SYNETROLLBACK_LOAD_HASH_SOFT_ROLLBACK_THRESHOLD 8U
#define SYNETROLLBACK_RESIM_MAX_BURST_TICKS_DEFAULT 24U
#define SYNETROLLBACK_RESIM_MAX_BURST_TICKS_CAP     64U

#if defined(SSB64_NETMENU)
static void syNetRollbackStickAbsorbMaybeStickyRefresh(void);
static void syNetRollbackStickAbsorbClampDeferredTarget(void);
static u32 syNetRollbackStickAbsorbDeferredTargetCap(void);
#endif
static sb32 syNetRollbackLightEpisodeIsActive(void);
static sb32 syNetRollbackBeginResim(u32 mismatch_tick, u32 target_tick, s32 correction_player);
static u32 syNetRollbackComputeResimTickLimit(void);
static void syNetRollbackAdvanceResimBudgetEx(u32 max_ticks_this_call);
static void syNetRollbackAdvanceResimBudget(void);
static u32 syNetRollbackClampResimTargetTick(u32 mismatch_tick, u32 target_tick);
static u32 syNetRollbackClampResimTargetTickEx(u32 mismatch_tick, u32 target_tick, u32 frontier, sb32 wire_locked);
static u32 syNetRollbackClampResimTargetTickAuthoritative(u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackEpisodeAuthorityEnabled(void);
static void syNetRollbackPendingEpisodeClearAll(void);
static void syNetRollbackPendingEpisodeClearSlot(s32 slot);
static sb32 syNetRollbackPendingEpisodeCopyValid(s32 slot, SYNetRollbackPendingEpisode *out);
static u32 syNetRollbackPendingEpisodeMaxTargetTick(void);
static void syNetRollbackPendingEpisodeSet(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick, u32 epoch_id,
					   u8 flags);
static s32 syNetRollbackResolveRemoteHumanPlayer(s32 preferred_slot);
static sb32 syNetRollbackRemoteHumanHasPredictedPublishedInSpan(u32 from_tick, u32 to_tick);
static void syNetRollbackMaybeLogEpisodeExec(sb32 from_peer_notify, sb32 light);
static void syNetRollbackLogEpisodeProof(const char *event, u32 epoch_id, u32 mismatch_tick, u32 load_tick,
					 u32 target_tick, const char *source, const char *reason,
					 const char *class_name);
static void syNetRollbackLogEpisodeProofBegin(s32 correction_player, u32 mismatch_tick, u32 load_tick, u32 target_tick,
					      u32 epoch_id);
static void syNetRollbackLogEpisodeProofDiverge(u32 load_tick, const char *reason, const char *class_name);
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
static sb32 syNetRollbackTryCancelResimBehindSharedFrontier(void);
static sb32 syNetRollbackTryRestartResimAtDeeperLoad(u32 deeper_load_tick);
static sb32 syNetRollbackTryAlignActiveEpisodeTuple(s32 slot, u32 load_tick, u32 mismatch_tick, u32 target_tick,
						    sb32 follower_local_auth);
static sb32 syNetRollbackPeerSymmetricNotifyIsStaleShallow(u32 notify_mismatch, u32 notify_load);
static void syNetRollbackClearStaleShallowPeerSymmetricNotify(u32 settled_mismatch_tick);
static sb32 syNetRollbackLoadPostTick(u32 tick);
static sb32 syNetRollbackTryDeeperLoadBeforeResim(u32 *io_load_tick, u32 *io_mismatch_tick);
static void syNetRollbackTryOpenResimBaselineGateFromPeerDigest(u32 load_tick, const SYNetRollbackHashSet *peer,
							       const u32 *peer_fighter_slot);
/* Early peer baseline (preemptive) arrived before AwaitingBaseline — open gate from LastPeerOutcome. */
static void syNetRollbackTryOpenBaselineGateFromStashedPeerOutcome(void);
static void syNetRollbackTryOpenResimReplayGateAfterAnimResync(u32 load_tick, const SYNetRollbackHashSet *peer);
static sb32 syNetRollbackTryEchoBaselineResponse(u32 load_tick);
static sb32 syNetRollbackBaselineEchoAllowed(u32 load_tick);
static sb32 syNetRollbackTryHashOnlyBaselineEcho(u32 load_tick);
static void syNetRollbackQueuePeerSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick, u8 notify_flags);
static void syNetRollbackResetPeerBaselineResyncStorm(void);
static sb32 syNetRollbackPeerBaselineResyncStormLimitReached(u32 load_tick);
static void syNetRollbackOnPeerBaselineResyncStormLimit(u32 load_tick);
static u32 syNetRollbackClampLoadTickForPeerSend(u32 load_tick);
static void syNetRollbackAlignResimLoadTickToWireBaseline(void);
static u32 syNetRollbackGetPeerDivergeResyncTicks(void);
static u32 syNetRollbackReanchorMismatchTick(u32 mismatch_tick, u32 frontier);
static u32 syNetRollbackLoadTickMinBound(u32 sim_tick);
static u32 syNetRollbackLoadTickRingMinBound(u32 sim_tick);
static u32 syNetRollbackResolveStateMismatchLoadTick(u32 validation_tick, u32 min_load,
						     sb32 apply_episode_floor);
static u32 syNetRollbackFindEarliestLoadSafeTickInSpan(u32 min_tick, u32 max_tick);
static u32 syNetRollbackPickInRingFcInputAgreeLoad(u32 validation_tick, u32 min_load, u32 *out_reason_code);
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
static sb32 syNetRollbackPeerSymmetricFcJoinKeepOnset(u32 mismatch_tick, u32 target_tick, u8 notify_flags);
static sb32 syNetRollbackPeerSymmetricSuppressedByFcStateRecovery(u32 mismatch_tick, u32 target_tick);
static sb32 syNetRollbackLocalEpisodeConflictsWithPeerNotify(u32 peer_mismatch, u32 peer_target);
static void syNetRollbackAbortInFlightResimForPeerEpisode(void);
static u32 syNetRollbackComputeAuthoritativeFcTarget(u32 mismatch_tick, u32 validation_tick);
static sb32 syNetRollbackUniverseMismatchPreferStateRecovery(u32 load_tick);
static sb32 syNetRollbackUniverseMismatchInputPoisonedAtLoad(u32 load_tick, u32 *out_mismatch, s32 *out_player);
static void syNetRollbackComparePeerBaselineToLocal(u32 load_tick, const SYNetRollbackHashSet *peer,
						    const u32 *peer_fighter_slot);
static void syNetRollbackPumpBaselineEchoRetry(void);
static sb32 syNetRollbackBaselineCompareQuiesced(void);
static sb32 syNetRollbackPeerSymmetricFlushQuiesced(void);
static void syNetRollbackFlushDeferredPeerSymmetric(void);
static sb32 syNetRollbackTryBeginResimFromPendingPeerSymmetric(u32 frontier, u32 scan_mismatch);
static sb32 syNetRollbackSnapshotReadyForBaselineCompare(u32 load_tick);
static sb32 syNetRollbackResolveLoadTickForSnapshot(u32 *io_load_tick, u32 *io_mismatch_tick);
static void syNetRollbackApplyLoadAnchorFragileWalkback(u32 *io_load_tick, u32 *io_mismatch_tick);
static sb32 syNetRollbackTryLoadPostTickWithFidelityWalkback(u32 *io_load_tick, u32 *io_mismatch_tick);
static sb32 syNetRollbackMaybeResimAnchorProbe(u32 load_tick);
#if defined(SSB64_NETMENU)
static u32 syNetRollbackAnchorProbeEpisodeMismatchTick(void);
#endif
static sb32 syNetRollbackAnchorProbeTryWalkbackLoad(u32 before_load, u32 min_load, u32 *io_load_tick,
						    u32 *io_mismatch_tick);
static sb32 syNetRollbackPeerBaselineDriftIsAnimOnly(const SYNetRollbackHashSet *peer,
						     const SYNetRollbackHashSet *local);
static sb32 syNetRollbackPeerBaselineGameplayDigestsMatch(const SYNetRollbackHashSet *peer,
							    const SYNetRollbackHashSet *local);
static sb32 syNetRollbackPeerBaselineDriftIsCameraOnlyCosmetic(const SYNetRollbackHashSet *peer,
							       const SYNetRollbackHashSet *local);
static sb32 syNetRollbackPeerBaselineDriftIsStaleAggregateFighOnly(u32 load_tick, const SYNetRollbackHashSet *peer,
								   const SYNetRollbackHashSet *local,
								   const u32 *peer_fighter_slot);
static sb32 syNetRollbackCollectBaselineCompareLocal(u32 load_tick, SYNetRollbackHashSet *out);
static void syNetRollbackAlignArmedBaselineCameraFromPeerWire(u32 load_tick, u32 peer_camera);
#if defined(SSB64_NETMENU)
static void syNetRollbackAlignArmedBaselineWeaponFromPeerWire(u32 load_tick, u32 peer_weapon);
static sb32 syNetRollbackPeerBaselineDriftIsWeaponOnlyVsArmed(const SYNetRollbackHashSet *peer);
static void syNetRollbackTryOpenResimReplayGateAfterPkHoldWeaponOnlyAbsorb(u32 load_tick,
									    const SYNetRollbackHashSet *peer,
									    const u32 *peer_fighter_slot);
#endif
static sb32 syNetRollbackPeerBaselineWireDigestsMatchArmed(const SYNetRollbackHashSet *peer);
static sb32 syNetRollbackPeerBaselineWireGameplayMatchArmed(const SYNetRollbackHashSet *peer);
static sb32 syNetRollbackPeerBaselineSlotSubsystemOk(const SYNetRollbackHashSet *peer);
static sb32 syNetRollbackPeerBaselineSlotGameplaySubsystemOk(const SYNetRollbackHashSet *peer);
static sb32 syNetRollbackCollectRingBaselineAtTick(u32 load_tick, SYNetRollbackHashSet *out);
static void syNetRollbackClearBaselineResimNegotiationFlags(void);
static void syNetRollbackTryOpenResimReplayGateAfterSlotRingResync(u32 load_tick, const SYNetRollbackHashSet *peer,
								   const u32 *peer_fighter_slot);
static void syNetRollbackTryOpenResimReplayGateAfterCameraRingResync(u32 load_tick, const SYNetRollbackHashSet *peer,
								     const u32 *peer_fighter_slot);
static sb32 syNetRollbackPeerBaselineDriftIsGameplayOnlyMap(const SYNetRollbackHashSet *peer,
							    const SYNetRollbackHashSet *local);
static sb32 syNetRollbackPeerBaselineHardGameplayDiverge(const SYNetRollbackHashSet *peer,
							  const SYNetRollbackHashSet *local);
static sb32 syNetRollbackTryFailClosedAfterStateDeepenExhaust(u32 failed_load_tick);
static void syNetRollbackFillArmedBaselineHashSet(SYNetRollbackHashSet *out);
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
				      sb32 initiator, sb32 from_peer_notify, sb32 light);
static void syNetRollbackEpisodeSetPhase(SYNetRollbackEpisodePhase phase);
static void syNetRollbackQueueDeferredInputCorrection(s32 player, u32 sim_tick);
typedef enum SYNetRollbackCorrectionMismatchSource
{
	nSYNetRollbackCorrectionSourceWire,
	nSYNetRollbackCorrectionSourceTimelinePlayer,
	nSYNetRollbackCorrectionSourceTimelineGlobal,
	nSYNetRollbackCorrectionSourceScan
} SYNetRollbackCorrectionMismatchSource;

static sb32 syNetRollbackComputeInputCorrectionTuple(s32 hint_player, u32 hint_tick, s32 *out_player, u32 *out_mismatch,
						     u32 *out_load, u32 *out_target,
						     SYNetRollbackCorrectionMismatchSource *out_source);
static void syNetRollbackLogCorrectionTupleIfEnabled(s32 hint_player, u32 hint_tick, s32 player, u32 mismatch,
						     u32 load_hint, u32 target,
						     SYNetRollbackCorrectionMismatchSource source);
static void syNetRollbackQueueDeferredInputCorrectionEx(s32 player, u32 sim_tick, u32 target_tick_override);
static void syNetRollbackArmSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick, sb32 follower_local_auth);
static void syNetRollbackArmSymmetricNotifyEx(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick,
					      u32 epoch_id, sb32 follower_local_auth);
static void syNetRollbackClearSymmetricNotifyAll(void);
static void syNetRollbackConsumePendingForceMismatchAfterResim(u32 mismatch_tick, u32 completed_target);
static void syNetRollbackOnResimCompleted(void);
static void syNetRollbackArmPredictionRecovery(u32 mismatch_tick, u32 frontier_tick, const SYNetInputFrame *hist);
static void syNetRollbackTryOutcomeAwareCorrection(u32 probe_frontier);
static u32 syNetRollbackFindEarliestPredictedRemoteTick(u32 from_tick, u32 to_tick, s32 *out_player);
static void syNetRollbackCoalesceScanResimSpan(u32 *io_mismatch, u32 *io_target, u32 frontier);
static void syNetRollbackResetCorrectionEpisode(void);
static void syNetRollbackArmBattleSimHoldAfterLoadFail(u32 load_tick);
static void syNetRollbackStopVsSessionForLoadFail(u32 load_tick, const char *reason);
static void syNetRollbackRequestLoadFailBattleExit(void);
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
	sSYNetRollbackVerifyEffectHash = FALSE;
	{
		const char *env_ef;

		env_ef = getenv("SSB64_NETPLAY_ROLLBACK_VERIFY_EFFECT_HASH");
		if ((env_ef != NULL) && (env_ef[0] != '\0') && (atoi(env_ef) != 0))
		{
			sSYNetRollbackVerifyEffectHash = TRUE;
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
	sSYNetRollbackBattleSimHoldAfterLoadFail = FALSE;
	sSYNetRollbackLoadFailBattleExitPending = FALSE;
	sSYNetRollbackLoadFailSceneRetargeted = FALSE;
	sSYNetRollbackBattleSimHoldLoadTick = 0U;
	sSYNetRollbackLiveWedgeFrontierTick = 0U;
	sSYNetRollbackLiveWedgeFrames = 0U;
	sSYNetRollbackBattleSimHoldFrames = 0U;
	sSYNetRollbackBattleSimHoldTeardownSent = FALSE;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackForceMismatchPlayerSlot = -1;
	sSYNetRollbackResimTicksPerFrame = 12U;
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
	sSYNetRollbackResimMaxBurstTicks = SYNETROLLBACK_RESIM_MAX_BURST_TICKS_DEFAULT;
	{
		const char *env_burst;

		env_burst = getenv("SSB64_NETPLAY_ROLLBACK_MAX_BURST_TICKS");
		if ((env_burst != NULL) && (env_burst[0] != '\0'))
		{
			s32 v = atoi(env_burst);

			if (v >= 0)
			{
				sSYNetRollbackResimMaxBurstTicks = (u32)v;
			}
		}
		if (sSYNetRollbackResimMaxBurstTicks > SYNETROLLBACK_RESIM_MAX_BURST_TICKS_CAP)
		{
			sSYNetRollbackResimMaxBurstTicks = SYNETROLLBACK_RESIM_MAX_BURST_TICKS_CAP;
		}
	}
	sSYNetRollbackResimBudgetedCatchUpLogged = FALSE;
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimMismatchTick = ~(u32)0;
	sSYNetRollbackResimTargetTick = ~(u32)0;
	sSYNetRollbackResimNextTick = ~(u32)0;
#if defined(PORT) && defined(SSB64_NETMENU)
	sSYNetRollbackResimBattleProvenThrough = 0U;
#endif
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
	sSYNetRollbackSynctestPostWaitEarlyRemaining = 0U;
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
	syNetRollbackPendingEpisodeClearAll();
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
	sSYNetRollbackPendingPeerSymmetricFlags = 0U;
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
	sSYNetRollbackDeferredMismatchSlotMask = 0U;
	sSYNetRollbackDeferredMismatchFromPeerSymmetric = FALSE;
	sSYNetRollbackPeerSymmetricRejectLiveCap = ~(u32)0;
	sSYNetRollbackStateHashRollback = TRUE;
	sSYNetRollbackStateHashLogsRemaining = 16U;
	sSYNetRollbackFcStateRecoverySuppressLogsRemaining = 8U;
	sSYNetRollbackGgpoCorrectionLogsRemaining = 32U;
	sSYNetRollbackLastPeerOutcomeValid = FALSE;
	sSYNetRollbackLastPeerOutcomeTick = ~(u32)0;
	sSYNetRollbackLastPeerOutcomeFighterSlotsValid = FALSE;
	sSYNetRollbackLastOutcomeProbeFrontier = ~(u32)0;
	sSYNetRollbackOutcomeCorrectionLogsRemaining = 16U;
	sSYNetRollbackCoalescedScanLogsRemaining = 16U;
	syNetRollbackResetCorrectionEpisode();
#if defined(PORT) && defined(SSB64_NETMENU)
	sSYNetRollbackHealedThrough = 0U;
#endif
	sSYNetRollbackPeerResolvedThrough = 0U;
	sSYNetRollbackStickAbsorbUntilSim = 0U;
	sSYNetRollbackStickAbsorbPlayer = -1;
#if defined(SSB64_NETMENU)
	sSYNetRollbackStickAbsorbExemptPlayer = -1;
	sSYNetRollbackStickAbsorbArmedFromSim = 0U;
	sSYNetRollbackLightEpisodeActive = FALSE;
#endif
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
	sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
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

sb32 syNetRollbackLoadHashVerifyEnabled(void)
{
#ifdef PORT
	return sSYNetRollbackLoadHashVerify;
#else
	return FALSE;
#endif
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

sb32 syNetRollbackResimGateCatchUpAllowed(void)
{
#ifdef PORT
#if defined(SSB64_NETMENU)
	if (sSYNetRollbackBeginResimInitialLoad != FALSE)
	{
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return (sSYNetRollbackResimBaselineGateOpen != FALSE) ? TRUE : FALSE;
	}
#endif
#endif
	return TRUE;
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
#if defined(PORT) && defined(SSB64_NETMENU)
	syNetplayStatusVarsBankResetSession();
#endif
	sSYNetRollbackSessionActive = TRUE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackInjectConsumed = FALSE;
	sSYNetRollbackDiagnosticResimConsumed = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackLoadFailCount = 0;
	sSYNetRollbackBattleSimHoldAfterLoadFail = FALSE;
	sSYNetRollbackLoadFailBattleExitPending = FALSE;
	sSYNetRollbackLoadFailSceneRetargeted = FALSE;
	sSYNetRollbackBattleSimHoldLoadTick = 0U;
	sSYNetRollbackLiveWedgeFrontierTick = 0U;
	sSYNetRollbackLiveWedgeFrames = 0U;
	sSYNetRollbackBattleSimHoldFrames = 0U;
	sSYNetRollbackBattleSimHoldTeardownSent = FALSE;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackAwaitLiveSimAfterResim = FALSE;
	sSYNetRollbackAwaitLiveSimSamePassBlock = FALSE;
	sSYNetRollbackLiveInterfaceRanThisPass = FALSE;
	sSYNetRollbackFinishLiveDeferred = FALSE;
	sSYNetRollbackFinishLiveFlushing = FALSE;
	sSYNetRollbackResimCompleteLogPending = FALSE;
	sSYNetRollbackResimCompleteLogMismatch = 0U;
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
	syNetRollbackPendingEpisodeClearAll();
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
	sSYNetRollbackPendingPeerSymmetricFlags = 0U;
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
	sSYNetRollbackDeferredMismatchSlotMask = 0U;
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
	sSYNetRollbackPreResimDeeperLoadUsed = FALSE;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackBaselineTimeoutWindowStartTick = 0U;
	sSYNetRollbackBaselineItemOnlySelfDriftLoadTick = ~(u32)0;
	sSYNetRollbackLoadSafePromotedThrough = 0U;
	sSYNetRollbackLoadSafePromoteLogBudget = 12U;
	sSYNetRollbackPeerBaselineForeignLoadTick = ~(u32)0;
	sSYNetRollbackFcRecoveryJoinAttempts = 0U;
#if defined(SSB64_NETMENU)
	sSYNetRollbackLightWireCoalesceWaits = 0U;
#endif
	sSYNetRollbackResimCorrectionPlayer = -1;
	sSYNetRollbackLastPeerOutcomeValid = FALSE;
	sSYNetRollbackLastPeerOutcomeTick = ~(u32)0;
	sSYNetRollbackLastPeerOutcomeFighterSlotsValid = FALSE;
	sSYNetRollbackLastOutcomeProbeFrontier = ~(u32)0;
	syNetRollbackResetCorrectionEpisode();
#if defined(PORT) && defined(SSB64_NETMENU)
	sSYNetRollbackHealedThrough = 0U;
#endif
	sSYNetRollbackPeerResolvedThrough = 0U;
	sSYNetRollbackStickAbsorbUntilSim = 0U;
	sSYNetRollbackStickAbsorbPlayer = -1;
#if defined(SSB64_NETMENU)
	sSYNetRollbackStickAbsorbExemptPlayer = -1;
	sSYNetRollbackStickAbsorbArmedFromSim = 0U;
	sSYNetRollbackLightEpisodeActive = FALSE;
#endif
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
	sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
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
	syNetRbeSchedNoteSessionStop(); /* emit final rbe shadow scorecard */
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
#ifdef PORT
	/* BATTLE_SIM_HOLD survives StopVSSession until scene exit / new VS start — do not clear here. */
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackAwaitLiveSimAfterResim = FALSE;
	sSYNetRollbackAwaitLiveSimSamePassBlock = FALSE;
	sSYNetRollbackLiveInterfaceRanThisPass = FALSE;
	sSYNetRollbackFinishLiveDeferred = FALSE;
	sSYNetRollbackFinishLiveFlushing = FALSE;
	sSYNetRollbackResimCompleteLogPending = FALSE;
	sSYNetRollbackResimCompleteLogMismatch = 0U;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackForceIdentityPending = FALSE;
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	memset(sSYNetRollbackSymmetricNotifyTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTick));
	memset(sSYNetRollbackSymmetricNotifyTargetTick, 0, sizeof(sSYNetRollbackSymmetricNotifyTargetTick));
	memset(sSYNetRollbackSymmetricNotifyLoadTick, 0, sizeof(sSYNetRollbackSymmetricNotifyLoadTick));
	memset(sSYNetRollbackSymmetricNotifyEpochId, 0, sizeof(sSYNetRollbackSymmetricNotifyEpochId));
	memset(sSYNetRollbackSymmetricNotifySendCount, 0, sizeof(sSYNetRollbackSymmetricNotifySendCount));
	memset(sSYNetRollbackSymmetricNotifyFlags, 0, sizeof(sSYNetRollbackSymmetricNotifyFlags));
	syNetRollbackPendingEpisodeClearAll();
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
	sSYNetRollbackPendingPeerSymmetricFlags = 0U;
	sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
	sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackDeferredPeerSymmetricSlot = -1;
	sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
	sSYNetRollbackDeferredPeerBaselineComparePending = FALSE;
	sSYNetRollbackDeferredPeerBaselineCompareLoadTick = ~(u32)0;
	syNetRollbackClearPeerEpochState();
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackPeerBaselineRetransmitCount = 0U;
	sSYNetRollbackResimBaselineDeeperAttempts = 0U;
	sSYNetRollbackPreResimDeeperLoadUsed = FALSE;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackLastBaselineEchoLoadTick = ~(u32)0;
	sSYNetRollbackLastBaselineEchoSimTick = 0U;
	sSYNetRollbackPeerBaselineResyncSteps = 0U;
	sSYNetRollbackPeerBaselineResyncOriginMismatch = ~(u32)0;
	sSYNetRollbackPeerBaselineResyncStormActive = FALSE;
	sSYNetRollbackBaselineItemOnlySelfDriftLoadTick = ~(u32)0;
	sSYNetRollbackLoadSafePromotedThrough = 0U;
	sSYNetRollbackLoadSafePromoteLogBudget = 12U;
	sSYNetRollbackPeerBaselineForeignLoadTick = ~(u32)0;
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
 * Fighter/world/RNG/item/wpn/map/eff are authoritative for sim; anim/cam are visual and rebuild on resim forward.
 */
static sb32 syNetRollbackLoadHashSimCoreMatchesSlot(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp,
						    u32 live_m, u32 live_r)
{
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackVerifyLoadHashMatchesSlot(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp,
						     u32 live_m, u32 live_r, u32 live_c, u32 live_a, u32 live_ef)
{
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)) ||
	    (live_c != syNetRbSnapshotGetSlotHashCamera(tick)) || (live_a != syNetRbSnapshotGetSlotHashAnimation(tick)))
	{
		return FALSE;
	}
	if ((sSYNetRollbackVerifyEffectHash != FALSE) &&
	    (live_ef != syNetRbSnapshotGetSlotHashEffect(tick)))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackTryRecoverEffectHashDrift(u32 tick, u32 live_ef)
{
	if ((sSYNetRollbackVerifyEffectHash == FALSE) || (live_ef == syNetRbSnapshotGetSlotHashEffect(tick)))
	{
		return FALSE;
	}
	if (syNetRbSnapshotTryRepairEffectHashForVerify(tick) == FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_HASH_DRIFT effect-repair ok tick=%u eff=0x%08X/0x%08X\n",
	    tick,
	    syNetRbSnapshotGetSlotHashEffect(tick),
	    syNetSyncHashActiveEffectsForRollback());
	return TRUE;
}

static sb32 syNetRollbackTryRecoverWeaponHashDrift(u32 tick, u32 live_wp)
{
	if (live_wp == syNetRbSnapshotGetSlotHashWeapon(tick))
	{
		return FALSE;
	}
	if (syNetRbSnapshotTryRepairWeaponHashForVerify(tick) == FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_HASH_DRIFT weapon-repair ok tick=%u wpn=0x%08X/0x%08X\n",
	    tick,
	    syNetRbSnapshotGetSlotHashWeapon(tick),
	    syNetSyncHashActiveWeaponsForRollback());
	return TRUE;
}

static sb32 syNetRollbackLoadHashDriftTrySoftContinue(u32 tick, u32 live_f, u32 live_m)
{
	if (syNetRollbackLoadHashDriftIsSoft() == FALSE)
	{
		return FALSE;
	}
	if (live_m != syNetRbSnapshotGetSlotHashMap(tick))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-continue blocked tick=%u reason=map_mismatch slot=0x%08X live=0x%08X\n",
		    tick,
		    syNetRbSnapshotGetSlotHashMap(tick),
		    live_m);
		return FALSE;
	}
	if (live_f != syNetRbSnapshotGetSlotHashFighter(tick))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-continue blocked tick=%u reason=fighter_mismatch slot=0x%08X live=0x%08X\n",
		    tick,
		    syNetRbSnapshotGetSlotHashFighter(tick),
		    live_f);
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-continue tick=%u rollbacks=%u (set "
	    "SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=0 to hard-abort; map drift never soft-continues)\n",
	    tick,
	    sSYNetRollbackRollbackCount);
	return TRUE;
}

static sb32 syNetRollbackLoadHashDriftIsPresentationalOnly(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp,
							    u32 live_m, u32 live_r, u32 live_c, u32 live_a,
							    u32 live_ef)
{
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)))
	{
		return FALSE;
	}
	if ((sSYNetRollbackVerifyEffectHash != FALSE) &&
	    (live_ef != syNetRbSnapshotGetSlotHashEffect(tick)))
	{
		return FALSE;
	}
	if ((live_a == syNetRbSnapshotGetSlotHashAnimation(tick)) &&
	    (live_c == syNetRbSnapshotGetSlotHashCamera(tick)))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackLoadHashDriftIsResimLoadContext(void)
{
	if (sSYNetRollbackResimPending != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackBeginResimInitialLoad != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		return TRUE;
	}
	if (sSYNetRollbackExecutingEpisode.valid != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

#if defined(SSB64_NETMENU)
/*
 * FC recovery load verify: aggregate ring figh can disagree with live while per-slot
 * fhash_light (ring recipe) still matches — joint/anim presentation finalize drift.
 */
static sb32 syNetRollbackFcRecoveryFighDriftOk(u32 tick, u32 live_f)
{
	u32 slot_f;
	u32 live_light;
	u32 slot_light;

	if (sSYNetRollbackFcStateRecoveryActive == FALSE)
	{
		return FALSE;
	}
	slot_f = syNetRbSnapshotGetSlotHashFighter(tick);
	if (live_f == slot_f)
	{
		return TRUE;
	}
	live_light = syNetRbSnapshotHashFightersLightFromLive();
	slot_light = syNetRbSnapshotGetSlotHashFighterLight(tick);
	return (live_light == slot_light) ? TRUE : FALSE;
}

/*
 * Load/synctest verify: aggregate ring figh can disagree with live while every per-player
 * light/full/anim slot hash still matches (egg-lay apply canonicalize + stale mid-fill ring fold).
 */
static sb32 syNetRollbackLoadVerifyPerSlotFighDriftOk(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp,
						      u32 live_m, u32 live_r, u32 live_a)
{
	if (live_f == syNetRbSnapshotGetSlotHashFighter(tick))
	{
		return FALSE;
	}
	if ((live_w != syNetRbSnapshotGetSlotHashWorld(tick)) || (live_i != syNetRbSnapshotGetSlotHashItem(tick)) ||
	    (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) || (live_m != syNetRbSnapshotGetSlotHashMap(tick)) ||
	    (live_r != syNetRbSnapshotGetSlotHashRng(tick)) ||
	    (live_a != syNetRbSnapshotGetSlotHashAnimation(tick)))
	{
		return FALSE;
	}
	return syNetRbSnapshotAllFighterSlotHashesMatchAtTick(tick);
}

/*
 * FC recovery: ring may retain weapon blobs from a poisoned local forward-sim span while live load
 * correctly has none (peer-empty @480 soak3). Allow resim load when sim-critical partitions match.
 */
static sb32 syNetRollbackFcRecoveryWpnOnlyDriftOk(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp,
						  u32 live_m, u32 live_r)
{
	if (sSYNetRollbackFcStateRecoveryActive == FALSE)
	{
		return FALSE;
	}
	if (live_wp == syNetRbSnapshotGetSlotHashWeapon(tick))
	{
		return FALSE;
	}
	if ((live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_m != syNetRbSnapshotGetSlotHashMap(tick)) ||
	    (live_r != syNetRbSnapshotGetSlotHashRng(tick)))
	{
		return FALSE;
	}
	if (syNetRollbackFcRecoveryFighDriftOk(tick, live_f) == FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_HASH_DRIFT fc_recovery wpn-only ok tick=%u wpn=0x%08X/0x%08X (stale ring weapons)\n",
	    tick,
	    syNetRbSnapshotGetSlotHashWeapon(tick),
	    live_wp);
	return TRUE;
}
#endif

static sb32 syNetRollbackResimSimCoreRejectDiagEnabled(void)
{
	static int s_cached = -999;
	const char *e;

	if (s_cached != -999)
	{
		return (s_cached != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_RESIM_SIM_CORE_DIAG");
	s_cached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_cached != 0) ? TRUE : FALSE;
}

/*
 * During resim load verify: world/item/wpn/map/rng restored correctly but presentation finalize
 * (figatree/joint anim/effects) can drift while sim-critical blobs match — allow resim forward.
 */
static sb32 syNetRollbackLoadHashDriftIsResimSimCoreOk(u32 tick, u32 live_f, u32 live_w, u32 live_i, u32 live_wp,
						       u32 live_m, u32 live_r)
{
	if (syNetRollbackLoadHashDriftIsResimLoadContext() == FALSE)
	{
		if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: resim-sim-core-reject tick=%u reason=not_in_resim resim_pending=%d begin_initial_load=%d fc_recovery=%d episode_valid=%d\n",
			    tick,
			    (int)sSYNetRollbackResimPending,
			    (int)sSYNetRollbackBeginResimInitialLoad,
			    (int)sSYNetRollbackFcStateRecoveryActive,
			    (int)sSYNetRollbackExecutingEpisode.valid);
		}
		return FALSE;
	}
	if (live_w != syNetRbSnapshotGetSlotHashWorld(tick))
	{
		if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRollback: resim-sim-core-reject tick=%u reason=world slot=0x%08X live=0x%08X\n",
			         tick,
			         syNetRbSnapshotGetSlotHashWorld(tick),
			         live_w);
		}
		return FALSE;
	}
	if (live_i != syNetRbSnapshotGetSlotHashItem(tick))
	{
		if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRollback: resim-sim-core-reject tick=%u reason=item slot=0x%08X live=0x%08X\n",
			         tick,
			         syNetRbSnapshotGetSlotHashItem(tick),
			         live_i);
		}
		return FALSE;
	}
	if (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick))
	{
		if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRollback: resim-sim-core-reject tick=%u reason=weapon slot=0x%08X live=0x%08X\n",
			         tick,
			         syNetRbSnapshotGetSlotHashWeapon(tick),
			         live_wp);
		}
		return FALSE;
	}
	if (live_m != syNetRbSnapshotGetSlotHashMap(tick))
	{
		if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRollback: resim-sim-core-reject tick=%u reason=map slot=0x%08X live=0x%08X\n",
			         tick,
			         syNetRbSnapshotGetSlotHashMap(tick),
			         live_m);
		}
		return FALSE;
	}
	if (live_r != syNetRbSnapshotGetSlotHashRng(tick))
	{
		if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRollback: resim-sim-core-reject tick=%u reason=rng slot=0x%08X live=0x%08X\n",
			         tick,
			         syNetRbSnapshotGetSlotHashRng(tick),
			         live_r);
		}
		return FALSE;
	}
	/*
	 * FC recovery, initial anchor walkback, and baseline deeper restart must not forward-sim
	 * poisoned fighter state (intro status transitions can slot≠live figh while world/item/rng match).
	 */
	if (((sSYNetRollbackFcStateRecoveryActive != FALSE) ||
	     (sSYNetRollbackBeginResimInitialLoad != FALSE) ||
	     (sSYNetRollbackResimDeeperLoadActive != FALSE)) &&
	    (live_f != syNetRbSnapshotGetSlotHashFighter(tick)))
	{
#if defined(SSB64_NETMENU)
		if (syNetRollbackFcRecoveryFighDriftOk(tick, live_f) != FALSE)
		{
			/* fhash_light ring recipe agrees — allow FC recovery resim despite aggregate figh drift. */
		}
		else
#endif
		{
			if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
			{
				const char *reason;

				if (sSYNetRollbackFcStateRecoveryActive != FALSE)
				{
					reason = "fc_recovery";
				}
				else if (sSYNetRollbackBeginResimInitialLoad != FALSE)
				{
					reason = "begin_initial_load";
				}
				else
				{
					reason = "deeper_load";
				}
				port_log(
				    "SSB64 NetRollback: resim-sim-core-reject tick=%u reason=figh %s slot=0x%08X live=0x%08X\n",
				    tick,
				    reason,
				    syNetRbSnapshotGetSlotHashFighter(tick),
				    live_f);
			}
			return FALSE;
		}
	}
	if (live_f != syNetRbSnapshotGetSlotHashFighter(tick))
	{
		if (syNetRbSnapshotAnyLiveFighterInIntroLoadFidelityScope() != FALSE)
		{
			if (syNetRollbackResimSimCoreRejectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: resim-sim-core-reject tick=%u reason=figh intro_fidelity_scope slot=0x%08X live=0x%08X\n",
				    tick,
				    syNetRbSnapshotGetSlotHashFighter(tick),
				    live_f);
			}
			return FALSE;
		}
	}
	(void)live_f;
	return TRUE;
}

static sb32 syNetRollbackVerifyResimReplayLoadSafe(u32 load_tick)
{
	u32 live_f;

	if (syNetRbSnapshotVerifyLiveFightersSanity(load_tick, "replay_gate") == FALSE)
	{
		return FALSE;
	}
	if (syNetRbSnapshotVerifyAppearPresentationIntegrity(load_tick) == FALSE)
	{
		return FALSE;
	}
	live_f = syNetSyncHashBattleFightersFull();
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(load_tick)) &&
	    (syNetRbSnapshotAnyLiveFighterInIntroLoadFidelityScope() != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: REPLAY_GATE_FIGH_DRIFT load_tick=%u slot=0x%08X live=0x%08X\n",
		    load_tick,
		    syNetRbSnapshotGetSlotHashFighter(load_tick),
		    live_f);
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackFailBeginResimOnUnsafeLoad(u32 load_tick, sb32 final_load_ok, sb32 anchor_probe_unresolved)
{
	port_log(
	    "SSB64 NetRollback: resim begin aborted load_tick=%u final_load_ok=%d anchor_probe_unresolved=%d\n",
	    load_tick,
	    (int)final_load_ok,
	    (int)anchor_probe_unresolved);
	sSYNetRollbackBeginResimInitialLoad = FALSE;
	sSYNetRollbackLoadFailCount++;
	syNetRollbackArmBattleSimHoldAfterLoadFail(load_tick);
	syNetRollbackResetCorrectionEpisode();
	syNetRollbackResetBaselineResimState();
	syNetRollbackRequestLoadFailBattleExit();
	syNetRollbackStopVsSessionForLoadFail(load_tick, "begin_resim_aborted");
	return FALSE;
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

	/*
	 * Deferred GGPO/symmetric already queued: allow BeginResim even when LastCommitted is unset.
	 * Without this, TryBegin can sit on correction_not_allowed while live-cap holds forever.
	 */
	if ((sSYNetRollbackDeferredMismatchPending != FALSE) &&
	    (mismatch_tick == sSYNetRollbackDeferredMismatchTick) &&
	    (sSYNetRollbackDeferredMismatchTargetTick > mismatch_tick))
	{
		return TRUE;
	}
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
	/* HealedThrough is session-durable — cleared only on VS session reset. */
}

#if defined(PORT) && defined(SSB64_NETMENU)
static void syNetRollbackRaiseResolvedThrough(u32 resolved_raise)
{
	if (resolved_raise > sSYNetRollbackEpisodeResolvedThrough)
	{
		sSYNetRollbackEpisodeResolvedThrough = resolved_raise;
	}
	if (resolved_raise > sSYNetRollbackHealedThrough)
	{
		sSYNetRollbackHealedThrough = resolved_raise;
	}
}

/* Exclusive-end coverage: live episode frontier, or durable heal if FC deepen cleared it. */
static u32 syNetRollbackExclusiveEndCoveredThrough(void)
{
	u32 covered;

	covered = sSYNetRollbackEpisodeResolvedThrough;
	if (sSYNetRollbackHealedThrough > covered)
	{
		covered = sSYNetRollbackHealedThrough;
	}
	return covered;
}

/*
 * Inputs-agree FC recovery must compare real fighter digests. Exclusive-end soft-continue
 * (ignore_stale / FIGH_STALE_AGGREGATE) papers SoftLip/Hold forks and prevents deepen from
 * seeing the onset the FC path already detected. Soak1 313936707: ignore_stale @1263 with
 * healed=1269 while FC onset=1265.
 */
static sb32 syNetRollbackFcHonestFighCompareActive(void)
{
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		return TRUE;
	}
	if ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchInputAgreed != FALSE))
	{
		return TRUE;
	}
	return FALSE;
}
#endif

u32 syNetRollbackGetEpisodeResolvedThrough(void)
{
	return sSYNetRollbackEpisodeResolvedThrough;
}

void syNetRollbackNotePeerResolvedThrough(u32 peer_resolved_through)
{
	if (peer_resolved_through == 0U)
	{
		return;
	}
	if (peer_resolved_through > sSYNetRollbackPeerResolvedThrough)
	{
		sSYNetRollbackPeerResolvedThrough = peer_resolved_through;
	}
}

u32 syNetRollbackGetPeerResolvedThrough(void)
{
	return sSYNetRollbackPeerResolvedThrough;
}

static u32 syNetRollbackGetSharedCorrectionFrontier(void)
{
	u32 frontier;

	frontier = sSYNetRollbackEpisodeResolvedThrough;
	if (sSYNetRollbackPeerResolvedThrough > frontier)
	{
		frontier = sSYNetRollbackPeerResolvedThrough;
	}
	return frontier;
}

/*
 * Intersected correction frontier — MIN(local, peer) when both are known.
 * Used by FC input-agree recovery load pick so both peers prefer the same
 * in-ring anchor when peer resolved_through has already arrived. MAX (shared)
 * remains the GGPO clamp / seal-cancel authority. See
 * docs/bugs/netplay_fc_recovery_divergent_load_hang_2026-07-27.md.
 */
static u32 syNetRollbackGetIntersectedCorrectionFrontier(void)
{
	u32 local_f;
	u32 peer_f;

	local_f = sSYNetRollbackEpisodeResolvedThrough;
	peer_f = sSYNetRollbackPeerResolvedThrough;
	if (local_f == 0U)
	{
		return peer_f;
	}
	if (peer_f == 0U)
	{
		return local_f;
	}
	return (peer_f < local_f) ? peer_f : local_f;
}

sb32 syNetRollbackIsBattleSimHoldActive(void)
{
	return (sSYNetRollbackBattleSimHoldAfterLoadFail != FALSE) ? TRUE : FALSE;
}

static void syNetRollbackClearBattleSimHoldAfterLoadFail(void)
{
	if (sSYNetRollbackBattleSimHoldAfterLoadFail != FALSE)
	{
		sSYNetRollbackBattleSimHoldAfterLoadFail = FALSE;
		sSYNetRollbackLoadFailBattleExitPending = FALSE;
		sSYNetRollbackLoadFailSceneRetargeted = FALSE;
		sSYNetRollbackBattleSimHoldLoadTick = 0U;
		sSYNetRollbackBattleSimHoldFrames = 0U;
		sSYNetRollbackBattleSimHoldTeardownSent = FALSE;
		port_log("SSB64 NetRollback: BATTLE_SIM_HOLD cleared\n");
	}
}

void syNetRollbackClearLoadFailBattleHold(void)
{
	syNetRollbackClearBattleSimHoldAfterLoadFail();
}

sb32 syNetRollbackConsumeLoadFailBattleSceneRetarget(void)
{
	if (sSYNetRollbackLoadFailSceneRetargeted == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackLoadFailSceneRetargeted = FALSE;
	syNetRollbackClearBattleSimHoldAfterLoadFail();
	return TRUE;
}

static void syNetRollbackRequestLoadFailBattleExit(void)
{
	sSYNetRollbackLoadFailBattleExitPending = TRUE;
}

void syNetRollbackOnPeerLoadFailAbort(u32 load_tick)
{
	if (syNetRollbackIsBattleSimHoldActive() == FALSE)
	{
		sSYNetRollbackLoadFailCount++;
		syNetRollbackArmBattleSimHoldAfterLoadFail(load_tick);
	}
	syNetRollbackResetCorrectionEpisode();
	syNetRollbackResetBaselineResimState();
	syNetRollbackRequestLoadFailBattleExit();
	port_log(
	    "SSB64 NetRollback: peer load_fail abort mirrored hold=%d sim=%u load_tick=%u\n",
	    (int)syNetRollbackIsBattleSimHoldActive(),
	    syNetInputGetTick(),
	    load_tick);
}

static u32 syNetRollbackLiveWedgeEscapeFrames(void)
{
	static s32 s_cached = -1;

	if (s_cached < 0)
	{
		const char *e = getenv("SSB64_NETPLAY_LIVE_WEDGE_ESCAPE_FRAMES");

		s_cached = ((e != NULL) && (e[0] != '\0')) ? atoi(e)
		                                            : (s32)SYNETROLLBACK_LIVE_WEDGE_ESCAPE_FRAMES_DEFAULT;
		if (s_cached < 0)
		{
			s_cached = 0;
		}
	}
	return (u32)s_cached;
}

static void syNetRollbackPumpLiveWedgeEscape(void)
{
	u32 threshold;
	u32 blocked_tick;
	u32 tick;

	threshold = syNetRollbackLiveWedgeEscapeFrames();
	if (threshold == 0U)
	{
		return; /* env-disabled */
	}
	if ((syNetPeerIsVSSessionActive() == FALSE) || (syNetRollbackIsActive() == FALSE) ||
	    (syNetReconnectHoldActive() != FALSE))
	{
		sSYNetRollbackLiveWedgeFrames = 0U;
		return;
	}
	/*
	 * Sample the frontier only outside resim: episode replays rewind the authoritative
	 * tick, and a wedge that churns failed recovery episodes must keep counting through
	 * them (that is exactly what the 2026-08-30 wedge looked like).
	 */
	if (syNetRollbackIsResimulating() == FALSE)
	{
		tick = syNetInputGetTick();
		if (tick > sSYNetRollbackLiveWedgeFrontierTick)
		{
			sSYNetRollbackLiveWedgeFrontierTick = tick;
			sSYNetRollbackLiveWedgeFrames = 0U;
			return;
		}
	}
	/*
	 * Only count frames with evidence: the commit gate must have refused a tick at or
	 * past the frontier. A frozen tick with no refusal (menus, results, pauses — the
	 * gate is not even consulted there) is not a wedge.
	 */
	blocked_tick = syNetInputGetLastLiveAdvanceBlockedTick();
	if ((blocked_tick == ~(u32)0) || (blocked_tick < sSYNetRollbackLiveWedgeFrontierTick))
	{
		sSYNetRollbackLiveWedgeFrames = 0U;
		return;
	}
	sSYNetRollbackLiveWedgeFrames++;
	if (sSYNetRollbackLiveWedgeFrames < threshold)
	{
		return;
	}
	{
		u32 cap = ~(u32)0;
		u32 cap_source = 0U;

		(void)syNetRollbackGetLiveSimCap(&cap, &cap_source);
		port_log(
		    "SSB64 NetRollback: LIVE_WEDGE_ESCAPE frontier=%u blocked_tick=%u frames=%u cap=%u cap_source=%u — "
		    "no live advance for ~%us with the commit gate refusing; escalating into BATTLE_SIM_HOLD escape\n",
		    sSYNetRollbackLiveWedgeFrontierTick, blocked_tick, (unsigned int)sSYNetRollbackLiveWedgeFrames,
		    cap, cap_source, (unsigned int)(sSYNetRollbackLiveWedgeFrames / 60U));
	}
	sSYNetRollbackLiveWedgeFrames = 0U;
	syNetRollbackArmBattleSimHoldAfterLoadFail(sSYNetRollbackLiveWedgeFrontierTick);
}

void syNetRollbackPumpLoadFailBattleExit(void)
{
#if defined(SSB64_NETMENU)
	extern void mnVSNetAutomatchAMAbortToCharacterSelect(const char *reason);
#endif

	if (syNetRollbackIsBattleSimHoldActive() == FALSE)
	{
		sSYNetRollbackBattleSimHoldFrames = 0U;
		syNetRollbackPumpLiveWedgeEscape();
		return;
	}
	if (sSYNetRollbackLoadFailBattleExitPending == FALSE)
	{
		/* Hold armed with no escape pending (pre-existing arm path, or a clear
		 * that raced the arm). Re-arm rather than sit frozen. */
		port_log("SSB64 NetRollback: BATTLE_SIM_HOLD escape re-armed sim=%u\n", syNetInputGetTick());
		syNetRollbackRequestLoadFailBattleExit();
	}
	if (syNetPeerIsVSSessionActive() != FALSE)
	{
		/*
		 * The scene exit below only runs once VS is down. A load-fail hold is
		 * terminal for this match (the deeper-load rescue already failed), so
		 * bring the session down ourselves instead of waiting for the peer to
		 * quit — which is what left soak 2026-08-21 frozen with a live peer.
		 */
		sSYNetRollbackBattleSimHoldFrames++;
		if ((sSYNetRollbackBattleSimHoldTeardownSent == FALSE) &&
		    (sSYNetRollbackBattleSimHoldFrames >= SYNETROLLBACK_BATTLE_SIM_HOLD_TEARDOWN_FRAMES))
		{
			sSYNetRollbackBattleSimHoldTeardownSent = TRUE;
			port_log(
			    "SSB64 NetRollback: BATTLE_SIM_HOLD escape watchdog sim=%u load_tick=%u frames=%u — "
			    "tearing down VS so the battle can exit\n",
			    syNetInputGetTick(),
			    sSYNetRollbackBattleSimHoldLoadTick,
			    (unsigned int)sSYNetRollbackBattleSimHoldFrames);
			syNetRollbackStopVsSessionForLoadFail(sSYNetRollbackBattleSimHoldLoadTick,
			                                      "battle_sim_hold_watchdog");
		}
		return;
	}
	sSYNetRollbackLoadFailBattleExitPending = FALSE;
	sSYNetRollbackLoadFailSceneRetargeted = TRUE;
	port_log("SSB64 NetRollback: load_fail battle exit sim=%u\n", syNetInputGetTick());
#if defined(SSB64_NETMENU)
	if (gSCManagerSceneData.is_vs_automatch_battle != FALSE)
	{
		mnVSNetAutomatchAMAbortToCharacterSelect("resim load fail");
	}
	else
	{
		gSCManagerSceneData.is_reset = TRUE;
		ifCommonAnnounceEndMessage();
	}
#else
	gSCManagerSceneData.is_reset = TRUE;
	ifCommonAnnounceEndMessage();
#endif
}

static void syNetRollbackStopVsSessionForLoadFail(u32 load_tick, const char *reason)
{
	sb32 peer_vs_active;
	sb32 rollback_active;

	peer_vs_active = syNetPeerIsVSSessionActive();
	rollback_active = syNetRollbackIsActive();
	port_log(
	    "SSB64 NetRollback: resim_load_fail — tear down load_tick=%u reason=%s peer_vs_active=%d rollback_active=%d hold=%d sim=%u\n",
	    load_tick,
	    (reason != NULL) ? reason : "unknown",
	    (int)peer_vs_active,
	    (int)rollback_active,
	    (int)syNetRollbackIsBattleSimHoldActive(),
	    syNetInputGetTick());
	if (peer_vs_active != FALSE)
	{
		syNetPeerSendVsSessionEndLoadFailNotifyPeer(load_tick);
		syNetPeerStopVSSession();
	}
	else
	{
		port_log(
		    "SSB64 NetRollback: resim_load_fail — peer VS already inactive; rollback session cleanup only (hold retained)\n");
		syNetRollbackStopVSSession();
	}
}

static void syNetRollbackArmBattleSimHoldAfterLoadFail(u32 load_tick)
{
	u32 sim_tick;

	sim_tick = syNetInputGetTick();
	(void)syNetRbSnapshotRestoreLiveEmergency();
	syNetRbSnapshotLogFighterStatusTrail("load_fail_restore", sim_tick);
	if (syNetRbSnapshotVerifyLiveFightersSanity(sim_tick, "load_fail_restore") == FALSE)
	{
		port_log(
		    "SSB64 NetRollback: BATTLE_SIM_HOLD live fighter sanity still bad after emergency restore sim=%u load_tick=%u\n",
		    sim_tick,
		    load_tick);
	}
	sSYNetRollbackBattleSimHoldAfterLoadFail = TRUE;
	sSYNetRollbackBattleSimHoldLoadTick = load_tick;
	sSYNetRollbackBattleSimHoldFrames = 0U;
	sSYNetRollbackBattleSimHoldTeardownSent = FALSE;
	/*
	 * Arming the hold always arms the escape with it. The hold blocks the battle
	 * sim, so no in-sim path can end the match afterwards — if the exit is not
	 * requested here the match freezes for as long as the window is open.
	 * Two of the five arm sites (both BeginResim load-fail paths) never requested
	 * it, which is exactly the one that fires in practice: soak 2026-08-21 froze
	 * at sim=8382 with 0 "load_fail battle exit" and 0 "BATTLE_SIM_HOLD cleared"
	 * for the rest of the session. Callers that already request it are idempotent.
	 * See docs/bugs/netplay_battle_sim_hold_no_escape_2026-08-21.md.
	 */
	syNetRollbackRequestLoadFailBattleExit();
	port_log(
	    "SSB64 NetRollback: BATTLE_SIM_HOLD armed sim=%u load_tick=%u reason=resim_load_fail fail_count=%u\n",
	    sim_tick,
	    load_tick,
	    sSYNetRollbackLoadFailCount);
}

#if defined(SSB64_NETMENU)
/*
 * Wire-exclusive end for a completing light episode: identical bound to Begin's
 * LIGHT_WIRE_READY clamp (contiguous remote-confirmed from the episode mismatch).
 * player < 0 scans every remote human slot (MIN) — the frontier protects all players,
 * so all remote slots must be wired through the protected span.
 */
static u32 syNetRollbackLightCloseWireReadyExclusiveEnd(u32 mismatch_tick, s32 player)
{
	u32 scan_cap;
	u32 live;
	u32 through;
	u32 ready;
	s32 slot;
	sb32 any;

	live = syNetInputGetTick();
	scan_cap = live + SYNETROLLBACK_LIGHT_WIRE_READY_SCAN_SLACK;
	if (scan_cap < mismatch_tick)
	{
		scan_cap = mismatch_tick;
	}
	if (player >= 0)
	{
		through = syNetInputContiguousRemoteConfirmedThrough(player, mismatch_tick, scan_cap);
		if (through >= mismatch_tick)
		{
			return through + 1U;
		}
		return mismatch_tick + 1U;
	}
	ready = ~(u32)0;
	any = FALSE;
	for (slot = 0; slot < GMCOMMON_PLAYERS_MAX; slot++)
	{
		if (syNetInputIsRemoteHumanSlot(slot) == FALSE)
		{
			continue;
		}
		through = syNetInputContiguousRemoteConfirmedThrough(slot, mismatch_tick, scan_cap);
		if (through < mismatch_tick)
		{
			return mismatch_tick + 1U;
		}
		if ((through + 1U) < ready)
		{
			ready = through + 1U;
		}
		any = TRUE;
	}
	if (any == FALSE)
	{
		return mismatch_tick + 1U;
	}
	return ready;
}

/*
 * resolved_through may only protect ticks the resim replayed with authoritative inputs.
 * Light episodes have no seal-row exchange, so span ticks past the wire frontier replay as
 * hold_last guesses; advancing the floor over them turns the late wire REPLACE into
 * CORRECTION_CLAMP_RESOLVED promote-only input loss (soak 2026-07-27: light ep 396-400
 * replayed 397-399 as hold_last 13,0; wire 40,0/53,2/57,4 never rewound -> tap-counter
 * fork -> FC diverge @721). Heavy sealed episodes replay no predictions -> cap is a no-op.
 * See docs/bugs/netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md.
 *
 * Close/frontier wire contract (2026-07-27): additionally, a light episode may never
 * raise resolved_through past the same contiguous remote-confirmed exclusive end that
 * bounds Begin (LIGHT_WIRE_READY). The predicted-replay watermark alone can miss spans
 * (watermark reset between replays); the wire ledger is the authority. Heavy sealed
 * episodes stay exempt — peer seal rows are authoritative even when the local wire
 * ledger lags. See docs/bugs/netplay_close_wire_cap_diverge_hang_exit_2026-07-27.md.
 */
static u32 syNetRollbackCapResolvedThroughForPredictedReplay(u32 completed_target)
{
	u32 watermark;
	u32 capped;

	capped = completed_target;
	watermark = syNetInputResimLowestPredictedReplayTick();
	if ((watermark != 0U) && (watermark < capped))
	{
		port_log(
		    "SSB64 NetRollback: RESOLVED_THROUGH_PRED_CAP target=%u -> %u (hold_last replay in span)\n",
		    completed_target,
		    watermark);
		capped = watermark;
	}
	if ((syNetRollbackLightEpisodeIsActive() != FALSE) && (sSYNetRollbackResimMismatchTick != 0U) &&
	    (sSYNetRollbackResimMismatchTick != ~(u32)0))
	{
		u32 ready;

		ready = syNetRollbackLightCloseWireReadyExclusiveEnd(sSYNetRollbackResimMismatchTick,
		                                                    sSYNetRollbackResimCorrectionPlayer);
		if ((ready > sSYNetRollbackResimMismatchTick) && (ready < capped))
		{
			port_log(
			    "SSB64 NetRollback: RESOLVED_THROUGH_WIRE_CAP target=%u -> %u mismatch=%u player=%d "
			    "(light close bounded by contiguous remote-confirmed exclusive end)\n",
			    capped,
			    ready,
			    sSYNetRollbackResimMismatchTick,
			    (int)sSYNetRollbackResimCorrectionPlayer);
			capped = ready;
		}
	}
	return capped;
}
#endif

static void syNetRollbackCloseCorrectionEpisode(u32 completed_target)
{
	if ((completed_target != 0U) && (completed_target != ~(u32)0))
	{
		u32 absorb_window;
		u32 resolved_raise;

		resolved_raise = completed_target;
#if defined(SSB64_NETMENU)
		resolved_raise = syNetRollbackCapResolvedThroughForPredictedReplay(completed_target);
#endif
#if defined(PORT) && defined(SSB64_NETMENU)
		syNetRollbackRaiseResolvedThrough(resolved_raise);
#else
		if (resolved_raise > sSYNetRollbackEpisodeResolvedThrough)
		{
			sSYNetRollbackEpisodeResolvedThrough = resolved_raise;
		}
#endif
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * Drop pending/outbound deepen past the closed target for the same onset.
		 * After SHRINK→Commit@peer_prefix, a stale peer SYNC at the abandoned deepen
		 * must not stick in pending (dup_pending). See
		 * docs/bugs/netplay_fc_shrink_target_fork_hang_2026-07-28.md.
		 */
		if ((sSYNetRollbackResimMismatchTick != 0U) && (sSYNetRollbackResimMismatchTick != ~(u32)0))
		{
			u32 settled_mismatch;
			s32 sym_slot;

			settled_mismatch = sSYNetRollbackResimMismatchTick;
			if ((sSYNetRollbackPendingPeerSymmetricTick == settled_mismatch) &&
			    (sSYNetRollbackPendingPeerSymmetricTargetTick != ~(u32)0) &&
			    (sSYNetRollbackPendingPeerSymmetricTargetTick > completed_target))
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_CLOSE_CLEAR_ABANDONED_DEEPEN pending mismatch=%u "
				    "target=%u->cleared completed=%u\n",
				    settled_mismatch,
				    sSYNetRollbackPendingPeerSymmetricTargetTick,
				    completed_target);
				sym_slot = sSYNetRollbackPendingPeerSymmetricSlot;
				syNetRollbackClearPendingPeerSymmetricNotify();
				if ((sym_slot >= 0) && (sym_slot < MAXCONTROLLERS))
				{
					syNetRollbackPendingEpisodeClearSlot(sym_slot);
				}
			}
			if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
			    (sSYNetRollbackDeferredPeerSymmetricTick == settled_mismatch) &&
			    (sSYNetRollbackDeferredPeerSymmetricTargetTick > completed_target))
			{
				port_log(
				    "SSB64 NetRollback: EPISODE_CLOSE_CLEAR_ABANDONED_DEEPEN deferred mismatch=%u "
				    "target=%u->cleared completed=%u\n",
				    settled_mismatch,
				    sSYNetRollbackDeferredPeerSymmetricTargetTick,
				    completed_target);
				sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
				sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
				sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
				sSYNetRollbackDeferredPeerSymmetricSlot = -1;
				sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
				sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
			}
			for (sym_slot = 0; sym_slot < MAXCONTROLLERS; sym_slot++)
			{
				if ((sSYNetRollbackSymmetricNotifyTick[sym_slot] == settled_mismatch) &&
				    (sSYNetRollbackSymmetricNotifyTargetTick[sym_slot] > completed_target))
				{
					sSYNetRollbackSymmetricNotifyTargetTick[sym_slot] = completed_target;
				}
			}
		}
#endif
		if ((sSYNetRollbackPeerEpochTargetTick != 0U) && (completed_target >= sSYNetRollbackPeerEpochTargetTick) &&
		    (syNetRollbackRetainPeerEpochAfterLocalResim() == FALSE))
		{
			syNetRollbackClearPeerEpochState();
		}
		/*
		 * Stick L/R storms open ep2/ep3 the tick after complete. Hold an absorb window so
		 * REPLACE coalesces into deferred instead of dual-initiator baseline races.
		 * See docs/bugs/netplay_stick_lr_baseline_stash_hang_2026-07-12.md.
		 * Soak 1156067044: DamageFall DI produced three stick episodes (5154/5165/5169) when
		 * absorb ≈ phase_lock (~8). Legacy widen (×2, cap 32) coalesced storms but NETMENU
		 * dual-stick deferred targets track the live frontier during coalesce → ~17-tick fat
		 * resim metronome. Shorter window: phase_lock clamped [4,8], no ×2.
		 * Hard ledger corrections expire absorb early (netinput LEDGER_REFRESH).
		 * See docs/bugs/netplay_stick_absorb_resim_metronome_2026-07-27.md.
		 *
		 * NETMENU dual-mover (soak 1811750376 @1200–1450): each peer only GGPOs its remote.
		 * Absorb scoped to ResimCorrectionPlayer let the *other* remote open a fresh
		 * initiator every ~D ticks → ~2.2 begins/GGPO ping-pong while both sticks move.
		 * Absorb any slot; TryBegin waits the window without live-cap (below).
		 * See docs/bugs/netplay_dual_stick_ggpo_resim_storm_2026-07-27.md.
		 * Follow-on hangs: peer_convergence (source=2) during absorb and after episode
		 * when CoalesceWaiting was false (no deferred) — live-cap/runway honor
		 * StickAbsorbWindowActive; NotePeerEpochTarget ignored during window. See
		 * docs/bugs/netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md.
		 *
		 * Multi-stick mash: absorb = clamp(phase_lock + K*(pressure-1), …, MAX) so N
		 * dirty remotes coalesce into one Begin (not per-slot ping-pong). See
		 * docs/bugs/netplay_multistick_correction_union_2026-07-27.md.
		 *
		 * Light input episodes: resim is local + cheap, so absorb is merge-only (2) —
		 * just enough to fold same-burst REPLACEs into one span. Long windows only
		 * added correction latency. See docs/bugs/netplay_light_input_episodes_2026-07-27.md.
		 */
#if defined(SSB64_NETMENU)
		if (syNetRollbackLightInputEpisodesEnabled() != FALSE)
		{
			absorb_window = SYNETROLLBACK_LIGHT_EPISODE_ABSORB_WINDOW;
		}
		else
		{
			absorb_window = syNetRollbackMultiStickAbsorbWindow(completed_target);
		}
#else
		absorb_window = syNetPeerGetPhaseLockPredictionWindowTicks();
		if (absorb_window < 8U)
		{
			absorb_window = 8U;
		}
		absorb_window *= 2U;
		if (absorb_window > 32U)
		{
			absorb_window = 32U;
		}
#endif
		sSYNetRollbackStickAbsorbUntilSim = completed_target + absorb_window;
#if defined(SSB64_NETMENU)
		sSYNetRollbackStickAbsorbArmedFromSim = completed_target;
		sSYNetRollbackStickAbsorbPlayer = -1;
		sSYNetRollbackStickAbsorbExemptPlayer = -1;
		syNetRollbackEpisodeFsmOnPostMatch();
#else
		sSYNetRollbackStickAbsorbPlayer = sSYNetRollbackResimCorrectionPlayer;
#endif
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

/* Always-on, rate-limited breadcrumb when deferred queue is dropped (DEFER_DIAG still full detail). */
static void syNetRollbackLogQueueDeferredDrop(u32 mismatch, u32 frontier, s32 player, const char *reason)
{
	static u32 sLastMismatch = ~(u32)0;
	static u32 sLastFrontier = ~(u32)0;
	static u32 sLastSimTick = ~(u32)0;
	u32 sim_tick;

	sim_tick = syNetInputGetTick();
	if ((mismatch == sLastMismatch) && (frontier == sLastFrontier) && (sim_tick == sLastSimTick))
	{
		return;
	}
	sLastMismatch = mismatch;
	sLastFrontier = frontier;
	sLastSimTick = sim_tick;
	port_log(
	    "SSB64 NetRollback: deferred_queue_drop reason=%s player=%d mismatch=%u frontier=%u sim=%u\n",
	    (reason != NULL) ? reason : "?",
	    (int)player,
	    mismatch,
	    frontier,
	    sim_tick);
	syNetRollbackLogDeferDiag(reason, mismatch, frontier, player);
}

/* Always-on, rate-limited TryBegin failure breadcrumb (DEFER_DIAG still dumps full defer_diag). */
static void syNetRollbackLogTryBeginFail(const char *stage, u32 mismatch, u32 target, s32 player)
{
	static u32 sLastMismatch = ~(u32)0;
	static u32 sLastSimTick = ~(u32)0;
	static const char *sLastStage = NULL;
	u32 sim_tick;

	sim_tick = syNetInputGetTick();
	if ((mismatch == sLastMismatch) && (sim_tick == sLastSimTick) && (stage == sLastStage))
	{
		return;
	}
	sLastMismatch = mismatch;
	sLastSimTick = sim_tick;
	sLastStage = stage;
	port_log(
	    "SSB64 NetRollback: try_begin_fail stage=%s mismatch=%u target=%u player=%d sim=%u last_committed=%u resolved_through=%u\n",
	    (stage != NULL) ? stage : "?",
	    mismatch,
	    target,
	    (int)player,
	    (unsigned int)sim_tick,
	    (unsigned int)sSYNetRollbackLastCommittedMismatchTick,
	    (unsigned int)sSYNetRollbackEpisodeResolvedThrough);
	syNetRollbackLogDeferDiag(stage, mismatch, target, player);
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
	/*
	 * Deferred GGPO arms DeferredCorrectionBlocksLiveAdvance (cap = mismatch-1). Cooldown is
	 * measured in sim ticks, so a freeze at sim==mismatch can never reach LastBegin+cooldown —
	 * stick L/R storms then deadlock (soak seed 1613454651: ep3 done@435, deferred 435→437,
	 * cap=434, commit_begin_failed forever). The pending deferred mismatch must bypass cooldown.
	 * See docs/bugs/netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md.
	 */
	if ((sSYNetRollbackDeferredMismatchPending != FALSE) &&
	    (mismatch_tick == sSYNetRollbackDeferredMismatchTick) &&
	    (sSYNetRollbackDeferredMismatchTargetTick > mismatch_tick))
	{
		return TRUE;
	}
	/*
	 * Peer-symmetric join after a just-completed episode: follower TryCommit often lands at
	 * sim == LastBegin+1 (ep1 joined at live frontier, ep2 SYNC one tick later). Cooldown would
	 * log "at tick" then silently fail BeginResim → seal_rows_missing hang (soak stick-up ~502).
	 * See docs/bugs/netplay_stick_up_boundary_seal_join_hang_2026-07-12.md.
	 */
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick == sSYNetRollbackPendingPeerSymmetricTick) &&
	    (sSYNetRollbackPendingPeerSymmetricTargetTick > mismatch_tick))
	{
		return TRUE;
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (mismatch_tick == sSYNetRollbackDeferredPeerSymmetricTick) &&
	    (sSYNetRollbackDeferredPeerSymmetricTargetTick > mismatch_tick))
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
		syNetRollbackLogTryBeginFail("commit_bad_tuple", mismatch_tick, target_tick, -1);
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	/*
	 * HealedThrough soft-continues exclusive-end baselines after FC deepen (seed 2064608876)
	 * but must NOT refuse Begin when mismatch < HealedThrough — that hung inputs_agree FC
	 * (soak1 313936707). FC recovery uses syNetRollbackFcHonestFighCompareActive instead.
	 * See docs/bugs/netplay_fc_healed_frontier_honest_fc_2026-07-28.md.
	 */
	if (syNetRollbackGlobalCooldownAllows(mismatch_tick) == FALSE)
	{
		syNetRollbackLogTryBeginFail("commit_cooldown", mismatch_tick, target_tick, -1);
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if ((load_tick == sSYNetRollbackSuppressReloadLoadTick) && (sim_tick <= sSYNetRollbackSuppressReloadUntilSim))
	{
		syNetRollbackLogTryBeginFail("commit_suppress_reload", mismatch_tick, target_tick, -1);
		syNetRollbackAbortCorrectionCommit(&snap);
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch_tick < sSYNetRollbackEpisodeResolvedThrough))
	{
		sb32 allow_frontier_deepen;

		/*
		 * Shared correction frontier: ordinary GGPO must not open behind resolved_through
		 * just because live sim advanced (soak1 642600542: ep0→420 then GGPO@414 reset
		 * episode → follower reject → seal hang). Only FC / state-hash deepen may reanchor.
		 * See docs/bugs/netplay_shared_correction_frontier_2026-07-19.md.
		 */
		allow_frontier_deepen =
		    ((sSYNetRollbackFcStateRecoveryActive != FALSE) ||
		     (sSYNetRollbackDeferredStateMismatchPending != FALSE) ||
		     ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
		      ((sSYNetRollbackPendingPeerSymmetricFlags & SYNETROLLBACK_SYM_NOTIFY_FLAG_FC_RECOVERY) !=
		       0U)) ||
		     ((sSYNetRollbackResimFromPeerSymmetric != FALSE) &&
		      ((sSYNetRollbackPendingPeerSymmetricFlags & SYNETROLLBACK_SYM_NOTIFY_FLAG_FC_RECOVERY) !=
		       0U)))
			? TRUE
			: FALSE;
		/*
		 * FC / state-hash deepen: allow episode reset when sim has reached the frontier
		 * (sim == resolved). Soak 1065668144: FC TryBegin at sim==resolved_through right
		 * after resim_complete used <= and hard-failed (commit_behind_resolved) or, when
		 * Pending was cleared before Commit, mis-labeled commit_behind_frontier.
		 * Peer FC_RECOVERY join after EPISODE_YIELD must deepen too (soak 2824158137).
		 * Ordinary GGPO still cannot open behind resolved. See
		 * docs/bugs/netplay_fc_commit_behind_frontier_deepen_2026-07-26.md,
		 * docs/bugs/netplay_fc_yield_commit_behind_frontier_2026-07-28.md.
		 *
		 * Exclusive-end soft-continue after deepen uses ExclusiveEndCoveredThrough (not a
		 * Begin refuse). FC/state-hash deepen may ResetCorrectionEpisode here.
		 */
#if defined(PORT) && defined(SSB64_NETMENU)
		if ((allow_frontier_deepen == FALSE) || (sim_tick < sSYNetRollbackEpisodeResolvedThrough))
#else
		if ((allow_frontier_deepen == FALSE) || (sim_tick <= sSYNetRollbackEpisodeResolvedThrough))
#endif
		{
			syNetRollbackLogTryBeginFail(
			    (allow_frontier_deepen == FALSE) ? "commit_behind_frontier" : "commit_behind_resolved",
			    mismatch_tick,
			    target_tick,
			    -1);
			syNetRollbackAbortCorrectionCommit(&snap);
			return FALSE;
		}
		syNetRollbackResetCorrectionEpisode();
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
				syNetRollbackLogTryBeginFail("commit_ext_max_storm", mismatch_tick, target_tick, -1);
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
			syNetRollbackLogTryBeginFail("commit_target_not_wider", mismatch_tick, target_tick, -1);
			syNetRollbackAbortCorrectionCommit(&snap);
			return FALSE;
		}
		if (sSYNetRollbackEpisodeExtensions >= SYNETROLLBACK_MAX_EPISODE_EXTENSIONS)
		{
			syNetRollbackLogTryBeginFail("commit_ext_max", mismatch_tick, target_tick, -1);
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
	{
		u32 resolved_raise;

		resolved_raise = sSYNetRollbackResimTargetTick;
#if defined(SSB64_NETMENU)
		/* Cap floor at hold_last replay watermark — see CloseCorrectionEpisode. */
		if ((resolved_raise != 0U) && (resolved_raise != ~(u32)0))
		{
			resolved_raise = syNetRollbackCapResolvedThroughForPredictedReplay(resolved_raise);
		}
#endif
#if defined(PORT) && defined(SSB64_NETMENU)
		syNetRollbackRaiseResolvedThrough(resolved_raise);
#else
		if (resolved_raise > sSYNetRollbackEpisodeResolvedThrough)
		{
			sSYNetRollbackEpisodeResolvedThrough = resolved_raise;
		}
#endif
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

static sb32 syNetRollbackPreemptiveBaselineCapIsStale(u32 load_tick)
{
	u32 preempt_mismatch;

	if (load_tick == 0U)
	{
		return TRUE;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) ||
	    (sSYNetRollbackDeferredPeerSymmetricPending != FALSE))
	{
		return FALSE;
	}
	preempt_mismatch = load_tick + 1U;
	/*
	 * Inclusive `<=`: load == resolved_through - 1 is the exclusive end of the
	 * just-committed span, not the onset of a new episode. Soak seed 214064425:
	 * after Commit@777, peer baseline@776 armed LIVE_CAP then PEER_SNAPSHOT_DIVERGE.
	 * Use ExclusiveEndCoveredThrough so FC deepen clearing EpisodeResolvedThrough
	 * does not re-arm (soak seed 2064608876).
	 * See docs/bugs/netplay_fc_exclusive_end_baseline_diverge_post_heal_2026-07-28.md,
	 * docs/bugs/netplay_fc_healed_frontier_survive_deepen_2026-07-28.md.
	 */
#if defined(PORT) && defined(SSB64_NETMENU)
	{
		u32 covered;

		covered = syNetRollbackExclusiveEndCoveredThrough();
		if ((covered != 0U) && (preempt_mismatch <= covered))
		{
			return TRUE;
		}
	}
#else
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) &&
	    (preempt_mismatch <= sSYNetRollbackEpisodeResolvedThrough))
	{
		return TRUE;
	}
#endif
	return FALSE;
}

static void syNetRollbackMaybeClearStalePeerSymmetricRejectLiveCap(void)
{
	u32 sim_tick;
	u32 cap;
	u32 cap_mismatch;

	if (sSYNetRollbackPeerSymmetricRejectLiveCap == ~(u32)0)
	{
		return;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) ||
	    (sSYNetRollbackDeferredPeerSymmetricPending != FALSE))
	{
		return;
	}
	sim_tick = syNetInputGetTick();
	cap = sSYNetRollbackPeerSymmetricRejectLiveCap;
	/*
	 * Cap guards mismatch = cap+1. Strict `<` vs resolved_through: mismatch == resolved_through
	 * is the first tick of the *next* episode (preemptive baseline for load=resolved-1). Clearing
	 * when sim >= resolved && sim > cap dropped that cap immediately (soak stick-up: arm cap=501
	 * for mismatch=502 with resolved=502 → CLEAR → sim advances → seal join fails).
	 * See docs/bugs/netplay_stick_up_boundary_seal_join_hang_2026-07-12.md.
	 */
	cap_mismatch = (cap < ~(u32)0) ? (cap + 1U) : ~(u32)0;
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) &&
	    (cap_mismatch < sSYNetRollbackEpisodeResolvedThrough) && (sim_tick > cap))
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_PREEMPTIVE_LIVE_CAP_CLEAR stale cap=%u cap_mismatch=%u sim=%u resolved_through=%u\n",
		    cap,
		    cap_mismatch,
		    sim_tick,
		    sSYNetRollbackEpisodeResolvedThrough);
		syNetRollbackClearPeerSymmetricRejectLiveCap();
	}
}

static sb32 syNetRollbackPeerSymmetricRejectBlocksLiveAdvance(u32 *out_cap)
{
	syNetRollbackMaybeClearStalePeerSymmetricRejectLiveCap();
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

static int sSYNetRollbackCorrectionTupleLogCache = -999;

static sb32 syNetRollbackCorrectionTupleLogEnabled(void)
{
	const char *env;

	if (sSYNetRollbackCorrectionTupleLogCache == -999)
	{
		env = getenv("SSB64_NETPLAY_ROLLBACK_CORRECTION_TUPLE_LOG");
		sSYNetRollbackCorrectionTupleLogCache =
		    ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
	}
	return (sSYNetRollbackCorrectionTupleLogCache != 0) ? TRUE : FALSE;
}

static const char *syNetRollbackCorrectionSourceName(SYNetRollbackCorrectionMismatchSource source)
{
	switch (source)
	{
	case nSYNetRollbackCorrectionSourceWire:
		return "wire";
	case nSYNetRollbackCorrectionSourceTimelinePlayer:
		return "timeline_player";
	case nSYNetRollbackCorrectionSourceTimelineGlobal:
		return "timeline_global";
	default:
		return "scan";
	}
}

static void syNetRollbackLogCorrectionTupleIfEnabled(s32 hint_player, u32 hint_tick, s32 player, u32 mismatch,
						   u32 load_hint, u32 target,
						   SYNetRollbackCorrectionMismatchSource source)
{
	if (syNetRollbackCorrectionTupleLogEnabled() == FALSE)
	{
		return;
	}
	port_log(
	    "SSB64 NetRollback: CORRECTION_TUPLE hint_player=%d hint_tick=%u player=%d mismatch=%u load_hint=%u target=%u source=%s timeline_earliest=%u last_confirmed=%u\n",
	    (int)hint_player,
	    hint_tick,
	    (int)player,
	    mismatch,
	    load_hint,
	    target,
	    syNetRollbackCorrectionSourceName(source),
	    (player >= 0) ? syNetInputTimelineGetEarliestIncorrectForPlayer(player) : 0U,
	    (player >= 0) ? syNetInputTimelineGetLastRemoteConfirmedSimTick(player) : 0U);
}

static sb32 syNetRollbackComputeInputCorrectionTuple(s32 hint_player, u32 hint_tick, s32 *out_player, u32 *out_mismatch,
						     u32 *out_load, u32 *out_target,
						     SYNetRollbackCorrectionMismatchSource *out_source)
{
	u32 frontier;
	u32 mismatch;
	s32 player;
	SYNetRollbackCorrectionMismatchSource source;
	SYNetInputFrame published;

	if ((out_player == NULL) || (out_mismatch == NULL) || (out_load == NULL) || (out_target == NULL))
	{
		return FALSE;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if (frontier == 0U)
	{
		return FALSE;
	}
	player = hint_player;
	mismatch = ~(u32)0;
	source = nSYNetRollbackCorrectionSourceScan;
	if ((hint_player >= 0) && (hint_tick != 0U) &&
	    (syNetInputGetHistoryFrame(hint_player, hint_tick, &published) != FALSE) &&
	    (syNetRollbackPublishedSimUsedPrediction(&published) != FALSE))
	{
		player = hint_player;
		mismatch = hint_tick;
		source = nSYNetRollbackCorrectionSourceWire;
	}
	if ((mismatch == ~(u32)0) && (hint_player >= 0))
	{
		u32 earliest;

		earliest = syNetInputTimelineGetEarliestIncorrectForPlayer(hint_player);
		if ((earliest != 0U) && (earliest < frontier) &&
		    (syNetRollbackTickHasValueMismatch(earliest, hint_player) != FALSE))
		{
			player = hint_player;
			mismatch = earliest;
			source = nSYNetRollbackCorrectionSourceTimelinePlayer;
		}
	}
	if (mismatch == ~(u32)0)
	{
		s32 global_player;

		global_player = -1;
		mismatch = syNetInputTimelineFindGlobalEarliestIncorrect(frontier, &global_player);
		if ((mismatch != ~(u32)0) && (global_player >= 0) &&
		    (syNetRollbackTickHasValueMismatch(mismatch, global_player) != FALSE))
		{
			player = global_player;
			source = nSYNetRollbackCorrectionSourceTimelineGlobal;
		}
		else
		{
			mismatch = ~(u32)0;
		}
	}
	if (mismatch == ~(u32)0)
	{
		mismatch = syNetRollbackFindEarliestInputMismatch(frontier, &player);
		if (mismatch == ~(u32)0)
		{
			return FALSE;
		}
		source = nSYNetRollbackCorrectionSourceScan;
	}
	*out_player = player;
	*out_mismatch = mismatch;
	*out_load = (mismatch > 0U) ? (mismatch - 1U) : 0U;
	*out_target = frontier;
	if (*out_target <= mismatch)
	{
		*out_target = mismatch + 1U;
	}
	if (syNetRollbackRemoteHumanHasPredictedPublishedInSpan(mismatch, *out_target) != FALSE)
	{
		u32 prediction_window;
		u32 extended_target;

		prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
		if (prediction_window < 4U)
		{
			prediction_window = 4U;
		}
		if (mismatch <= ~(u32)0 - prediction_window)
		{
			extended_target = mismatch + prediction_window;
			if (extended_target > *out_target)
			{
				*out_target = (extended_target <= frontier) ? extended_target : frontier;
			}
		}
	}
	if (out_source != NULL)
	{
		*out_source = source;
	}
	return TRUE;
}

static void syNetRollbackQueueDeferredInputCorrectionEx(s32 player, u32 sim_tick, u32 target_tick_override)
{
	u32 frontier;
	u32 target_tick;
	sb32 from_peer_symmetric;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	from_peer_symmetric = sSYNetRollbackDeferredMismatchFromPeerSymmetric;
	/*
	 * Shared correction frontier: any local GGPO behind resolved_through is clamped to
	 * the frontier (boundary episode / absorb coalesce). Deep FC reanchors bypass this
	 * path (TryCommit + FcStateRecovery). Formerly only shallow (≤ phase_lock) clamps
	 * applied — soak1 642600542 behind=6 > phase_lock=4 opened mismatch=414 < resolved=420.
	 * See docs/bugs/netplay_shared_correction_frontier_2026-07-19.md.
	 */
	if ((from_peer_symmetric == FALSE) && (sSYNetRollbackEpisodeResolvedThrough != 0U) &&
	    (sim_tick < sSYNetRollbackEpisodeResolvedThrough))
	{
		port_log(
		    "SSB64 NetRollback: CORRECTION_CLAMP_RESOLVED player=%d mismatch=%u->%u resolved_through=%u\n",
		    (int)player,
		    sim_tick,
		    sSYNetRollbackEpisodeResolvedThrough,
		    sSYNetRollbackEpisodeResolvedThrough);
		sim_tick = sSYNetRollbackEpisodeResolvedThrough;
	}
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
	else if (sim_tick > frontier)
	{
		u32 live_sim;
		u32 prediction_window;
		u32 wire_tick;

		/*
		 * Wire can arrive for tick T while live_sim is still T-1 (frontier == T). The old
		 * sim_tick >= frontier guard dropped those corrections silently after ep0 — GGPO
		 * logged "queued" but deferred never armed → promote-only drift → FC@480 figh
		 * (soak 1775398700). Early wire lead (T > frontier) used to drop entirely
		 * (soak 1309587627: mismatch=1962 frontier=1961 sim=1960); clamp to live and
		 * widen target so REPLACE still arms deferred through the wire span.
		 * See docs/bugs/netplay_ggpo_early_wire_frontier_drop_map_cam_2026-07-12.md.
		 */
		live_sim = syNetInputGetTick();
		wire_tick = sim_tick;
		prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
		if (prediction_window < 4U)
		{
			prediction_window = 4U;
		}
		if ((live_sim == 0U) || (wire_tick > live_sim + prediction_window))
		{
			syNetRollbackLogQueueDeferredDrop(sim_tick, frontier, player, "queue_past_frontier");
			return;
		}
		if (target_tick_override < (wire_tick + 1U))
		{
			target_tick_override = wire_tick + 1U;
		}
		sim_tick = live_sim;
		port_log(
		    "SSB64 NetRollback: CORRECTION_CLAMP_EARLY_WIRE player=%d wire=%u -> mismatch=%u target_override=%u frontier=%u\n",
		    (int)player,
		    wire_tick,
		    sim_tick,
		    target_tick_override,
		    frontier);
	}
	target_tick = frontier;
	if (target_tick <= sim_tick)
	{
		target_tick = sim_tick + 1U;
	}
	if ((syNetRollbackRemoteHumanHasPredictedPublishedInSpan(sim_tick, target_tick) != FALSE) &&
	    (target_tick_override == 0U))
	{
		u32 prediction_window;
		u32 extended_target;

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
		syNetRollbackDeferredMismatchNoteSlot(player);
		if (sim_tick < sSYNetRollbackDeferredMismatchTick)
		{
#if defined(SSB64_NETMENU)
			/*
			 * Do not pull mismatch below a hold_last watermark already covered by
			 * deferred — MERGE_DEEPEN 740→739 left DEFERRED_KEEP unable to retain the
			 * predicted-replay tick (JumpAerial SoftLip soak 2026-07-27). Still widen
			 * the target so the follow-up episode covers the new wire.
			 */
			{
				u32 pred_watermark;

				pred_watermark = syNetInputResimLowestPredictedReplayTick();
				if ((pred_watermark != 0U) &&
				    (sSYNetRollbackDeferredMismatchTick >= pred_watermark) &&
				    (sim_tick < pred_watermark))
				{
					port_log(
					    "SSB64 NetRollback: CORRECTION_MERGE_DEEPEN_PRED_HOLD player=%d "
					    "keep_mismatch=%u reject_deepen=%u target=%u watermark=%u "
					    "slot_mask=0x%x\n",
					    (int)player,
					    sSYNetRollbackDeferredMismatchTick,
					    sim_tick,
					    (target_tick > sSYNetRollbackDeferredMismatchTargetTick)
						? target_tick
						: sSYNetRollbackDeferredMismatchTargetTick,
					    pred_watermark,
					    (unsigned int)sSYNetRollbackDeferredMismatchSlotMask);
					if (target_tick > sSYNetRollbackDeferredMismatchTargetTick)
					{
						sSYNetRollbackDeferredMismatchTargetTick = target_tick;
					}
					syNetRollbackStickAbsorbClampDeferredTarget();
					syNetRollbackAlignDeferredTargetToWireReady("MERGE_PRED_HOLD");
					return;
				}
			}
#endif
			port_log(
			    "SSB64 NetRollback: CORRECTION_MERGE_DEEPEN player=%d mismatch=%u->%u target=%u "
			    "slot_mask=0x%x\n",
			    (int)player,
			    sSYNetRollbackDeferredMismatchTick,
			    sim_tick,
			    target_tick,
			    (unsigned int)sSYNetRollbackDeferredMismatchSlotMask);
			sSYNetRollbackDeferredMismatchTick = sim_tick;
		}
		if (sim_tick >= sSYNetRollbackDeferredMismatchTick)
		{
			if (target_tick > sSYNetRollbackDeferredMismatchTargetTick)
			{
				sSYNetRollbackDeferredMismatchTargetTick = target_tick;
			}
#if defined(SSB64_NETMENU)
			syNetRollbackStickAbsorbClampDeferredTarget();
			syNetRollbackAlignDeferredTargetToWireReady("MERGE_WIDEN");
#endif
			return;
		}
		if (target_tick > sSYNetRollbackDeferredMismatchTargetTick)
		{
			sSYNetRollbackDeferredMismatchTargetTick = target_tick;
		}
		sSYNetRollbackDeferredMismatchTick = sim_tick;
#if defined(SSB64_NETMENU)
		syNetRollbackStickAbsorbClampDeferredTarget();
		syNetRollbackAlignDeferredTargetToWireReady("MERGE_DEEPEN");
#endif
		return;
	}
	sSYNetRollbackDeferredMismatchPending = TRUE;
	sSYNetRollbackDeferredMismatchTick = sim_tick;
	sSYNetRollbackDeferredMismatchTargetTick = target_tick;
	sSYNetRollbackDeferredMismatchSlotMask = 0U;
	syNetRollbackDeferredMismatchNoteSlot(player);
#if defined(SSB64_NETMENU)
	syNetRollbackStickAbsorbClampDeferredTarget();
	syNetRollbackAlignDeferredTargetToWireReady("QUEUE");
#endif
	if (from_peer_symmetric != FALSE)
	{
		syNetRollbackClearPeerSymmetricRejectLiveCap();
		syNetRollbackLogDeferDiag("symmetric_queue_ok", sim_tick, target_tick, player);
	}
}

sb32 syNetRollbackShouldQueueGgpoCorrection(u32 sim_tick)
{
	/*
	 * Use the episode-window bypass, not bare debounce. After a jump-onset resim,
	 * ArmDebounceAfterResim blocks MismatchAllowed for DebounceFrames (default 3) while
	 * feel-0 send-lead provisionals still REPLACE with the real stick (soak 2089186088:
	 * Linux simmed 394 with provisional sy=71, then REPLACE→sy=55 with 0× GGPO → KneeBend
	 * / Jump phase fork → SYNCTEST_FAIL@394 / PEER_SNAPSHOT_DIVERGE@419).
	 * CorrectionAllowedAtTick still honors debounce when outside [LastCommitted, +phase_lock].
	 */
	return syNetRollbackCorrectionAllowedAtTick(sim_tick);
}

void syNetRollbackRequestInputCorrection(s32 player, u32 sim_tick)
{
	SYNetInputFrame published;
	SYNetInputFrame remote;
	sb32 deferred_force;
	sb32 had_published;
	sb32 had_remote;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	/*
	 * BRANCH_DEFERRED same-stick: owner may have committed Dash while peer stayed Turn on
	 * equal hold_last. Ticket must still force GGPO after ledger StoreFrame / Promote clears
	 * is_predicted and StickReplace sees GameplayEquals — ComputeInputCorrectionTuple also
	 * requires a prediction bit / value mismatch and returns FALSE here.
	 * Soak 1541066087 @575: QueueOrWiden cleared the ticket then Request dropped silently.
	 * See docs/bugs/netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md.
	 */
	deferred_force = syNetplayBranchDeferredNeedsRewind(player, sim_tick);
	/*
	 * Active resim: fold REPLACE into the open episode (target widen / deferred merge).
	 * Open deferred without resim is handled by QueueOrWidenStickCorrection after stick policy.
	 */
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		/*
		 * Fold REPLACE into the open episode, but keep BRANCH_DEFERRED tickets until
		 * deferred_force / StickReplace actually queues GGPO. Clearing here ate same-stick
		 * heals when owner wire landed after the fold (soak 1334008206 @420).
		 */
		syNetRollbackDeferRemoteInputCorrection(player, sim_tick);
		(void)deferred_force;
		return;
	}
	had_published = syNetInputGetHistoryFrame(player, sim_tick, &published);
	had_remote = syNetInputGetRemoteHistoryFrame(player, sim_tick, &remote);
	/*
	 * deferred_force is only for true same-stick confirm after BRANCH_DEFERRED. Stick deltas
	 * (soak 662339918 Linux @527 −77→0, @686 61→46) must use StickReplace / ShouldQueue —
	 * forcing here tagged branch_deferred_same_stick, bypassed debounce, and fed the protocol
	 * scan storm into the baseline-universe kill window.
	 * Missing published/remote: keep the ticket for ledger refresh; do not arm with zeros.
	 * Unequal sticks: drop force but keep the ticket until StickReplace queues or declines
	 * (clearing before ShouldQueue dropped both paths).
	 */
	if ((deferred_force != FALSE) &&
	    ((had_published == FALSE) || (had_remote == FALSE) || (published.buttons != remote.buttons) ||
	     (published.stick_x != remote.stick_x) || (published.stick_y != remote.stick_y)))
	{
		deferred_force = FALSE;
	}
	if ((deferred_force == FALSE) && (syNetRollbackShouldQueueGgpoCorrection(sim_tick) == FALSE))
	{
		return;
	}
	if (deferred_force != FALSE)
	{
		u32 frontier;

		frontier = syNetInputGetTick();
		if (frontier < ~(u32)0)
		{
			frontier++;
		}
		if (frontier <= sim_tick)
		{
			frontier = sim_tick + 1U;
		}
		if (sSYNetRollbackGgpoCorrectionLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: GGPO input correction queued player=%d sim_tick=%u frontier=%u "
			    "published btn=0x%04X sx=%d sy=%d pred=%u | remote btn=0x%04X sx=%d sy=%d "
			    "branch_deferred_same_stick\n",
			    (int)player, sim_tick, frontier,
			    (unsigned int)published.buttons, published.stick_x, published.stick_y,
			    (unsigned int)published.is_predicted, (unsigned int)remote.buttons, remote.stick_x,
			    remote.stick_y);
			sSYNetRollbackGgpoCorrectionLogsRemaining--;
		}
		syNetInputTimelineNotePublishedRemoteMismatch(player, sim_tick);
		if (syNetRollbackEpisodeFsmEnabled() != FALSE)
		{
			SYNetRollbackEpisodeEvent ev;

			memset(&ev, 0, sizeof(ev));
			ev.type = nSYNetRollbackEpisodeEventInputMismatch;
			ev.slot = player;
			ev.mismatch_tick = sim_tick;
			ev.target_tick = frontier;
			syNetRollbackEpisodeEnqueueEvent(&ev);
		}
		syNetRollbackQueueDeferredInputCorrectionEx(player, sim_tick, frontier);
		syNetplayBranchClearDeferred(player, sim_tick);
		return;
	}
	if ((had_published == FALSE) || (had_remote == FALSE))
	{
		return;
	}
	if (syNetRollbackPublishedSimUsedPrediction(&published) == FALSE)
	{
		return;
	}
	if (syNetInputStickReplaceNeedsRewind(player, sim_tick, &published, &remote, &published) == FALSE)
	{
		/*
		 * Stick path declined (micro/absorb). Drop a stale BRANCH_DEFERRED ticket so the
		 * next Request does not keep revisiting the same unequal confirm.
		 */
		if (syNetplayBranchDeferredNeedsRewind(player, sim_tick) != FALSE)
		{
			syNetplayBranchClearDeferred(player, sim_tick);
		}
		return;
	}
	/* Stick REPLACE owns this confirm — ticket no longer needed for same-stick force. */
	if (syNetplayBranchDeferredNeedsRewind(player, sim_tick) != FALSE)
	{
		syNetplayBranchClearDeferred(player, sim_tick);
	}
#if defined(SSB64_NETMENU)
	/*
	 * Invent micro REPLACE: hash_confirm Promote / soft-defer before arming deferred GGPO.
	 * CommitRemoteConfirmedWire queues here before ledger refresh — without this gate,
	 * CORRECTION_TUPLE armed resim even when LEDGER later resolved hash_confirm (soak
	 * 1451059971 @645). See docs/bugs/netplay_hash_confirm_suppress_wire_ggpo_2026-07-26.md.
	 */
	if (syNetInputStickReplaceTryHashConfirmSchedule(player, sim_tick, &published, &remote) != FALSE)
	{
		return;
	}
#endif
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
		s32 corr_player;
		u32 corr_mismatch;
		u32 corr_load;
		u32 corr_target;
		SYNetRollbackCorrectionMismatchSource corr_source;

		if (syNetRollbackComputeInputCorrectionTuple(player, sim_tick, &corr_player, &corr_mismatch, &corr_load,
							     &corr_target, &corr_source) == FALSE)
		{
			return;
		}
		syNetRollbackLogCorrectionTupleIfEnabled(player, sim_tick, corr_player, corr_mismatch, corr_load,
							 corr_target, corr_source);
		memset(&ev, 0, sizeof(ev));
		ev.type = nSYNetRollbackEpisodeEventInputMismatch;
		ev.slot = corr_player;
		ev.mismatch_tick = corr_mismatch;
		ev.target_tick = corr_target;
		syNetRollbackEpisodeEnqueueEvent(&ev);
		/*
		 * Arm deferred resim before CommitRemoteConfirmedWire Promote clears is_predicted.
		 * Drain alone re-runs ComputeInputCorrectionTuple after Promote and often returns FALSE
		 * (published already matches wire) — soak 1799351904 queued 12× GGPO with 0× BeginResim
		 * before FC@480 inputs=MATCH JumpF phase skew. See docs/bugs/netplay_episode_fsm_ggpo_drop_after_promote_2026-07-12.md.
		 */
		syNetRollbackQueueDeferredInputCorrectionEx(corr_player, corr_mismatch, corr_target);
		return;
	}
	{
		s32 corr_player;
		u32 corr_mismatch;
		u32 corr_load;
		u32 corr_target;
		SYNetRollbackCorrectionMismatchSource corr_source;

		if (syNetRollbackComputeInputCorrectionTuple(player, sim_tick, &corr_player, &corr_mismatch, &corr_load,
							     &corr_target, &corr_source) == FALSE)
		{
			return;
		}
		syNetRollbackLogCorrectionTupleIfEnabled(player, sim_tick, corr_player, corr_mismatch, corr_load,
							 corr_target, corr_source);
		syNetRollbackQueueDeferredInputCorrectionEx(corr_player, corr_mismatch, corr_target);
	}
}

void syNetRollbackQueueOrWidenStickCorrection(s32 player, u32 sim_tick)
{
	sb32 deferred_ticket;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	/*
	 * Do not clear BRANCH_DEFERRED tickets before RequestInputCorrection — that re-check
	 * uses DeferredNeedsRewind for same-stick predicted→wire, and clearing first made
	 * StickReplaceNeedsRewind return FALSE (soak 1541066087 @575). Ticket is consumed inside
	 * Request on the deferred_force path, or here when folded into an open episode/absorb.
	 */
	deferred_ticket = syNetplayBranchDeferredNeedsRewind(player, sim_tick);
#if defined(SSB64_NETMENU)
	/*
	 * Hash-confirm / soft-defer before fold-into-open-deferred. Otherwise a prior invent
	 * QueueOrWiden that lost the race still widens when ledger later wants Promote-only.
	 */
	{
		SYNetInputFrame published;
		SYNetInputFrame remote;

		if ((syNetInputGetHistoryFrame(player, sim_tick, &published) != FALSE) &&
		    (syNetInputGetRemoteHistoryFrame(player, sim_tick, &remote) != FALSE) &&
		    (syNetInputStickReplaceTryHashConfirmSchedule(player, sim_tick, &published, &remote) != FALSE))
		{
			if (deferred_ticket != FALSE)
			{
				syNetplayBranchClearDeferred(player, sim_tick);
			}
			return;
		}
	}
#endif
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE) ||
	    (sSYNetRollbackDeferredMismatchPending != FALSE))
	{
		/*
		 * Keep BRANCH_DEFERRED tickets across resim/pending fold — same-stick force still
		 * needs DeferredNeedsRewind after ledger refresh (soak 1334008206 @420).
		 * Jibaku stick absorb retired — see netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md.
		 */
		syNetRollbackDeferRemoteInputCorrection(player, sim_tick);
		(void)deferred_ticket;
		return;
	}
	/*
	 * Post-episode absorb: continuous stick REPLACE must not open a fresh initiator episode
	 * (CORRECTION_CLAMP_RESOLVED → ep every phase_lock ticks → follower seal join hang).
	 * Coalesce via deferred (arms once, subsequent REPLACE merge). Do not silent-return:
	 * write-once may freeze published while wire updates — skipping GGPO forks figh
	 * (soak 1468769950). See docs/bugs/netplay_stick_up_boundary_seal_join_hang_2026-07-12.md
	 * and docs/bugs/netplay_confirmed_publish_write_once_2026-07-12.md.
	 */
	if ((sSYNetRollbackStickAbsorbUntilSim != 0U) && (syNetInputGetTick() <= sSYNetRollbackStickAbsorbUntilSim) &&
	    ((sSYNetRollbackStickAbsorbPlayer < 0) || (sSYNetRollbackStickAbsorbPlayer == player)))
	{
#if defined(SSB64_NETMENU)
		/*
		 * Hard polarity NoteHard exempts one slot so its Begin is not stuck behind
		 * dual-slot absorb; other slots still coalesce. See
		 * docs/bugs/netplay_stick_absorb_dual_hot_notehard_2026-07-27.md.
		 */
		if ((sSYNetRollbackStickAbsorbExemptPlayer < 0) ||
		    (sSYNetRollbackStickAbsorbExemptPlayer != player))
#endif
		{
#if defined(SSB64_NETMENU)
			syNetRollbackStickAbsorbMaybeStickyRefresh();
#endif
			syNetRollbackQueueDeferredInputCorrection(player, sim_tick);
#if defined(SSB64_NETMENU)
			syNetRollbackStickAbsorbClampDeferredTarget();
#endif
			if (deferred_ticket != FALSE)
			{
				syNetplayBranchClearDeferred(player, sim_tick);
			}
			return;
		}
	}
	syNetRollbackRequestInputCorrection(player, sim_tick);
}

void syNetRollbackDeferRemoteInputCorrection(s32 player, u32 sim_tick)
{
	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	/*
	 * Open deferred window (no active resim): stick-storm coalesce — merge + widen target only.
	 * Avoids ep2/ep3/ep4 for continuous analog within the same prediction window.
	 */
	if ((sSYNetRollbackDeferredMismatchPending != FALSE) && (sSYNetRollbackResimPending == FALSE) &&
	    (syNetRollbackIsResimulating() == FALSE))
	{
		syNetRollbackQueueDeferredInputCorrection(player, sim_tick);
		return;
	}
	/*
	 * Must arm while actively replaying too — ResimPending is cleared when the load/replay
	 * gate opens, so a mid-resim late wire used to hit `ResimPending==FALSE` and silently
	 * drop (promote-only history) while hold_last ticks stayed wrong. Light-ep JumpAerial
	 * SoftLip soak 2026-07-27 @740: wire (-19,31) during hold_last(-7,13) replay.
	 */
	if ((sSYNetRollbackResimPending == FALSE) && (syNetRollbackIsResimulating() == FALSE))
	{
		return;
	}
	if (sim_tick < sSYNetRollbackResimMismatchTick)
	{
		return;
	}
	/*
	 * Stick REPLACE at/after the open episode's target used to drop here, then re-queue
	 * GGPO after resim complete with mismatch often behind resolved_through. Fold into
	 * deferred target extension so the next episode (if any) starts at/after the sealed
	 * frontier after CORRECTION_CLAMP_RESOLVED.
	 * See docs/bugs/netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md.
	 */
	if (sim_tick >= sSYNetRollbackResimTargetTick)
	{
		u32 new_target;

		new_target = sim_tick + 1U;
		if (new_target > sSYNetRollbackResimTargetTick)
		{
			syNetRollbackQueueDeferredInputCorrectionEx(player, sSYNetRollbackResimMismatchTick, new_target);
		}
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

static void syNetRollbackClearLastPeerOutcome(void)
{
	sSYNetRollbackLastPeerOutcomeValid = FALSE;
	sSYNetRollbackLastPeerOutcomeTick = ~(u32)0;
	sSYNetRollbackLastPeerOutcomeFighterSlotsValid = FALSE;
	sSYNetRollbackLastPeerOutcomeEffectValid = FALSE;
}

static sb32 syNetRollbackOutcomeCorrectionSuppressedDuringEpisode(u32 probe_frontier)
{
	u32 prediction_window;
	u32 suppress_until;

	prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (prediction_window < 4U)
	{
		prediction_window = 4U;
	}
	if (sSYNetRollbackEpisodeResolvedThrough != 0U)
	{
		if (sSYNetRollbackEpisodeResolvedThrough > (~(u32)0) - (prediction_window * 2U))
		{
			suppress_until = ~(u32)0;
		}
		else
		{
			suppress_until = sSYNetRollbackEpisodeResolvedThrough + (prediction_window * 2U);
		}
		if (probe_frontier <= suppress_until)
		{
			return TRUE;
		}
	}
	if (sSYNetRollbackEpisodeAnchorMismatch == ~(u32)0)
	{
		return FALSE;
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
	if ((sSYNetRollbackLastCommittedMismatchTick != ~(u32)0) &&
	    (sSYNetRollbackLastPeerOutcomeTick < sSYNetRollbackLastCommittedMismatchTick))
	{
		syNetRollbackClearLastPeerOutcome();
		return;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) &&
	    (sSYNetRollbackLastPeerOutcomeTick < sSYNetRollbackEpisodeResolvedThrough))
	{
		syNetRollbackClearLastPeerOutcome();
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
		player = syNetRollbackResolveRemoteHumanPlayer(-1);
		if (player < 0)
		{
			return;
		}
	}
	target_tick = probe_frontier;
	if (target_tick <= mismatch)
	{
		target_tick = mismatch + 1U;
	}
	if ((sSYNetRollbackLastCommittedMismatchTick != ~(u32)0) && (mismatch < sSYNetRollbackLastCommittedMismatchTick))
	{
		syNetRollbackClearLastPeerOutcome();
		return;
	}
	if (syNetRollbackCorrectionAllowedAtTick(mismatch) == FALSE)
	{
		syNetRollbackClearLastPeerOutcome();
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
	u32 load_hint;
	u32 prediction_window;
	s32 player;
	SYNetRollbackCorrectionMismatchSource corr_source;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if (syNetRollbackComputeInputCorrectionTuple(-1, 0U, &player, &mismatch, &load_hint, &target_tick, &corr_source) ==
	    FALSE)
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
		player = syNetRollbackResolveRemoteHumanPlayer(sSYNetRollbackDeferredMismatchPlayer);
	}
	if (player < 0)
	{
		return;
	}
	if (target_tick < sSYNetRollbackResimTargetTick)
	{
		target_tick = sSYNetRollbackResimTargetTick;
	}
	if (syNetRollbackTryCommitCorrectionBegin(mismatch, mismatch - 1U, target_tick, NULL) == FALSE)
	{
		return;
	}
	syNetRollbackLogCorrectionTupleIfEnabled(-1, 0U, player, mismatch, load_hint, target_tick, corr_source);
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
	syNetRollbackConsumePendingForceMismatchAfterResim(sSYNetRollbackResimMismatchTick,
	                                                 sSYNetRollbackResimTargetTick);
	/*
	 * Only the FC recovery episode itself may clear the arm. A concurrent light GGPO that
	 * finishes past FcStateRecoveryTarget (common while deferred FC waits on fc_resim_busy)
	 * used to clear the flag here — BeginResim then classified the deferred FC as light,
	 * LIGHT_WIRE_READY_CLAMP shrunk the target, and seal tuples forked vs the peer still on
	 * heavy FC (soak seed 2117792121: Android light 574→576 cleared arm for FC 561→573).
	 * DeferredStateMismatchPending means FC has not BeginResim'd yet — keep the arm.
	 * See docs/bugs/netplay_fc_recovery_light_demote_seal_skew_2026-07-28.md.
	 */
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchPending == FALSE) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick != ~(u32)0) &&
	    (sSYNetRollbackResimTargetTick >= sSYNetRollbackFcStateRecoveryTargetTick) &&
	    (sSYNetRollbackResimMismatchTick != ~(u32)0) &&
	    (sSYNetRollbackResimMismatchTick >= sSYNetRollbackFcStateRecoveryMismatchTick))
	{
		syNetRollbackClearFcStateRecovery();
		syNetRollbackClearFcDeepenGuard();
	}
	syNetRollbackNoteEpisodeResimCompleted();
	syNetRollbackClearLastPeerOutcome();
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
#if defined(SSB64_NETMENU)
	if ((sSYNetRollbackResimTargetTick != ~(u32)0) && (sSYNetRollbackResimTargetTick != 0U))
	{
		syNetRbSnapshotRefreshIntroPresentationAfterResimComplete(sSYNetRollbackResimTargetTick);
	}
#endif
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

static u32 syNetRollbackDeferredMismatchSlotCount(void)
{
	u32 mask;
	u32 count;

	mask = sSYNetRollbackDeferredMismatchSlotMask;
	count = 0U;
	while (mask != 0U)
	{
		count += mask & 1U;
		mask >>= 1;
	}
	return count;
}

static void syNetRollbackDeferredMismatchSyncPlayerFromMask(void)
{
	u32 mask;
	s32 slot;
	s32 found;

	mask = sSYNetRollbackDeferredMismatchSlotMask;
	if (mask == 0U)
	{
		sSYNetRollbackDeferredMismatchPlayer = -1;
		return;
	}
	if (syNetRollbackDeferredMismatchSlotCount() > 1U)
	{
		/* Union episode: reconcile all remotes. */
		sSYNetRollbackDeferredMismatchPlayer = -1;
		return;
	}
	found = -1;
	for (slot = 0; slot < MAXCONTROLLERS; slot++)
	{
		if ((mask & (1U << slot)) != 0U)
		{
			found = slot;
			break;
		}
	}
	sSYNetRollbackDeferredMismatchPlayer = found;
}

static void syNetRollbackDeferredMismatchNoteSlot(s32 player)
{
	if ((player >= 0) && (player < MAXCONTROLLERS))
	{
		sSYNetRollbackDeferredMismatchSlotMask |= (1U << player);
	}
	syNetRollbackDeferredMismatchSyncPlayerFromMask();
}

static void syNetRollbackClearDeferredInputMismatch(void)
{
	sSYNetRollbackDeferredMismatchPending = FALSE;
	sSYNetRollbackDeferredMismatchTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchTargetTick = ~(u32)0;
	sSYNetRollbackDeferredMismatchPlayer = -1;
	sSYNetRollbackDeferredMismatchSlotMask = 0U;
	sSYNetRollbackDeferredMismatchFromPeerSymmetric = FALSE;
#if defined(SSB64_NETMENU)
	sSYNetRollbackLightWireCoalesceWaits = 0U;
#endif
	syNetRollbackClearPeerSymmetricRejectLiveCap();
}

#if defined(SSB64_NETMENU)
void syNetRollbackCancelDeferredInputCorrectionIfMismatchTick(s32 player, u32 sim_tick)
{
	static u32 sCancelLogsRemaining = 16U;

	if ((sim_tick == 0U) || (sSYNetRollbackDeferredMismatchPending == FALSE))
	{
		return;
	}
	if (sSYNetRollbackDeferredMismatchTick != sim_tick)
	{
		return;
	}
	if ((player >= 0) && (player < MAXCONTROLLERS))
	{
		if ((sSYNetRollbackDeferredMismatchSlotMask & (1U << player)) == 0U)
		{
			return;
		}
		sSYNetRollbackDeferredMismatchSlotMask &= ~(1U << player);
		if (sSYNetRollbackDeferredMismatchSlotMask == 0U)
		{
			if (sCancelLogsRemaining > 0U)
			{
				port_log(
				    "SSB64 NetRollback: HASH_CONFIRM_CANCEL_DEFERRED player=%d mismatch_tick=%u "
				    "target_tick=%u (union empty)\n",
				    (int)player, (unsigned int)sim_tick,
				    (unsigned int)sSYNetRollbackDeferredMismatchTargetTick);
				sCancelLogsRemaining--;
			}
			syNetRollbackClearDeferredInputMismatch();
			return;
		}
		syNetRollbackDeferredMismatchSyncPlayerFromMask();
		if (sCancelLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: HASH_CONFIRM_CANCEL_DEFERRED player=%d mismatch_tick=%u "
			    "target_tick=%u remain_mask=0x%x\n",
			    (int)player, (unsigned int)sim_tick,
			    (unsigned int)sSYNetRollbackDeferredMismatchTargetTick,
			    (unsigned int)sSYNetRollbackDeferredMismatchSlotMask);
			sCancelLogsRemaining--;
		}
		return;
	}
	if (sCancelLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: HASH_CONFIRM_CANCEL_DEFERRED player=%d mismatch_tick=%u "
		    "target_tick=%u prior_slot=%d slot_mask=0x%x\n",
		    (int)player, (unsigned int)sim_tick,
		    (unsigned int)sSYNetRollbackDeferredMismatchTargetTick,
		    (int)sSYNetRollbackDeferredMismatchPlayer,
		    (unsigned int)sSYNetRollbackDeferredMismatchSlotMask);
		sCancelLogsRemaining--;
	}
	syNetRollbackClearDeferredInputMismatch();
}

void syNetRollbackForceDeferredInputCorrection(s32 player, u32 sim_tick, const char *reason)
{
	u32 frontier;
	static u32 sForceLogsRemaining = 16U;

	if ((syNetRollbackIsActive() == FALSE) || (sim_tick == 0U))
	{
		return;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	/*
	 * Open resim / pending deferred: fold like QueueOrWiden — do not drop the heal.
	 * StickReplace cannot arm after ledger StoreFrame cleared is_predicted.
	 */
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE) ||
	    (sSYNetRollbackDeferredMismatchPending != FALSE))
	{
		syNetRollbackDeferRemoteInputCorrection(player, sim_tick);
		if (sForceLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetRollback: HASH_CONFIRM_FORCE_GGPO player=%d sim_tick=%u "
			    "reason=%s path=fold_open\n",
			    (int)player, (unsigned int)sim_tick, (reason != NULL) ? reason : "?");
			sForceLogsRemaining--;
		}
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
	if (sForceLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: HASH_CONFIRM_FORCE_GGPO player=%d sim_tick=%u frontier=%u "
		    "reason=%s path=queue\n",
		    (int)player, (unsigned int)sim_tick, (unsigned int)frontier,
		    (reason != NULL) ? reason : "?");
		sForceLogsRemaining--;
	}
	syNetInputTimelineNotePublishedRemoteMismatch(player, sim_tick);
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		SYNetRollbackEpisodeEvent ev;

		memset(&ev, 0, sizeof(ev));
		ev.type = nSYNetRollbackEpisodeEventInputMismatch;
		ev.slot = player;
		ev.mismatch_tick = sim_tick;
		ev.target_tick = frontier;
		syNetRollbackEpisodeEnqueueEvent(&ev);
	}
	syNetRollbackQueueDeferredInputCorrectionEx(player, sim_tick, frontier);
}
#endif

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
#if defined(SSB64_NETMENU)
sb32 syNetRollbackStickAbsorbWindowActive(void)
{
	if (sSYNetRollbackStickAbsorbUntilSim == 0U)
	{
		return FALSE;
	}
	if (syNetInputGetTick() > sSYNetRollbackStickAbsorbUntilSim)
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetRollbackStickAbsorbCoalesceWaiting(void)
{
	if (syNetRollbackStickAbsorbWindowActive() == FALSE)
	{
		return FALSE;
	}
	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return FALSE;
	}
	if (sSYNetRollbackDeferredMismatchFromPeerSymmetric != FALSE)
	{
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Light episodes: absorb still arms (peer-cap / epoch ignore) but never delays Begin.
	 * Coalesce wait let SoftLip run on Wait geometry while the owner already Dashed
	 * (soak 2026-07-27: mismatch=648 deferred ~3t behind absorb → status=10 SoftLip vs
	 * peer status=18 → figh briefly healed @655 then permanent cam/pos fork → FC@961
	 * replay_determinism with inputs agreeing). Same-burst REPLACE still merges via
	 * deferred Queue while pending / open resim. See
	 * docs/bugs/netplay_light_absorb_coalesce_onset_delay_2026-07-27.md.
	 */
	if (syNetRollbackLightInputEpisodesEnabled() != FALSE)
	{
		return FALSE;
	}
#endif
	if ((sSYNetRollbackDeferredMismatchTick == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick <= sSYNetRollbackDeferredMismatchTick))
	{
		return FALSE;
	}
	/*
	 * Single-slot hard polarity: exempt may Begin while absorb blocks others.
	 * Multi-slot union stays coalesced (no per-slot Begin ping-pong).
	 */
	if ((sSYNetRollbackStickAbsorbExemptPlayer >= 0) &&
	    (syNetRollbackDeferredMismatchSlotCount() <= 1U) &&
	    (sSYNetRollbackDeferredMismatchPlayer == sSYNetRollbackStickAbsorbExemptPlayer))
	{
		return FALSE;
	}
	return TRUE;
}

/*
 * Max deferred target while absorb coalesce waits: mismatch + absorb window.
 * Prevents frontier-tracking fat spans (soak Linux 715→724 during coalesce).
 */
static u32 syNetRollbackStickAbsorbDeferredTargetCap(void)
{
	u32 mismatch;
	u32 window;

	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return ~(u32)0;
	}
	mismatch = sSYNetRollbackDeferredMismatchTick;
	if ((mismatch == 0U) || (mismatch == ~(u32)0))
	{
		return ~(u32)0;
	}
	window = syNetRollbackMultiStickAbsorbWindow(syNetInputGetTick());
	if (window < 4U)
	{
		window = 4U;
	}
	if (mismatch > (~(u32)0 - window))
	{
		return ~(u32)0;
	}
	return mismatch + window;
}

void syNetRollbackStickAbsorbNoteHardCorrection(s32 player)
{
	if (player < 0)
	{
		return;
	}
	if ((sSYNetRollbackStickAbsorbUntilSim == 0U) ||
	    (syNetInputGetTick() > sSYNetRollbackStickAbsorbUntilSim))
	{
		return;
	}
	/*
	 * Do not clear global absorb (that reopened cross-slot Begin ping-pong). Exempt
	 * this slot only so polarity/button Begin can arm; other remotes stay coalesced.
	 */
	sSYNetRollbackStickAbsorbExemptPlayer = player;
}

#if defined(SSB64_NETMENU)
/* Default ON — kill with SSB64_NETPLAY_ROLLBACK_LIGHT_INPUT_EPISODES=0. */
static sb32 syNetRollbackLightInputEpisodesEnabled(void)
{
	const char *env;

	if (sSYNetRollbackLightInputEpisodesEnvCache == -999)
	{
		sSYNetRollbackLightInputEpisodesEnvCache = 1;
		env = getenv("SSB64_NETPLAY_ROLLBACK_LIGHT_INPUT_EPISODES");
		if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
		{
			sSYNetRollbackLightInputEpisodesEnvCache = 0;
		}
	}
	return (sSYNetRollbackLightInputEpisodesEnvCache != 0) ? TRUE : FALSE;
}

/*
 * Exclusive resim target covering only contiguous remote-confirmed ticks from mismatch.
 * Multi-slot deferred: MIN across dirty remotes (union Begin needs every slot wired).
 * Returns mismatch+1 when no wire yet (minimal span; caller may still hold).
 */
static u32 syNetRollbackLightWireReadyTargetExclusive(u32 mismatch_tick, s32 player)
{
	u32 scan_cap;
	u32 live;
	u32 through;
	u32 ready;
	s32 slot;
	sb32 any;

	if ((mismatch_tick == 0U) || (mismatch_tick == ~(u32)0))
	{
		return 0U;
	}
	live = syNetInputGetTick();
	scan_cap = live + SYNETROLLBACK_LIGHT_WIRE_READY_SCAN_SLACK;
	if (scan_cap < mismatch_tick)
	{
		scan_cap = mismatch_tick;
	}
	if (player >= 0)
	{
		through = syNetInputContiguousRemoteConfirmedThrough(player, mismatch_tick, scan_cap);
		if (through >= mismatch_tick)
		{
			return through + 1U;
		}
		return mismatch_tick + 1U;
	}
	ready = ~(u32)0;
	any = FALSE;
	for (slot = 0; slot < GMCOMMON_PLAYERS_MAX; slot++)
	{
		if ((sSYNetRollbackDeferredMismatchSlotMask & (1U << slot)) == 0U)
		{
			continue;
		}
		if (syNetInputIsRemoteHumanSlot(slot) == FALSE)
		{
			continue;
		}
		through = syNetInputContiguousRemoteConfirmedThrough(slot, mismatch_tick, scan_cap);
		if (through < mismatch_tick)
		{
			return mismatch_tick + 1U;
		}
		if ((through + 1U) < ready)
		{
			ready = through + 1U;
		}
		any = TRUE;
	}
	if (any == FALSE)
	{
		return mismatch_tick + 1U;
	}
	return ready;
}

static void syNetRollbackAlignTargetToWireReady(u32 mismatch_tick, s32 player, u32 *inout_target,
					       const char *tag)
{
	u32 ready;
	u32 before;

	if ((inout_target == NULL) || (syNetRollbackLightInputEpisodesEnabled() == FALSE))
	{
		return;
	}
	if ((mismatch_tick == 0U) || (mismatch_tick == ~(u32)0) || (*inout_target <= mismatch_tick))
	{
		return;
	}
	ready = syNetRollbackLightWireReadyTargetExclusive(mismatch_tick, player);
	if ((ready == 0U) || (ready <= mismatch_tick))
	{
		return;
	}
	before = *inout_target;
	if (before == ready)
	{
		return;
	}
	*inout_target = ready;
	port_log(
	    "SSB64 NetRollback: LIGHT_WIRE_READY_%s mismatch=%u target=%u->%u player=%d reason=%s "
	    "(contiguous remote-confirmed exclusive end)\n",
	    (before > ready) ? "CLAMP" : "WIDEN",
	    mismatch_tick,
	    before,
	    ready,
	    (int)player,
	    (tag != NULL) ? tag : "align");
}

static void syNetRollbackAlignDeferredTargetToWireReady(const char *tag)
{
	s32 player;

	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return;
	}
	player = sSYNetRollbackDeferredMismatchPlayer;
	if (syNetRollbackDeferredMismatchSlotCount() > 1U)
	{
		player = -1;
	}
	syNetRollbackAlignTargetToWireReady(sSYNetRollbackDeferredMismatchTick, player,
					    &sSYNetRollbackDeferredMismatchTargetTick, tag);
}
#endif

static sb32 syNetRollbackLightEpisodeIsActive(void)
{
#if defined(SSB64_NETMENU)
	return sSYNetRollbackLightEpisodeActive;
#else
	return FALSE;
#endif
}

/*
 * Multi-stick absorb length from hot remote pressure (scales to N remotes).
 * pressure = hot_remote_count, floored to 2 when MultiStickHot (local + ≥1 remote).
 */
static u32 syNetRollbackMultiStickAbsorbWindow(u32 sim_tick)
{
	u32 base;
	u32 pressure;
	u32 window;

	base = syNetPeerGetPhaseLockPredictionWindowTicks();
	if (base < 4U)
	{
		base = 4U;
	}
	if (base > 8U)
	{
		base = 8U;
	}
	pressure = syNetInputHotRemoteHumanSlotCount(sim_tick);
	if ((pressure < 2U) && (syNetInputMultiStickHotActive(sim_tick) != FALSE))
	{
		pressure = 2U;
	}
	if (pressure < 2U)
	{
		return base;
	}
	window = base + (SYNETROLLBACK_MULTI_STICK_ABSORB_K * (pressure - 1U));
	if (window > SYNETROLLBACK_MULTI_STICK_ABSORB_MAX)
	{
		window = SYNETROLLBACK_MULTI_STICK_ABSORB_MAX;
	}
	return window;
}

/*
 * While multi-stick pressure stays hot, push absorb expiry forward (capped) so REPLACE
 * storms do not Begin every phase_lock ticks. Cap from arm tick avoids indefinite defer.
 */
static void syNetRollbackStickAbsorbMaybeStickyRefresh(void)
{
	u32 now;
	u32 ext;
	u32 cap;
	u32 window;

	if ((sSYNetRollbackStickAbsorbUntilSim == 0U) || (sSYNetRollbackStickAbsorbArmedFromSim == 0U))
	{
		return;
	}
	/* Light input episodes: merge-only absorb — never extend the window. */
	if (syNetRollbackLightInputEpisodesEnabled() != FALSE)
	{
		return;
	}
	now = syNetInputGetTick();
	if (now > sSYNetRollbackStickAbsorbUntilSim)
	{
		return;
	}
	if (syNetInputMultiStickHotActive(now) == FALSE)
	{
		return;
	}
	window = syNetRollbackMultiStickAbsorbWindow(now);
	ext = now + window;
	cap = sSYNetRollbackStickAbsorbArmedFromSim + SYNETROLLBACK_MULTI_STICK_ABSORB_CAP;
	if (ext > cap)
	{
		ext = cap;
	}
	if (ext > sSYNetRollbackStickAbsorbUntilSim)
	{
		sSYNetRollbackStickAbsorbUntilSim = ext;
	}
}

/*
 * Bound deferred target growth under absorb coalesce so REPLACE storms do not revive
 * frontier-tracking fat spans (Fix2 metronome / soak 715→724). Always clamp while
 * coalesce waits — not only when MultiStickHot (that gate missed the hang soak).
 */
static void syNetRollbackStickAbsorbClampDeferredTarget(void)
{
	u32 max_target;

	if (syNetRollbackStickAbsorbCoalesceWaiting() == FALSE)
	{
		return;
	}
	max_target = syNetRollbackStickAbsorbDeferredTargetCap();
	if ((max_target == ~(u32)0) ||
	    (sSYNetRollbackDeferredMismatchTargetTick <= max_target))
	{
		return;
	}
	sSYNetRollbackDeferredMismatchTargetTick = max_target;
}
#endif

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
#if defined(SSB64_NETMENU)
	/*
	 * Stick-absorb coalesce: REPLACE widens deferred while Begin waits the absorb window.
	 * Cap at mismatch-1 would freeze sim before StickAbsorbUntil → classic live-cap hang
	 * (netplay_stick_storm_cooldown_livecap_deadlock). Peer-sym deferred still caps.
	 * See docs/bugs/netplay_dual_stick_ggpo_resim_storm_2026-07-27.md.
	 */
	if (syNetRollbackStickAbsorbCoalesceWaiting() != FALSE)
	{
		return FALSE;
	}
#endif
	/*
	 * Ness jibaku TryBegin defer retired (soak 41294254): lifting live-cap while Begin was
	 * blocked grew span-50+ predict windows and late resim failed jibaku. Begin proceeds
	 * immediately; live-cap at mismatch-1 forces the short confirmed-input burst.
	 * Jibaku stick absorb retired — stick REPLACE uses portable frame-delta GGPO.
	 * See docs/bugs/netplay_ness_pk_defer_retire_input_resim_2026-07-26.md.
	 */
	*out_cap = (sSYNetRollbackDeferredMismatchTick > 0U) ? (sSYNetRollbackDeferredMismatchTick - 1U) : 0U;
	return TRUE;
}

sb32 syNetRollbackDeferredInputCorrectionCoversTick(u32 tick)
{
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
	if (tick < sSYNetRollbackDeferredMismatchTick)
	{
		return FALSE;
	}
	if (tick >= sSYNetRollbackDeferredMismatchTargetTick)
	{
		return FALSE;
	}
	return TRUE;
}

#if defined(SSB64_NETMENU)
sb32 syNetRollbackDeferredInputCorrectionCoversPlayerTick(s32 player, u32 tick)
{
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return FALSE;
	}
	if (sSYNetRollbackDeferredMismatchSlotMask != 0U)
	{
		if ((sSYNetRollbackDeferredMismatchSlotMask & (1U << player)) == 0U)
		{
			return FALSE;
		}
	}
	else if ((sSYNetRollbackDeferredMismatchPlayer >= 0) &&
		 (sSYNetRollbackDeferredMismatchPlayer != player))
	{
		return FALSE;
	}
	return syNetRollbackDeferredInputCorrectionCoversTick(tick);
}
#endif

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

/* Abandon GGPO deferred work that can never run (stale mismatch vs last commit / resolved episode). */
static sb32 syNetRollbackGgpoDeferredShouldAbandon(u32 mismatch, u32 target)
{
	u32 sim_tick;

	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch < sSYNetRollbackEpisodeResolvedThrough))
	{
		return TRUE;
	}
	if ((sSYNetRollbackLastCommittedMismatchTick != ~(u32)0) && (mismatch < sSYNetRollbackLastCommittedMismatchTick))
	{
		return TRUE;
	}
	if (syNetRollbackSymmetricDeferredCommitRetriable(mismatch, target) == FALSE)
	{
		return TRUE;
	}
	/*
	 * Stick-storm: deferred live-cap holds sim at mismatch while TryCommit rejects target that
	 * does not extend LastTarget (or max extensions). Without abandon, epoch_hold spins until quit.
	 */
	sim_tick = syNetInputGetTick();
	if ((sSYNetRollbackEpisodeLastTargetTick != 0U) && (target <= sSYNetRollbackEpisodeLastTargetTick) &&
	    (sim_tick >= mismatch))
	{
		return TRUE;
	}
	return FALSE;
}

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * FC vs peer-sym overlap defer retired. Active resim already serializes via fc_resim_busy;
 * forever-hold of FC Begin while peer-sym deferred GGPO overlaps grew stalls. Peer priority
 * is "TryBegin peer-sym first" in TryBeginDeferredStateMismatch, not fc_symmetric_defers.
 * See docs/bugs/netplay_fc_episode_begin_stall_retire_2026-07-26.md.
 */
static sb32 syNetRollbackDeferFrameCommitForSymmetric(u32 fc_mismatch)
{
	(void)fc_mismatch;
	return FALSE;
}
#else
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
#endif

#ifdef PORT
#if defined(SSB64_NETMENU)
/*
 * Pause/Unpause TryBegin defer retired (same policy as ness_pk_defer). Confirmed-input
 * GGPO/FC must Begin immediately; live-cap stays at mismatch-1. Pause hashed-world couple
 * is a resim-quality problem if it fails — not a reason to grow predict while Begin is
 * blocked. See docs/bugs/netplay_pause_defer_retire_input_resim_2026-07-26.md.
 */
static sb32 syNetRollbackDeferResimForPauseTransition(void)
{
	return FALSE;
}

/*
 * Ness PK TryBegin defer retired. Policy is input-contract + immediate confirmed-input burst
 * resim (including mid-jibaku when a real Hold-aim REPLACE lands late). Volatile stick noise
 * (jibaku stick absorb also retired — portable GGPO stick). See
 * docs/bugs/netplay_ness_pk_defer_retire_input_resim_2026-07-26.md and
 * docs/bugs/netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md.
 */
static sb32 syNetRollbackDeferResimForNessPKThunder(void)
{
	return FALSE;
}

static sb32 syNetRollbackDeferFcStateRecoveryForNessPKThunder(void)
{
	return FALSE;
}
#else
/* Offline PORT: Pause/Unpause mutates hashed world; defer until both peers return to Go. */
static sb32 syNetRollbackDeferResimForPauseTransition(void)
{
	if (gSCManagerBattleState == NULL)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState->game_status == nSCBattleGameStatusPause) ||
	    (gSCManagerBattleState->game_status == nSCBattleGameStatusUnpause))
	{
		return TRUE;
	}
	return FALSE;
}
#endif
#endif

static sb32 syNetRollbackTryBeginDeferredMismatch(void)
{
	u32 mismatch;
	u32 target;
	s32 player;

	if (sSYNetRollbackDeferredMismatchPending == FALSE)
	{
		return FALSE;
	}
#ifdef PORT
	if (syNetRollbackDeferResimForPauseTransition() != FALSE)
	{
		syNetRollbackLogTryBeginFail("pause_defer", sSYNetRollbackDeferredMismatchTick,
					     sSYNetRollbackDeferredMismatchTargetTick,
					     sSYNetRollbackDeferredMismatchPlayer);
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	if (syNetRollbackDeferResimForNessPKThunder() != FALSE)
	{
		syNetRollbackLogTryBeginFail("ness_pk_defer", sSYNetRollbackDeferredMismatchTick,
					     sSYNetRollbackDeferredMismatchTargetTick,
					     sSYNetRollbackDeferredMismatchPlayer);
		return FALSE;
	}
#endif
#endif
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Dual-stick storm: coalesce REPLACE into deferred across the absorb window; one Begin
	 * when the window expires (widened target). Peer-sym still Begins immediately.
	 * Live-cap lifted while waiting — see DeferredCorrectionBlocksLiveAdvance /
	 * StickAbsorbCoalesceWaiting (peer_convergence + ComputePeerEpochLiveCap).
	 */
	if (syNetRollbackStickAbsorbCoalesceWaiting() != FALSE)
	{
		syNetRollbackLogTryBeginFail("stick_absorb_coalesce", sSYNetRollbackDeferredMismatchTick,
					     sSYNetRollbackDeferredMismatchTargetTick,
					     sSYNetRollbackDeferredMismatchPlayer);
		/*
		 * Drop peer_convergence that may have re-armed from symmetric notify of this
		 * deferred span — otherwise epoch_cap source=2 freezes sim and absorb never
		 * expires (soak1 dual-stick hang @441).
		 */
		syNetRollbackEpisodeFsmOnPostMatch();
		return FALSE;
	}
#endif
	mismatch = sSYNetRollbackDeferredMismatchTick;
	target = sSYNetRollbackDeferredMismatchTargetTick;
	player = sSYNetRollbackDeferredMismatchPlayer;
	/*
	 * Union of dirty remotes → Begin with correction_player=-1 so reconcile applies
	 * all remote humans in [mismatch,target). Single dirty slot keeps that player.
	 */
	if (syNetRollbackDeferredMismatchSlotCount() > 1U)
	{
		player = -1;
	}
#if defined(SSB64_NETMENU)
	/* Re-align just before Begin — more wire may have landed since KEEP/MERGE. */
	syNetRollbackAlignTargetToWireReady(mismatch, player, &target, "TRY_BEGIN");
	sSYNetRollbackDeferredMismatchTargetTick = target;
	/*
	 * Light wire coalesce: span-1 (target == mismatch+1) means only one contiguous
	 * confirmed tick — Begin would micro-chase every stick REPLACE and SoftLip-fork
	 * (soak 3849468025: 155 light eps / avg gap ~2.7t). Wait up to MAX_WAITS pumps
	 * for the burst to widen ONLY while live-cap can still hold (sim < mismatch):
	 * DeferredCorrectionBlocksLiveAdvance caps at mismatch-1, so Wait geometry
	 * cannot advance under the wrong stick.
	 *
	 * When sim >= mismatch the cap cannot rewind — soak 1064408286: 100% of
	 * LIGHT_WIRE_COALESCE waits had sim already past (e.g. mismatch=417 sim=420).
	 * Delaying Begin then only prolongs SoftLip prediction under wrong sticks;
	 * Begin immediately with the span-1 wire we have. Peer-symmetric deferred
	 * never waits. Force Begin after the budget so a lone wire tick still heals.
	 * See docs/bugs/netplay_light_wire_coalesce_past_sim_2026-07-27.md.
	 */
	if ((syNetRollbackLightInputEpisodesEnabled() != FALSE) &&
	    (sSYNetRollbackDeferredMismatchFromPeerSymmetric == FALSE) &&
	    (target == (mismatch + 1U)) &&
	    (syNetInputGetTick() < mismatch))
	{
		if (sSYNetRollbackLightWireCoalesceWaits < SYNETROLLBACK_LIGHT_WIRE_COALESCE_MAX_WAITS)
		{
			sSYNetRollbackLightWireCoalesceWaits++;
			syNetRollbackLogTryBeginFail("light_wire_coalesce", mismatch, target, player);
			port_log(
			    "SSB64 NetRollback: LIGHT_WIRE_COALESCE wait=%u/%u mismatch=%u target=%u player=%d "
			    "sim=%u (span-1; live-cap held for contiguous wire widen)\n",
			    (unsigned int)sSYNetRollbackLightWireCoalesceWaits,
			    (unsigned int)SYNETROLLBACK_LIGHT_WIRE_COALESCE_MAX_WAITS,
			    mismatch,
			    target,
			    (int)player,
			    (unsigned int)syNetInputGetTick());
			return FALSE;
		}
		port_log(
		    "SSB64 NetRollback: LIGHT_WIRE_COALESCE force_begin mismatch=%u target=%u player=%d "
		    "(wait budget exhausted; span-1 heal)\n",
		    mismatch,
		    target,
		    (int)player);
	}
	else
	{
		/*
		 * Includes span-1 with sim >= mismatch: coalesce skipped — live-cap cannot
		 * rewind; Begin immediately (see bugdoc light_wire_coalesce_past_sim).
		 */
		sSYNetRollbackLightWireCoalesceWaits = 0U;
	}
#endif
	if ((mismatch == ~(u32)0) || (target <= mismatch))
	{
		syNetRollbackClearDeferredInputMismatch();
		return FALSE;
	}
	if (syNetRollbackCorrectionAllowedAtTick(mismatch) == FALSE)
	{
		syNetRollbackLogTryBeginFail("correction_not_allowed", mismatch, target, player);
		if (syNetRollbackGgpoDeferredShouldAbandon(mismatch, target) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: ggpo deferred abandon mismatch_tick=%u target_tick=%u slot=%d "
			    "slot_mask=0x%x\n",
			    mismatch,
			    target,
			    (int)player,
			    (unsigned int)sSYNetRollbackDeferredMismatchSlotMask);
			syNetRollbackClearDeferredInputMismatch();
			syNetRollbackClearLastPeerOutcome();
			syNetRollbackLogDeferDiag("ggpo_deferred_abandon", mismatch, target, player);
		}
		return FALSE;
	}
	{
		u32 frontier;
		sb32 wire_lock;
		s32 ep_slot;

		frontier = syNetInputGetTick();
		if (frontier < ~(u32)0)
		{
			frontier++;
		}
		wire_lock = syNetRollbackSymmetricWireLockActive();
		if (sSYNetRollbackDeferredMismatchFromPeerSymmetric != FALSE)
		{
			SYNetRollbackPendingEpisode defer_ep;

			ep_slot = (player >= 0) ? player : sSYNetRollbackDeferredMismatchPlayer;
			if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
			    (syNetRollbackPendingEpisodeCopyValid(ep_slot, &defer_ep) != FALSE))
			{
				sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
				target = syNetRollbackClampResimTargetTickAuthoritative(mismatch, defer_ep.target_tick);
			}
		}
		else
		{
			target = syNetRollbackClampResimTargetTickEx(mismatch, target, frontier, wire_lock);
		}
	}
	{
		SYNetRollbackCorrectionCommitSnap commit_snap;

		sb32 deferred_from_peer_symmetric;
		u32 saved_slot_mask;

		deferred_from_peer_symmetric = sSYNetRollbackDeferredMismatchFromPeerSymmetric;
		saved_slot_mask = sSYNetRollbackDeferredMismatchSlotMask;
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
			else if (syNetRollbackGgpoDeferredShouldAbandon(mismatch, target) != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: ggpo deferred abandon mismatch_tick=%u target_tick=%u slot=%d "
				    "slot_mask=0x%x\n",
				    mismatch,
				    target,
				    (int)player,
				    (unsigned int)saved_slot_mask);
				syNetRollbackClearDeferredInputMismatch();
				syNetRollbackClearLastPeerOutcome();
				syNetRollbackLogDeferDiag("ggpo_deferred_abandon", mismatch, target, player);
			}
			else
			{
				syNetRollbackLogTryBeginFail("commit_begin_failed", mismatch, target, player);
				/*
				 * Live-cap (mismatch-1) is armed while Begin cannot commit. If sim is already
				 * at/past mismatch, holding forever deadlocks cooldown/suppress (stick L/R
				 * storms). Clear deferred so live can advance; a later REPLACE may re-queue.
				 * See docs/bugs/netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md.
				 */
				if ((deferred_from_peer_symmetric == FALSE) && (syNetInputGetTick() >= mismatch))
				{
					port_log(
					    "SSB64 NetRollback: ggpo deferred lift_livecap mismatch_tick=%u target_tick=%u slot=%d (commit_begin_failed)\n",
					    mismatch,
					    target,
					    (int)player);
					syNetRollbackClearDeferredInputMismatch();
				}
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
			    "SSB64 NetRollback: GGPO deferred input correction resim mismatch_tick=%u target_tick=%u "
			    "slot=%d slot_mask=0x%x\n",
			    mismatch,
			    target,
			    (int)player,
			    (unsigned int)saved_slot_mask);
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
				sSYNetRollbackDeferredMismatchSlotMask = saved_slot_mask;
				syNetRollbackDeferredMismatchSyncPlayerFromMask();
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

/*
 * Ring-capacity load floor only — no episode floor.
 *
 * The episode floor (EpisodeResolvedThrough - 1, applied by
 * syNetRollbackLoadTickMinBound below) exists to stop a NEW episode opening
 * behind a completed light episode. It must not bind the RESCUE paths that
 * run after a load has already failed: there, the failed tick is itself the
 * floor, so FindLatestLoadSafe/Valid(failed - 1, floor) can only ever return
 * ~0 and the walkback breaks before its first retry — turning a recoverable
 * load-fidelity failure into a terminal BATTLE_SIM_HOLD.
 *
 * Same trap, same reasoning as the FC input-agree onset path
 * (apply_episode_floor=FALSE in syNetRollbackResolveStateMismatchLoadTick).
 * Walking below resolved_through is safe here because the resim re-derives
 * every tick forward from the deeper load; the alternative is a dead match.
 * See docs/bugs/netplay_light_frontier_load_fail_walkback_floor_2026-08-21.md.
 */
static u32 syNetRollbackLoadTickRingMinBound(u32 sim_tick)
{
	u32 min_load;
	u32 ring_cap;

	min_load = 0U;
	ring_cap = syNetRbSnapshotRingCapacity();
	if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
	{
		min_load = sim_tick - (ring_cap - 2U);
	}
	return min_load;
}

static u32 syNetRollbackLoadTickMinBound(u32 sim_tick)
{
	u32 min_load;
	u32 episode_floor;

	min_load = syNetRollbackLoadTickRingMinBound(sim_tick);
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

static u32 syNetRollbackResolveStateMismatchLoadTick(u32 validation_tick, u32 min_load,
						     sb32 apply_episode_floor)
{
	u32 probe;
	u32 resolved;
	u32 sim_floor;
	u32 ring_min;
	u32 episode_floor;

	/*
	 * apply_episode_floor=TRUE (ordinary / TryBegin): LoadTickMinBound may raise min_load to
	 * EpisodeResolvedThrough-1 so we do not open behind a completed light episode.
	 *
	 * apply_episode_floor=FALSE (FC input-agree onset): light episodes march resolved_through
	 * past a SoftLip fork without healing state. Raising min_load to ~frontier makes
	 * FindLatestLoadSafe(onset_load, frontier) return ~0 (onset_load < min_tick), then
	 * RING_CLAMP jumps to the frontier and July-27 fail-closes ("ring clamp abandoned onset").
	 * Soak1 2026-07-28 seed 3538623210: onset_load=612 in-ring, resolved_through=714 →
	 * Resolve miss → clamped_load=714. Use caller ring floor only. See
	 * docs/bugs/netplay_fc_onset_resolve_episode_floor_2026-07-28.md.
	 */
	ring_min = min_load;
	episode_floor = 0U;
	if (sSYNetRollbackEpisodeResolvedThrough > 1U)
	{
		episode_floor = sSYNetRollbackEpisodeResolvedThrough - 1U;
	}
	if (apply_episode_floor != FALSE)
	{
		sim_floor = syNetRollbackLoadTickMinBound(syNetInputGetTick());
		if (sim_floor > min_load)
		{
			min_load = sim_floor;
		}
	}
	/*
	 * When frame-commit input digests agree but fighter/world diverged, load from the last agreed
	 * validation tick — not validation_tick-1 (often already poisoned by silent drift).
	 */
	if ((sSYNetRollbackDeferredStateMismatchInputAgreed != FALSE) &&
	    (sSYNetRollbackLastFrameCommitStateAgreedTick > 0U))
	{
		probe = sSYNetRollbackLastFrameCommitStateAgreedTick;
	}
	else
	{
		probe = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
	}
	resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(probe, min_load);
	if ((resolved == ~(u32)0) && (sSYNetRollbackLastFrameCommitStateAgreedTick != 0U))
	{
		resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(sSYNetRollbackLastFrameCommitStateAgreedTick,
									min_load);
	}
	if ((resolved == ~(u32)0) && (sSYNetRollbackDeferredStateMismatchInputAgreed != FALSE) &&
	    (sSYNetRollbackLastFrameCommitStateAgreedTick > 0U))
	{
		/* Ring min_load can sit above last agreed validation; search full ring before giving up. */
		resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(probe, 0U);
		if (resolved == ~(u32)0)
		{
			resolved =
			    syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(sSYNetRollbackLastFrameCommitStateAgreedTick, 0U);
		}
	}
	if (resolved == ~(u32)0)
	{
		resolved = syNetRbSnapshotFindLatestValidTickAtOrBefore(probe, min_load);
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	if ((apply_episode_floor == FALSE) && (sSYNetRollbackStateHashLogsRemaining > 0U))
	{
		port_log(
		    "SSB64 NetRollback: FRAME_COMMIT_ONSET_RESOLVE probe=%u ring_min=%u episode_floor=%u "
		    "resolved=%u (episode floor not applied)\n",
		    probe,
		    ring_min,
		    episode_floor,
		    resolved);
		sSYNetRollbackStateHashLogsRemaining--;
	}
#else
	(void)ring_min;
	(void)episode_floor;
#endif
	return resolved;
}

/*
 * Earliest load-safe snapshot in [min_tick, max_tick]. Used when a preferred deep onset sits
 * outside the ring — fall forward to the first still-satisfiable anchor rather than storm-abort.
 */
static u32 syNetRollbackFindEarliestLoadSafeTickInSpan(u32 min_tick, u32 max_tick)
{
	u32 t;

	if ((max_tick < min_tick) || (max_tick == 0U))
	{
		return ~(u32)0;
	}
	if (min_tick == 0U)
	{
		min_tick = 1U;
	}
	for (t = min_tick; t <= max_tick; t++)
	{
		if (syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(t, t) == t)
		{
			return t;
		}
		if (t == ~(u32)0)
		{
			break;
		}
	}
	return ~(u32)0;
}

/*
 * Input-agree FC recovery when shared/predicted onset load is outside the ring.
 * Prefer bilaterally known anchors (shared correction frontier) before scanning the ring.
 * reason_code: 1=frontier, 2=earliest_in_ring, 3=latest_in_ring. Returns ~0 if none.
 */
static u32 syNetRollbackPickInRingFcInputAgreeLoad(u32 validation_tick, u32 min_load, u32 *out_reason_code)
{
	u32 max_load;
	u32 frontier;
	u32 resolved;

	if (out_reason_code != NULL)
	{
		*out_reason_code = 0U;
	}
	max_load = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
	if ((max_load == 0U) || (max_load < min_load))
	{
		return ~(u32)0;
	}

	/*
	 * Prefer the intersected (MIN) frontier so both peers land on the same
	 * load-safe slot when peer resolved_through is already known. MAX shared
	 * frontier produced soak 4126729879 divergent FC loads (1057 vs 1047).
	 */
	frontier = syNetRollbackGetIntersectedCorrectionFrontier();
	if ((frontier >= min_load) && (frontier <= max_load))
	{
		resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(frontier, min_load);
		if (resolved != ~(u32)0)
		{
			if (out_reason_code != NULL)
			{
				*out_reason_code = 1U;
			}
			return resolved;
		}
	}

	resolved = syNetRollbackFindEarliestLoadSafeTickInSpan(min_load, max_load);
	if (resolved != ~(u32)0)
	{
		if (out_reason_code != NULL)
		{
			*out_reason_code = 2U;
		}
		return resolved;
	}

	resolved = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(max_load, min_load);
	if (resolved != ~(u32)0)
	{
		if (out_reason_code != NULL)
		{
			*out_reason_code = 3U;
		}
	}
	return resolved;
}

void syNetRollbackNoteFrameCommitStateAgreed(u32 validation_tick)
{
	if (validation_tick > sSYNetRollbackLastFrameCommitStateAgreedTick)
	{
		sSYNetRollbackLastFrameCommitStateAgreedTick = validation_tick;
	}
#ifdef PORT
	if ((syNetRollbackIsActive() != FALSE) && (validation_tick > 0U))
	{
		syNetRbSnapshotPinLoadSafeAtTick(validation_tick);
	}
#endif
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
#ifdef PORT
	if (syNetRollbackDeferResimForPauseTransition() != FALSE)
	{
		syNetRollbackLogTryBeginFail("fc_pause_defer", sSYNetRollbackDeferredStateMismatchTick,
					     sSYNetRollbackDeferredStateMismatchTargetTick, -1);
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	if (syNetRollbackDeferFcStateRecoveryForNessPKThunder() != FALSE)
	{
		syNetRollbackLogTryBeginFail("fc_ness_pk_defer", sSYNetRollbackDeferredStateMismatchTick,
					     sSYNetRollbackDeferredStateMismatchTargetTick, -1);
		return FALSE;
	}
#endif
#endif
	if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		syNetRollbackLogTryBeginFail("fc_resim_busy", sSYNetRollbackDeferredStateMismatchTick,
					     sSYNetRollbackDeferredStateMismatchTargetTick, -1);
		return FALSE;
	}
	if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
	{
		if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) ||
		    (sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0))
		{
#if defined(PORT) && defined(SSB64_NETMENU)
			u32 prefer_frontier;

			/*
			 * Peer episode priority without forever-wait: flush + TryBegin peer-sym
			 * now; if it starts, FC retries next pump. If peer-sym cannot Begin,
			 * fall through to FC (fc_resim_busy serializes). Retired fc_waiting_peer_episode.
			 * See docs/bugs/netplay_fc_episode_begin_stall_retire_2026-07-26.md.
			 */
			prefer_frontier = syNetInputGetTick();
			if (prefer_frontier < ~(u32)0)
			{
				prefer_frontier++;
			}
			syNetRollbackFlushDeferredPeerSymmetric();
			if (syNetRollbackTryBeginResimFromPendingPeerSymmetric(prefer_frontier, ~(u32)0) != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: fc_peer_sym_prefer_began fc_mismatch=%u fc_target=%u sim=%u\n",
				    sSYNetRollbackDeferredStateMismatchTick,
				    sSYNetRollbackDeferredStateMismatchTargetTick,
				    syNetInputGetTick());
				return FALSE;
			}
			syNetRollbackLogDeferDiag("fc_peer_sym_prefer_fallthrough",
						  sSYNetRollbackDeferredStateMismatchTick,
						  sSYNetRollbackDeferredStateMismatchTargetTick, -1);
#else
			syNetRollbackLogTryBeginFail("fc_waiting_peer_episode", sSYNetRollbackDeferredStateMismatchTick,
						     sSYNetRollbackDeferredStateMismatchTargetTick, -1);
			syNetRollbackLogDeferDiag("fc_waiting_peer_episode", sSYNetRollbackDeferredStateMismatchTick,
						  sSYNetRollbackDeferredStateMismatchTargetTick, -1);
			return FALSE;
#endif
		}
	}
	mismatch = sSYNetRollbackDeferredStateMismatchTick;
	target = sSYNetRollbackDeferredStateMismatchTargetTick;
	if (syNetRollbackDeferFrameCommitForSymmetric(mismatch) != FALSE)
	{
		syNetRollbackLogTryBeginFail("fc_symmetric_defers", mismatch, target, -1);
		syNetRollbackLogDeferDiag("symmetric_defers_frame_commit", mismatch, target, -1);
		return FALSE;
	}
	if ((mismatch == ~(u32)0) || (target <= mismatch))
	{
		sSYNetRollbackDeferredStateMismatchPending = FALSE;
		sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
		syNetRollbackClearFcStateRecovery();
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
#if defined(SSB64_NETMENU)
	/*
	 * GGPO already uses CorrectionAllowedAtTick (episode-window bypass). Bare debounce after
	 * a stick GGPO at the same mismatch left FC pending while peer_convergence (source=2)
	 * capped live at target+slack — sim never reached DebounceUntil (soak 1811750376
	 * epoch_cap=2082). Mirror GGPO; on residual deny clear convergence so live can advance.
	 * See docs/bugs/netplay_dual_stick_ggpo_resim_storm_2026-07-27.md.
	 */
	if (syNetRollbackCorrectionAllowedAtTick(mismatch) == FALSE)
	{
		syNetRollbackLogTryBeginFail("fc_debounce", mismatch, target, -1);
		syNetRollbackEpisodeFsmOnPostMatch();
		port_log(
		    "SSB64 NetRollback: fc_debounce_clear_peer_convergence mismatch=%u target=%u sim=%u\n",
		    mismatch,
		    target,
		    syNetInputGetTick());
		return FALSE;
	}
#else
	if (syNetRollbackMismatchAllowedDuringDebounce(mismatch) == FALSE)
	{
		syNetRollbackLogTryBeginFail("fc_debounce", mismatch, target, -1);
		return FALSE;
	}
#endif
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
			/*
			 * FC input-agree recovery: keep ring floor only so TryBegin can load at the
			 * shared onset behind EpisodeResolvedThrough (same as onset Resolve).
			 */
			resolved = syNetRollbackResolveStateMismatchLoadTick(
			    mismatch,
			    min_load,
			    (sSYNetRollbackFcStateRecoveryActive != FALSE) ? FALSE : TRUE);
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
#if defined(SSB64_NETMENU)
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		u32 clamp_mismatch;

		clamp_mismatch = mismatch;
		if (syNetplayNessClampFcRecoveryLoadTick(&load_tick, &clamp_mismatch) != FALSE)
		{
			mismatch = clamp_mismatch;
			sSYNetRollbackDeferredStateMismatchTick = mismatch;
			if (target <= mismatch)
			{
				target = mismatch + 1U;
				sSYNetRollbackDeferredStateMismatchTargetTick = target;
			}
		}
	}
	/*
	 * Safety net: input-agree FC recovery that still armed a deep onset (or raced before
	 * RING_CLAMP) must not storm-abort when an in-ring load-safe anchor exists.
	 */
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) != FALSE))
	{
		u32 min_load_safe;
		u32 ring_cap_safe;
		u32 sim_tick_safe;
		u32 clamp_load;
		u32 clamp_reason;
		u32 validation_probe;

		sim_tick_safe = syNetInputGetTick();
		ring_cap_safe = syNetRbSnapshotRingCapacity();
		min_load_safe = 0U;
		if ((ring_cap_safe > 2U) && (sim_tick_safe > (ring_cap_safe - 2U)))
		{
			min_load_safe = sim_tick_safe - (ring_cap_safe - 2U);
		}
		validation_probe = target;
		if ((validation_probe == 0U) || (validation_probe == ~(u32)0))
		{
			validation_probe = (sim_tick_safe < ~(u32)0) ? (sim_tick_safe + 1U) : sim_tick_safe;
		}
		clamp_reason = 0U;
		clamp_load = syNetRollbackPickInRingFcInputAgreeLoad(validation_probe, min_load_safe, &clamp_reason);
		if ((clamp_load != ~(u32)0) && (clamp_load >= min_load_safe))
		{
			port_log(
			    "SSB64 NetRollback: FRAME_COMMIT_TRYBEGIN_RING_CLAMP requested_load=%u clamped_load=%u "
			    "mismatch=%u min_load=%u frontier=%u reason=%u\n",
			    load_tick,
			    clamp_load,
			    clamp_load + 1U,
			    min_load_safe,
			    syNetRollbackGetIntersectedCorrectionFrontier(),
			    clamp_reason);
			load_tick = clamp_load;
			mismatch = clamp_load + 1U;
			sSYNetRollbackDeferredStateMismatchTick = mismatch;
			if (target <= mismatch)
			{
				target = mismatch + 1U;
				sSYNetRollbackDeferredStateMismatchTargetTick = target;
			}
		}
	}
#endif
	if (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) != FALSE)
	{
		syNetRollbackLogTryBeginFail("fc_storm_limit", mismatch, target, -1);
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
			/* Do not leave FcStateRecoveryActive orphaned — that suppresses peer SYNC forever. */
			syNetRollbackClearFcStateRecovery();
			syNetRollbackResetPeerBaselineResyncStorm();
			return FALSE;
		}
	}
	{
		sb32 input_agreed;

		input_agreed = sSYNetRollbackDeferredStateMismatchInputAgreed;
		if ((input_agreed != FALSE) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
		{
			target = syNetRollbackClampResimTargetTickAuthoritative(mismatch, target);
		}
		else
		{
			target = syNetRollbackClampResimTargetTick(mismatch, target);
		}
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * Keep DeferredStateMismatchPending through TryCommit so allow_frontier_deepen
		 * still sees the FC/state-hash path. Clearing before Commit (legacy) made FC
		 * deepen look like ordinary GGPO → commit_behind_frontier + drop arm.
		 * See docs/bugs/netplay_fc_commit_behind_frontier_deepen_2026-07-26.md.
		 */
		{
			SYNetRollbackCorrectionCommitSnap commit_snap;

			if (syNetRollbackTryCommitCorrectionBegin(mismatch, load_tick, target, &commit_snap) == FALSE)
			{
				syNetRollbackLogTryBeginFail("fc_commit_failed", mismatch, target, -1);
				syNetRollbackLogDeferDiag("state_resync_commit_failed", mismatch, target, -1);
				/* Keep pending + FC recovery armed for the next pump. */
				sSYNetRollbackDeferredStateMismatchPending = TRUE;
				sSYNetRollbackDeferredStateMismatchTick = mismatch;
				sSYNetRollbackDeferredStateMismatchTargetTick = target;
				sSYNetRollbackDeferredStateMismatchInputAgreed = input_agreed;
				return FALSE;
			}
			sSYNetRollbackDeferredStateMismatchPending = FALSE;
			sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
			sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
			sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
			/*
			 * Re-pin FC arm after clearing deferred: a light episode finishing while we
			 * waited (fc_resim_busy) can race-clear FcStateRecoveryActive via
			 * OnResimCompleted. BeginResim must see the arm so it stays heavy.
			 */
			sSYNetRollbackFcStateRecoveryActive = TRUE;
			sSYNetRollbackFcStateRecoveryMismatchTick = mismatch;
			sSYNetRollbackFcStateRecoveryTargetTick = target;
			syNetRollbackArmPeerEpochForStateResim(mismatch, target);
			syNetPeerFrameCommitDiagNoteRecoveryStarted();
			syNetPeerArmPostRecoveryConvergenceWatch();
			port_log(
			    "SSB64 NetRollback: deferred frame-commit state resim mismatch_tick=%u target_tick=%u\n",
			    mismatch,
			    target);
			if (syNetRollbackBeginResim(mismatch, target, -1) == FALSE)
			{
				syNetRollbackLogTryBeginFail("fc_begin_resim_failed", mismatch, target, -1);
				syNetRollbackAbortCorrectionCommit(&commit_snap);
				syNetRollbackClearPeerEpochState();
				syNetRollbackClearBattleSimHoldAfterLoadFail();
				sSYNetRollbackDeferredStateMismatchPending = TRUE;
				sSYNetRollbackDeferredStateMismatchTick = mismatch;
				sSYNetRollbackDeferredStateMismatchTargetTick = target;
				sSYNetRollbackDeferredStateMismatchInputAgreed = input_agreed;
				return FALSE;
			}
		}
#else
		sSYNetRollbackDeferredStateMismatchPending = FALSE;
		sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
		{
			SYNetRollbackCorrectionCommitSnap commit_snap;

			if (syNetRollbackTryCommitCorrectionBegin(mismatch, load_tick, target, &commit_snap) == FALSE)
			{
				syNetRollbackLogTryBeginFail("fc_commit_failed", mismatch, target, -1);
				syNetRollbackLogDeferDiag("state_resync_commit_failed", mismatch, target, -1);
				syNetRollbackClearPeerEpochState();
				syNetRollbackClearFcStateRecovery();
				syNetRollbackResetPeerBaselineResyncStorm();
				return FALSE;
			}
			syNetRollbackArmPeerEpochForStateResim(mismatch, target);
			syNetPeerFrameCommitDiagNoteRecoveryStarted();
			syNetPeerArmPostRecoveryConvergenceWatch();
			port_log(
			    "SSB64 NetRollback: deferred frame-commit state resim mismatch_tick=%u target_tick=%u\n",
			    mismatch,
			    target);
			if (syNetRollbackBeginResim(mismatch, target, -1) == FALSE)
			{
				syNetRollbackLogTryBeginFail("fc_begin_resim_failed", mismatch, target, -1);
				syNetRollbackAbortCorrectionCommit(&commit_snap);
				syNetRollbackClearPeerEpochState();
				syNetRollbackClearBattleSimHoldAfterLoadFail();
				syNetRollbackClearFcStateRecovery();
				syNetRollbackResetPeerBaselineResyncStorm();
				return FALSE;
			}
		}
#endif
	}
	sSYNetRollbackRollbackCount++;
	sSYNetRollbackResimOrdinal = sSYNetRollbackRollbackCount;
	sSYNetRollbackLastCommittedMismatchTick = mismatch;
	syNetRollbackAdvanceResimBudget();
	return TRUE;
}

static void syNetRollbackHandleFrameCommitStateMismatchCore(u32 validation_tick, const SYNetFrameCommitToken *local,
							    const SYNetFrameCommitToken *peer)
{
	u32 mismatch_tick;
	u32 target_tick;
	u32 seed_onset;
	u32 last_agreed_for_seed;

	seed_onset = ~(u32)0;
	last_agreed_for_seed = sSYNetRollbackLastFrameCommitStateAgreedTick;
	syNetPeerFrameCommitDiagNoteDeferredArmed();
	{
		u32 probe;
		u32 min_load;
		u32 ring_cap;
		u32 sim_tick;
		u32 resolved;
		sb32 inputs_agree;
		sb32 escalate_bypass;

		inputs_agree = (local->input_digest == peer->input_digest) ? TRUE : FALSE;
		escalate_bypass = FALSE;
#if defined(SSB64_NETMENU)
		escalate_bypass = syNetPeerSnapAgreeEscalateBypassInputSkew();
#endif
		/*
		 * Input-agree onset path, or snap_agree escalate bypass when digests still skew
		 * (see FRAME_COMMIT_INPUT_SKEW_ESCALATE_BYPASS / soak 1221028269).
		 */
		if ((inputs_agree != FALSE) || (escalate_bypass != FALSE))
		{
			u32 scan_begin;
			u32 predicted_onset;
			u32 shared_onset;
			u32 onset;
			u32 local_agreed;
			u32 peer_agreed;
			u32 bilateral_agreed;

			/*
			 * Snap_agree watermarks can fork while input digests still match (soak
			 * 3388243596: Linux LastAgreed=974 vs Android=962 → shared onset 974 vs 962
			 * → asymmetric FC mismatch). Wire state_agreed_tick and scan from min().
			 */
			local_agreed = sSYNetRollbackLastFrameCommitStateAgreedTick;
			peer_agreed = peer->state_agreed_tick;
			if (local_agreed == 0U)
			{
				bilateral_agreed = peer_agreed;
			}
			else if (peer_agreed == 0U)
			{
				bilateral_agreed = local_agreed;
			}
			else if (local_agreed < peer_agreed)
			{
				bilateral_agreed = local_agreed;
			}
			else
			{
				bilateral_agreed = peer_agreed;
			}
			scan_begin = 1U;
			if (bilateral_agreed > 0U)
			{
				scan_begin = bilateral_agreed;
			}
			last_agreed_for_seed = bilateral_agreed;
			/*
			 * Prefer shared published non-neutral onset (identical on both peers when input digests match)
			 * over local predicted-usage flags. Local flags mark idle hold-last prediction on the remote
			 * slot from scan_begin and fork recovery (Android onset=1020 vs Linux onset=960 → Wait/Dash).
			 * Escalate+input-skew: histories may disagree — prefer bilateral watermark over local scan.
			 */
			shared_onset = syNetInputFindEarliestHumanNonNeutralInSpan(scan_begin, validation_tick);
			predicted_onset = syNetInputFindEarliestPredictedRemoteUsageInSpan(scan_begin, validation_tick);
			if (inputs_agree != FALSE)
			{
				onset = (shared_onset != ~(u32)0) ? shared_onset : predicted_onset;
			}
			else if (bilateral_agreed > 0U)
			{
				onset = bilateral_agreed;
			}
			else
			{
				onset = (shared_onset != ~(u32)0) ? shared_onset : predicted_onset;
			}
			if (onset != ~(u32)0)
			{
				u32 onset_load;
				u32 clamp_reason;

				seed_onset = onset;
				if (sSYNetRollbackStateHashLogsRemaining > 0U)
				{
					port_log(
					    "SSB64 NetRollback: FRAME_COMMIT_INPUT_AGREE_ONSET validation=%u onset=%u shared=%u "
					    "predicted=%u scan_begin=%u local_agreed=%u peer_agreed=%u inputs_agree=%d "
					    "escalate_bypass=%d\n",
					    validation_tick,
					    onset,
					    shared_onset,
					    predicted_onset,
					    scan_begin,
					    local_agreed,
					    peer_agreed,
					    (int)inputs_agree,
					    (int)escalate_bypass);
					sSYNetRollbackStateHashLogsRemaining--;
				}
				sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
				mismatch_tick = onset;
				sim_tick = syNetInputGetTick();
				ring_cap = syNetRbSnapshotRingCapacity();
				min_load = 0U;
				if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
				{
					min_load = sim_tick - (ring_cap - 2U);
				}
				/*
				 * Keep July 11 shared-onset preference for bilateral agreement, but when that
				 * load sits outside the ring (or Resolve cannot satisfy min_load), fall forward
				 * to the earliest safe in-ring anchor. Predicted onset stays diagnostic-only
				 * while shared is present (local predicted-usage is not bilateral).
				 * Soak 1410199591: shared=3633 → load 3632 with ring=128/sim≈3873 →
				 * fc_storm_limit / PEER_BASELINE_RESYNC_STORM despite resolved_through=3751.
				 */
				onset_load = (onset > 0U) ? (onset - 1U) : 0U;
				/*
				 * Input-agree FC recovery only when we can load at (or before) the shared
				 * onset. Two unrecovered cases — both fail closed, no short resim:
				 *  1) onset older than ring (onset_load < min_load)
				 *  2) onset in-ring but Resolve misses → RING_CLAMP abandons onset
				 *     (clamped_load > onset_load). Soak seed 4086048930 validation=481
				 *     onset=454 min_load=354 → clamp 480/477; permanent fork from 454;
				 *     doomed frontier resim then PEER at 477.
				 * See docs/bugs/netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md.
				 */
				{
					const char *unrec_reason;
					u32 clamp_load_diag;

					unrec_reason = NULL;
					clamp_load_diag = ~(u32)0;
					if ((min_load > 0U) && (onset_load < min_load))
					{
						unrec_reason = "fork older than ring";
					}
					resolved = ~(u32)0;
					if (unrec_reason == NULL)
					{
						if (onset_load >= min_load)
						{
							if (onset > 0U)
							{
								resolved = syNetRollbackResolveStateMismatchLoadTick(
								    onset - 1U, min_load, FALSE);
							}
							else
							{
								resolved = syNetRollbackResolveStateMismatchLoadTick(
								    validation_tick, min_load, FALSE);
							}
						}
						if ((resolved == ~(u32)0) || (resolved < min_load))
						{
							clamp_reason = 0U;
							resolved = syNetRollbackPickInRingFcInputAgreeLoad(
							    validation_tick, min_load, &clamp_reason);
							if (resolved != ~(u32)0)
							{
								clamp_load_diag = resolved;
								port_log(
								    "SSB64 NetRollback: FRAME_COMMIT_INPUT_AGREE_ONSET_RING_CLAMP "
								    "validation=%u onset=%u shared=%u predicted=%u clamped_load=%u "
								    "mismatch=%u min_load=%u frontier=%u reason=%u\n",
								    validation_tick,
								    onset,
								    shared_onset,
								    predicted_onset,
								    resolved,
								    resolved + 1U,
								    min_load,
								    syNetRollbackGetIntersectedCorrectionFrontier(),
								    clamp_reason);
								if (resolved > onset_load)
								{
									unrec_reason = "ring clamp abandoned onset";
								}
								else
								{
									mismatch_tick = resolved + 1U;
								}
							}
						}
						else if ((resolved + 1U) < mismatch_tick)
						{
							mismatch_tick = resolved + 1U;
						}
					}
					if (unrec_reason != NULL)
					{
						SYNetRollbackHashSet local_h;
						SYNetRollbackHashSet peer_h;
						u32 snap_tick;
						u32 slot_digests[SYNET_FRAME_COMMIT_FIGHTER_SLOTS];
						s32 si;

						snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
						port_log(
						    "SSB64 NetRollback: FRAME_COMMIT_INPUT_AGREE_ONSET_UNRECOVERABLE "
						    "validation=%u onset=%u shared=%u predicted=%u onset_load=%u "
						    "min_load=%u clamped_load=%u sim=%u ring_cap=%u — %s; "
						    "fail closed (no short resim)\n",
						    validation_tick,
						    onset,
						    shared_onset,
						    predicted_onset,
						    onset_load,
						    min_load,
						    (clamp_load_diag != ~(u32)0) ? clamp_load_diag : 0U,
						    sim_tick,
						    ring_cap,
						    unrec_reason);
						syNetRbSnapshotLogFighterFieldDiffAtTick(
						    snap_tick, "frame_commit_onset_unrecoverable");
						memset(&local_h, 0, sizeof(local_h));
						memset(&peer_h, 0, sizeof(peer_h));
						local_h.fighter = local->fighter_digest;
						local_h.world = local->world_digest;
						local_h.item = local->item_digest;
						local_h.rng = local->rng_digest;
						local_h.effect = local->effect_digest;
						peer_h.fighter = peer->fighter_digest;
						peer_h.world = peer->world_digest;
						peer_h.item = peer->item_digest;
						peer_h.rng = peer->rng_digest;
						peer_h.effect = peer->effect_digest;
						for (si = 0; si < SYNET_FRAME_COMMIT_FIGHTER_SLOTS; si++)
						{
							slot_digests[si] = peer->fighter_slot_digest[si];
						}
						syNetRollbackFailPeerSnapshotDiverge(snap_tick, &peer_h, &local_h,
										     slot_digests);
						return;
					}
					/* Onset Resolve succeeded — mark input-agree for TryBegin / auth target. */
					sSYNetRollbackDeferredStateMismatchInputAgreed = inputs_agree;
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
				u32 agreed_probe;

				sSYNetRollbackDeferredStateMismatchInputAgreed = inputs_agree;
				sim_tick = syNetInputGetTick();
				ring_cap = syNetRbSnapshotRingCapacity();
				min_load = 0U;
				if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)))
				{
					min_load = sim_tick - (ring_cap - 2U);
				}
				agreed_probe = (bilateral_agreed > 0U) ? bilateral_agreed : validation_tick;
				/* Input-agree FC path: do not let EpisodeResolvedThrough hide the load. */
				resolved = syNetRollbackResolveStateMismatchLoadTick(agreed_probe, min_load, FALSE);
				if (resolved != ~(u32)0)
				{
					mismatch_tick = resolved + 1U;
				}
				else
				{
					mismatch_tick = validation_tick;
				}
				probe = syNetInputGetTick();
				if (probe < ~(u32)0)
				{
					probe++;
				}
				mismatch_tick = syNetRollbackReanchorMismatchTick(mismatch_tick, probe);
				if (mismatch_tick > validation_tick)
				{
					mismatch_tick = validation_tick;
				}
				if (sSYNetRollbackStateHashLogsRemaining > 0U)
				{
					port_log(
					    "SSB64 NetRollback: FRAME_COMMIT_INPUT_AGREE_REANCHOR validation=%u "
					    "bilateral_agreed=%u local_agreed=%u peer_agreed=%u mismatch=%u resolved_load=%u\n",
					    validation_tick,
					    bilateral_agreed,
					    local_agreed,
					    peer_agreed,
					    mismatch_tick,
					    resolved);
					sSYNetRollbackStateHashLogsRemaining--;
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
			resolved = syNetRollbackResolveStateMismatchLoadTick(validation_tick, min_load, TRUE);
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
	/*
	 * Seed capture: the FC drill-down at syNetRollbackOnPeerFrameCommitStateMismatch fires at the
	 * validation frontier (post-cascade), where every fighter field already differs. When inputs agree
	 * the real fork is one tick after the last agreed state (mismatch_tick) — e.g. a 1-tick cross-ISA
	 * divergence in a DK cargo-carry fall. The resim that overwrites the forward-sim snapshots is
	 * deferred (set below, run later), so slot[mismatch_tick] here still holds the forward-sim blob.
	 * Dump the per-fighter blob at the seed tick so the two peers' logs name the forking slot/field at
	 * its origin (compare the blob_* columns cross-peer). Env-gated by SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF.
	 *
	 * Input-onset walkback often predates a soft-lip TopN.x fork (soak 1315107154: onset=510,
	 * MpLanding fork@521, FC@557). When onset sits far behind last agreed FC, also dump
	 * frame_commit_physics_seed at last_agreed and emit FRAME_COMMIT_SEED_HINT so offline
	 * tools (MpLanding / SoftLipX) are preferred over the onset seed.
	 */
	if ((local->input_digest == peer->input_digest) && (mismatch_tick > 0U))
	{
		syNetRbSnapshotLogFighterFieldDiffAtTick(mismatch_tick, "frame_commit_seed");
		if ((local->fighter_digest != peer->fighter_digest) && (last_agreed_for_seed > 0U) &&
		    ((seed_onset == ~(u32)0) || ((seed_onset + 8U) < last_agreed_for_seed) ||
		     ((seed_onset + 8U) < mismatch_tick) || ((mismatch_tick + 8U) < last_agreed_for_seed)))
		{
			if (last_agreed_for_seed != mismatch_tick)
			{
				syNetRbSnapshotLogFighterFieldDiffAtTick(last_agreed_for_seed,
									"frame_commit_physics_seed");
			}
			port_log(
			    "SSB64 NetRollback: FRAME_COMMIT_SEED_HINT validation=%u onset=%u last_agreed=%u "
			    "recovery_seed=%u physics_seed=%u "
			    "note=onset_may_predate_physics_fork_use_MpLanding_SoftLipX\n",
			    validation_tick,
			    seed_onset,
			    last_agreed_for_seed,
			    mismatch_tick,
			    last_agreed_for_seed);
		}
	}
	if (((local->input_digest == peer->input_digest)
#if defined(SSB64_NETMENU)
	     || (syNetPeerSnapAgreeEscalateBypassInputSkew() != FALSE)
#endif
		 ) &&
	    (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
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
	/*
	 * Input digests disagree: this is a prediction miss (or pairing skew), not a pure
	 * sim-determinism fork. Arming FC state recovery with correction_player=-1 races the
	 * peer's stick GGPO (soak 2653556481: RebirthWait stick → Fall @1559; Android queued
	 * input GGPO while Linux opened FC state episode @1560 → seal storm / session end).
	 * Let deferred input GGPO heal, or wait for the peer's input episode.
	 * See docs/bugs/netplay_fc_rebirth_stick_drop_input_skew_2026-07-12.md.
	 *
	 * Escalate bypass: sustained snap_agree figh mismatch already proved a state fork;
	 * waiting for digest converge ages onset past the ring (soak 1221028269).
	 */
	if (local->input_digest != peer->input_digest)
	{
		u32 snap_tick;
		sb32 escalate_bypass_end;

		escalate_bypass_end = FALSE;
#if defined(SSB64_NETMENU)
		escalate_bypass_end = syNetPeerSnapAgreeEscalateBypassInputSkew();
#endif
		snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
		if (escalate_bypass_end == FALSE)
		{
			if (syNetRollbackDeferredInputCorrectionCoversTick(snap_tick) != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: FRAME_COMMIT_INPUT_SKEW_PENDING_GGPO validation=%u snap=%u "
				    "defer_mismatch=%u defer_target=%u — not arming state resim\n",
				    validation_tick,
				    snap_tick,
				    sSYNetRollbackDeferredMismatchTick,
				    sSYNetRollbackDeferredMismatchTargetTick);
				return;
			}
			port_log(
			    "SSB64 NetRollback: FRAME_COMMIT_INPUT_SKEW_WAIT validation=%u snap=%u "
			    "inp_local=0x%08X inp_peer=0x%08X — not arming state resim (expect input GGPO)\n",
			    validation_tick,
			    snap_tick,
			    local->input_digest,
			    peer->input_digest);
			return;
		}
		port_log(
		    "SSB64 NetRollback: FRAME_COMMIT_INPUT_SKEW_ESCALATE_BYPASS validation=%u snap=%u "
		    "inp_local=0x%08X inp_peer=0x%08X mismatch=%u — arming FC state recovery\n",
		    validation_tick,
		    snap_tick,
		    local->input_digest,
		    peer->input_digest,
		    mismatch_tick);
	}
	if ((local->input_digest == peer->input_digest)
#if defined(SSB64_NETMENU)
	    || (syNetPeerSnapAgreeEscalateBypassInputSkew() != FALSE)
#endif
	)
	{
		sSYNetRollbackFcStateRecoveryActive = TRUE;
		sSYNetRollbackFcStateRecoveryMismatchTick = mismatch_tick;
		sSYNetRollbackFcStateRecoveryTargetTick = target_tick;
		sSYNetRollbackFcRecoveryJoinAttempts = 0U;
	}
	sSYNetRollbackDeferredStateMismatchPending = TRUE;
	sSYNetRollbackDeferredStateMismatchTick = mismatch_tick;
	sSYNetRollbackDeferredStateMismatchTargetTick = target_tick;
}

void syNetRollbackOnPeerFrameCommitStateMismatch(u32 validation_tick, const SYNetFrameCommitToken *local,
						 const SYNetFrameCommitToken *peer)
{
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
		    "SSB64 NetRollback: FRAME_COMMIT_STATE_DIVERGE validation=%u local figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X eff=0x%08X | peer figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X eff=0x%08X inp_local=0x%08X inp_peer=0x%08X\n",
		    validation_tick,
		    local->fighter_digest,
		    local->world_digest,
		    local->item_digest,
		    local->rng_digest,
		    local->effect_digest,
		    peer->fighter_digest,
		    peer->world_digest,
		    peer->item_digest,
		    peer->rng_digest,
		    peer->effect_digest,
		    local->input_digest,
		    peer->input_digest);
		sSYNetRollbackStateHashLogsRemaining--;
	}
	if (local->fighter_digest != peer->fighter_digest)
	{
		u32 snap_tick;
		s32 p;

		snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
		/*
		 * figh was the one cross-peer frame-commit partition with no drill-down: item and
		 * rng each name the diverging field/walk below, but a fighter fork (e.g. DK cargo
		 * carry cross-ISA drift) printed only the aggregate digests, so the offending slot
		 * and field could not be identified. Mirror the item/rng path: per-slot fighter
		 * hashes localize which player forked (compare host vs guest logs side by side),
		 * and the field diff (gated by SSB64_NETPLAY_FIGHTER_FIELD_DIFF=1) names the field.
		 */
		syNetSyncLogFighterSlotHashes(snap_tick);
		syNetRbSnapshotLogFighterFieldDiffAtTick(snap_tick, "frame_commit_figh_diverge");
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * Input digest hashes authority sticks (buttons/sx/sy), not fighter tap/hold
		 * counters. When inp MATCH but tap_stick_* peer-differs, name the contract:
		 * either published history at snap_tick matches what the digest hashed (true
		 * replay_determinism / older fork), or history drifted from authority resolve.
		 * See docs/bugs/netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md.
		 */
		if (local->input_digest == peer->input_digest)
		{
			for (p = 0; p < SYNET_FRAME_COMMIT_FIGHTER_SLOTS; p++)
			{
				SYNetInputFrame hist;
				sb32 have_hist;
				const SYNetFrameCommitFighterDiag *ld;
				const SYNetFrameCommitFighterDiag *pd;

				ld = &local->fighter_diag[p];
				pd = &peer->fighter_diag[p];
				if ((ld->valid == 0U) && (pd->valid == 0U))
				{
					continue;
				}
				have_hist = syNetInputGetHistoryFrame(p, snap_tick, &hist);
				port_log(
				    "SSB64 NetRollback: FRAME_COMMIT_INPUT_APPLY_AUDIT validation=%u snap=%u player=%d "
				    "inp_match=1 hist_ok=%d hist_sx=%d hist_sy=%d "
				    "local_tap_y=0x%02X peer_tap_y=0x%02X local_hold_x=0x%02X peer_hold_x=0x%02X "
				    "local_hold_y=0x%02X peer_hold_y=0x%02X "
				    "note=inp_digest_is_hist_sticks_not_fighter_tap\n",
				    validation_tick,
				    snap_tick,
				    (int)p,
				    (int)have_hist,
				    have_hist ? (int)hist.stick_x : 0,
				    have_hist ? (int)hist.stick_y : 0,
				    (unsigned int)(ld->tap_stick_y & 0xFFU),
				    (unsigned int)(pd->tap_stick_y & 0xFFU),
				    (unsigned int)(ld->hold_stick_x & 0xFFU),
				    (unsigned int)(pd->hold_stick_x & 0xFFU),
				    (unsigned int)(ld->hold_stick_y & 0xFFU),
				    (unsigned int)(pd->hold_stick_y & 0xFFU));
			}
		}
#endif
	}
	if (local->item_digest != peer->item_digest)
	{
		u32 snap_tick;

		snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
		syNetSyncLogItemHashDriftDiag(snap_tick,
		                              local->item_digest,
		                              peer->item_digest,
		                              "frame_commit_item_diverge");
		syNetSyncLogItemFieldDiffDiag(snap_tick,
		                              local->item_digest,
		                              peer->item_digest,
		                              "frame_commit_item_diverge");
	}
	if (local->rng_digest != peer->rng_digest)
	{
		u32 snap_tick;

		snap_tick = (validation_tick > 0U) ? (validation_tick - 1U) : 0U;
		syNetSyncLogRngHashDriftDiag(snap_tick, local->rng_digest, peer->rng_digest, "frame_commit_rng_diverge");
	}
	syNetRollbackHandleFrameCommitStateMismatchCore(validation_tick, local, peer);
}

void syNetRollbackOnFrameCommitLiveHashGuard(u32 validation_tick, const SYNetFrameCommitToken *local,
					   const SYNetFrameCommitToken *peer, u32 live_figh, u32 live_world)
{
	if ((local == NULL) || (peer == NULL))
	{
		return;
	}
	if ((syNetRollbackIsActive() == FALSE) || (sSYNetRollbackStateHashRollback == FALSE))
	{
		return;
	}
	if (syNetFrameCommitStateDigestsDiverge(local, peer) != FALSE)
	{
		return;
	}
	if (sSYNetRollbackStateHashLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: FRAME_COMMIT_LIVE_HASH_GUARD validation=%u live_figh=0x%08X live_world=0x%08X "
		    "token_figh_local=0x%08X token_figh_peer=0x%08X token_world_local=0x%08X token_world_peer=0x%08X "
		    "inp_local=0x%08X inp_peer=0x%08X\n",
		    validation_tick,
		    live_figh,
		    live_world,
		    local->fighter_digest,
		    peer->fighter_digest,
		    local->world_digest,
		    peer->world_digest,
		    local->input_digest,
		    peer->input_digest);
		sSYNetRollbackStateHashLogsRemaining--;
	}
	syNetRollbackHandleFrameCommitStateMismatchCore(validation_tick, local, peer);
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
#if defined(PORT) && defined(SSB64_NETMENU)
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		notify_flags |= SYNETROLLBACK_SYM_NOTIFY_FLAG_FC_RECOVERY;
	}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
		if (sSYNetRollbackFcStateRecoveryActive != FALSE)
		{
			sSYNetRollbackSymmetricNotifyFlags[slot] |= SYNETROLLBACK_SYM_NOTIFY_FLAG_FC_RECOVERY;
		}
#endif
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
#if defined(SSB64_NETMENU)
	/*
	 * Post-episode absorb: ignore peer-epoch / FSM convergence arming. Live-cap is
	 * lifted while WindowActive; updating PeerEpochTarget here would re-cap the
	 * instant the window expires (soak 3438514102 epoch_cap source=2 @723).
	 * See docs/bugs/netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md.
	 */
	if (syNetRollbackStickAbsorbWindowActive() != FALSE)
	{
		syNetRollbackEpisodeFsmOnPostMatch();
		return;
	}
#endif
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
	slot = syNetRollbackResolveRemoteHumanPlayer(-1);
	if (slot >= 0)
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
#if defined(SSB64_NETMENU)
	/*
	 * Absorb window waits on sim ticks. peer_target from deferred / notify would re-cap
	 * live (source|=2) and freeze before StickAbsorbUntil — including gaps with no
	 * deferred pending (post-Begin / post-complete). See
	 * docs/bugs/netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md.
	 */
	if (syNetRollbackStickAbsorbWindowActive() != FALSE)
	{
		return ~(u32)0;
	}
#endif
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
	/*
	 * Own deferred corrections cap live again after ness_pk_defer retirement — Begin is not
	 * held off for jibaku, so target+slack cannot freeze a defer that never clears.
	 * See docs/bugs/netplay_ness_pk_defer_retire_input_resim_2026-07-26.md.
	 */
	{
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
	}
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick != ~(u32)0) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick > peer_target))
	{
		peer_target = sSYNetRollbackFcStateRecoveryTargetTick;
	}
	{
		u32 ep_target;

		ep_target = syNetRollbackPendingEpisodeMaxTargetTick();
		if (ep_target > peer_target)
		{
			peer_target = ep_target;
		}
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
#if defined(SSB64_NETMENU)
			/*
			 * peer_convergence (source=2): clear and fall through when (a) absorb
			 * window is active, or (b) live already reached the cap with no resim /
			 * deferred work — otherwise soak @723 hangs forever after absorb expires.
			 * See docs/bugs/netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md.
			 */
			if ((fsm_source & 2U) != 0U)
			{
				u32 live_sim;

				live_sim = syNetInputGetTick();
				if ((syNetRollbackStickAbsorbWindowActive() != FALSE) ||
				    ((sSYNetRollbackResimPending == FALSE) &&
				     (syNetRollbackIsResimulating() == FALSE) &&
				     (sSYNetRollbackDeferredMismatchPending == FALSE) &&
				     (fsm_cap != ~(u32)0) && (live_sim >= fsm_cap)))
				{
					syNetRollbackEpisodeFsmOnPostMatch();
					syNetRollbackClearPeerEpochState();
				}
				else
				{
					if (out_cap_source != NULL)
					{
						*out_cap_source = fsm_source;
					}
					*out_max_live_sim = fsm_cap;
					return TRUE;
				}
			}
			else
#endif
			{
				if (out_cap_source != NULL)
				{
					*out_cap_source = fsm_source;
				}
				*out_max_live_sim = fsm_cap;
				return TRUE;
			}
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
				s32 corr_player;
				u32 corr_mismatch;
				u32 corr_load;
				u32 corr_target;
				SYNetRollbackCorrectionMismatchSource corr_source;

				corr_player = ev.slot;
				corr_mismatch = ev.mismatch_tick;
				corr_target = ev.target_tick;
				/*
				 * Prefer a fresh tuple when still visible, but never drop the event after
				 * Promote has cleared PublishedSimUsedPrediction (see RequestInputCorrection).
				 */
				if (syNetRollbackComputeInputCorrectionTuple(ev.slot, ev.mismatch_tick, &corr_player,
									     &corr_mismatch, &corr_load, &corr_target,
									     &corr_source) != FALSE)
				{
					syNetRollbackLogCorrectionTupleIfEnabled(ev.slot, ev.mismatch_tick, corr_player,
											 corr_mismatch, corr_load, corr_target,
											 corr_source);
				}
				else
				{
					if ((corr_target == 0U) || (corr_target <= corr_mismatch))
					{
						corr_target = corr_mismatch + 1U;
					}
				}
				syNetRollbackQueueDeferredInputCorrectionEx(corr_player, corr_mismatch, corr_target);
			}
			break;
		case nSYNetRollbackEpisodeEventPeerSymmetric:
		{
			u8 notify_flags;

			notify_flags = 0U;
			if (ev.follower_local_auth != FALSE)
			{
				notify_flags |= SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH;
			}
			syNetRollbackOnPeerSymmetricRollbackNotifyEx(ev.slot, ev.mismatch_tick, ev.target_tick, ev.load_tick,
								     ev.epoch_id, notify_flags);
			break;
		}
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

sb32 syNetRollbackShouldDeferInterfaceDuringResimWait(void)
{
#ifdef PORT
	static u32 sDeferInterfaceLogTick = ~(u32)0;
	u32 sim_tick;
	u32 preempt_cap;

	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	/*
	 * Host baseline digest can arrive before ROLLBACK_SYNC while follower live sim already advanced past
	 * load_tick (Android soak: BASELINE_RECV@229 then sim=231 → LOAD_SLOT_LIVE_DRIFT + intro offset pop).
	 */
	if ((sSYNetRollbackResimPending == FALSE) &&
	    (syNetRollbackPeerSymmetricRejectBlocksLiveAdvance(&preempt_cap) != FALSE) && (sim_tick > preempt_cap))
	{
		if (sim_tick != sDeferInterfaceLogTick)
		{
			if (syNetRollbackDeferDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: defer_preemptive_baseline_cap sim=%u cap=%u (interface+battle)\n",
				    sim_tick,
				    preempt_cap);
			}
			sDeferInterfaceLogTick = sim_tick;
		}
		return TRUE;
	}
	if (sSYNetRollbackResimPending == FALSE)
	{
		return FALSE;
	}
	/* Forward resim still runs BattleSimOnly + interface; only defer during baseline/seal wait. */
	if (sSYNetRollbackResimBaselineGateOpen != FALSE)
	{
		return FALSE;
	}
	if (sim_tick != sDeferInterfaceLogTick)
	{
		const char *env;

		env = getenv("SSB64_NETPLAY_BATTLE_GO_LOG");
		if ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0))
		{
			port_log(
			    "SSB64 NetRollback: defer_resim_seal_wait sim=%u mismatch=%u load=%u target=%u (interface+battle)\n",
			    sim_tick,
			    sSYNetRollbackResimMismatchTick,
			    sSYNetRollbackResimLoadTick,
			    sSYNetRollbackResimTargetTick);
		}
		sDeferInterfaceLogTick = sim_tick;
	}
	return TRUE;
#endif
	return FALSE;
}

void syNetRollbackRefreshDeferredIntroPresentation(void)
{
#ifdef PORT
#if defined(SSB64_NETMENU)
	u32 sim_tick;
	u32 anchor_tick;
	u32 preempt_cap;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetRollbackShouldDeferInterfaceDuringResimWait() == FALSE)
	{
		return;
	}
	sim_tick = syNetInputGetTick();
	anchor_tick = 0U;
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimLoadTick != ~(u32)0) &&
	    (sSYNetRollbackResimLoadTick != 0U))
	{
		anchor_tick = sSYNetRollbackResimLoadTick;
	}
	else if (syNetRollbackPeerSymmetricRejectBlocksLiveAdvance(&preempt_cap) != FALSE)
	{
		anchor_tick = (preempt_cap > 0U) ? (preempt_cap - 1U) : 0U;
	}
	syNetRbSnapshotRefreshDeferredIntroPresentation(sim_tick, anchor_tick);
#endif
#endif
}

sb32 syNetRollbackShouldBlockLiveBattleAdvance(u32 sim_tick)
{
	u32 cap;
	u32 cap_source;
	u32 local_resim;
	u32 peer_target;

	if (sSYNetRollbackBattleSimHoldAfterLoadFail != FALSE)
	{
		static u32 sLastBattleSimHoldLogTick = ~(u32)0;

		if (sim_tick != sLastBattleSimHoldLogTick)
		{
			port_log(
			    "SSB64 NetRollback: BATTLE_SIM_HOLD blocking battle advance sim=%u resim=%d peer_vs_active=%d rollback_active=%d\n",
			    sim_tick,
			    (int)syNetRollbackIsResimulating(),
			    (int)syNetPeerIsVSSessionActive(),
			    (int)syNetRollbackIsActive());
			sLastBattleSimHoldLogTick = sim_tick;
		}
		return TRUE;
	}
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

/*
 * Soak diagnosability: every reject below was previously silent, which made a peer
 * that ignores an episode recruit indistinguishable from packet loss (see
 * docs/bugs/netplay_fc_recovery_seal_rows_peer_absent_2026-06-11.md). Rate-limited
 * so retransmit storms produce at most one line per ~30 sim ticks.
 */
static void syNetRollbackLogPeerSymmetricNotifyReject(const char *reason, s32 slot, u32 mismatch_tick,
						      u32 target_tick)
{
	static u32 sLastLogSimTick = ~(u32)0;
	u32 sim_tick;

	sim_tick = syNetInputGetTick();
	if ((sLastLogSimTick != ~(u32)0) && (sim_tick >= sLastLogSimTick) && ((sim_tick - sLastLogSimTick) < 30U))
	{
		return;
	}
	sLastLogSimTick = sim_tick;
	port_log(
	    "SSB64 NetRollback: PEER_SYMMETRIC_NOTIFY_REJECT reason=%s slot=%d mismatch_tick=%u target_tick=%u sim=%u applied=%u resolved_through=%u pending=%u deferred=%u\n",
	    reason,
	    (int)slot,
	    mismatch_tick,
	    target_tick,
	    sim_tick,
	    ((slot >= 0) && (slot < MAXCONTROLLERS)) ? sSYNetRollbackPeerSymmetricAppliedTick[slot] : 0U,
	    sSYNetRollbackEpisodeResolvedThrough,
	    (sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) ? sSYNetRollbackPendingPeerSymmetricTick : 0U,
	    (sSYNetRollbackDeferredPeerSymmetricPending != FALSE) ? sSYNetRollbackDeferredPeerSymmetricTick : 0U);
}

sb32 syNetRollbackAcceptPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick, u8 notify_flags)
{
	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (syNetRollbackIsActive() == FALSE))
	{
		syNetRollbackLogPeerSymmetricNotifyReject("rollback_inactive", slot, mismatch_tick, target_tick);
		return FALSE;
	}
	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (mismatch_tick == 0U))
	{
		syNetRollbackLogPeerSymmetricNotifyReject("bad_args", slot, mismatch_tick, target_tick);
		return FALSE;
	}
	if ((sSYNetRollbackPeerSymmetricAppliedTick[slot] != 0U) &&
	    (mismatch_tick <= sSYNetRollbackPeerSymmetricAppliedTick[slot]))
	{
		syNetRollbackLogPeerSymmetricNotifyReject("already_applied", slot, mismatch_tick, target_tick);
		return FALSE;
	}
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (mismatch_tick < sSYNetRollbackEpisodeResolvedThrough))
	{
		/*
		 * Ordinary GGPO behind the frontier still rejects. FC recovery must join at the
		 * peer onset so seal spans match (clamp→shorter follower span → missing_slots).
		 * See docs/bugs/netplay_fc_join_keep_onset_resolved_clamp_2026-07-28.md.
		 */
		if (syNetRollbackPeerSymmetricFcJoinKeepOnset(mismatch_tick, target_tick, notify_flags) == FALSE)
		{
			syNetRollbackLogPeerSymmetricNotifyReject("resolved_through", slot, mismatch_tick, target_tick);
			return FALSE;
		}
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick == sSYNetRollbackPendingPeerSymmetricTick) &&
	    (target_tick != 0U))
	{
		if (target_tick <= sSYNetRollbackPendingPeerSymmetricTargetTick)
		{
			syNetRollbackLogPeerSymmetricNotifyReject("dup_pending", slot, mismatch_tick, target_tick);
			return FALSE;
		}
		sSYNetRollbackPendingPeerSymmetricTargetTick = target_tick;
		if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
		{
			syNetRollbackPendingEpisodeSet(slot, mismatch_tick, target_tick, 0U, 0U, 0U);
		}
		port_log(
		    "SSB64 NetRollback: PEER_SYMMETRIC_NOTIFY_WIDEN pending slot=%d mismatch_tick=%u target_tick=%u sim=%u\n",
		    (int)slot,
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
		return FALSE;
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (mismatch_tick == sSYNetRollbackDeferredPeerSymmetricTick) &&
	    (target_tick != 0U))
	{
		if (target_tick <= sSYNetRollbackDeferredPeerSymmetricTargetTick)
		{
			syNetRollbackLogPeerSymmetricNotifyReject("dup_deferred", slot, mismatch_tick, target_tick);
			return FALSE;
		}
		sSYNetRollbackDeferredPeerSymmetricTargetTick = target_tick;
		port_log(
		    "SSB64 NetRollback: PEER_SYMMETRIC_NOTIFY_WIDEN deferred slot=%d mismatch_tick=%u target_tick=%u sim=%u\n",
		    (int)slot,
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
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
						  u32 epoch_id, u8 notify_flags)
{
	u32 frontier;
	sb32 follower_local_auth;

	follower_local_auth =
	    ((notify_flags & SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH) != 0U) ? TRUE : FALSE;

	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (syNetRollbackIsActive() == FALSE))
	{
		return;
	}
	if (syNetRollbackPeerSymmetricNotifyIsStaleShallow(mismatch_tick, load_tick) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM peer_symmetric_ignore_stale_shallow notify_mismatch=%u notify_load=%u sim=%u\n",
		    mismatch_tick,
		    load_tick,
		    syNetInputGetTick());
		return;
	}
	/*
	 * Shared correction frontier: soft-clamp any behind-resolved SYNC (target still past
	 * frontier) so the follower joins. Deep stale notifies with target ≤ resolved still
	 * reject via Accept. FC recovery keeps peer onset (seal span must match initiator).
	 * See docs/bugs/netplay_shared_correction_frontier_2026-07-19.md,
	 * docs/bugs/netplay_fc_join_keep_onset_resolved_clamp_2026-07-28.md.
	 */
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) &&
	    (mismatch_tick < sSYNetRollbackEpisodeResolvedThrough) &&
	    (target_tick > sSYNetRollbackEpisodeResolvedThrough))
	{
		if (syNetRollbackPeerSymmetricFcJoinKeepOnset(mismatch_tick, target_tick, notify_flags) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: PEER_SYMMETRIC_FC_JOIN_KEEP_ONSET slot=%d mismatch=%u target=%u "
			    "load=%u resolved_through=%u flags=0x%02X\n",
			    (int)slot,
			    mismatch_tick,
			    target_tick,
			    load_tick,
			    sSYNetRollbackEpisodeResolvedThrough,
			    (unsigned int)notify_flags);
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: PEER_SYMMETRIC_CLAMP_RESOLVED slot=%d mismatch=%u->%u target=%u load=%u resolved_through=%u\n",
			    (int)slot,
			    mismatch_tick,
			    sSYNetRollbackEpisodeResolvedThrough,
			    target_tick,
			    load_tick,
			    sSYNetRollbackEpisodeResolvedThrough);
			mismatch_tick = sSYNetRollbackEpisodeResolvedThrough;
			if ((load_tick != 0U) && (load_tick >= mismatch_tick))
			{
				load_tick = mismatch_tick - 1U;
			}
		}
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
	if (syNetRollbackAcceptPeerSymmetricRollbackNotify(slot, mismatch_tick, target_tick, notify_flags) == FALSE)
	{
		return;
	}
	/*
	 * Matching peer SYNC while FC state recovery is armed but not yet BeginResim'd:
	 * clear deferred local TryBegin so we do not dual-init under fc_waiting_peer_episode.
	 * Keep FcStateRecoveryActive — allow_frontier_deepen needs it for peer join at the
	 * FC onset behind resolved_through. Soak 2824158137: ClearFc here → commit_behind_frontier
	 * → dup_pending forever. See docs/bugs/netplay_fc_yield_commit_behind_frontier_2026-07-28.md.
	 */
	if ((sSYNetRollbackResimPending == FALSE) && (syNetRollbackIsResimulating() == FALSE) &&
	    (((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	      (syNetRollbackFcStateRecoveryCoversSpan(mismatch_tick, target_tick) != FALSE)) ||
	     ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	      (sSYNetRollbackDeferredStateMismatchTick == mismatch_tick) &&
	      ((sSYNetRollbackDeferredStateMismatchTargetTick == ~(u32)0) ||
	       (sSYNetRollbackDeferredStateMismatchTargetTick == target_tick)))))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_YIELD unstarted FC recovery to peer notify mismatch=%u target=%u "
		    "fc_mismatch=%u fc_target=%u defer_pending=%d keep_fc_arm=1 sim=%u\n",
		    mismatch_tick,
		    target_tick,
		    sSYNetRollbackFcStateRecoveryMismatchTick,
		    sSYNetRollbackFcStateRecoveryTargetTick,
		    (int)sSYNetRollbackDeferredStateMismatchPending,
		    syNetInputGetTick());
		sSYNetRollbackDeferredStateMismatchPending = FALSE;
		sSYNetRollbackDeferredStateMismatchTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchTargetTick = ~(u32)0;
		sSYNetRollbackDeferredStateMismatchInputAgreed = FALSE;
		syNetRollbackResetPeerBaselineResyncStorm();
	}
	if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
	    (syNetRollbackLocalEpisodeConflictsWithPeerNotify(mismatch_tick, target_tick) != FALSE))
	{
		if (syNetRollbackPeerSymmetricNotifyIsStaleShallow(mismatch_tick, load_tick) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: EPISODE_YIELD_skip_stale_shallow notify_mismatch=%u notify_load=%u sim=%u\n",
			    mismatch_tick,
			    load_tick,
			    syNetInputGetTick());
			return;
		}
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
		syNetRollbackPendingEpisodeSet(slot, mismatch_tick, target_tick, load_tick, epoch_id, notify_flags);
	}
	syNetRollbackNotePeerEpochTarget(slot, mismatch_tick, target_tick);
	if (syNetRollbackPeerSymmetricFlushQuiesced() == FALSE)
	{
		if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
		    (mismatch_tick == sSYNetRollbackDeferredPeerSymmetricTick) &&
		    (target_tick > sSYNetRollbackDeferredPeerSymmetricTargetTick))
		{
			sSYNetRollbackDeferredPeerSymmetricTargetTick = target_tick;
			sSYNetRollbackDeferredPeerSymmetricFlags |= notify_flags;
		}
		else if ((sSYNetRollbackDeferredPeerSymmetricPending == FALSE) ||
		         (mismatch_tick <= sSYNetRollbackDeferredPeerSymmetricTick))
		{
			sSYNetRollbackDeferredPeerSymmetricPending = TRUE;
			sSYNetRollbackDeferredPeerSymmetricTick = mismatch_tick;
			sSYNetRollbackDeferredPeerSymmetricTargetTick = target_tick;
			sSYNetRollbackDeferredPeerSymmetricSlot = slot;
			sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = follower_local_auth;
			sSYNetRollbackDeferredPeerSymmetricFlags = notify_flags;
			syNetRollbackArmPeerSymmetricRejectLiveCap(mismatch_tick);
		}
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback deferred mismatch_tick=%u target_tick=%u flags=0x%02X sim=%u\n",
		    mismatch_tick,
		    target_tick,
		    (unsigned int)notify_flags,
		    syNetInputGetTick());
		return;
	}
	/*
	 * Baseline echo retry sets BaselineCompareQuiesced FALSE but must not defer SYNC: soak hang had
	 * ECHO_RETRY_DEFER + deferred SYNC + live-cap with 0× follower resim begin while seals rejected.
	 */
	if ((syNetRollbackBaselineCompareQuiesced() == FALSE) &&
	    (sSYNetRollbackBaselineEchoRetryLoadTick != ~(u32)0))
	{
		port_log(
		    "SSB64 NetRollback: peer symmetric queue despite echo_retry load_tick=%u mismatch_tick=%u target_tick=%u sim=%u\n",
		    sSYNetRollbackBaselineEchoRetryLoadTick,
		    mismatch_tick,
		    target_tick,
		    syNetInputGetTick());
	}
	syNetRollbackQueuePeerSymmetricNotify(slot, mismatch_tick, target_tick, notify_flags);
}

void syNetRollbackOnPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick, u8 notify_flags)
{
	syNetRollbackOnPeerSymmetricRollbackNotifyEx(slot, mismatch_tick, target_tick, 0U, 0U, notify_flags);
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
	/*
	 * Freeze the load_tick ring slot while baseline echo retry is armed or we are parked on the
	 * load anchor awaiting peer digest. Forward MpLanding / catch-up at sim==load_tick used to
	 * overwrite the armed baseline (soak1 608380406: map 0x0E9C39D5 → 0x0949DF1A mid-echo) and
	 * trip PEER_SNAPSHOT_DIVERGE against the peer's still-valid digest.
	 * See docs/bugs/netplay_baseline_universe_mismatch_ignored_2026-07-12.md.
	 */
	if ((sSYNetRollbackBaselineEchoRetryLoadTick != ~(u32)0) &&
	    (tick == sSYNetRollbackBaselineEchoRetryLoadTick))
	{
		port_log(
		    "SSB64 NetRollback: SNAPSHOT_SAVE_SKIP echo_retry_freeze tick=%u attempt=%u\n",
		    tick,
		    sSYNetRollbackBaselineEchoRetryAttempts);
		return FALSE;
	}
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
	    (tick == sSYNetRollbackResimLoadTick))
	{
		port_log(
		    "SSB64 NetRollback: SNAPSHOT_SAVE_SKIP awaiting_baseline_freeze tick=%u\n",
		    tick);
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Resim: refuse SavePostTick(T>load) until BattleSimOnly proved completion for T.
	 * Broken deepen labeled load Pupupu under mismatch → Linux[t]==Android[t-1] map PEER.
	 * See docs/bugs/netplay_deepen_resim_map_save_phase_skew_peer_2026-07-28.md.
	 */
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAnchorProbeActive == FALSE) &&
	    (sSYNetRollbackResimLoadTick != ~(u32)0) && (tick > sSYNetRollbackResimLoadTick) &&
	    (sSYNetRollbackResimBattleProvenThrough < tick))
	{
		port_log(
		    "SSB64 NetRollback: SNAPSHOT_SAVE_SKIP resim_no_battle_proof tick=%u load=%u proven=%u\n",
		    (unsigned int)tick,
		    (unsigned int)sSYNetRollbackResimLoadTick,
		    (unsigned int)sSYNetRollbackResimBattleProvenThrough);
		return FALSE;
	}
#endif
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

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Retroactive load-safe promotion. SavePostTick marks snapshots captured under predicted
 * remote input load-unsafe — correct at capture time, but stale once the wire strict-confirms
 * those inputs and published matches (state is then identical to a confirmed-path capture).
 * Without promotion, prediction-heavy peers keep only ancient load-safe slots: episode joins
 * rewind (EPISODE_LOAD_REWIND) to a load_tick the initiator does not share.
 *
 * Scan the live snapshot ring each committed tick and pin every eligible unsafe slot.
 * Do NOT require contiguous promotion from tick 1: an early Wait/bootstrap tick that is
 * still awaiting confirmed remote (or has no published row yet) used to `break` the walk
 * and permanently stall the watermark (soak 3218864814: 0× LOADSAFE_PROMOTE, then Linux
 * EPISODE_LOAD_REWIND 734→720). Load-safe slots are independent — FindLatestLoadSafe picks
 * the newest safe at-or-before the requested load; promoting T+1 without T is correct.
 * See docs/bugs/netplay_divergent_load_tick_baseline_stall_2026-07-12.md.
 */
#define SYNETROLLBACK_LOADSAFE_PROMOTE_BUDGET 16U
#define SYNETROLLBACK_LOADSAFE_PROMOTE_SCAN_MAX 128U

static void syNetRollbackPromoteConfirmedLoadSafeSnapshots(u32 completed_tick)
{
	u32 t;
	u32 min_t;
	u32 budget;
	u32 iter;
	u32 promoted;
	u32 skipped_pending;
	u32 ring_cap;
	u32 highest_promoted;

	if ((syNetRollbackIsActive() == FALSE) || (sSYNetRollbackResimPending != FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (sSYNetRollbackDeferredMismatchPending != FALSE) ||
	    (sSYNetRollbackDeferredStateMismatchPending != FALSE))
	{
		return;
	}
	if (completed_tick <= 1U)
	{
		return;
	}
	ring_cap = syNetRbSnapshotRingCapacity();
	min_t = 1U;
	if ((ring_cap > 2U) && (completed_tick > (ring_cap - 2U)))
	{
		min_t = completed_tick - (ring_cap - 2U);
		if (min_t < 1U)
		{
			min_t = 1U;
		}
	}
	/*
	 * Always rescan from the ring floor. Previously-skipped "pending" ticks (no confirmed
	 * yet) become promotable once the wire lands; starting only at watermark+1 left those
	 * holes load-unsafe forever after a stall.
	 */
	t = min_t;
	budget = SYNETROLLBACK_LOADSAFE_PROMOTE_BUDGET;
	iter = 0U;
	promoted = 0U;
	skipped_pending = 0U;
	highest_promoted = sSYNetRollbackLoadSafePromotedThrough;
	if (highest_promoted < min_t)
	{
		highest_promoted = (min_t > 1U) ? (min_t - 1U) : 0U;
	}
	while ((budget > 0U) && (iter < SYNETROLLBACK_LOADSAFE_PROMOTE_SCAN_MAX) && (t < completed_tick))
	{
		iter++;
		if (syNetRbSnapshotGetStoredSubsystemHashes(t, NULL, NULL, NULL, NULL) == FALSE)
		{
			/* No snapshot in ring (wrap / skipped save): nothing to pin. */
			if (t > highest_promoted)
			{
				highest_promoted = t;
			}
			t++;
			continue;
		}
		if (syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(t, t) == t)
		{
			if (t > highest_promoted)
			{
				highest_promoted = t;
			}
			t++;
			continue;
		}
		if (syNetInputRemoteHumanPublishedMatchesConfirmedForSimTick(t) == FALSE)
		{
			/*
			 * Not yet strict-confirmed, or published still disagrees (pending GGPO).
			 * Skip this tick and keep scanning — do not stall the whole ring.
			 */
			skipped_pending++;
			t++;
			continue;
		}
		syNetRbSnapshotPinLoadSafeAtTick(t);
		if (t > highest_promoted)
		{
			highest_promoted = t;
		}
		promoted++;
		budget--;
		t++;
	}
	sSYNetRollbackLoadSafePromotedThrough = highest_promoted;
	if ((promoted != 0U) && (sSYNetRollbackLoadSafePromoteLogBudget > 0U))
	{
		sSYNetRollbackLoadSafePromoteLogBudget--;
		port_log(
		    "SSB64 NetRollback: LOADSAFE_PROMOTE through=%u count=%u skipped_pending=%u sim=%u min=%u\n",
		    sSYNetRollbackLoadSafePromotedThrough,
		    promoted,
		    skipped_pending,
		    completed_tick,
		    min_t);
	}
	else if ((promoted == 0U) && (skipped_pending != 0U) && (sSYNetRollbackLoadSafePromoteLogBudget > 0U) &&
	         ((completed_tick & 63U) == 0U))
	{
		/* Rare stall breadcrumb: unsafe slots exist but none matched confirmed yet. */
		sSYNetRollbackLoadSafePromoteLogBudget--;
		port_log(
		    "SSB64 NetRollback: LOADSAFE_PROMOTE_PENDING through=%u skipped_pending=%u sim=%u min=%u\n",
		    sSYNetRollbackLoadSafePromotedThrough,
		    skipped_pending,
		    completed_tick,
		    min_t);
	}
}
#endif

#ifdef PORT
static SYNetRollbackHashSet syNetRollbackCollectHashes(void)
{
	SYNetRollbackHashSet hashes;

	hashes.fighter = syNetSyncHashBattleFightersFull();
	hashes.world = syNetSyncHashRollbackWorld();
	hashes.item = syNetSyncHashActiveItemsForRollback();
	hashes.weapon = syNetSyncHashActiveWeaponsForRollback();
	hashes.map = syNetRbSnapshotComputeMapHashLive();
	hashes.rng = syNetSyncHashRNGSeed();
	hashes.camera = syNetSyncHashGMCamera();
	hashes.animation = syNetSyncHashFighterAnimationStateForRollback();
	hashes.effect = syNetSyncHashActiveEffectsForRollback();
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
	u32 live_ef;

	if (sSYNetRollbackLoadHashVerify == FALSE)
	{
		return TRUE;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Synctest verify-only: effect/shield repair and figatree finalize can clobber fighter physics /
	 * joint anim after the slot blob was applied. Re-stamp canonical pose immediately before hashing.
	 */
	if ((syNetRbSnapRepairStageIsVerifyOnly() != FALSE) &&
	    (syNetRbSnapshotSlotVerifyPresentationFragileAtTick(tick) == FALSE))
	{
		syNetRbSnapshotReapplyJointAnimAtTick(tick);
	}
	syNetRbSnapshotFinalizeVerifyEffectState(tick);
#if defined(SSB64_NETMENU)
	/* Verify-only egg-lay re-pin (PrepareYoshiEggLayForVerifyHash) runs inside the loaded-slot
	 * prepare path — do not re-restore countdown fields here on the live synctest round-trip. */
	syNetRbSnapshotPrepareYoshiEggLayForVerifyHash(tick);
#endif
	/*
	 * FinalizeVerifyEffectState re-runs link-bomb window / quake / shield effect reconcile after
	 * PrepareLoadedSlotForVerify's item blob pass. Re-stamp slot item blobs immediately before the
	 * item hash compare (same pattern as FinalizeEffectsForVerifyHash for eff-only drift).
	 */
	if (syNetRbSnapshotGetSlotItemCount(tick) > 0U)
	{
		syNetRbSnapshotReconcileLoadedItemsForVerify(tick);
	}
#endif
#if defined(SSB64_NETMENU)
	syNetRbSnapshotCanonicalizeActiveItemsForNetplay();
	syNetplayCanonicalizeGMCameraSimState();
	if (syNetRbSnapRepairStageIsVerifyOnly() != FALSE)
	{
		syNetRbSnapshotRepairItemThrowWindowForVerify();
	}
#endif
	live_f = syNetSyncHashBattleFightersFull();
	live_w = syNetSyncHashRollbackWorld();
	live_i = syNetSyncHashActiveItemsForRollback();
#if defined(SSB64_NETMENU)
	/*
	 * Deferred unmatched weapons from ApplyWeapons must eject before the first wpn hash.
	 * Commit was previously only on post-verify success — soak2 Kirby cutter @749 left the
	 * tick-750 orphan live through VERIFY (slot count=1, live count=2).
	 */
	syNetRbSnapshotCommitDeferredWeaponEject(tick);
#endif
	live_wp = syNetSyncHashActiveWeaponsForRollback();
#if defined(SSB64_NETMENU)
	if (syNetRbSnapRepairStageIsVerifyOnly() != FALSE)
	{
		live_m = syNetRbSnapshotComputeMapHashLiveForVerify(tick);
	}
	else
#endif
	{
		live_m = syNetRbSnapshotComputeMapHashLive();
	}
	live_r = syNetSyncHashRNGSeed();
	live_c = syNetSyncHashGMCamera();
	live_a = syNetSyncHashFighterAnimationStateForRollback();
#if defined(SSB64_NETMENU)
	/*
	 * Item/camera canonicalize and the non-eff live hash passes can churn the effect link after the
	 * first finalize (efDisplayEnsureParticleDrawInfrastructure recreates dl 10/15/18 hooks; slot
	 * enforce ejects recycled-id=1011 landing VFX duplicates). soak2 @1869673246 tick 2789: eff_fold_diag
	 * right after the first finalize matched the slot (0xE84E34D5) but live_ef after canonicalize +
	 * fighter/item/map hashes did not (0xD669C0BF) -> false eff-only SYNCTEST_FAIL on both peers.
	 * Re-stamp slot-authoritative effects immediately before the eff compare.
	 */
	if (syNetRbSnapRepairStageIsVerifyOnly() != FALSE)
	{
		syNetRbSnapshotFinalizeEffectsForVerifyHash(tick);
		syNetSyncLogActiveEffectsFoldDiag("verify", tick);
	}
#endif
	live_ef = syNetSyncHashActiveEffectsForRollback();
	if (syNetRbSnapshotVerifyLiveFightersSanity(tick, "load_verify") == FALSE)
	{
		port_log("SSB64 NetRollback: LOAD_FIGHTER_SANITY_FAIL tick=%u — rejecting loaded slot\n", tick);
		return FALSE;
	}
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)) ||
	    (live_c != syNetRbSnapshotGetSlotHashCamera(tick)) || (live_a != syNetRbSnapshotGetSlotHashAnimation(tick)) ||
	    ((sSYNetRollbackVerifyEffectHash != FALSE) &&
	     (live_ef != syNetRbSnapshotGetSlotHashEffect(tick))))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT tick=%u figh=0x%08X/0x%08X world=0x%08X/0x%08X item=0x%08X/0x%08X "
		    "wpn=0x%08X/0x%08X map=0x%08X/0x%08X rng=0x%08X/0x%08X cam=0x%08X/0x%08X anim=0x%08X/0x%08X "
		    "eff=0x%08X/0x%08X class=snapshot_fidelity\n",
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
		    live_a,
		    syNetRbSnapshotGetSlotHashEffect(tick),
		    live_ef);
		syNetRbSnapshotLogFighterLoadVerifyDiag(tick,
		                                        live_f,
		                                        syNetRbSnapshotGetSlotHashFighter(tick),
		                                        live_a,
		                                        syNetRbSnapshotGetSlotHashAnimation(tick));
		syNetRbSnapshotLogGuardShieldLoadDriftDiag(tick,
		                                           live_f,
		                                           syNetRbSnapshotGetSlotHashFighter(tick),
		                                           live_a,
		                                           syNetRbSnapshotGetSlotHashAnimation(tick));
		syNetRbSnapshotLogFighterFieldDiffOnLoadDrift(tick);
		syNetDesyncClassifierOnLoadHashDrift(tick);
		if (live_m != syNetRbSnapshotGetSlotHashMap(tick))
		{
			syNetRbSnapshotLogMapHashDriftDiag(tick);
		}
		if (live_i != syNetRbSnapshotGetSlotHashItem(tick))
		{
			syNetSyncLogItemHashDriftDiag(tick,
			                             syNetRbSnapshotGetSlotHashItem(tick),
			                             live_i,
			                             "load_hash_drift_item");
			syNetSyncLogItemFieldDiffDiag(tick, syNetRbSnapshotGetSlotHashItem(tick), live_i, "load_hash_drift_item");
			syNetRbSnapshotLogItemBlobFieldDiffOnLoadDrift(tick);
		}
		else if (syNetRbSnapshotGetSlotItemCount(tick) > 0)
		{
			syNetSyncLogItemHashDriftDiag(tick,
			                             syNetRbSnapshotGetSlotHashItem(tick),
			                             live_i,
			                             "load_hash_drift_with_items");
		}
		if ((sSYNetRollbackVerifyEffectHash != FALSE) &&
		    (live_ef != syNetRbSnapshotGetSlotHashEffect(tick)))
		{
			if (syNetRbSnapYoshiEggLayCaptureWindowActiveWithoutEgg() != FALSE)
			{
				syNetRbSnapshotReconcileYoshiEggLayEffectsAtTick(tick);
				syNetRbSnapshotReconcileGuardShieldEffectsAtTick(tick);
			}
			else
			{
				syNetRbSnapshotReconcileGuardShieldEffectsAtTick(tick);
				syNetRbSnapshotReconcileYoshiEggLayEffectsAtTick(tick);
			}
			live_ef = syNetSyncHashActiveEffectsForRollback();
		}
		if (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick))
		{
			if (syNetRollbackTryRecoverWeaponHashDrift(tick, live_wp) != FALSE)
			{
				live_wp = syNetSyncHashActiveWeaponsForRollback();
			}
		}
		if (syNetRollbackVerifyLoadHashMatchesSlot(tick, live_f, live_w, live_i, live_wp, live_m, live_r, live_c,
		                                         live_a, live_ef) != FALSE)
		{
			port_log("SSB64 NetRollback: LOAD_HASH_DRIFT repair-ok — continuing verify tick=%u\n", tick);
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsPresentationalOnly(tick, live_f, live_w, live_i, live_wp, live_m, live_r,
		                                                 live_c, live_a, live_ef) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT presentational-only — continuing resim tick=%u snap_anim=0x%08X live_anim=0x%08X snap_cam=0x%08X live_cam=0x%08X\n",
			    tick,
			    syNetRbSnapshotGetSlotHashAnimation(tick),
			    live_a,
			    syNetRbSnapshotGetSlotHashCamera(tick),
			    live_c);
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsResimSimCoreOk(tick, live_f, live_w, live_i, live_wp, live_m, live_r) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT resim-sim-core-ok — continuing resim tick=%u figh=0x%08X/0x%08X anim=0x%08X/0x%08X eff=0x%08X/0x%08X item_count=%u\n",
			    tick,
			    syNetRbSnapshotGetSlotHashFighter(tick),
			    live_f,
			    syNetRbSnapshotGetSlotHashAnimation(tick),
			    live_a,
			    syNetRbSnapshotGetSlotHashEffect(tick),
			    live_ef,
			    (unsigned int)syNetRbSnapshotGetSlotItemCount(tick));
			return TRUE;
		}
		if ((syNetRollbackLoadHashSimCoreMatchesSlot(tick, live_f, live_w, live_i, live_wp, live_m, live_r) !=
		     FALSE) &&
		    (syNetRollbackTryRecoverEffectHashDrift(tick, live_ef) != FALSE))
		{
			return TRUE;
		}
		if (syNetRbSnapshotLoadHashEffSoftContinueBlocked(tick) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-continue blocked tick=%u reason=guard_escape_eff_coupling\n",
			    tick);
		}
		else if (syNetRollbackLoadHashDriftTrySoftContinue(tick, live_f, live_m) != FALSE)
		{
			return TRUE;
		}
#if defined(SSB64_NETMENU)
		if (syNetRollbackLoadVerifyPerSlotFighDriftOk(tick, live_f, live_w, live_i, live_wp, live_m, live_r,
		                                              live_a) != FALSE)
		{
			syNetRbSnapshotRefreshSlotHashFighterWhenPerSlotMatch(tick);
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT per-slot-figh-ok — continuing verify tick=%u ring_figh=0x%08X live_figh=0x%08X\n",
			    tick,
			    syNetRbSnapshotGetSlotHashFighter(tick),
			    live_f);
			return TRUE;
		}
		if (syNetRollbackFcRecoveryWpnOnlyDriftOk(tick, live_f, live_w, live_i, live_wp, live_m, live_r) != FALSE)
		{
			return TRUE;
		}
#endif
		return FALSE;
	}
	return TRUE;
#else
	(void)tick;
	return TRUE;
#endif
}

static sb32 syNetRollbackResimAnchorProbeEnabled(void)
{
	static int s_cached = -999;
	const char *e;

	if (s_cached != -999)
	{
		return (s_cached != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_RESIM_ANCHOR_PROBE");
	s_cached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_cached != 0) ? TRUE : FALSE;
}

/*
 * Hunt an earlier load-safe tick when walkback reload fails at the first candidate.
 * Returns TRUE when *io_load_tick was loaded successfully.
 */
static sb32 syNetRollbackAnchorProbeTryWalkbackLoad(u32 before_load, u32 min_load, u32 *io_load_tick,
						    u32 *io_mismatch_tick)
{
	u32 failed_floor;
	u32 hunt;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL) || (before_load == 0U))
	{
		return FALSE;
	}
	failed_floor = before_load;
	for (hunt = 0U; (hunt < SYNETROLLBACK_LOAD_TICK_REWIND_MAX) && (failed_floor > min_load) && (failed_floor > 0U);
	     hunt++)
	{
		u32 probe;
		u32 candidate;

		probe = failed_floor - 1U;
		if (probe < min_load)
		{
			break;
		}
		candidate = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(probe, min_load);
		if (candidate == ~(u32)0)
		{
			candidate = syNetRbSnapshotFindLatestValidTickAtOrBefore(probe, min_load);
		}
		if ((candidate == ~(u32)0) || (candidate >= before_load))
		{
			break;
		}
		if (syNetRollbackLoadPostTick(candidate) != FALSE)
		{
			*io_load_tick = candidate;
			*io_mismatch_tick = candidate + 1U;
			return TRUE;
		}
		port_log(
		    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_WALKBACK reload failed=%u (hunt from=%u)\n",
		    candidate,
		    before_load);
		failed_floor = candidate;
	}
	return FALSE;
}

/*
 * After load at load_tick, advance one sealed sim step and compare live hashes to the ring snapshot at
 * probe_tick (= load_tick + 1). Load load_tick, forward-sim one step, compare ring[probe_tick] to live.
 * Caller (resim begin) keeps the loaded world; diagnostic-only callers restore emergency after probe.
 * Returns TRUE when load+1 forward sim does not match ring figh/anim (actionable fidelity mismatch).
 */
#if defined(SSB64_NETMENU)
static u32 syNetRollbackAnchorProbeEpisodeMismatchTick(void)
{
	if ((sSYNetRollbackBeginResimInitialLoad != FALSE) &&
	    (sSYNetRollbackExecutingEpisode.valid != FALSE) &&
	    (sSYNetRollbackExecutingEpisode.mismatch_tick != 0U))
	{
		return sSYNetRollbackExecutingEpisode.mismatch_tick;
	}
	if ((sSYNetRollbackResimMismatchTick != 0U) && (sSYNetRollbackResimMismatchTick != ~(u32)0))
	{
		return sSYNetRollbackResimMismatchTick;
	}
	return 0U;
}
#endif

static sb32 syNetRollbackMaybeResimAnchorProbe(u32 load_tick)
{
	u32 probe_tick;
	sb32 emerg_ok;
	u32 ring_f;
	u32 ring_a;
	u32 ring_cam;
	u32 ring_load_f;
	u32 ring_load_a;
	SYNetRollbackHashSet live;
	sb32 keep_loaded;
	sb32 log_verbose;
	sb32 postload_figh_mismatch;
	sb32 postload_anim_mismatch;
	sb32 postload_mismatch;
	sb32 step_figh_mismatch;
	sb32 step_anim_mismatch;
	sb32 step_mismatch;
	sb32 actionable_mismatch;
#if defined(SSB64_NETMENU)
	sb32 intro_anchor_probe;
#endif

	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		return FALSE;
	}
	log_verbose = syNetRollbackResimAnchorProbeEnabled();
	probe_tick = load_tick + 1U;
	if ((probe_tick == 0U) || (syNetRbSnapshotIsTickCommitted(probe_tick) == FALSE))
	{
		return FALSE;
	}
	ring_f = syNetRbSnapshotGetSlotHashFighter(probe_tick);
	ring_a = syNetRbSnapshotGetSlotHashAnimation(probe_tick);
	ring_cam = syNetRbSnapshotGetSlotHashCamera(probe_tick);
#if defined(SSB64_NETMENU)
	intro_anchor_probe = syNetRbSnapshotAnyLiveFighterInIntroLoadFidelityScope();
#endif
	emerg_ok = syNetRbSnapshotCaptureLiveEmergency();
	keep_loaded = (sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE) ||
	              (sSYNetRollbackBeginResimInitialLoad != FALSE);

	if (syNetRbSnapshotLoad(load_tick) == FALSE)
	{
		if (emerg_ok != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		return FALSE;
	}
	syNetRbSnapshotPrepareLoadedSlotForVerify(load_tick);
	syNetRbSnapshotLogFighterStatusTrail("anchor_probe_prepare", load_tick);
	if (syNetRbSnapshotVerifyLiveFightersSanity(load_tick, "anchor_probe_prepare") == FALSE)
	{
		if (emerg_ok != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		port_log(
		    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_SANITY_FAIL load=%u — skipping probe forward sim\n",
		    load_tick);
		return TRUE;
	}
	/*
	 * Post-load oracle must run on the PrepareLoaded product only. ResyncLiveFightersFromSlotForSim
	 * re-runs joint topology / anchor-probe prep (VsLoadJointFidelityRepair, knockdown coll refresh)
	 * for the upcoming +1 sim — that deliberately perturbs figh fold fields and caused spurious
	 * postload_figh_fail with step_figh_ok on every tick (soak2 DK throw @503: 16-step walkback to
	 * load-16; Android follower anchored @487 vs Linux initiator @503).
	 */
	ring_load_f = syNetRbSnapshotGetSlotHashFighter(load_tick);
	ring_load_a = syNetRbSnapshotGetSlotHashAnimation(load_tick);
	live = syNetRollbackCollectHashes();
	postload_figh_mismatch = (ring_load_f != live.fighter) ? TRUE : FALSE;
	postload_anim_mismatch = (ring_load_a != live.animation) ? TRUE : FALSE;
	postload_mismatch = (postload_figh_mismatch != FALSE) || (postload_anim_mismatch != FALSE);
	if (log_verbose != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_POSTLOAD load=%u ring figh=0x%08X anim=0x%08X | "
		    "live figh=0x%08X anim=0x%08X match_f=%d match_a=%d postload_figh_fail=%d postload_anim_fail=%d\n",
		    load_tick,
		    ring_load_f,
		    ring_load_a,
		    live.fighter,
		    live.animation,
		    (postload_figh_mismatch == FALSE) ? 1 : 0,
		    (postload_anim_mismatch == FALSE) ? 1 : 0,
		    (postload_figh_mismatch != FALSE) ? 1 : 0,
		    (postload_anim_mismatch != FALSE) ? 1 : 0);
		if (postload_figh_mismatch != FALSE)
		{
			syNetRbSnapshotLogFighterFieldDiffAtTick(load_tick, "resim_anchor_probe_postload_figh");
		}
		if (postload_anim_mismatch != FALSE)
		{
			syNetRbSnapshotLogFighterFieldDiffAtTick(load_tick, "resim_anchor_probe_postload_anim");
		}
	}
#if defined(SSB64_NETMENU)
	/*
	 * Episode mismatch tick (FORCE_MISMATCH / corrected input): ring@probe was captured on forward
	 * sim before correction. When post-load restore matched, skip the +1 probe sim entirely — running
	 * it only poisons presentation (soak3 @520 DK joint spin) and forces spurious walkback.
	 */
	{
		u32 episode_mismatch = syNetRollbackAnchorProbeEpisodeMismatchTick();

		if ((postload_mismatch == FALSE) && (probe_tick == episode_mismatch) &&
		    (episode_mismatch != 0U))
		{
			if (log_verbose != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_STEP_BYPASS probe_tick=%u load=%u reason=postload_ok_episode_mismatch\n",
				    probe_tick,
				    load_tick);
			}
			sSYNetRollbackResimAnchorProbeLastMismatch = FALSE;
			syNetInputSetTick(load_tick);
			if (keep_loaded == FALSE)
			{
				if (emerg_ok != FALSE)
				{
					(void)syNetRbSnapshotRestoreLiveEmergency();
				}
			}
			return FALSE;
		}
	}
#endif
	syNetRbSnapshotResyncLiveFightersFromSlotForSim(load_tick);
	/*
	 * Reseed published/remote input slots from history at load_tick before the +1 probe step.
	 * BeginResim calls syNetInputRollbackPrepareForResim only after the walkback loop; without
	 * this, Turn→Dash and other tap_stick-window transitions diverge (soak3 @479: tap_stick 6 vs 254).
	 */
#if defined(SSB64_NETMENU)
	syNetInputSetTick(load_tick);
	syNetInputRollbackPrepareForResim(probe_tick);
#endif
	syNetInputSetTick(probe_tick);
	syNetInputPublishSynchronizedTick(probe_tick);
#if defined(SSB64_NETMENU)
	if ((intro_anchor_probe != FALSE) ||
	    (syNetRbSnapshotAnchorProbeWaitSteadyScopeAtTicks(load_tick, probe_tick) != FALSE) ||
	    (syNetRbSnapshotAnchorProbeMixedIntroPhysicsWaitScopeAtTicks(load_tick, probe_tick) != FALSE))
	{
		syNetRbSnapshotRebindFighterMPCollForAnchorProbePreSim();
		if (intro_anchor_probe != FALSE)
		{
			syNetRbSnapshotLogIntroAnchorSimTrail("pre", load_tick, probe_tick);
		}
	}
#endif
	sSYNetRollbackResimAnchorProbeActive = TRUE;
	scVSBattleFuncUpdateBattleSimOnly();
	sSYNetRollbackResimAnchorProbeActive = FALSE;
#if defined(SSB64_NETMENU)
	if (intro_anchor_probe != FALSE)
	{
		syNetRbSnapshotSyncAppearGobjTranslateFromTopNForAnchorProbe();
		syNetRbSnapshotReconcileAnchorProbeTransitionFromProbeSlot(load_tick, probe_tick);
		syNetRbSnapshotReconcileAnchorProbeAppearSteadyFromProbeSlot(load_tick, probe_tick);
	}
	/* Wait peers in mixed Appear/Entry+Wait steps and dual-Wait intro walkback (@149, @113). */
	syNetRbSnapshotReconcileAnchorProbeWaitSteadyFromProbeSlot(load_tick, probe_tick);
	syNetRbSnapshotReconcileAnchorProbeMixedAppearWaitFromProbeSlot(load_tick, probe_tick);
	syNetRbSnapshotReconcileAnchorProbeMixedEntryWaitFromProbeSlot(load_tick, probe_tick);
	if ((intro_anchor_probe == FALSE) &&
	    (syNetRbSnapshotAnyLiveFighterInIntroLoadFidelityScope() == FALSE))
	{
		syNetRbSnapshotReconcileAnchorProbeGameplayFromProbeSlot(load_tick, probe_tick);
	}
	if ((intro_anchor_probe != FALSE) ||
	    (syNetRbSnapshotAnchorProbeWaitSteadyScopeAtTicks(load_tick, probe_tick) != FALSE) ||
	    (syNetRbSnapshotAnchorProbeMixedIntroPhysicsWaitScopeAtTicks(load_tick, probe_tick) != FALSE))
	{
		/* Ring light fold uses gobj_translate for *p_translate; rebind after +1 sim before hash. */
		syNetRbSnapshotRebindFighterMPCollForAnchorProbe();
		syNetRbSnapshotTerminalAnchorProbeWaitFoldFromProbeSlot(load_tick, probe_tick);
		if (intro_anchor_probe != FALSE)
		{
			syNetRbSnapshotLogIntroAnchorSimTrail("post", load_tick, probe_tick);
		}
	}
#endif

	live = syNetRollbackCollectHashes();
#if defined(SSB64_NETMENU)
	{
		u32 ring_f_light;
		u32 live_f_light;

		ring_f_light = syNetRbSnapshotGetSlotHashFighterLight(probe_tick);
		live_f_light = syNetRbSnapshotHashFightersLightFromLive();
		step_figh_mismatch = (ring_f_light != live_f_light) ? TRUE : FALSE;
		if (intro_anchor_probe != FALSE)
		{
			ring_f = ring_f_light;
			live.fighter = live_f_light;
		}
	}
#else
	step_figh_mismatch = (ring_f != live.fighter) ? TRUE : FALSE;
#endif
	step_anim_mismatch = (ring_a != live.animation) ? TRUE : FALSE;
	step_mismatch = (step_figh_mismatch != FALSE) || (step_anim_mismatch != FALSE);
#if defined(SSB64_NETMENU)
	/*
	 * Episode mismatch tick uses corrected inputs on resim; ring@probe was captured on forward sim
	 * before correction (FORCE_MISMATCH btn XOR). Post-load fidelity is the anchor — skip +1 step
	 * compare at the original mismatch tick when load restore matched.
	 */
	{
		u32 episode_mismatch = syNetRollbackAnchorProbeEpisodeMismatchTick();

		if ((postload_mismatch == FALSE) && (step_mismatch != FALSE) &&
		    (probe_tick == episode_mismatch) && (episode_mismatch != 0U))
		{
			if (log_verbose != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_STEP_SKIPPED probe_tick=%u reason=input_correction_mismatch_tick\n",
				    probe_tick);
			}
			step_figh_mismatch = FALSE;
			step_anim_mismatch = FALSE;
			step_mismatch = FALSE;
		}
		/*
		 * Walkback hunt: post-load matched but +1 anim diverged (ring captured pre-correction inputs).
		 * Figh light already matched — anim-only step mismatch must not force deeper load rewind.
		 */
		if ((sSYNetRollbackBeginResimInitialLoad != FALSE) && (postload_mismatch == FALSE) &&
		    (step_figh_mismatch == FALSE) && (step_anim_mismatch != FALSE))
		{
			if (log_verbose != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_STEP_ANIM_SKIPPED load=%u probe_tick=%u reason=postload_ok_figh_ok_anim_only\n",
				    load_tick,
				    probe_tick);
			}
			step_anim_mismatch = FALSE;
			step_mismatch = FALSE;
		}
		/*
		 * Load landed one tick ahead of ring@load (postload figh drift) but +1 probe matches
		 * ring@probe — shared finalize path mints N+1 gameplay while slot digest still tags N.
		 * Accept: walkback would never find a better anchor (soak2 @503..504, postload_f step_f).
		 */
		if ((postload_figh_mismatch != FALSE) && (postload_anim_mismatch == FALSE) &&
		    (step_figh_mismatch == FALSE) && (step_anim_mismatch == FALSE))
		{
			if (log_verbose != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_POSTLOAD_BYPASS load=%u probe_tick=%u reason=postload_ahead_step_ok ring_figh=0x%08X live_figh=0x%08X\n",
				    load_tick,
				    probe_tick,
				    ring_load_f,
				    live.fighter);
			}
			postload_figh_mismatch = FALSE;
			postload_mismatch = FALSE;
		}
	}
#endif
	actionable_mismatch = (postload_mismatch != FALSE) || (step_mismatch != FALSE);
	if (log_verbose != FALSE)
	{
		syNetRbSnapshotLogRingBlobStatusTrailAtTick("anchor_probe_ring", probe_tick);
		port_log(
		    "SSB64 NetRollback: RESIM_ANCHOR_PROBE load=%u probe_tick=%u ring figh=0x%08X anim=0x%08X cam=0x%08X | "
		    "live figh=0x%08X anim=0x%08X cam=0x%08X match_f=%d match_a=%d match_cam=%d "
		    "postload_figh_fail=%d postload_anim_fail=%d step_figh_fail=%d step_anim_fail=%d\n",
		    load_tick,
		    probe_tick,
		    ring_f,
		    ring_a,
		    ring_cam,
		    live.fighter,
		    live.animation,
		    live.camera,
		    (step_figh_mismatch == FALSE) ? 1 : 0,
		    (step_anim_mismatch == FALSE) ? 1 : 0,
		    (ring_cam == live.camera) ? 1 : 0,
		    (postload_figh_mismatch != FALSE) ? 1 : 0,
		    (postload_anim_mismatch != FALSE) ? 1 : 0,
		    (step_figh_mismatch != FALSE) ? 1 : 0,
		    (step_anim_mismatch != FALSE) ? 1 : 0);
	}
	if (step_mismatch != FALSE)
	{
		if (log_verbose != FALSE)
		{
			syNetRbSnapshotLogFighterStatusTrail("anchor_probe_step", probe_tick);
			if (step_figh_mismatch != FALSE)
			{
				syNetRbSnapshotLogFighterFieldDiffAtTick(probe_tick, "resim_anchor_probe_step_figh");
			}
			if (step_anim_mismatch != FALSE)
			{
				syNetRbSnapshotLogFighterFieldDiffAtTick(probe_tick, "resim_anchor_probe_step_anim");
			}
		}
	}
	if ((actionable_mismatch != FALSE) && (syNetRbSnapshotIsTickCommitted(load_tick) != FALSE))
	{
		syNetRbSnapshotMarkLoadUnsafe(load_tick);
		port_log(
		    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_MISMATCH load=%u probe_tick=%u "
		    "postload_f=%d postload_a=%d step_f=%d step_a=%d ring figh=0x%08X anim=0x%08X live figh=0x%08X anim=0x%08X\n",
		    load_tick,
		    probe_tick,
		    (postload_figh_mismatch != FALSE) ? 1 : 0,
		    (postload_anim_mismatch != FALSE) ? 1 : 0,
		    (step_figh_mismatch != FALSE) ? 1 : 0,
		    (step_anim_mismatch != FALSE) ? 1 : 0,
		    ring_f,
		    ring_a,
		    live.fighter,
		    live.animation);
	}
	sSYNetRollbackResimAnchorProbeLastMismatch = actionable_mismatch;
	if (keep_loaded == FALSE)
	{
		if (emerg_ok != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
	}
	else
	{
		sb32 reload_ok;

		/* Undo load+1 probe sim before re-establishing post-load contract (keep_loaded resim paths). */
		syNetInputSetTick(load_tick);
		if (emerg_ok != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		reload_ok = syNetRollbackLoadPostTick(load_tick);
		if (reload_ok != FALSE)
		{
			syNetInputSetTick(load_tick);
		}
		else
		{
			syNetInputSetTick(load_tick);
			port_log(
			    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_RELOAD_FAIL load=%u — sim tick held at load anchor\n",
			    load_tick);
		}
	}
	return actionable_mismatch;
}

#if defined(SSB64_NETMENU)
static void syNetRollbackLoadPostTickCommitSideEffects(u32 load_tick)
{
	syNetRbSnapshotCommitDeferredWeaponEject(load_tick);
	/*
	 * Hold/Travel gate catch-up must not run on resim anchor loads or while awaiting peer baseline:
	 * EndIfDue/LaunchIfDue advance Fox/Pikachu status during load+verify (sim tick often ahead of
	 * load_tick), producing LOAD_SLOT_LIVE_DRIFT and PEER_SNAPSHOT_DIVERGE. Forward resim replays
	 * catch-up via syNetRbSnapRefreshFoxResimPresentationFromSlot / ApplyFighterNetplayPost once
	 * sSYNetRollbackResimBaselineGateOpen.
	 */
	if (syNetRollbackResimGateCatchUpAllowed() != FALSE)
	{
		syNetplayPikachuCatchUpAllAfterLoadVerify();
		syNetplayFoxCatchUpAllAfterLoadVerify();
	}
}

static void syNetRollbackWhispyPresentationAfterLoad(u32 tick, const char *reason)
{
	syNetRbSnapRepairPupupuWhispyPresentationAfterLoad(tick, reason);
}
#endif

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
	syNetRbSnapshotPrepareLoadedSlotForVerify(tick);
	syNetRbSnapshotLogFighterStatusTrail("load_post_prepare", tick);
	if (syNetRbSnapshotVerifyLiveFightersSanity(tick, "load_post_prepare") == FALSE)
	{
		if (emergency_valid != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		syNetRbSnapshotCancelDeferredWeaponEject();
		return FALSE;
	}
#endif
	if (syNetRollbackVerifyLoadedSlot(tick) == FALSE)
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
		u32 live_ef;

		sSYNetRollbackLoadFailCount++;
		live_f = syNetSyncHashBattleFightersFull();
		live_w = syNetSyncHashRollbackWorld();
		live_i = syNetSyncHashActiveItemsForRollback();
		live_wp = syNetSyncHashActiveWeaponsForRollback();
		live_m = syNetRbSnapshotComputeMapHashLive();
		live_r = syNetSyncHashRNGSeed();
		live_c = syNetSyncHashGMCamera();
		live_a = syNetSyncHashFighterAnimationStateForRollback();
		live_ef = syNetSyncHashActiveEffectsForRollback();
		/* Mid-baseline negotiation: slot/live drift is expected on poisoned history (~1033 soak). */
		if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
		    (sSYNetRollbackResimBaselineGateOpen == FALSE))
		{
			syNetRbSnapshotMarkLoadUnsafe(tick);
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-ignored (baseline storm) tick=%u\n",
			    tick);
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "baseline_storm");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
		if (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick))
		{
			if (syNetRollbackTryRecoverWeaponHashDrift(tick, live_wp) != FALSE)
			{
				live_wp = syNetSyncHashActiveWeaponsForRollback();
			}
		}
		if (syNetRollbackVerifyLoadHashMatchesSlot(tick, live_f, live_w, live_i, live_wp, live_m, live_r, live_c,
		                                         live_a, live_ef) != FALSE)
		{
			port_log("SSB64 NetRollback: LOAD_HASH_DRIFT repair-ok (post-verify) — continuing resim tick=%u\n",
			         tick);
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "repair_ok");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsPresentationalOnly(tick, live_f, live_w, live_i, live_wp, live_m, live_r,
		                                                 live_c, live_a, live_ef) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT presentational-only (post-verify) — continuing resim tick=%u\n",
			    tick);
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "presentational_only");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsResimSimCoreOk(tick, live_f, live_w, live_i, live_wp, live_m, live_r) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT resim-sim-core-ok (post-verify) — continuing resim tick=%u figh=0x%08X/0x%08X anim=0x%08X/0x%08X eff=0x%08X/0x%08X item_count=%u\n",
			    tick,
			    syNetRbSnapshotGetSlotHashFighter(tick),
			    live_f,
			    syNetRbSnapshotGetSlotHashAnimation(tick),
			    live_a,
			    syNetRbSnapshotGetSlotHashEffect(tick),
			    live_ef,
			    (unsigned int)syNetRbSnapshotGetSlotItemCount(tick));
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "resim_sim_core_ok");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
		if ((syNetRollbackLoadHashSimCoreMatchesSlot(tick, live_f, live_w, live_i, live_wp, live_m, live_r) !=
		     FALSE) &&
		    (syNetRollbackTryRecoverEffectHashDrift(tick, live_ef) != FALSE))
		{
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "effect_hash_recovered");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
		if ((live_w == syNetRbSnapshotGetSlotHashWorld(tick)) &&
		    (live_i == syNetRbSnapshotGetSlotHashItem(tick)) && (live_m == syNetRbSnapshotGetSlotHashMap(tick)) &&
		    (live_r == syNetRbSnapshotGetSlotHashRng(tick)) &&
		    (syNetRollbackFcRecoveryFighDriftOk(tick, live_f) != FALSE) &&
		    (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) &&
		    (syNetRollbackTryRecoverWeaponHashDrift(tick, live_wp) != FALSE))
		{
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "weapon_hash_recovered");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
		if (syNetRbSnapshotLoadHashEffSoftContinueBlocked(tick) != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT soft-continue blocked tick=%u reason=guard_escape_eff_coupling\n",
			    tick);
		}
		else if (syNetRollbackLoadHashDriftTrySoftContinue(tick, live_f, live_m) != FALSE)
		{
			syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
			syNetRollbackWhispyPresentationAfterLoad(tick, "soft_continue");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
			return TRUE;
		}
#if defined(SSB64_NETMENU)
		if (syNetRollbackFcRecoveryWpnOnlyDriftOk(tick, live_f, live_w, live_i, live_wp, live_m, live_r) != FALSE)
		{
			syNetRbSnapshotRebindAllFighters();
			syNetRollbackWhispyPresentationAfterLoad(tick, "fc_recovery_wpn_only");
			syNetRollbackLoadPostTickCommitSideEffects(tick);
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsResimLoadContext() != FALSE)
		{
			syNetRbSnapshotMarkLoadUnsafe(tick);
			port_log(
			    "SSB64 NetRollback: LOAD_HASH_DRIFT resim fidelity — deferring session stop tick=%u (caller may walk back)\n",
			    tick);
			if (emergency_valid != FALSE)
			{
				(void)syNetRbSnapshotRestoreLiveEmergency();
			}
			syNetRbSnapshotCancelDeferredWeaponEject();
			return FALSE;
		}
#endif
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT — restoring live world and stopping VS session (tick %u) class=snapshot_fidelity\n",
		    tick);
		syNetRollbackLogEpisodeProofDiverge(tick, "load_hash_drift_abort", "snapshot_fidelity");
		if (emergency_valid != FALSE)
		{
			(void)syNetRbSnapshotRestoreLiveEmergency();
		}
		syNetRbSnapshotCancelDeferredWeaponEject();
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetRollbackStopVSSession();
		syNetPeerStopVSSession();
#endif
		return FALSE;
	}
	syNetRbSnapshotReapplyJointAnimAtTick(tick);
#if defined(SSB64_NETMENU)
	syNetRollbackWhispyPresentationAfterLoad(tick, "verify_ok");
	syNetRollbackLoadPostTickCommitSideEffects(tick);
#endif
	if (syNetRollbackIsBattleSimHoldActive() == FALSE)
	{
		syNetRollbackClearBattleSimHoldAfterLoadFail();
	}
	else
	{
		port_log(
		    "SSB64 NetRollback: BATTLE_SIM_HOLD retained after load_post_ok tick=%u (load-fail freeze still active)\n",
		    tick);
	}
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

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Solo `.ssb64r` diagnostic: after BeginResim arms the peer baseline gate, self-match and open
 * the replay gate (no UDP peer / seal exchange).
 */
static void syNetRollbackOpenDiagnosticSoloReplayGate(void)
{
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return;
	}
	sSYNetRollbackResimBaselineDigestMatched = TRUE;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	syNetRollbackEpisodeResetPeerSealRowsState();
	syNetRollbackTryOpenResimReplayGate();
	if ((sSYNetRollbackResimBaselineGateOpen == FALSE) && (sSYNetRollbackResimPending != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: DIAGNOSTIC_SOLO_GATE_FORCE_OPEN load_tick=%u mismatch=%u target=%u\n",
		    sSYNetRollbackResimLoadTick,
		    sSYNetRollbackResimMismatchTick,
		    sSYNetRollbackResimTargetTick);
		if (sSYNetRollbackResimLoadTick != 0U)
		{
			syNetRbSnapshotRefreshPresentationForLoadedTick(sSYNetRollbackResimLoadTick);
		}
		if (syNetRollbackVerifyResimReplayLoadSafe(sSYNetRollbackResimLoadTick) == FALSE)
		{
			port_log(
			    "SSB64 NetRollback: diagnostic solo gate blocked load_tick=%u\n",
			    sSYNetRollbackResimLoadTick);
			return;
		}
		syNetRbSnapshotResetIntroPresentationRepairState();
		syNetRbSnapshotCosmeticAppearPresentationAfterReplayGate(sSYNetRollbackResimLoadTick);
		if (syNetRollbackEpisodeFsmEnabled() != FALSE)
		{
			syNetRollbackEpisodeFreezePostInputDigest();
		}
		syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseForwardResim);
		syNetRollbackAdvanceResimBudget();
	}
}

static void syNetRollbackTryDiagnosticForcedResim(u32 completed_tick)
{
	u32 want;
	u32 mismatch;
	u32 target;

	if (syNetReplayIsDiagnosticPlaybackActive() == FALSE)
	{
		return;
	}
	want = syNetReplayGetDiagnosticResimTick();
	if ((want == ~(u32)0) || (sSYNetRollbackDiagnosticResimConsumed != FALSE))
	{
		return;
	}
	if (completed_tick < want)
	{
		return;
	}
	if ((syNetRollbackIsActive() == FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return;
	}
	mismatch = (want > 4U) ? (want - 3U) : 1U;
	target = want + 1U;
	if (target <= mismatch)
	{
		target = mismatch + 1U;
	}
	sSYNetRollbackDiagnosticResimConsumed = TRUE;
	port_log(
	    "SSB64 NetRollback: DIAGNOSTIC_FORCED_RESIM completed=%u mismatch=%u target=%u (solo .ssb64r)\n",
	    completed_tick,
	    mismatch,
	    target);
	if (syNetRollbackBeginResim(mismatch, target, -1) == FALSE)
	{
		port_log("SSB64 NetRollback: DIAGNOSTIC_FORCED_RESIM begin failed\n");
		return;
	}
	syNetRollbackOpenDiagnosticSoloReplayGate();
}
#endif /* PORT && SSB64_NETMENU */

void syNetRollbackNoteLiveInterfaceRanThisPass(void)
{
#ifdef PORT
	sSYNetRollbackLiveInterfaceRanThisPass = TRUE;
#endif
}

void syNetRollbackOnLiveFuncUpdateBegin(void)
{
#ifdef PORT
	/*
	 * New FuncUpdate boundary: clear same-pass save block, then flush any Finish that was
	 * deferred because live interface already ran when resim completed mid-pass.
	 * Await stays armed until AllowLivePostBattleSave sees a real gcRunAll this pass.
	 */
	sSYNetRollbackLiveInterfaceRanThisPass = FALSE;
	sSYNetRollbackAwaitLiveSimSamePassBlock = FALSE;
	if (sSYNetRollbackFinishLiveDeferred != FALSE)
	{
		sSYNetRollbackFinishLiveDeferred = FALSE;
		sSYNetRollbackFinishLiveFlushing = TRUE;
		if (syNetRollbackFinishForwardResim() != FALSE)
		{
			if (sSYNetRollbackResimCompleteLogPending != FALSE)
			{
				u32 mismatch;

				mismatch = sSYNetRollbackResimCompleteLogMismatch;
				sSYNetRollbackResimCompleteLogPending = FALSE;
				sSYNetRollbackResimCompleteLogMismatch = 0U;
				syNetRollbackEmitResimCompleteAfterFinish(mismatch);
			}
		}
		sSYNetRollbackFinishLiveFlushing = FALSE;
	}
#endif
}

/* Once per frame after battle sim: persist the world that finished `syNetInputGetTick()` (completed sim index). */
sb32 syNetRollbackAllowLivePostBattleSave(sb32 live_battle_sim_ran, u32 tick_at_live_battle)
{
#ifdef PORT
	if (sSYNetRollbackAwaitLiveSimAfterResim == FALSE)
	{
		return TRUE;
	}
	/*
	 * Finish deferred to next FuncUpdate begin (or legacy SamePassBlock): do not treat this
	 * pass's pre-Finish live_battle_sim_ran as proof. No peer-local emergency/ring restore —
	 * that forked Hold gravity and JumpAerial TopN.x (soaks 2082786682 / 1903062639).
	 */
	if (sSYNetRollbackAwaitLiveSimSamePassBlock != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: POST_RESIM_LIVE_SAVE_DEFER sim=%u battle_ran=%d tick_at_sim=%u reason=finish_boundary_pending\n",
		    (unsigned int)syNetInputGetTick(),
		    (int)live_battle_sim_ran,
		    (unsigned int)tick_at_live_battle);
		return FALSE;
	}
	if ((live_battle_sim_ran == FALSE) || (syNetInputGetTick() != tick_at_live_battle))
	{
		port_log(
		    "SSB64 NetRollback: POST_RESIM_LIVE_SAVE_DEFER sim=%u battle_ran=%d tick_at_sim=%u\n",
		    (unsigned int)syNetInputGetTick(),
		    (int)live_battle_sim_ran,
		    (unsigned int)tick_at_live_battle);
		return FALSE;
	}
	sSYNetRollbackAwaitLiveSimAfterResim = FALSE;
	return TRUE;
#else
	(void)live_battle_sim_ran;
	(void)tick_at_live_battle;
	return TRUE;
#endif
}

void syNetRollbackAfterBattleUpdate(void)
{
	u32 completed_tick;

	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		return;
	}
	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	completed_tick = syNetInputGetTick();
#ifdef PORT
#if defined(SSB64_NETMENU)
	if (syNetplayRollbackLiveForwardSimEligible() != FALSE)
	{
		if (syNetRbSnapshotAnyFighterGrabCouplingActive() != FALSE)
		{
			syNetRbSnapshotRefreshGrabCouplingGeometry();
		}
		syNetplayNessRunLiveJibakuCatchUpAll();
		syNetplayRebirthCatchUpFightersTick();
		syNetRbSnapCatchUpCaptainGroundKickForwardIfDue();
		syNetRbSnapForwardPruneStaleKirbyInhaleWindEffects();
		syNetRbSnapForwardPruneStaleKirbyFinalCutterBladeEffects();
		syNetRbSnapForwardPruneStaleFoxReflectors();
		if (syNetRbSnapYoshiEggLayCaptureWindowActiveWithoutEgg() != FALSE)
		{
			syNetRbSnapReconcileYoshiEggLayEffectsLive();
			syNetRbSnapReconcileGuardShieldEffectsLive();
		}
		else
		{
			syNetRbSnapReconcileGuardShieldEffectsLive();
			syNetRbSnapReconcileYoshiEggLayEffectsLive();
		}
	}
#endif
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAnchorProbeActive == FALSE))
	{
		return;
	}
	/*
	 * Resim loop saves [mismatch, target) then advances GetTick to `target` (exclusive frontier).
	 * CloseCorrection sets resolved_through=target. Use strict `<` so the first live completion of
	 * `target` still SavePostTick — `<=` skipped that slot (soak1 SYNCTEST_FAIL@393 load_ok=0 after
	 * POST_RESIM_LIVE sim=393 with map_hash_save 391/392 then 394, never 393).
	 * See docs/bugs/netplay_synctest_post_resim_target_save_gap_2026-07-13.md.
	 */
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) && (completed_tick < sSYNetRollbackEpisodeResolvedThrough) &&
	    (sSYNetRollbackResimAnchorProbeActive == FALSE))
	{
#if defined(SSB64_NETMENU)
		if (syNetplayRollbackLiveForwardSimEligible() != FALSE)
		{
			syNetRbSnapshotFlushDeferredYoshiEggLayHatchCosmetics();
		}
#endif
		return;
	}
#if defined(SSB64_NETMENU)
	/* Ground-truth forward-vs-resim joint AObj probe (default off; independent of quantize). */
	syNetplayTraceActiveFighterAObj(completed_tick);
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayCanonicalizeActiveFightersForNetplay();
		syNetRbSnapshotCanonicalizeActiveItemsForNetplay();
		syNetRbSnapshotAfterSimLinkBombForwardRepair();
	}
#endif
#endif
	if (sSYNetRollbackResimAnchorProbeActive != FALSE)
	{
		if (syNetRollbackResimAnchorProbeEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_SAVE_SKIPPED tick=%u\n",
			    completed_tick);
		}
	}
	else
	{
		syNetRollbackSavePostTick(completed_tick);
#if defined(PORT) && defined(SSB64_NETMENU)
		syNetRollbackPromoteConfirmedLoadSafeSnapshots(completed_tick);
#endif
	}
#ifdef PORT
	syNetRollbackFlushDeferredPeerSymmetric();
	syNetRollbackPumpBaselineEchoRetry();
	if ((sSYNetRollbackResimAnchorProbeActive == FALSE) && (sSYNetRollbackSynctestEnabled != FALSE) &&
	    (completed_tick >= sSYNetRollbackSynctestNextProbeTick) && (completed_tick > 0U))
	{
		const char *skip_reason;
		u32 probe_tick;
		sb32 emergency_ok;
		sb32 verify_ok;
		sb32 load_ok;

		/*
		 * Mid-episode load/verify against a slot that resim is rewriting produces opaque
		 * SYNCTEST_FAIL (soak 1309587627 @1952 during stick+Whispy storm). Defer probe.
		 */
		if ((sSYNetRollbackResimPending != FALSE) || (syNetRollbackIsResimulating() != FALSE) ||
		    (sSYNetRollbackDeferredMismatchPending != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: SYNCTEST_SKIP tick=%u reason=resim_in_flight\n",
			    completed_tick);
			sSYNetRollbackSynctestNextProbeTick = completed_tick + 1U;
		}
		else if (syNetRbSnapshotSynctestShouldSkip(&skip_reason) != FALSE)
		{
			port_log("SSB64 NetRollback: SYNCTEST_SKIP tick=%u reason=%s\n",
			         completed_tick,
			         (skip_reason != NULL) ? skip_reason : "unknown");
#if defined(SSB64_NETMENU)
			syNetSyncLogItemThrowWindowDiag(completed_tick, skip_reason);
#endif
			sSYNetRollbackSynctestNextProbeTick = completed_tick + 1U;
			/*
			 * Arm a short post-Wait burst: first Go ticks (and first stick) often finish before
			 * the normal +120 cadence would run again (soak ended ~414 with only SYNCTEST_OK@389).
			 */
			if ((skip_reason != NULL) && (strcmp(skip_reason, "intro_wait") == 0))
			{
				sSYNetRollbackSynctestPostWaitEarlyRemaining = SYNETROLLBACK_SYNCTEST_POST_WAIT_EARLY_PROBES;
			}
		}
		else
		{
			probe_tick = completed_tick - 1U;
			/*
			 * Missing ring slot is not gameplay drift (post-resim target gap, ring eviction).
			 * Treat as SKIP — FAIL only when load+verify ran against a present snap.
			 */
			if (syNetRbSnapshotGetStoredSubsystemHashes(probe_tick, NULL, NULL, NULL, NULL) == FALSE)
			{
				port_log(
				    "SSB64 NetRollback: SYNCTEST_SKIP tick=%u reason=no_snapshot completed=%u "
				    "resolved_through=%u\n",
				    probe_tick,
				    completed_tick,
				    sSYNetRollbackEpisodeResolvedThrough);
				sSYNetRollbackSynctestNextProbeTick = completed_tick + 1U;
			}
			else
			{
#if defined(SSB64_NETMENU)
				syNetRbSnapStashYoshiEggLayWaitTimerBeforeSynctest(probe_tick);
#endif
				emergency_ok = syNetRbSnapshotCaptureLiveEmergency();
				verify_ok = FALSE;
				load_ok = FALSE;
				if (emergency_ok != FALSE)
				{
					syNetRbSnapRepairStageSetVerifyOnly(TRUE);
				}
				if (emergency_ok != FALSE)
				{
					load_ok = syNetRbSnapshotLoad(probe_tick);
				}
				if ((emergency_ok != FALSE) && (load_ok != FALSE))
				{
					syNetRbSnapshotPrepareLoadedSlotForVerify(probe_tick);
					syNetRbSnapDiagLogGuardShieldJointPose("probe_loaded_pre_verify");
					verify_ok = syNetRollbackVerifyLoadedSlot(probe_tick);
					syNetRbSnapDiagLogGuardShieldJointPose("probe_post_verify");
				}
				syNetRbSnapRepairStageSetVerifyOnly(FALSE);
				if (emergency_ok != FALSE)
				{
					(void)syNetRbSnapshotRestoreLiveEmergency();
#if defined(SSB64_NETMENU)
					syNetRbSnapRestoreYoshiEggLayWaitTimerAfterSynctest(probe_tick, "synctest_restore");
#endif
					syNetRbSnapshotRecoverGuardShieldBubblesAfterSynctest();
					syNetRbSnapshotRecoverYoshiEggLayHatchAfterSynctest();
					syNetRbSnapshotPurgeOrphanEffectShellsAfterSynctest();
					syNetRbSnapDiagLogGuardShieldJointPose("synctest_post_recover");
#if defined(SSB64_NETMENU)
					/*
					 * Synctest load pins CObj with used_run=0 (load-hash fidelity). Emergency restore
					 * puts live CObj back. Do NOT unconditionally gmCameraRunFuncCamera here — that
					 * advances hashed camera scalars one tick on the probing peer only (soak1
					 * DamageFly SYNCTEST@1713). Integrate only when CObj.at is interest-yanked
					 * (>500), preserving the cliffwait quake recovery path.
					 * See docs/bugs/netplay_synctest_emergency_damagefly_mpcoll_2026-07-27.md,
					 * docs/bugs/netplay_cliffwait_camera_cobj_yank_2026-07-10.md.
					 */
					syNetRbSnapSynctestRecoverCameraIfInterestYanked();
					syNetRollbackWhispyPresentationAfterLoad(completed_tick, "synctest_restore");
					syNetRbSnapDiagLogGuardShieldJointPose("synctest_post_whispy_presentation");
#endif
				}
				if (verify_ok == FALSE)
				{
					port_log(
					    "SSB64 NetRollback: SYNCTEST_FAIL tick=%u emergency_ok=%d load_ok=%d "
					    "(LOAD_HASH_DRIFT above if verify ran; else load/emergency failed)\n",
					    probe_tick,
					    (int)emergency_ok,
					    (int)load_ok);
				}
				else
				{
					port_log("SSB64 NetRollback: SYNCTEST_OK tick=%u\n", probe_tick);
				}
				if (sSYNetRollbackSynctestPostWaitEarlyRemaining > 0U)
				{
					sSYNetRollbackSynctestPostWaitEarlyRemaining--;
					sSYNetRollbackSynctestNextProbeTick = completed_tick + 1U;
					port_log(
					    "SSB64 NetRollback: SYNCTEST_EARLY_POST_WAIT next=%u remaining=%u\n",
					    sSYNetRollbackSynctestNextProbeTick,
					    sSYNetRollbackSynctestPostWaitEarlyRemaining);
				}
				else
				{
					sSYNetRollbackSynctestNextProbeTick = completed_tick + 120U;
				}
			}
		}
	}
#if defined(SSB64_NETMENU)
	syNetRbSnapshotFlushDeferredYoshiEggLayHatchCosmetics();
	syNetRollbackTryDiagnosticForcedResim(completed_tick);
#endif
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
#if defined(SSB64_NETMENU)
	/*
	 * Ticks replayed with hold_last guesses are not resolved: a wire REPLACE that landed
	 * mid-resim gets promote-patched into published (post_pre_promote), so the reconcile
	 * below would see published==remote and clear the earliest-incorrect mark even though
	 * the *state* for that tick was simmed with the guess. Keep those marks so the next
	 * episode's scan deepens to the true fork (soak 2026-07-27 tick 397).
	 */
	{
		u32 watermark;

		watermark = syNetInputResimLowestPredictedReplayTick();
		if ((watermark != 0U) && (watermark < to_tick))
		{
			to_tick = watermark;
		}
	}
#endif
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
	slot = syNetRollbackResolveRemoteHumanPlayer(-1);
	if (slot >= 0)
	{
		return slot;
	}
	return -1;
}

static void syNetRollbackConsumePendingForceMismatchAfterResim(u32 mismatch_tick, u32 completed_target)
{
	u32 pending;

	if (sSYNetRollbackForceMismatch == FALSE)
	{
		return;
	}
	pending = sSYNetRollbackForceMismatchPendingTick;
	if (pending == ~(u32)0)
	{
		return;
	}
	if ((mismatch_tick == 0U) || (mismatch_tick == ~(u32)0) || (completed_target == 0U) ||
	    (completed_target == ~(u32)0) || (completed_target <= mismatch_tick))
	{
		return;
	}
	if ((pending < mismatch_tick) || (pending >= completed_target))
	{
		return;
	}
	port_log(
	    "SSB64 NetRollback: FORCE_MISMATCH consume post-resim tick=%u span=[%u,%u) (sealed inputs authoritative)\n",
	    pending,
	    mismatch_tick,
	    completed_target);
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = TRUE;
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
	if (sSYNetRollbackResimPending != FALSE)
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
	syNetSyncCollectFighterSlotHashes(out_slot_hash);
}

static sb32 syNetRollbackBaselineUniverseRepeatStorm(u32 load_tick, u32 peer_figh, u32 local_figh)
{
	if ((load_tick != sSYNetRollbackBaselineUniverseRepeatLoad) ||
	    (peer_figh != sSYNetRollbackBaselineUniverseRepeatPeerFigh) ||
	    (local_figh != sSYNetRollbackBaselineUniverseRepeatLocalFigh))
	{
		sSYNetRollbackBaselineUniverseRepeatLoad = load_tick;
		sSYNetRollbackBaselineUniverseRepeatPeerFigh = peer_figh;
		sSYNetRollbackBaselineUniverseRepeatLocalFigh = local_figh;
		sSYNetRollbackBaselineUniverseRepeatCount = 1U;
		return FALSE;
	}
	sSYNetRollbackBaselineUniverseRepeatCount++;
	if (sSYNetRollbackBaselineUniverseRepeatCount > SYNETROLLBACK_BASELINE_UNIVERSE_REPEAT_CAP)
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_UNIVERSE_STORM_CAP load_tick=%u peer_figh=0x%08X local_figh=0x%08X repeats=%u — suppressing baseline retransmit\n",
		    load_tick,
		    peer_figh,
		    local_figh,
		    sSYNetRollbackBaselineUniverseRepeatCount);
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRollbackBaselineProceedOnTimeoutEnabled(void)
{
	const char *env;

	env = getenv("SSB64_NETPLAY_RESIM_BASELINE_PROCEED_ON_TIMEOUT");
	return ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? TRUE : FALSE;
}

static sb32 syNetRollbackResimAwaitingPeerSealRows(void)
{
	if ((syNetRollbackEpisodeFsmEnabled() == FALSE) || (syNetRollbackEpisodeInputsSealed() == FALSE) ||
	    (sSYNetRollbackResimBaselineDigestMatched == FALSE))
	{
		return FALSE;
	}
	return (syNetRollbackEpisodeAllPeerSealRowsComplete() == FALSE) ? TRUE : FALSE;
}

static u32 syNetRollbackGetResimBaselineGateTimeoutFrames(void)
{
	u32 span;
	u32 chunks;
	u32 timeout;
	const char *env;
	int env_min;

	timeout = SYNETROLLBACK_BASELINE_GATE_TIMEOUT_FRAMES;
	if (syNetRollbackEpisodeInputsSealed() != FALSE)
	{
		span = syNetRollbackEpisodeGetSealSpan();
		if (span > 0U)
		{
			chunks = (span + SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX - 1U) /
			         SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX;
			timeout += chunks * SYNETROLLBACK_BASELINE_GATE_TIMEOUT_SPAN_CHUNK_FRAMES;
		}
	}
	env = getenv("SSB64_NETPLAY_RESIM_BASELINE_GATE_TIMEOUT_MIN");
	if ((env != NULL) && (env[0] != '\0'))
	{
		env_min = atoi(env);
		if (env_min > (int)timeout)
		{
			timeout = (u32)env_min;
		}
	}
	if (timeout > SYNETROLLBACK_BASELINE_GATE_TIMEOUT_MAX_FRAMES)
	{
		timeout = SYNETROLLBACK_BASELINE_GATE_TIMEOUT_MAX_FRAMES;
	}
	return timeout;
}

static void syNetRollbackEpisodeReset(void)
{
	memset(&sSYNetRollbackEpisode, 0, sizeof(sSYNetRollbackEpisode));
#if defined(SSB64_NETMENU)
	sSYNetRollbackLightEpisodeActive = FALSE;
#endif
}

static void syNetRollbackAlignResimLoadTickToWireBaseline(void)
{
	u32 wire_load;

	if ((sSYNetRollbackResimAwaitingPeerBaseline == FALSE) || (sSYNetRollbackPeerBaselineLoadTick == 0U))
	{
		return;
	}
	wire_load = sSYNetRollbackPeerBaselineLoadTick;
	if (wire_load == sSYNetRollbackResimLoadTick)
	{
		return;
	}
	port_log(
	    "SSB64 NetRollback: BASELINE_WIRE_LOAD_ALIGN episode_load=%u wire_load=%u sim=%u\n",
	    sSYNetRollbackResimLoadTick,
	    wire_load,
	    syNetInputGetTick());
	sSYNetRollbackResimLoadTick = wire_load;
}

static void syNetRollbackEpisodeSyncToLegacy(void)
{
	/*
	 * Light episodes never arm the FSM — routing through FsmSyncToLegacy would read the
	 * inactive FSM (phase Live) and clobber ResimPending mid-replay. Use legacy fields.
	 */
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackLightEpisodeIsActive() == FALSE))
	{
		syNetRollbackEpisodeFsmSyncToLegacy(&sSYNetRollbackResimPending, &sSYNetRollbackResimAwaitingPeerBaseline,
						  &sSYNetRollbackResimBaselineGateOpen, &sSYNetRollbackResimMismatchTick,
						  &sSYNetRollbackResimLoadTick, &sSYNetRollbackResimTargetTick,
						  &sSYNetRollbackResimCorrectionPlayer, &sSYNetRollbackResimFromPeerSymmetric);
		syNetRollbackAlignResimLoadTickToWireBaseline();
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
	syNetRollbackAlignResimLoadTickToWireBaseline();
}

static void syNetRollbackEpisodeBegin(u32 mismatch_tick, u32 load_tick, u32 target_tick, s32 corrected_slot,
				      sb32 initiator, sb32 from_peer_notify, sb32 light)
{
	if (sSYNetRollbackEpochId < ~(u32)0)
	{
		sSYNetRollbackEpochId++;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Light input episode: local-only GGPO correction. No FSM begin, no input seal,
	 * no baseline gate — phase goes straight to ForwardResim so the span replays
	 * immediately. See docs/bugs/netplay_light_input_episodes_2026-07-27.md.
	 */
	if (light != FALSE)
	{
		sSYNetRollbackLightEpisodeActive = TRUE;
		sSYNetRollbackEpisode.phase = nSYNetRollbackEpisodePhaseForwardResim;
		sSYNetRollbackEpisode.mismatch_tick = mismatch_tick;
		sSYNetRollbackEpisode.load_tick = load_tick;
		sSYNetRollbackEpisode.target_tick = target_tick;
		sSYNetRollbackEpisode.corrected_slot = corrected_slot;
		sSYNetRollbackEpisode.initiator = initiator;
		sSYNetRollbackEpisode.from_peer_notify = FALSE;
		syNetRollbackEpisodeSyncToLegacy();
		return;
	}
#else
	(void)light;
#endif
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmBegin(sSYNetRollbackEpochId, mismatch_tick, load_tick, target_tick, corrected_slot,
					     initiator, from_peer_notify);
		/* Seal through FSM-clamped target (span may exceed SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN). */
		syNetRollbackEpisodeSealInputs(mismatch_tick, syNetRollbackEpisodeFsmGetTargetTick(), corrected_slot);
		sSYNetRollbackEpisode.mismatch_tick = mismatch_tick;
		sSYNetRollbackEpisode.load_tick = load_tick;
		sSYNetRollbackEpisode.target_tick = syNetRollbackEpisodeFsmGetTargetTick();
		sSYNetRollbackEpisode.corrected_slot = corrected_slot;
		sSYNetRollbackEpisode.initiator = initiator;
		sSYNetRollbackEpisode.from_peer_notify = from_peer_notify;
		sSYNetRollbackEpisode.phase = nSYNetRollbackEpisodePhaseAwaitingBaseline;
		syNetRollbackEpisodeSyncToLegacy();
		syNetRollbackTryOpenBaselineGateFromStashedPeerOutcome();
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
	syNetRollbackTryOpenBaselineGateFromStashedPeerOutcome();
}

static void syNetRollbackEpisodeSetPhase(SYNetRollbackEpisodePhase phase)
{
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackLightEpisodeIsActive() == FALSE))
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

static void syNetRollbackEmitResimCompleteAfterFinish(u32 completed_mismatch)
{
	/* rbe sched shadow: one episode ≈ one mispredict ridden for ~depth ticks;
	 * arms rbe cushion rebuild at the boundary. */
	syNetRbeSchedNoteResimComplete(completed_mismatch, syNetInputGetTick());
	syNetSyncLogRollbackWorldDetail("rollback_post", completed_mismatch);
	syNetSyncLogFighterDetail("rollback_post", completed_mismatch);
	ifCommonItemArrowPruneStaleInterfaces();
	syNetRollbackLogResimComplete();
	syNetRollbackOnResimCompleted();
	syNetRollbackArmDebounceAfterResim();
}

static sb32 syNetRollbackFinishForwardResim(void)
{
	u32 completed_target;
	u32 mismatch_tick;
	s32 correction_player;

	/*
	 * Boundary Finish: if live interface already ran this FuncUpdate, defer closing to Live
	 * until the next OnLiveFuncUpdateBegin so both peers share the same post-resim path
	 * (no SamePass emergency restore). Arm Await+SamePassBlock only to reject stale Allow.
	 */
	if ((sSYNetRollbackFinishLiveFlushing == FALSE) &&
	    (sSYNetRollbackLiveInterfaceRanThisPass != FALSE))
	{
		/*
		 * Still pin feel-0 while deferred — Promote waits until depth clears, but mid-span
		 * holes must not wait for Commit Finish (soak 449622004: FINISH_DEFER @435 then
		 * Verify early-return skipped Resync → LOCAL_PUBLISH_LATCH_REFUSE 433-435).
		 * See docs/bugs/netplay_post_resim_finish_defer_feel0_pin_2026-07-26.md.
		 */
		if ((sSYNetRollbackResimMismatchTick != 0U) &&
		    (sSYNetRollbackResimMismatchTick != ~(u32)0U) &&
		    (sSYNetRollbackResimTargetTick != 0U) &&
		    (sSYNetRollbackResimTargetTick != ~(u32)0U) &&
		    (sSYNetRollbackResimTargetTick > sSYNetRollbackResimMismatchTick))
		{
			syNetInputRollbackResyncControllersAfterResim(sSYNetRollbackResimMismatchTick,
			                                             sSYNetRollbackResimTargetTick);
		}
		sSYNetRollbackFinishLiveDeferred = TRUE;
		sSYNetRollbackAwaitLiveSimAfterResim = TRUE;
		sSYNetRollbackAwaitLiveSimSamePassBlock = TRUE;
		port_log(
		    "SSB64 NetRollback: FINISH_DEFER_TO_FUNCUPDATE_BEGIN sim=%u target=%u (live interface already ran)\n",
		    (unsigned int)syNetInputGetTick(),
		    (unsigned int)sSYNetRollbackResimTargetTick);
		return FALSE;
	}

	completed_target = sSYNetRollbackResimTargetTick;
	mismatch_tick = sSYNetRollbackResimMismatchTick;
	correction_player = sSYNetRollbackResimCorrectionPlayer;
	/* Light episodes skip the Verify/peer post-digest handshake — close directly. */
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackLightEpisodeIsActive() == FALSE) &&
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
			/*
			 * Verify early-return used to skip Resync entirely; Commit Finish later
			 * Promotes without feel-0 → LATCH_REFUSE. Pin here (Promote still waits
			 * for depth=0 on the Commit fall-through).
			 */
			if ((mismatch_tick != 0U) && (mismatch_tick != ~(u32)0U) && (completed_target != 0U) &&
			    (completed_target != ~(u32)0U) && (completed_target > mismatch_tick))
			{
				syNetInputRollbackResyncControllersAfterResim(mismatch_tick, completed_target);
			}
			return TRUE;
		}
	}
	if ((mismatch_tick != 0U) && (mismatch_tick != ~(u32)0U) && (completed_target != 0U) &&
	    (completed_target != ~(u32)0U) && (completed_target > mismatch_tick))
	{
		syNetInputRollbackReconcileAfterResimCompleted(mismatch_tick, completed_target, correction_player);
	}
#if defined(SSB64_NETMENU)
	/*
	 * Capture before EpisodeReset clears sSYNetRollbackLightEpisodeActive.
	 * Light resim SavePostTick covers [mismatch, target) only; exclusive target
	 * stays first-pass poison under AwaitLiveSimAfterResim. Invalidate so the
	 * next TryBegin cannot ResolveLoadTick onto that slot.
	 * See docs/bugs/netplay_light_exclusive_frontier_poison_load_2026-07-28.md.
	 */
	sb32 light_finishing = syNetRollbackLightEpisodeIsActive();
#endif
	syNetRollbackCloseCorrectionEpisode(completed_target);
	syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseLive);
	syNetRollbackEpisodeReset();
#if defined(SSB64_NETMENU)
	if ((light_finishing != FALSE) && (completed_target != 0U) && (completed_target != ~(u32)0U))
	{
		syNetRbSnapshotInvalidateTick(completed_target);
		port_log("SSB64 NetRollback: LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE tick=%u "
		         "(light finish; first-pass poison not loadable)\n",
		         (unsigned int)completed_target);
	}
#endif
	sSYNetRollbackResimPending = FALSE;
	/* Exclusive frontier: block live save/commit until the next real gcRunAll for GetTick. */
	sSYNetRollbackAwaitLiveSimAfterResim = TRUE;
	/* Boundary Finish: never arm SamePass restore — next pass's interface is the proof battle. */
	sSYNetRollbackAwaitLiveSimSamePassBlock = FALSE;
	/*
	 * Early post-Wait probes can arm next=target-1 while resim is in flight. Bump past the
	 * exclusive target so synctest does not race the first post-resim SavePostTick.
	 */
	if ((completed_target != 0U) && (completed_target != ~(u32)0U) &&
	    (sSYNetRollbackSynctestNextProbeTick <= completed_target))
	{
		sSYNetRollbackSynctestNextProbeTick = completed_target + 1U;
	}
	if ((mismatch_tick != 0U) && (mismatch_tick != ~(u32)0U) && (completed_target != 0U) &&
	    (completed_target != ~(u32)0U) && (completed_target > mismatch_tick))
	{
		syNetInputRollbackResyncControllersAfterResim(mismatch_tick, completed_target);
	}
#if defined(SSB64_NETMENU)
	if (syNetplayRollbackLiveForwardSimEligible() != FALSE)
	{
		if (syNetRbSnapYoshiEggLayCaptureWindowActiveWithoutEgg() != FALSE)
		{
			syNetRbSnapReconcileYoshiEggLayEffectsLive();
			syNetRbSnapReconcileGuardShieldEffectsLive();
		}
		else
		{
			syNetRbSnapReconcileGuardShieldEffectsLive();
			syNetRbSnapReconcileYoshiEggLayEffectsLive();
		}
	}
#endif
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	sSYNetRollbackResimNextTick = ~(u32)0;
#if defined(SSB64_NETMENU)
	sSYNetRollbackResimBattleProvenThrough = 0U;
#endif
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackResimBudgetedCatchUpLogged = FALSE;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
#if defined(SSB64_NETMENU)
	/*
	 * Resync pinned exclusive-target gameplay while ResimDepth>0 blocked Promote. Flush
	 * LOCAL_PUBLISH now so the first live tick cannot BRANCH_COMMITTED without wire
	 * (soak 1334008206 @420).
	 */
	if ((mismatch_tick != 0U) && (mismatch_tick != ~(u32)0U) && (completed_target != 0U) &&
	    (completed_target != ~(u32)0U) && (completed_target > mismatch_tick))
	{
		syNetInputRollbackPromoteAfterResimExit(mismatch_tick, completed_target);
	}
#endif
	syNetRollbackClearSymmetricNotifyAll();
	syNetRollbackClearPeerSymmetricRejectLiveCap();
	if (syNetRollbackDeferDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_PREEMPTIVE_LIVE_CAP_CLEAR resim_complete sim=%u resolved_through=%u\n",
		    syNetInputGetTick(),
		    sSYNetRollbackEpisodeResolvedThrough);
	}
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
	return TRUE;
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
	sb32 seal_stall;
	sb32 baseline_storm;

	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		return;
	}
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) ||
	    (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return;
	}
	/*
	 * Peer baseline often arrives as PREEMPTIVE before AwaitingBaseline. Re-compare the stash
	 * each pump so matching digests open the gate instead of storm-capping matched figh.
	 * Soak1 2125145770: Linux stuck at load=416 with peer_figh==local_figh, baseline_matched=0.
	 */
	syNetRollbackTryOpenBaselineGateFromStashedPeerOutcome();
	if ((sSYNetRollbackResimBaselineGateOpen != FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE))
	{
		return;
	}
	seal_stall = syNetRollbackResimAwaitingPeerSealRows();
	baseline_storm = FALSE;
	if ((sSYNetRollbackResimBaselineDigestMatched == FALSE) && (sSYNetRollbackLastPeerOutcomeValid != FALSE) &&
	    (sSYNetRollbackLastPeerOutcomeTick == sSYNetRollbackResimLoadTick) &&
	    (syNetRollbackPeerDigestUniverseMismatch(&sSYNetRollbackLastPeerOutcomeHash) != FALSE) &&
	    (syNetRollbackBaselineUniverseRepeatStorm(sSYNetRollbackResimLoadTick,
						       sSYNetRollbackLastPeerOutcomeHash.fighter,
						       sSYNetRollbackPeerBaselineFigh) != FALSE))
	{
		baseline_storm = TRUE;
	}
	/* Rate-limit baseline retransmits; keep pumping seal rows when baseline already matched. */
	if ((sSYNetRollbackResimBaselineDigestMatched == FALSE) && (baseline_storm == FALSE) &&
	    (sSYNetRollbackPeerBaselineRetransmitCount > 0U) &&
	    ((sSYNetRollbackResimBaselineWaitFrames & 1U) != 0U) && (seal_stall == FALSE))
	{
		return;
	}
	if ((sSYNetRollbackResimBaselineDigestMatched == FALSE) && (baseline_storm == FALSE))
	{
		sSYNetRollbackPeerBaselineSendPending = TRUE;
		syNetPeerTrySendRollbackBaselineDigest();
	}
	if ((seal_stall != FALSE) || (syNetRollbackEpisodeInputsSealed() != FALSE))
	{
		syNetRollbackEpisodePrepareSealRowsRetransmit();
		syNetRollbackEpisodePumpOutboundSealRows(4U);
	}
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

static SYNetRollbackHashSet syNetRollbackCollectSlotBaselineDigests(u32 load_tick)
{
	SYNetRollbackHashSet slot;

	memset(&slot, 0, sizeof(slot));
	slot.fighter = syNetRbSnapshotGetSlotHashFighter(load_tick);
	slot.world = syNetRbSnapshotGetSlotHashWorld(load_tick);
	slot.item = syNetRbSnapshotGetSlotHashItem(load_tick);
	slot.rng = syNetRbSnapshotGetSlotHashRng(load_tick);
	slot.animation = syNetRbSnapshotGetSlotHashAnimation(load_tick);
	slot.weapon = syNetRbSnapshotGetSlotHashWeapon(load_tick);
	slot.map = syNetRbSnapshotGetSlotHashMap(load_tick);
	slot.camera = syNetRbSnapshotGetSlotHashCamera(load_tick);
	slot.effect = syNetRbSnapshotGetSlotHashEffect(load_tick);
	return slot;
}

static void syNetRollbackArmResimBaselineAfterLoad(u32 load_tick)
{
	SYNetRollbackHashSet live;
	SYNetRollbackHashSet wire;

	load_tick = syNetRollbackClampLoadTickForPeerSend(load_tick);
	live = syNetRollbackCollectHashes();
	wire = syNetRollbackCollectSlotBaselineDigests(load_tick);
	sSYNetRollbackResimLoadTick = load_tick;
#if defined(SSB64_NETMENU)
	/* Load slot is proven; mismatch+ requires BattleSimOnly before SavePostTick. */
	sSYNetRollbackResimBattleProvenThrough = load_tick;
#endif
	sSYNetRollbackResimPreHashes = live;
	sSYNetRollbackResimPreHashesValid = TRUE;
	sSYNetRollbackPeerBaselineSendPending = TRUE;
	sSYNetRollbackResimBaselineDigestMatched = FALSE;
	sSYNetRollbackResimSealRowsTimeoutRetries = 0U;
	/* Fresh baseline arm: any item-only self load-fidelity drift is re-evaluated post-load below. */
	sSYNetRollbackBaselineItemOnlySelfDriftLoadTick = ~(u32)0;
	sSYNetRollbackPeerBaselineForeignLoadTick = ~(u32)0;
	sSYNetRollbackPeerBaselineLoadTick = load_tick;
	/* Wire digests use ring slot hashes (saved at load_tick), not post-coupling live. */
	sSYNetRollbackPeerBaselineFigh = wire.fighter;
	sSYNetRollbackPeerBaselineWorld = wire.world;
	sSYNetRollbackPeerBaselineItem = wire.item;
	sSYNetRollbackPeerBaselineRng = wire.rng;
	sSYNetRollbackPeerBaselineAnim = wire.animation;
	sSYNetRollbackPeerBaselineWeapon = wire.weapon;
	sSYNetRollbackPeerBaselineMap = wire.map;
	sSYNetRollbackPeerBaselineCamera = wire.camera;
	sSYNetRollbackPeerBaselineEffect = wire.effect;
	sSYNetRollbackPeerBaselineSlotFigh = syNetRbSnapshotGetSlotHashFighter(load_tick);
	sSYNetRollbackPeerBaselineSlotWorld = syNetRbSnapshotGetSlotHashWorld(load_tick);
	sSYNetRollbackPeerBaselineSlotItem = syNetRbSnapshotGetSlotHashItem(load_tick);
	sSYNetRollbackPeerBaselineSlotRng = syNetRbSnapshotGetSlotHashRng(load_tick);
	sSYNetRollbackPeerBaselineSlotWeapon = syNetRbSnapshotGetSlotHashWeapon(load_tick);
	sSYNetRollbackPeerBaselineSlotMap = syNetRbSnapshotGetSlotHashMap(load_tick);
	sSYNetRollbackPeerBaselineSlotCamera = syNetRbSnapshotGetSlotHashCamera(load_tick);
	sSYNetRollbackPeerBaselineSlotEffect = syNetRbSnapshotGetSlotHashEffect(load_tick);
	syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, sSYNetRollbackPeerBaselineFighterSlot);
	{
		u32 live_slot[GMCOMMON_PLAYERS_MAX];
		s32 slot_si;

		syNetRollbackCollectFighterSlotHashes(live_slot);
		for (slot_si = 0; slot_si < GMCOMMON_PLAYERS_MAX; slot_si++)
		{
			if (live_slot[slot_si] != sSYNetRollbackPeerBaselineFighterSlot[slot_si])
			{
				port_log(
				    "SSB64 NetRollback: BASELINE_SLOT_RING_ARM load_tick=%u player=%d ring_slot=0x%08X live_slot=0x%08X\n",
				    load_tick,
				    (int)slot_si,
				    sSYNetRollbackPeerBaselineFighterSlot[slot_si],
				    live_slot[slot_si]);
			}
		}
	}
	{
		u32 live_camera;

		live_camera = syNetRollbackCollectHashes().camera;
		if (live_camera != sSYNetRollbackPeerBaselineSlotCamera)
		{
			port_log(
			    "SSB64 NetRollback: BASELINE_CAMERA_RING_ARM load_tick=%u ring_cam=0x%08X live_cam=0x%08X\n",
			    load_tick,
			    sSYNetRollbackPeerBaselineSlotCamera,
			    live_camera);
		}
	}
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
	    "SSB64 NetRollback: resim baseline (post-load tick=%u) wire(slot) figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X | live figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X\n",
	    load_tick,
	    wire.fighter,
	    wire.world,
	    wire.item,
	    wire.rng,
	    wire.animation,
	    live.fighter,
	    live.world,
	    live.item,
	    live.rng,
	    live.animation);
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
		/*
		 * Item-only self load-fidelity drift: loading our own slot at load_tick did not reproduce our own
		 * saved item hash (e.g. Peach's Castle GBumper during a Firefox collision folds to a resting value
		 * on one ISA only). figh/world/rng already agree, so the divergent field is one the item fold reads
		 * but the apply probe does not print. Emit the per-field item diff at the exact load-fidelity failure
		 * point so a repro with SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1 pins the non-idempotent bumper field,
		 * and record the load_tick so the baseline gate timeout backs off to an earlier clean slot rather
		 * than freezing both peers into a VS_SESSION_END hard desync.
		 * See docs/bugs/netplay_castle_bumper_resim_baseline_item_load_fidelity_2026-07-03.md.
		 */
		if (live.item != sSYNetRollbackPeerBaselineSlotItem)
		{
			syNetSyncLogItemHashDriftDiag(load_tick, sSYNetRollbackPeerBaselineSlotItem, live.item,
						      "resim_baseline_item");
			syNetSyncLogItemFieldDiffDiag(load_tick, sSYNetRollbackPeerBaselineSlotItem, live.item,
						      "resim_baseline_item");
			if ((live.fighter == sSYNetRollbackPeerBaselineSlotFigh) &&
			    (live.world == sSYNetRollbackPeerBaselineSlotWorld) &&
			    (live.rng == sSYNetRollbackPeerBaselineSlotRng) &&
			    (live.weapon == sSYNetRollbackPeerBaselineSlotWeapon) &&
			    (live.map == sSYNetRollbackPeerBaselineSlotMap))
			{
				sSYNetRollbackBaselineItemOnlySelfDriftLoadTick = load_tick;
			}
		}
#if defined(SSB64_NETMENU)
		if (syNetRollbackLoadVerifyPerSlotFighDriftOk(load_tick, live.fighter, live.world, live.item, live.weapon,
							      live.map, live.rng, live.animation) != FALSE)
		{
			syNetRbSnapshotRefreshSlotHashFighterWhenPerSlotMatch(load_tick);
			port_log(
			    "SSB64 NetRollback: PEER_BASELINE_WIRE_LIVE_FIGH load_tick=%u ring_figh=0x%08X live_figh=0x%08X\n",
			    load_tick,
			    sSYNetRollbackPeerBaselineFigh,
			    live.fighter);
			sSYNetRollbackPeerBaselineFigh = live.fighter;
		}
#endif
	}
#if defined(SSB64_NETMENU)
	/*
	 * Light input episodes: keep pre-hashes / slot digests for local diagnostics, but never
	 * put digests on the wire (sync send here was racing the post-Arm pending clear and
	 * poisoning the peer with BASELINE_PREEMPTIVE_LIVE_CAP). See
	 * docs/bugs/netplay_light_input_episodes_2026-07-27.md.
	 */
	if (syNetRollbackLightEpisodeIsActive() != FALSE)
	{
		sSYNetRollbackPeerBaselineSendPending = FALSE;
		port_log("SSB64 NetRollback: RESIM_BASELINE_WIRE_SKIP light load_tick=%u\n", load_tick);
		return;
	}
#endif
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
	sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
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

	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: RESIM_BASELINE_ECHO skipped (BATTLE_SIM_HOLD) load_tick=%u sim=%u\n",
		    load_tick,
		    syNetInputGetTick());
		return FALSE;
	}
	if (load_tick == 0U)
	{
		return FALSE;
	}
	if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &slot_figh, &slot_world, &slot_item, &slot_rng) == FALSE)
	{
		port_log("SSB64 NetRollback: RESIM_BASELINE_ECHO_SKIP load_tick=%u reason=no_snapshot\n", load_tick);
		return FALSE;
	}
	emergency_valid = FALSE;
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick == sSYNetRollbackResimLoadTick))
	{
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
	}
	else
	{
		u32 sim_tick;

		sim_tick = syNetInputGetTick();
		if (sim_tick > load_tick)
		{
			if (syNetRbSnapshotLoad(load_tick) == FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_BASELINE_ECHO_FAIL load_tick=%u sim=%u reason=live_ahead_load\n",
				    load_tick,
				    sim_tick);
				return FALSE;
			}
			syNetRbSnapshotRefreshPresentationForLoadedTick(load_tick);
			syNetRbSnapshotResyncLiveFightersFromSlotForSim(load_tick);
			port_log(
			    "SSB64 NetRollback: RESIM_BASELINE_ECHO live_apply load_tick=%u sim=%u drift_ticks=%u\n",
			    load_tick,
			    sim_tick,
			    sim_tick - load_tick);
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: RESIM_BASELINE_ECHO ring-only load_tick=%u sim=%u (no live snapshot apply)\n",
			    load_tick,
			    sim_tick);
		}
	}
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

/*
 * Hash-only baseline echo: reply with our stored snapshot ring digests for `load_tick` without
 * loading or arming anything. Serves peers whose episode rewound to a deeper load_tick than
 * ours (divergent-load stall): the regular echo path refuses ticks at or below resolved_through
 * (post-Live), which left the deeper-loaded follower awaiting a baseline nobody would ever send
 * (seed 2657747101: Android await@408 vs Linux Live resolved@426).
 * See docs/bugs/netplay_divergent_load_tick_baseline_stall_2026-07-12.md.
 */
static sb32 syNetRollbackTryHashOnlyBaselineEcho(u32 load_tick)
{
	SYNetRollbackHashSet slot;
	u32 fighter_slot[GMCOMMON_PLAYERS_MAX];
	u32 sim_tick;
	u32 ring_cap;

	if (load_tick == 0U)
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if ((load_tick == sSYNetRollbackLastBaselineEchoLoadTick) &&
	    (sim_tick >= sSYNetRollbackLastBaselineEchoSimTick) &&
	    ((sim_tick - sSYNetRollbackLastBaselineEchoSimTick) < SYNETROLLBACK_PEER_BASELINE_ECHO_MIN_ADVANCE_TICKS))
	{
		return FALSE;
	}
	ring_cap = syNetRbSnapshotRingCapacity();
	if ((ring_cap > 2U) && (sim_tick > (ring_cap - 2U)) && (load_tick < (sim_tick - (ring_cap - 2U))))
	{
		return FALSE;
	}
	if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, NULL, NULL, NULL, NULL) == FALSE)
	{
		return FALSE;
	}
	slot = syNetRollbackCollectSlotBaselineDigests(load_tick);
	syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, fighter_slot);
	if (syNetPeerSendRollbackBaselineDigestDirect(load_tick, slot.fighter, slot.world, slot.item, slot.rng,
						      slot.animation, slot.weapon, slot.map, slot.camera, slot.effect,
						      fighter_slot) == FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_ECHO hash_only load_tick=%u sim=%u figh=0x%08X\n",
	    load_tick,
	    sim_tick,
	    slot.fighter);
	sSYNetRollbackLastBaselineEchoLoadTick = load_tick;
	sSYNetRollbackLastBaselineEchoSimTick = sim_tick;
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

static void syNetRollbackPendingEpisodeClearAll(void)
{
	memset(sSYNetRollbackPendingEpisodeBySlot, 0, sizeof(sSYNetRollbackPendingEpisodeBySlot));
}

static void syNetRollbackPendingEpisodeClearSlot(s32 slot)
{
	if ((slot >= 0) && (slot < MAXCONTROLLERS))
	{
		memset(&sSYNetRollbackPendingEpisodeBySlot[slot], 0, sizeof(SYNetRollbackPendingEpisode));
	}
}

static sb32 syNetRollbackPendingEpisodeCopyValid(s32 slot, SYNetRollbackPendingEpisode *out)
{
	SYNetRollbackPendingEpisode *ep;

	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (out == NULL))
	{
		return FALSE;
	}
	ep = &sSYNetRollbackPendingEpisodeBySlot[slot];
	if (ep->valid == FALSE)
	{
		return FALSE;
	}
	*out = *ep;
	return TRUE;
}

static u32 syNetRollbackPendingEpisodeMaxTargetTick(void)
{
	u32 max_target;
	s32 slot;

	max_target = 0U;
	for (slot = 0; slot < MAXCONTROLLERS; slot++)
	{
		SYNetRollbackPendingEpisode *ep;

		ep = &sSYNetRollbackPendingEpisodeBySlot[slot];
		if ((ep->valid != FALSE) && (ep->target_tick > max_target))
		{
			max_target = ep->target_tick;
		}
	}
	return max_target;
}

static s32 syNetRollbackResolveRemoteHumanPlayer(s32 preferred_slot)
{
	u32 frontier;
	s32 global_player;

	if ((preferred_slot >= 0) && (preferred_slot < MAXCONTROLLERS) &&
	    (syNetInputIsRemoteHumanSlot(preferred_slot) != FALSE))
	{
		return preferred_slot;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	global_player = -1;
	if ((frontier != 0U) &&
	    (syNetInputTimelineFindGlobalEarliestIncorrect(frontier, &global_player) != ~(u32)0) && (global_player >= 0))
	{
		return global_player;
	}
	{
		s32 slot;

		if (syNetPeerGetRemoteHumanSlotByIndex(0, &slot) != FALSE)
		{
			return slot;
		}
	}
	return preferred_slot;
}

static sb32 syNetRollbackRemoteHumanHasPredictedPublishedInSpan(u32 from_tick, u32 to_tick)
{
	u32 t;
	s32 ri;
	s32 remote_player;
	SYNetInputFrame published;

	if ((from_tick >= to_tick) || (to_tick == 0U))
	{
		return FALSE;
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
			if (syNetRollbackPublishedSimUsedPrediction(&published) != FALSE)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

static void syNetRollbackPendingEpisodeSet(s32 slot, u32 mismatch_tick, u32 target_tick, u32 load_tick, u32 epoch_id,
					   u8 flags)
{
	SYNetRollbackPendingEpisode *ep;
	u32 resolved_load;

	if ((slot < 0) || (slot >= MAXCONTROLLERS) || (mismatch_tick == 0U) || (target_tick <= mismatch_tick))
	{
		return;
	}
	resolved_load = load_tick;
	if (resolved_load == 0U)
	{
		resolved_load = (mismatch_tick > 0U) ? (mismatch_tick - 1U) : 0U;
	}
	ep = &sSYNetRollbackPendingEpisodeBySlot[slot];
	if (ep->valid == FALSE)
	{
		ep->valid = TRUE;
		ep->slot = slot;
		ep->mismatch_tick = mismatch_tick;
		ep->target_tick = target_tick;
		ep->load_tick = resolved_load;
		ep->epoch_id = epoch_id;
		ep->flags = flags;
		return;
	}
	if ((mismatch_tick > ep->mismatch_tick) && (epoch_id <= ep->epoch_id))
	{
		sb32 allow_boundary_raise;

		/*
		 * Locked tuple from a settled episode must not block the next episode at
		 * resolved_through (QueuePeerSymmetricNotify often re-Sets with epoch_id=0).
		 * See docs/bugs/netplay_stick_up_boundary_seal_join_hang_2026-07-12.md.
		 */
		allow_boundary_raise =
		    ((sSYNetRollbackEpisodeResolvedThrough != 0U) &&
		     (ep->mismatch_tick < sSYNetRollbackEpisodeResolvedThrough) &&
		     (mismatch_tick >= sSYNetRollbackEpisodeResolvedThrough))
			? TRUE
			: FALSE;
		if (allow_boundary_raise == FALSE)
		{
			port_log(
			    "SSB64 NetRollback: EPISODE_TUPLE_REJECT slot=%d raise_mismatch=%u locked=%u epoch_in=%u epoch_locked=%u\n",
			    (int)slot,
			    mismatch_tick,
			    ep->mismatch_tick,
			    epoch_id,
			    ep->epoch_id);
			if (target_tick > ep->target_tick)
			{
				ep->target_tick = target_tick;
			}
			return;
		}
		port_log(
		    "SSB64 NetRollback: EPISODE_TUPLE_BOUNDARY_RAISE slot=%d mismatch=%u->%u target=%u resolved_through=%u\n",
		    (int)slot,
		    ep->mismatch_tick,
		    mismatch_tick,
		    target_tick,
		    sSYNetRollbackEpisodeResolvedThrough);
		ep->mismatch_tick = mismatch_tick;
		ep->load_tick = resolved_load;
		ep->target_tick = target_tick;
		if (epoch_id > ep->epoch_id)
		{
			ep->epoch_id = epoch_id;
		}
		ep->flags |= flags;
		return;
	}
	if (mismatch_tick < ep->mismatch_tick)
	{
		port_log(
		    "SSB64 NetRollback: CORRECTION_MERGE_DEEPEN slot=%d mismatch=%u->%u target=%u epoch=%u\n",
		    (int)slot,
		    ep->mismatch_tick,
		    mismatch_tick,
		    target_tick,
		    epoch_id);
		ep->mismatch_tick = mismatch_tick;
		if ((resolved_load < ep->load_tick) || (ep->load_tick == 0U))
		{
			ep->load_tick = resolved_load;
		}
	}
	if (target_tick > ep->target_tick)
	{
		ep->target_tick = target_tick;
	}
	if ((resolved_load < ep->load_tick) || (ep->load_tick == 0U))
	{
		ep->load_tick = resolved_load;
	}
	if (epoch_id > ep->epoch_id)
	{
		ep->epoch_id = epoch_id;
	}
	ep->flags |= flags;
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

static void syNetRollbackMaybeLogEpisodeExec(sb32 from_peer_notify, sb32 light)
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
	owner = (light != FALSE) ? "local_light" : (from_peer_notify != FALSE ? "peer_follower" : "local_initiator");
	port_log(
	    "SSB64 NetRollback: EPISODE_EXEC owner=%s req_load=%u req_mismatch=%u req_target=%u exec_load=%u exec_mismatch=%u exec_target=%u sim=%u epoch=%u slot=%d timeline_earliest=%u last_confirmed=%u\n",
	    owner,
	    req->load_tick,
	    req->mismatch_tick,
	    req->target_tick,
	    sSYNetRollbackResimLoadTick,
	    sSYNetRollbackResimMismatchTick,
	    sSYNetRollbackResimTargetTick,
	    syNetInputGetTick(),
	    req->epoch_id,
	    (int)req->slot,
	    (req->slot >= 0) ? syNetInputTimelineGetEarliestIncorrectForPlayer(req->slot) : 0U,
	    (req->slot >= 0) ? syNetInputTimelineGetLastRemoteConfirmedSimTick(req->slot) : 0U);
}

/*
 * Observe-only layer certificate. Always-on for netmenu VS (not gated on
 * SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY). See docs/netplay_rollback_refactor_contracts.md.
 */
static const char *syNetRollbackProofSourceName(SYNetRollbackCorrectionMismatchSource source)
{
	switch (source)
	{
	case nSYNetRollbackCorrectionSourceWire:
		return "wire";
	case nSYNetRollbackCorrectionSourceTimelinePlayer:
	case nSYNetRollbackCorrectionSourceTimelineGlobal:
		return "timeline";
	default:
		return "scan";
	}
}

static void syNetRollbackLogEpisodeProof(const char *event, u32 epoch_id, u32 mismatch_tick, u32 load_tick,
					 u32 target_tick, const char *source, const char *reason,
					 const char *class_name)
{
	u32 conf0;
	u32 conf1;
	u32 early0;
	u32 early1;
	u32 need_conf;
	sb32 agree_through_load;
	const char *snap;
	const char *seal;

	if ((event == NULL) || (event[0] == '\0'))
	{
		return;
	}
	if ((u32)gSCManagerSceneData.scene_curr != (u32)nSCKindVSBattle)
	{
		return;
	}
	conf0 = syNetInputTimelineGetLastRemoteConfirmedSimTick(0);
	conf1 = syNetInputTimelineGetLastRemoteConfirmedSimTick(1);
	early0 = syNetInputTimelineGetEarliestIncorrectForPlayer(0);
	early1 = syNetInputTimelineGetEarliestIncorrectForPlayer(1);
	agree_through_load =
	    (syNetRollbackUniverseMismatchInputPoisonedAtLoad(load_tick, NULL, NULL) == FALSE) ? TRUE : FALSE;
	snap = (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, NULL, NULL, NULL, NULL) != FALSE) ? "ok" : "missing";
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackEpisodeInputsSealed() != FALSE))
	{
		seal = "ok";
	}
	else
	{
		need_conf = (target_tick > 0U) ? (target_tick - 1U) : 0U;
		if ((conf0 >= need_conf) && (conf1 >= need_conf))
		{
			seal = "ok";
		}
		else
		{
			seal = "wait";
		}
	}
	if ((source == NULL) || (source[0] == '\0'))
	{
		source = "scan";
	}
	if ((reason == NULL) || (reason[0] == '\0'))
	{
		reason = "earliest_authoritative_correction";
	}
	if ((class_name == NULL) || (class_name[0] == '\0'))
	{
		if (snap[0] == 'm')
		{
			class_name = "snapshot_fidelity";
		}
		else if (agree_through_load == FALSE)
		{
			class_name = "protocol";
		}
		else
		{
			class_name = "protocol";
		}
	}
	port_log(
	    "SSB64 NetRollback: EPISODE_PROOF event=%s epoch=%u mismatch=%u load=%u target=%u "
	    "source=%s reason=%s conf=[p0:%u,p1:%u] earliest=[p0:%u,p1:%u] snap=%s seal=%s "
	    "agree_through_load=%d class=%s\n",
	    event,
	    epoch_id,
	    mismatch_tick,
	    load_tick,
	    target_tick,
	    source,
	    reason,
	    conf0,
	    conf1,
	    early0,
	    early1,
	    snap,
	    seal,
	    (int)agree_through_load,
	    class_name);
}

static void syNetRollbackLogEpisodeProofBegin(s32 correction_player, u32 mismatch_tick, u32 load_tick, u32 target_tick,
					      u32 epoch_id)
{
	s32 corr_player;
	u32 corr_mismatch;
	u32 corr_load;
	u32 corr_target;
	SYNetRollbackCorrectionMismatchSource corr_source;
	const char *source_name;

	source_name = "scan";
	if (syNetRollbackComputeInputCorrectionTuple(correction_player, mismatch_tick, &corr_player, &corr_mismatch,
						     &corr_load, &corr_target, &corr_source) != FALSE)
	{
		source_name = syNetRollbackProofSourceName(corr_source);
	}
	syNetRollbackLogEpisodeProof("begin",
				     epoch_id,
				     mismatch_tick,
				     load_tick,
				     target_tick,
				     source_name,
				     "earliest_authoritative_correction",
				     NULL);
}

static void syNetRollbackLogEpisodeProofDiverge(u32 load_tick, const char *reason, const char *class_name)
{
	u32 mismatch;
	u32 target;
	u32 epoch;

	mismatch = sSYNetRollbackResimMismatchTick;
	target = sSYNetRollbackResimTargetTick;
	epoch = sSYNetRollbackEpochId;
	if (sSYNetRollbackExecutingEpisode.valid != FALSE)
	{
		if (mismatch == 0U)
		{
			mismatch = sSYNetRollbackExecutingEpisode.mismatch_tick;
		}
		if (target == 0U)
		{
			target = sSYNetRollbackExecutingEpisode.target_tick;
		}
		epoch = sSYNetRollbackExecutingEpisode.epoch_id;
		if ((load_tick == 0U) || (load_tick == ~(u32)0))
		{
			load_tick = sSYNetRollbackExecutingEpisode.load_tick;
		}
	}
	if (((load_tick == 0U) || (load_tick == ~(u32)0)) && (sSYNetRollbackResimLoadTick != ~(u32)0))
	{
		load_tick = sSYNetRollbackResimLoadTick;
	}
	syNetRollbackLogEpisodeProof("diverge", epoch, mismatch, load_tick, target, "scan", reason, class_name);
}

#define SYNETROLLBACK_FRONTIER_AHEAD_WARN_TOLERANCE 3U

/* Same cap as SharedCommit / AdvanceAllowed runway (remote_sim + phase_lock). */
static u32 syNetRollbackComputeRemoteSimResimCap(void)
{
	u32 hr;

	hr = syNetPeerGetHighestRemoteTick();
	if (hr == 0U)
	{
		return ~(u32)0;
	}
	return syNetPeerGetRemoteSimRunwayCap();
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

static void syNetRollbackClearBaselineResimNegotiationFlags(void)
{
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackResimBaselineDigestMatched = FALSE;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
}

static void syNetRollbackResetBaselineResimState(void)
{
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackResimStallFrames = 0U;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	sSYNetRollbackResimPreHashesValid = FALSE;
	sSYNetRollbackResimLoadTick = ~(u32)0;
#if defined(SSB64_NETMENU)
	sSYNetRollbackLightEpisodeActive = FALSE;
	sSYNetRollbackResimBattleProvenThrough = 0U;
#endif
	syNetRollbackClearBaselineResimNegotiationFlags();
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
	sSYNetRollbackFcRecoveryJoinAttempts = 0U;
}

void syNetRollbackOnEpisodeTargetShrunkToPeerPrefix(u32 mismatch_tick, u32 old_target, u32 new_target)
{
	s32 slot;
	sb32 touched;

	if ((mismatch_tick == 0U) || (new_target <= mismatch_tick) || (new_target >= old_target))
	{
		return;
	}
	touched = FALSE;
#if defined(PORT) && defined(SSB64_NETMENU)
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (sSYNetRollbackFcStateRecoveryMismatchTick == mismatch_tick) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick != ~(u32)0) &&
	    (sSYNetRollbackFcStateRecoveryTargetTick > new_target))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_SHRINK_SYNC fc_arm target=%u->%u mismatch=%u\n",
		    sSYNetRollbackFcStateRecoveryTargetTick,
		    new_target,
		    mismatch_tick);
		sSYNetRollbackFcStateRecoveryTargetTick = new_target;
		touched = TRUE;
	}
	if ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchTick == mismatch_tick) &&
	    (sSYNetRollbackDeferredStateMismatchTargetTick != ~(u32)0) &&
	    (sSYNetRollbackDeferredStateMismatchTargetTick > new_target))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_SHRINK_SYNC deferred_fc target=%u->%u mismatch=%u\n",
		    sSYNetRollbackDeferredStateMismatchTargetTick,
		    new_target,
		    mismatch_tick);
		sSYNetRollbackDeferredStateMismatchTargetTick = new_target;
		touched = TRUE;
	}
#endif
	if ((sSYNetRollbackResimMismatchTick == mismatch_tick) && (sSYNetRollbackResimTargetTick != ~(u32)0) &&
	    (sSYNetRollbackResimTargetTick > new_target))
	{
		sSYNetRollbackResimTargetTick = new_target;
		touched = TRUE;
	}
	if ((sSYNetRollbackEpisode.mismatch_tick == mismatch_tick) && (sSYNetRollbackEpisode.target_tick > new_target))
	{
		sSYNetRollbackEpisode.target_tick = new_target;
		touched = TRUE;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick == mismatch_tick) &&
	    (sSYNetRollbackPendingPeerSymmetricTargetTick != ~(u32)0) &&
	    (sSYNetRollbackPendingPeerSymmetricTargetTick > new_target))
	{
		sSYNetRollbackPendingPeerSymmetricTargetTick = new_target;
		touched = TRUE;
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (sSYNetRollbackDeferredPeerSymmetricTick == mismatch_tick) &&
	    (sSYNetRollbackDeferredPeerSymmetricTargetTick > new_target))
	{
		sSYNetRollbackDeferredPeerSymmetricTargetTick = new_target;
		touched = TRUE;
	}
	if ((sSYNetRollbackPeerEpochMismatchTick == mismatch_tick) && (sSYNetRollbackPeerEpochTargetTick > new_target))
	{
		sSYNetRollbackPeerEpochTargetTick = new_target;
		touched = TRUE;
	}
	for (slot = 0; slot < MAXCONTROLLERS; slot++)
	{
		if ((sSYNetRollbackSymmetricNotifyTick[slot] == mismatch_tick) &&
		    (sSYNetRollbackSymmetricNotifyTargetTick[slot] > new_target))
		{
			sSYNetRollbackSymmetricNotifyTargetTick[slot] = new_target;
			touched = TRUE;
		}
		if ((sSYNetRollbackPendingEpisodeBySlot[slot].valid != FALSE) &&
		    (sSYNetRollbackPendingEpisodeBySlot[slot].mismatch_tick == mismatch_tick) &&
		    (sSYNetRollbackPendingEpisodeBySlot[slot].target_tick > new_target))
		{
			sSYNetRollbackPendingEpisodeBySlot[slot].target_tick = new_target;
			touched = TRUE;
		}
	}
	if (touched != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_SHRINK_SYNC_TARGETS mismatch=%u old_target=%u new_target=%u sim=%u\n",
		    mismatch_tick,
		    old_target,
		    new_target,
		    syNetInputGetTick());
	}
}

static void syNetRollbackOnFcDeepenStormLimit(u32 load_tick)
{
	port_log(
	    "SSB64 NetRollback: FC_DEEPEN_STORM load_tick=%u attempts=%u fc_mismatch=%u sim=%u — aborting baseline deepen loop\n",
	    load_tick,
	    (unsigned int)sSYNetRollbackFcDeepenAttempts,
	    sSYNetRollbackFcStateRecoveryMismatchTick,
	    syNetInputGetTick());
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM Abort(snapshot_fidelity) load_tick=%u (fc deepen storm / baseline fidelity exhausted)\n",
		    load_tick);
		syNetRollbackLogEpisodeProofDiverge(load_tick, "fc_deepen_storm_exhausted", "snapshot_fidelity");
		syNetRollbackEpisodeFsmSetPhase(nSYNetRollbackEpisodeFsmPhaseAbort);
	}
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

/*
 * FC recovery join: keep peer mismatch/target when behind local resolved_through.
 * Ordinary GGPO still soft-clamps. Wire FC_RECOVERY covers retransmits after local yield.
 */
static sb32 syNetRollbackPeerSymmetricFcJoinKeepOnset(u32 mismatch_tick, u32 target_tick, u8 notify_flags)
{
#if !(defined(PORT) && defined(SSB64_NETMENU))
	(void)mismatch_tick;
	(void)target_tick;
	(void)notify_flags;
	return FALSE;
#else
	if ((sSYNetRollbackEpisodeResolvedThrough == 0U) ||
	    (mismatch_tick >= sSYNetRollbackEpisodeResolvedThrough) ||
	    (target_tick <= sSYNetRollbackEpisodeResolvedThrough))
	{
		return FALSE;
	}
	/*
	 * Stale deepen after SHRINK_TO_PEER_PREFIX: local already closed this onset
	 * (LastCommittedMismatch) through resolved_through and no longer has an FC arm
	 * wanting the deeper target. Peer's pre-shrink ROLLBACK_SYNC still carries
	 * FC_RECOVERY + abandoned deepen → keep-onset would re-queue forever
	 * (dup_pending / invent spin). Soak seed 2437168353: 2278→2296 after commit@2288.
	 * Legitimate escalate deepen re-arms local FC before notify.
	 * See docs/bugs/netplay_fc_shrink_target_fork_hang_2026-07-28.md.
	 */
	if ((sSYNetRollbackFcStateRecoveryActive == FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchPending == FALSE) &&
	    (sSYNetRollbackLastCommittedMismatchTick == mismatch_tick) &&
	    (sSYNetRollbackEpisodeResolvedThrough > mismatch_tick))
	{
		return FALSE;
	}
	if ((notify_flags & SYNETROLLBACK_SYM_NOTIFY_FLAG_FC_RECOVERY) != 0U)
	{
		return TRUE;
	}
	if ((sSYNetRollbackFcStateRecoveryActive != FALSE) &&
	    (syNetRollbackFcStateRecoveryCoversSpan(mismatch_tick, target_tick) != FALSE))
	{
		return TRUE;
	}
	if ((sSYNetRollbackDeferredStateMismatchPending != FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchTick == mismatch_tick) &&
	    ((sSYNetRollbackDeferredStateMismatchTargetTick == ~(u32)0) ||
	     (sSYNetRollbackDeferredStateMismatchTargetTick == target_tick)))
	{
		return TRUE;
	}
	return FALSE;
#endif
}

static sb32 syNetRollbackPeerSymmetricSuppressedByFcStateRecovery(u32 mismatch_tick, u32 target_tick)
{
	u32 defer_mismatch;
	u32 defer_target;

	/*
	 * Suppress only once local FC recovery has actually BeginResim'd. Arming alone
	 * (FcStateRecoveryActive / DeferredStateMismatchPending without ResimPending) must
	 * NOT drop peer SYNC — both peers arm the same span on FC diverge; the initiator's
	 * notify must be accepted so the slower peer can join as follower, exchange seals,
	 * and clear the preemptive baseline live-cap. Suppress-while-unstarted hung soak2
	 * (Android recovery_started=0, seal stale_episode_tuple, cap=378 @ sim=480).
	 * See docs/bugs/netplay_fc_recovery_suppress_join_deadlock_2026-07-13.md.
	 */
	if ((sSYNetRollbackResimPending == FALSE) && (syNetRollbackIsResimulating() == FALSE))
	{
		return FALSE;
	}
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

/*
 * TRUE only when a published↔remote input value mismatch exists at or before load_tick — the load
 * snapshot itself was produced under disagreeing inputs (poisoned band, ~1032 soak). A mismatch
 * strictly after load (typical feel-0 stick episode that opened this resim) does not poison the
 * load ring; figh diverge there is cross-peer state drift.
 */
static sb32 syNetRollbackUniverseMismatchInputPoisonedAtLoad(u32 load_tick, u32 *out_mismatch, s32 *out_player)
{
	u32 frontier;
	u32 mismatch;
	s32 player;

	if (out_mismatch != NULL)
	{
		*out_mismatch = ~(u32)0;
	}
	if (out_player != NULL)
	{
		*out_player = -1;
	}
	if ((load_tick == 0U) || (load_tick == ~(u32)0))
	{
		return FALSE;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, &player);
	if (out_mismatch != NULL)
	{
		*out_mismatch = mismatch;
	}
	if (out_player != NULL)
	{
		*out_player = player;
	}
	if ((mismatch == ~(u32)0) || (mismatch > load_tick))
	{
		return FALSE;
	}
	return TRUE;
}

/* Peers agreed on load_tick but rng/figh/world differ — classify input-poisoned vs state diverge. */
static void syNetRollbackAbortToInputCorrectionFromUniverseMismatch(u32 load_tick)
{
	u32 frontier;
	u32 mismatch;
	s32 player;
	sb32 had_pending;
	sb32 input_poisoned;

	/*
	 * Do not require ResimPending. TryOpen used to ResetBaseline *before* calling this, which
	 * cleared pending and made the mismatch a silent no-op — soak1 608380406 then died on
	 * PEER_SNAPSHOT_DIVERGE after echo-retry compared a clobbered load_tick slot.
	 * See docs/bugs/netplay_baseline_universe_mismatch_ignored_2026-07-12.md.
	 */
	had_pending = sSYNetRollbackResimPending;
	input_poisoned = syNetRollbackUniverseMismatchInputPoisonedAtLoad(load_tick, &mismatch, &player);
	/*
	 * State diverge at load (inputs agree through load_tick): deepen before FC PreferStateRecovery
	 * TryFc path — that path sets InFlight and returned without deeper when AbortPending bounced
	 * straight back into input correction (soak1 1160137450 @741).
	 */
	if (input_poisoned == FALSE)
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_UNIVERSE_MISMATCH load_tick=%u → state deepen (inputs agree through load; earliest_input=%u) sim=%u had_pending=%d class=replay_determinism\n",
		    load_tick,
		    (mismatch != ~(u32)0) ? mismatch : 0U,
		    syNetInputGetTick(),
		    (int)had_pending);
		syNetRollbackClearPeerSymmetricRejectLiveCap();
		/*
		 * AbortPending falls through to deeper when !poisoned. Do not ResetBaseline first —
		 * pending must stay armed for TryRestartResimAtDeeperLoad.
		 */
		syNetRollbackAbortPendingResimForBaselineMismatch(load_tick);
		return;
	}
	if (syNetRollbackTryFcStateRecoveryDeepen(load_tick) != FALSE)
	{
		return;
	}
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
	if (player < 0)
	{
		player = syNetRollbackResolveRemoteHumanPlayer(-1);
	}
	port_log(
	    "SSB64 NetRollback: BASELINE_UNIVERSE_MISMATCH load_tick=%u → input correction mismatch=%u player=%d sim=%u had_pending=%d class=protocol\n",
	    load_tick,
	    mismatch,
	    (int)player,
	    syNetInputGetTick(),
	    (int)had_pending);
	/*
	 * Abandoning AwaitingBaseline into Live while a preemptive baseline live-cap remains armed
	 * freezes sim below the deferred GGPO target (soak1 4134815356: cap=570 peer_target=573).
	 * Clear the reject cap so input correction can BeginResim.
	 * See docs/bugs/netplay_feel0_send_before_sample_release_skew_2026-07-13.md.
	 */
	syNetRollbackClearPeerSymmetricRejectLiveCap();
	syNetRollbackQueueDeferredInputCorrectionEx(player, mismatch, frontier);
	if (sSYNetRollbackResimPending != FALSE)
	{
		syNetRollbackResetBaselineResimState();
	}
}

static void syNetRollbackAbortPendingResimForBaselineMismatch(u32 failed_load_tick)
{
	u32 probe;
	u32 min_load;
	u32 local_deeper;
	u32 restart_load;
	u32 peer_ann;
	u32 resim_load;

	if ((sSYNetRollbackLastPeerOutcomeValid != FALSE) && (sSYNetRollbackLastPeerOutcomeTick == failed_load_tick))
	{
		syNetRollbackTryOpenResimReplayGateAfterAnimResync(failed_load_tick, &sSYNetRollbackLastPeerOutcomeHash);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
		if (sSYNetRollbackLastPeerOutcomeFighterSlotsValid != FALSE)
		{
			syNetRollbackTryOpenResimReplayGateAfterSlotRingResync(failed_load_tick,
									       &sSYNetRollbackLastPeerOutcomeHash,
									       sSYNetRollbackLastPeerOutcomeFighterSlot);
			if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
			{
				return;
			}
		}
		syNetRollbackTryOpenResimReplayGateAfterCameraRingResync(
		    failed_load_tick, &sSYNetRollbackLastPeerOutcomeHash,
		    (sSYNetRollbackLastPeerOutcomeFighterSlotsValid != FALSE) ? sSYNetRollbackLastPeerOutcomeFighterSlot
									      : NULL);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
#if defined(SSB64_NETMENU)
		syNetRollbackTryOpenResimReplayGateAfterPkHoldWeaponOnlyAbsorb(
		    failed_load_tick, &sSYNetRollbackLastPeerOutcomeHash,
		    (sSYNetRollbackLastPeerOutcomeFighterSlotsValid != FALSE) ? sSYNetRollbackLastPeerOutcomeFighterSlot
									      : NULL);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
#endif
	}
	resim_load = sSYNetRollbackResimLoadTick;
	if ((sSYNetRollbackLastPeerOutcomeValid != FALSE) && (sSYNetRollbackLastPeerOutcomeTick == resim_load) &&
	    (resim_load == failed_load_tick) &&
	    (syNetRollbackPeerDigestUniverseMismatch(&sSYNetRollbackLastPeerOutcomeHash) != FALSE))
	{
		/*
		 * Input-poisoned load band → GGPO. Else fall through to deeper-load restart —
		 * do not bounce to AbortToInputCorrection (that abandoned AwaitingBaseline→Live
		 * when earliest stick mismatch was after load; soak1 1160137450 @741).
		 * See docs/bugs/netplay_baseline_universe_state_vs_input_routing_2026-07-13.md.
		 */
		if (syNetRollbackUniverseMismatchInputPoisonedAtLoad(failed_load_tick, NULL, NULL) != FALSE)
		{
			if (syNetRollbackTryFcStateRecoveryDeepen(failed_load_tick) != FALSE)
			{
				return;
			}
			syNetRollbackAbortToInputCorrectionFromUniverseMismatch(failed_load_tick);
			return;
		}
		port_log(
		    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH universe state diverge load_tick=%u → deeper (inputs agree through load) sim=%u\n",
		    failed_load_tick,
		    syNetInputGetTick());
	}
	syNetRollbackClearBaselineResimNegotiationFlags();
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
	/*
	 * Prefer the earlier of local ring / peer-announced load so both peers deepen to the same
	 * tick when the peer already walked back (soak1 1643097122: unilateral 2307→2305 vs 2305→2303).
	 */
	if ((restart_load != ~(u32)0) && (restart_load < failed_load_tick))
	{
		if (sSYNetRollbackResimBaselineDeeperAttempts < SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS)
		{
			sSYNetRollbackResimBaselineDeeperAttempts++;
			if (syNetRollbackTryRestartResimAtDeeperLoad(restart_load) != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH negotiated restart load_tick=%u failed_load=%u peer_ann=%u local_deeper=%u attempt=%u\n",
				    restart_load,
				    failed_load_tick,
				    peer_ann,
				    (unsigned int)((local_deeper != ~(u32)0) ? local_deeper : 0U),
				    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
				return;
			}
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH deeper exhausted load_tick=%u failed_load=%u attempts=%u\n",
			    restart_load,
			    failed_load_tick,
			    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
			if (syNetRollbackTryFailClosedAfterStateDeepenExhaust(failed_load_tick) != FALSE)
			{
				return;
			}
		}
	}
	/*
	 * State diverge (inputs agree through load) still mismatched after deepen budget — do not
	 * ArmPeerBaselineResync and keep simulating forked (soak1 1643097122: exhaust @2305 then
	 * PEER_SNAPSHOT_DIVERGE @2311 with world/map/figh all split). Fail closed now.
	 * See docs/bugs/netplay_baseline_state_deepen_exhaust_fail_closed_2026-07-15.md.
	 */
	if (syNetRollbackTryFailClosedAfterStateDeepenExhaust(failed_load_tick) != FALSE)
	{
		return;
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
	sb32 fc_input_agree_recovery;

	if ((peer_load_tick == 0U) || (sSYNetRollbackResimPending == FALSE) ||
	    (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) || (sSYNetRollbackResimBaselineGateOpen != FALSE))
	{
		return FALSE;
	}
	fc_input_agree_recovery = (sSYNetRollbackFcStateRecoveryActive != FALSE) ? TRUE : FALSE;
	/*
	 * Authoritative peer-locked tuples must not silently rewrite load. Exception:
	 * FC input-agree state recovery — each peer independently ring-clamps load from
	 * its local frontier, so AuthoritativeEpisodeActive would otherwise block the
	 * MIN(local, peer) join forever (soak 4126729879: Linux 1057 never negotiated
	 * down to Android 1047 → seal hang). See
	 * docs/bugs/netplay_fc_recovery_divergent_load_hang_2026-07-27.md.
	 */
	if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
	    (sSYNetRollbackAuthoritativeEpisodeActive != FALSE) &&
	    (sSYNetRollbackFcStateRecoveryActive == FALSE))
	{
		return FALSE;
	}
	local_load = sSYNetRollbackResimLoadTick;
	if ((local_load == ~(u32)0) || (peer_load_tick >= local_load))
	{
		return FALSE;
	}
	/*
	 * Peer walked back for effect-probe fragility after we already matched baseline @local_load —
	 * do not downgrade the initiator (soak1 @520: Linux matched @519, peer @487 → 471 crash).
	 * FC input-agree recovery with unmatched baseline is the opposite case — negotiate.
	 */
	if ((sSYNetRollbackResimBaselineDigestMatched != FALSE) && (local_load == sSYNetRollbackResimLoadTick) &&
	    (fc_input_agree_recovery == FALSE))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_TICK_NEGOTIATE refused local baseline matched load=%u peer=%u sim=%u\n",
		    local_load,
		    peer_load_tick,
		    syNetInputGetTick());
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: LOAD_TICK_NEGOTIATE local=%u peer=%u negotiated=%u sim=%u fc_recovery=%d\n",
	    local_load,
	    peer_load_tick,
	    peer_load_tick,
	    syNetInputGetTick(),
	    (int)sSYNetRollbackFcStateRecoveryActive);
	if (fc_input_agree_recovery != FALSE)
	{
		if (sSYNetRollbackFcRecoveryJoinAttempts >= SYNETROLLBACK_FC_RECOVERY_JOIN_MAX_ATTEMPTS)
		{
			port_log(
			    "SSB64 NetRollback: LOAD_TICK_NEGOTIATE refused fc_join exhausted local=%u peer=%u attempts=%u\n",
			    local_load,
			    peer_load_tick,
			    (unsigned int)sSYNetRollbackFcRecoveryJoinAttempts);
			return FALSE;
		}
		sSYNetRollbackFcRecoveryJoinAttempts++;
		return syNetRollbackTryRestartResimAtDeeperLoad(peer_load_tick);
	}
	if (sSYNetRollbackResimBaselineDeeperAttempts >= SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS)
	{
		port_log(
		    "SSB64 NetRollback: LOAD_TICK_NEGOTIATE refused deeper exhausted local=%u peer=%u attempts=%u\n",
		    local_load,
		    peer_load_tick,
		    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
		return FALSE;
	}
	sSYNetRollbackResimBaselineDeeperAttempts++;
	return syNetRollbackTryRestartResimAtDeeperLoad(peer_load_tick);
}

#ifdef PORT
static void syNetRollbackLogBaselineMismatchBisect(u32 load_tick, const SYNetRollbackHashSet *peer,
						   const u32 *peer_fighter_slot, sb32 slot_ok)
{
	sb32 slots_match;
	s32 first_slot_div;
	s32 si;

	if (peer == NULL)
	{
		return;
	}
	slots_match = syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, sSYNetRollbackPeerBaselineFighterSlot);
	first_slot_div = -1;
	if (slots_match == FALSE)
	{
		for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
		{
			if ((peer_fighter_slot == NULL) ||
			    (peer_fighter_slot[si] != sSYNetRollbackPeerBaselineFighterSlot[si]))
			{
				first_slot_div = si;
				break;
			}
		}
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_BISECT load_tick=%u slot_ok=%d slots_match=%d first_slot_div=%d "
	    "requires_anim=%d peer_anim=0x%08X armed_anim=0x%08X | "
	    "peer_vs_armed figh=%d world=%d item=%d rng=%d wpn=%d map=%d cam=%d eff=%d anim=%d | "
	    "peer_vs_slot figh=%d world=%d item=%d rng=%d wpn=%d map=%d cam=%d eff=%d | "
	    "peer_map=0x%08X armed_map=0x%08X slot_map=0x%08X\n",
	    load_tick,
	    (int)slot_ok,
	    (int)slots_match,
	    (int)first_slot_div,
	    (int)syNetRollbackEpisodeFsmBaselineRequiresAnimMatch(),
	    peer->animation,
	    sSYNetRollbackPeerBaselineAnim,
	    (int)(peer->fighter != sSYNetRollbackPeerBaselineFigh),
	    (int)(peer->world != sSYNetRollbackPeerBaselineWorld),
	    (int)(peer->item != sSYNetRollbackPeerBaselineItem),
	    (int)(peer->rng != sSYNetRollbackPeerBaselineRng),
	    (int)(peer->weapon != sSYNetRollbackPeerBaselineWeapon),
	    (int)(peer->map != sSYNetRollbackPeerBaselineMap),
	    (int)(peer->camera != sSYNetRollbackPeerBaselineCamera),
	    (int)((sSYNetRollbackLastPeerOutcomeEffectValid != FALSE) &&
	          (peer->effect != sSYNetRollbackPeerBaselineEffect)),
	    (int)(peer->animation != sSYNetRollbackPeerBaselineAnim),
	    (int)(peer->fighter != sSYNetRollbackPeerBaselineSlotFigh),
	    (int)(peer->world != sSYNetRollbackPeerBaselineSlotWorld),
	    (int)(peer->item != sSYNetRollbackPeerBaselineSlotItem),
	    (int)(peer->rng != sSYNetRollbackPeerBaselineSlotRng),
	    (int)(peer->weapon != sSYNetRollbackPeerBaselineSlotWeapon),
	    (int)(peer->map != sSYNetRollbackPeerBaselineSlotMap),
	    (int)(peer->camera != sSYNetRollbackPeerBaselineSlotCamera),
	    (int)((sSYNetRollbackLastPeerOutcomeEffectValid != FALSE) &&
	          (peer->effect != sSYNetRollbackPeerBaselineSlotEffect)),
	    peer->map,
	    sSYNetRollbackPeerBaselineMap,
	    sSYNetRollbackPeerBaselineSlotMap);
	if (first_slot_div >= 0)
	{
		port_log(
		    "SSB64 NetRollback: RESIM_BASELINE_BISECT slot_div player=%d peer_slot=0x%08X local_slot=0x%08X\n",
		    (int)first_slot_div,
		    (peer_fighter_slot != NULL) ? peer_fighter_slot[first_slot_div] : 0U,
		    sSYNetRollbackPeerBaselineFighterSlot[first_slot_div]);
	}
}
#endif

static void syNetRollbackTryOpenBaselineGateFromStashedPeerOutcome(void)
{
	const u32 *slots;

	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) ||
	    (sSYNetRollbackResimBaselineGateOpen != FALSE) || (sSYNetRollbackResimBaselineDigestMatched != FALSE))
	{
		return;
	}
	if ((sSYNetRollbackLastPeerOutcomeValid == FALSE) ||
	    (sSYNetRollbackLastPeerOutcomeTick != sSYNetRollbackResimLoadTick))
	{
		return;
	}
	slots = (sSYNetRollbackLastPeerOutcomeFighterSlotsValid != FALSE) ? sSYNetRollbackLastPeerOutcomeFighterSlot
									 : NULL;
	port_log(
	    "SSB64 NetRollback: BASELINE_STASH_COMPARE load_tick=%u peer_figh=0x%08X local_figh=0x%08X\n",
	    sSYNetRollbackResimLoadTick,
	    sSYNetRollbackLastPeerOutcomeHash.fighter,
	    sSYNetRollbackPeerBaselineFigh);
	syNetRollbackTryOpenResimBaselineGateFromPeerDigest(sSYNetRollbackResimLoadTick,
							     &sSYNetRollbackLastPeerOutcomeHash, slots);
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
			syNetSyncLogBaselineUniverseDiff(load_tick, peer->fighter, sSYNetRollbackPeerBaselineFigh,
							 peer->world, sSYNetRollbackPeerBaselineWorld, peer->rng,
							 sSYNetRollbackPeerBaselineRng);
			syNetRbSnapshotLogFighterFieldDiffOnLoadDrift(load_tick);
			sSYNetRollbackFcDeepenDetailLogged = TRUE;
		}
		if (syNetRollbackBaselineUniverseRepeatStorm(load_tick, peer->fighter, sSYNetRollbackPeerBaselineFigh) !=
		    FALSE)
		{
			return;
		}
		/*
		 * Queue input correction *before* ResetBaseline. Reset-then-Abort left pending=0 so
		 * AbortToInputCorrection no-op'd (soak1 608380406 @426 figh diverge → PEER_SNAPSHOT_DIVERGE).
		 */
		syNetRollbackAbortToInputCorrectionFromUniverseMismatch(load_tick);
		return;
	}
	slot_ok = syNetRollbackPeerBaselineSlotGameplaySubsystemOk(peer);
	if ((slot_ok != FALSE) &&
	    (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, sSYNetRollbackPeerBaselineFighterSlot) != FALSE) &&
	    syNetRollbackPeerBaselineWireGameplayMatchArmed(peer) != FALSE)
	{
		if (peer->camera != sSYNetRollbackPeerBaselineCamera)
		{
			syNetRollbackAlignArmedBaselineCameraFromPeerWire(load_tick, peer->camera);
		}
		port_log(
		    "SSB64 NetRollback: resim baseline digest matched load_tick=%u figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X weapon=0x%08X map=0x%08X camera=0x%08X effect=0x%08X anim=0x%08X\n",
		    load_tick,
		    peer->fighter,
		    peer->world,
		    peer->item,
		    peer->rng,
		    peer->weapon,
		    peer->map,
		    peer->camera,
		    peer->effect,
		    peer->animation);
		sSYNetRollbackResimBaselineDigestMatched = TRUE;
		sSYNetRollbackResimBaselineWaitFrames = 0U;
		sSYNetRollbackPeerBaselineSendPending = FALSE;
		sSYNetRollbackBaselineTimeoutStreak = 0U;
		sSYNetRollbackFcDeepenInFlight = FALSE;
		syNetRollbackTryOpenResimReplayGate();
		return;
	}
	if ((slot_ok != FALSE) && (syNetRollbackPeerBaselineWireGameplayMatchArmed(peer) != FALSE))
	{
		syNetRollbackTryOpenResimReplayGateAfterAnimResync(load_tick, peer);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
		syNetRollbackTryOpenResimReplayGateAfterSlotRingResync(load_tick, peer, peer_fighter_slot);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
		syNetRollbackTryOpenResimReplayGateAfterCameraRingResync(load_tick, peer, peer_fighter_slot);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
	}
#if defined(SSB64_NETMENU)
	syNetRollbackTryOpenResimReplayGateAfterPkHoldWeaponOnlyAbsorb(load_tick, peer, peer_fighter_slot);
	if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
	{
		return;
	}
#endif
#ifdef PORT
	syNetRollbackLogBaselineMismatchBisect(load_tick, peer, peer_fighter_slot, slot_ok);
#endif
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
	u32 outbound_pending;

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
		syNetRollbackEpisodeLogSealRowsWaitDetail(sSYNetRollbackResimLoadTick, missing);
		syNetRollbackEpisodePumpOutboundSealRows(4U);
		return;
	}
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) &&
	    (syNetRollbackEpisodeLocalSealRowsSendComplete() == FALSE))
	{
		outbound_pending = syNetRollbackEpisodeGetLocalSealRowsSendPendingMask();
		port_log(
		    "SSB64 NetRollback: EPISODE_SEAL_ROWS_WAIT outbound load_tick=%u pending_local_slots=0x%X\n",
		    sSYNetRollbackResimLoadTick,
		    outbound_pending);
		syNetRollbackEpisodePumpOutboundSealRows(4U);
		return;
	}
	sSYNetRollbackResimSealRowsTimeoutRetries = 0U;
#ifdef PORT
	if (sSYNetRollbackResimLoadTick != 0U)
	{
		syNetRbSnapshotRefreshPresentationForLoadedTick(sSYNetRollbackResimLoadTick);
	}
	if (syNetRollbackVerifyResimReplayLoadSafe(sSYNetRollbackResimLoadTick) == FALSE)
	{
		port_log(
		    "SSB64 NetRollback: resim replay gate blocked load_tick=%u mismatch=%u target=%u\n",
		    sSYNetRollbackResimLoadTick,
		    sSYNetRollbackResimMismatchTick,
		    sSYNetRollbackResimTargetTick);
		sSYNetRollbackLoadFailCount++;
		syNetRollbackArmBattleSimHoldAfterLoadFail(sSYNetRollbackResimLoadTick);
		syNetRollbackResetBaselineResimState();
		syNetRollbackResetCorrectionEpisode();
		syNetRollbackRequestLoadFailBattleExit();
		syNetRollbackStopVsSessionForLoadFail(sSYNetRollbackResimLoadTick, "replay_gate_blocked");
		return;
	}
	syNetRbSnapshotResetIntroPresentationRepairState();
	syNetRbSnapshotCosmeticAppearPresentationAfterReplayGate(sSYNetRollbackResimLoadTick);
#endif
	port_log(
	    "SSB64 NetRollback: resim replay gate open load_tick=%u mismatch=%u target=%u\n",
	    sSYNetRollbackResimLoadTick,
	    sSYNetRollbackResimMismatchTick,
	    sSYNetRollbackResimTargetTick);
#if defined(SSB64_NETMENU)
	syNetplayResimReplayHangDiagNoteReplayGateOpen("try_open_replay_gate");
#endif
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFreezePostInputDigest();
	}
	syNetRollbackEpisodeSetPhase(nSYNetRollbackEpisodePhaseForwardResim);
	syNetRollbackAdvanceResimBudget();
}

static sb32 syNetRollbackTryRestartResimAtDeeperLoad(u32 deeper_load_tick)
{
	u32 mismatch_tick;
	u32 target_tick;
	s32 correction_player;
	u32 walk_attempts;
	sb32 loaded;

	if (deeper_load_tick == 0U)
	{
		return FALSE;
	}
	/*
	 * Authoritative peer-locked tuples must not silently rewrite load — except FC
	 * input-agree recovery, which ring-clamps load locally and must join the peer's
	 * MIN load (soak 3849468025: Restart refused authoritative → join never ran).
	 */
	if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
	    (sSYNetRollbackAuthoritativeEpisodeActive != FALSE) &&
	    (sSYNetRollbackFcStateRecoveryActive == FALSE))
	{
		return FALSE;
	}
	mismatch_tick = deeper_load_tick + 1U;
	if (syNetRollbackResolveLoadTickForSnapshot(&deeper_load_tick, &mismatch_tick) == FALSE)
	{
		return FALSE;
	}
	sSYNetRollbackResimDeeperLoadActive = TRUE;
	loaded = FALSE;
	walk_attempts = 0U;
	while (loaded == FALSE)
	{
		if (syNetRollbackLoadPostTick(deeper_load_tick) != FALSE)
		{
			loaded = TRUE;
			break;
		}
		if ((walk_attempts >= SYNETROLLBACK_LOAD_TICK_REWIND_MAX) || (deeper_load_tick == 0U))
		{
			break;
		}
		{
			u32 before_load;
			u32 min_load;
			u32 next_load;

			before_load = deeper_load_tick;
			/* Rescue path (load already failed): ring floor only — the episode
			 * floor would pin min_load at the failed tick and strand the walk.
			 * See syNetRollbackLoadTickRingMinBound. */
			min_load = syNetRollbackLoadTickRingMinBound(syNetInputGetTick());
			next_load = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(before_load - 1U, min_load);
			if (next_load == ~(u32)0)
			{
				next_load = syNetRbSnapshotFindLatestValidTickAtOrBefore(before_load - 1U, min_load);
			}
			if ((next_load == ~(u32)0) || (next_load >= before_load))
			{
				break;
			}
			deeper_load_tick = next_load;
			mismatch_tick = deeper_load_tick + 1U;
			if (syNetRollbackResolveLoadTickForSnapshot(&deeper_load_tick, &mismatch_tick) == FALSE)
			{
				break;
			}
			port_log(
			    "SSB64 NetRollback: RESIM_DEEPER_LOAD_WALKBACK from=%u to=%u mismatch=%u attempt=%u\n",
			    before_load,
			    deeper_load_tick,
			    mismatch_tick,
			    walk_attempts + 1U);
			walk_attempts++;
		}
	}
	sSYNetRollbackResimDeeperLoadActive = FALSE;
	if (loaded == FALSE)
	{
		return FALSE;
	}
	target_tick = sSYNetRollbackResimTargetTick;
	correction_player = sSYNetRollbackResimCorrectionPlayer;
	syNetSyncLogRollbackWorldDetail("rollback_load_deeper", deeper_load_tick);
	syNetSyncLogFighterDetail("rollback_load_deeper", deeper_load_tick);
	syNetRollbackArmResimBaselineAfterLoad(deeper_load_tick);
	syNetRollbackMaybeResimAnchorProbe(deeper_load_tick);
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
	port_log(
	    "SSB64 NetRollback: resim baseline deeper restart load_tick=%u mismatch_tick=%u target_tick=%u attempt=%u\n",
	    deeper_load_tick,
	    mismatch_tick,
	    target_tick,
	    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
	return TRUE;
}

static sb32 syNetRollbackPeerSymmetricNotifyIsStaleShallow(u32 notify_mismatch, u32 notify_load)
{
	u32 cur_load;
	u32 cur_mismatch;

	if ((notify_mismatch == 0U) || (sSYNetRollbackResimPending == FALSE))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeFsmIsActive() != FALSE)
	{
		cur_load = syNetRollbackEpisodeFsmGetLoadTick();
		cur_mismatch = syNetRollbackEpisodeFsmGetMismatchTick();
	}
	else
	{
		cur_load = sSYNetRollbackResimLoadTick;
		cur_mismatch = sSYNetRollbackResimMismatchTick;
	}
	if ((cur_mismatch == 0U) || (notify_mismatch <= cur_mismatch))
	{
		return FALSE;
	}
	/* Unspecified load: shallow mismatch alone is stale. */
	if (notify_load == 0U)
	{
		return TRUE;
	}
	/* Same or shallower peer load while local episode is at a deeper anchor. */
	if (notify_load >= cur_load)
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRollbackClearStaleShallowPeerSymmetricNotify(u32 settled_mismatch_tick)
{
	s32 sym_slot;

	if (settled_mismatch_tick == 0U)
	{
		return;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (sSYNetRollbackPendingPeerSymmetricTick > settled_mismatch_tick))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM clear_stale_pending_symmetric pending_mismatch=%u settled_mismatch=%u sim=%u\n",
		    sSYNetRollbackPendingPeerSymmetricTick,
		    settled_mismatch_tick,
		    syNetInputGetTick());
		sym_slot = sSYNetRollbackPendingPeerSymmetricSlot;
		syNetRollbackClearPendingPeerSymmetricNotify();
		if ((sym_slot >= 0) && (sym_slot < MAXCONTROLLERS))
		{
			syNetRollbackPendingEpisodeClearSlot(sym_slot);
		}
	}
	if ((sSYNetRollbackDeferredPeerSymmetricPending != FALSE) &&
	    (sSYNetRollbackDeferredPeerSymmetricTick > settled_mismatch_tick))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM clear_stale_deferred_symmetric deferred_mismatch=%u settled_mismatch=%u sim=%u\n",
		    sSYNetRollbackDeferredPeerSymmetricTick,
		    settled_mismatch_tick,
		    syNetInputGetTick());
		sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
		sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricSlot = -1;
		sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
		sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
		syNetRollbackClearPeerSymmetricRejectLiveCap();
	}
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
	if (mismatch_tick > cur_mismatch)
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM tuple_align_skip_stale_shallow load=%u cur_mismatch=%u peer_mismatch=%u sim=%u\n",
		    align_load,
		    cur_mismatch,
		    mismatch_tick,
		    syNetInputGetTick());
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
#if defined(SSB64_NETMENU)
	/* Replaying from mismatch without a new Load: drop stale battle proof from any prior span. */
	sSYNetRollbackResimBattleProvenThrough = align_load;
#endif
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

/*
 * Divergent-load stall escape: our episode rewound to a deeper load_tick than the peer's
 * (EPISODE_LOAD_REWIND) and the baseline exchange — keyed on exact load_tick — can never match;
 * the peer may already be Live and will stop sending its baseline entirely. If we hold the
 * complete sealed input span, self-verified our own load, and observed peer baseline traffic
 * only for a different load_tick, proceed with the replay instead of freezing: the sealed
 * inputs are canonical, and post-resim verification (RESIM_POST digest / synctest / FC)
 * still catches real state divergence.
 * See docs/bugs/netplay_divergent_load_tick_baseline_stall_2026-07-12.md.
 */
static sb32 syNetRollbackTryProceedAfterDivergentLoadBaselineTimeout(u32 load_tick)
{
	if ((syNetRollbackEpisodeFsmEnabled() == FALSE) || (sSYNetRollbackResimBaselineDigestMatched != FALSE))
	{
		return FALSE;
	}
	if ((sSYNetRollbackPeerBaselineForeignLoadTick == ~(u32)0) ||
	    (sSYNetRollbackPeerBaselineForeignLoadTick == load_tick))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeAllPeerSealRowsComplete() == FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_DIVERGENT_LOAD_PROCEED load_tick=%u peer_load=%u sim=%u (seals complete; replay without baseline agreement)\n",
	    load_tick,
	    sSYNetRollbackPeerBaselineForeignLoadTick,
	    syNetInputGetTick());
	sSYNetRollbackResimBaselineDigestMatched = TRUE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
	sSYNetRollbackResimPending = TRUE;
	syNetRollbackTryOpenResimReplayGate();
	return TRUE;
}

/*
 * FC input-agree recovery opened at divergent loads (each peer ring-clamped from its
 * own frontier). Seals never complete while loads disagree, so DIVERGENT_LOAD_PROCEED
 * cannot fire. Higher peer must join MIN(local, foreign); if already at MIN and the
 * peer still has not joined after a streak budget, fail closed — no SoftLip/absorb wait.
 * See docs/bugs/netplay_fc_recovery_divergent_load_hang_2026-07-27.md.
 */
static sb32 syNetRollbackTryFcRecoveryDivergentLoadTimeout(u32 load_tick)
{
	u32 foreign;
	u32 shared_load;

	if (sSYNetRollbackFcStateRecoveryActive == FALSE)
	{
		return FALSE;
	}
	if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
	{
		return FALSE;
	}
	foreign = sSYNetRollbackPeerBaselineForeignLoadTick;
	if ((foreign == ~(u32)0) || (foreign == 0U) || (foreign == load_tick))
	{
		return FALSE;
	}
	shared_load = (foreign < load_tick) ? foreign : load_tick;
	if (shared_load < load_tick)
	{
		port_log(
		    "SSB64 NetRollback: FC_RECOVERY_DIVERGENT_LOAD_JOIN load_tick=%u -> %u peer_load=%u streak=%u\n",
		    load_tick,
		    shared_load,
		    foreign,
		    (unsigned int)sSYNetRollbackBaselineTimeoutStreak);
		if (syNetRollbackTryNegotiateResimLoadTickWithPeer(shared_load) != FALSE)
		{
			sSYNetRollbackBaselineTimeoutStreak = 0U;
			return TRUE;
		}
	}
	if (sSYNetRollbackBaselineTimeoutStreak < 2U)
	{
		/* Lower peer (or join failed once): give the higher peer one more timeout to join. */
		sSYNetRollbackResimBaselineWaitFrames = 0U;
		sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
		sSYNetRollbackResimPending = TRUE;
		sSYNetRollbackPeerBaselineSendPending = TRUE;
		port_log(
		    "SSB64 NetRollback: FC_RECOVERY_DIVERGENT_LOAD_WAIT load_tick=%u peer_load=%u streak=%u "
		    "(await shared load join)\n",
		    load_tick,
		    foreign,
		    (unsigned int)sSYNetRollbackBaselineTimeoutStreak);
		return TRUE;
	}
	port_log(
	    "SSB64 NetRollback: FC_RECOVERY_DIVERGENT_LOAD_ABORT load_tick=%u peer_load=%u streak=%u "
	    "(inputs_agree state recovery — fail closed)\n",
	    load_tick,
	    foreign,
	    (unsigned int)sSYNetRollbackBaselineTimeoutStreak);
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	syNetRollbackClearFcStateRecovery();
	syNetRollbackClearFcDeepenGuard();
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmSessionReset();
	}
	else
	{
		syNetRollbackEpisodeReset();
	}
	if (sSYNetRollbackPeerSnapshotAbort != FALSE)
	{
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetRollbackStopVSSession();
		syNetPeerStopVSSession();
	}
	return TRUE;
}

/*
 * Behind-frontier episode already covered by a completed span (local and/or peer
 * resolved_through). Abandon seal wait instead of hard-desync. Soak1 642600542:
 * ep0 resolved@420, host opened mismatch@414, guest never sealed → EXHAUSTED.
 *
 * Do NOT call syNetRollbackIsResimulating() here: BeginResim sets ResimDepth=1 for the
 * whole AwaitingBaseline seal wait, so IsResimulating stays TRUE and cancel never fires
 * (soak 1734260705: mismatch=396 load=395 resolved=399 → SEAL_ROWS_EXHAUSTED).
 */
static sb32 syNetRollbackTryCancelResimBehindSharedFrontier(void)
{
	u32 mismatch;
	u32 load_tick;
	u32 shared;
	u32 episode_mismatch;

	if (sSYNetRollbackResimPending == FALSE)
	{
		return FALSE;
	}
	/* Already in forward replay — too late to abandon at the seal gate. */
	if (sSYNetRollbackResimBaselineGateOpen != FALSE)
	{
		return FALSE;
	}
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) &&
	    (syNetRollbackEpisodeFsmIsResimulating() != FALSE))
	{
		return FALSE;
	}
	if (sSYNetRollbackResimBaselineDigestMatched == FALSE)
	{
		return FALSE;
	}
	syNetRollbackEpisodeSyncToLegacy();
	mismatch = sSYNetRollbackResimMismatchTick;
	load_tick = sSYNetRollbackResimLoadTick;
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE))
	{
		episode_mismatch = syNetRollbackEpisodeFsmGetMismatchTick();
		if ((episode_mismatch != 0U) && (episode_mismatch != ~(u32)0) &&
		    ((mismatch == 0U) || (mismatch == ~(u32)0) || (episode_mismatch < mismatch)))
		{
			mismatch = episode_mismatch;
		}
		if (syNetRollbackEpisodeFsmGetLoadTick() != 0U)
		{
			load_tick = syNetRollbackEpisodeFsmGetLoadTick();
		}
	}
	if ((mismatch == 0U) || (mismatch == ~(u32)0))
	{
		return FALSE;
	}
	shared = syNetRollbackGetSharedCorrectionFrontier();
	if (shared == 0U)
	{
		return FALSE;
	}
	/*
	 * Behind frontier if mismatch is older than shared, or load sits more than one tick
	 * before shared (stale mismatch with HR-clamped load — soak 1734260705).
	 */
	if ((mismatch >= shared) && ((load_tick == 0U) || (load_tick >= (shared - 1U))))
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: FRONTIER_SEAL_CANCEL mismatch=%u shared_frontier=%u local_resolved=%u "
	    "peer_resolved=%u load=%u (already settled; abandon behind-frontier episode)\n",
	    mismatch,
	    shared,
	    sSYNetRollbackEpisodeResolvedThrough,
	    sSYNetRollbackPeerResolvedThrough,
	    load_tick);
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
	sSYNetRollbackResimBaselineGateOpen = FALSE;
	sSYNetRollbackResimBaselineDigestMatched = FALSE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackResimSealRowsTimeoutRetries = 0U;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackResimMismatchTick = ~(u32)0;
	sSYNetRollbackResimLoadTick = ~(u32)0;
	sSYNetRollbackResimTargetTick = ~(u32)0;
	sSYNetRollbackResimCorrectionPlayer = -1;
	sSYNetRollbackResimFromPeerSymmetric = FALSE;
	syNetRollbackClearSymmetricNotifyAll();
	syNetRollbackClearPendingPeerSymmetricNotify();
	syNetRollbackEpisodeResetPeerSealRowsState();
	if (syNetRollbackEpisodeFsmEnabled() != FALSE)
	{
		syNetRollbackEpisodeFsmSessionReset();
	}
	else
	{
		syNetRollbackEpisodeReset();
		sSYNetRollbackEpisode.phase = nSYNetRollbackEpisodePhaseLive;
	}
	sSYNetRollbackEpisodeAnchorMismatch = ~(u32)0;
	sSYNetRollbackEpisodeAnchorLoadTick = ~(u32)0;
	sSYNetRollbackEpisodeLastTargetTick = 0U;
	sSYNetRollbackEpisodeExtensions = 0U;
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
	if (syNetRollbackTryCancelResimBehindSharedFrontier() != FALSE)
	{
		return;
	}
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) &&
	    (syNetRollbackEpisodeAllPeerSealRowsComplete() == FALSE))
	{
		u32 missing_seal;

		missing_seal = syNetRollbackEpisodeGetMissingPeerSealSlotsMask();
		port_log(
		    "SSB64 NetRollback: RESIM_SEAL_ROWS_TIMEOUT load_tick=%u missing_slots=0x%X\n",
		    load_tick,
		    missing_seal);
		syNetRollbackEpisodeLogSealRowsWaitDetail(load_tick, missing_seal);
		if ((sSYNetRollbackResimBaselineDigestMatched != FALSE) &&
		    (sSYNetRollbackBaselineTimeoutStreak < SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_MAX) &&
		    (sSYNetRollbackResimSealRowsTimeoutRetries < SYNETROLLBACK_SEAL_ROWS_TIMEOUT_MAX_RETRIES))
		{
			sSYNetRollbackResimSealRowsTimeoutRetries++;
			sSYNetRollbackResimBaselineWaitFrames = 0U;
			sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
			sSYNetRollbackResimPending = TRUE;
			syNetRollbackEpisodePrepareSealRowsRetransmit();
			syNetPeerTrySendEpisodeSealRows();
			port_log(
			    "SSB64 NetRollback: RESIM_SEAL_ROWS_TIMEOUT retry load_tick=%u attempt=%u span=%u gate_timeout_frames=%u\n",
			    load_tick,
			    (unsigned int)sSYNetRollbackResimSealRowsTimeoutRetries,
			    syNetRollbackEpisodeGetSealSpan(),
			    syNetRollbackGetResimBaselineGateTimeoutFrames());
			return;
		}
		/*
		 * Retry budget exhausted and the peer never delivered its seal rows (peer never
		 * joined the episode). If the load-tick baseline already matched cross-peer and
		 * every missing row resolves from wire-confirmed remote input history, self-seal
		 * and finish the resim locally instead of freezing into hard desync recovery.
		 * See docs/bugs/netplay_fc_recovery_seal_rows_peer_absent_2026-06-11.md.
		 *
		 * Peer-absent only: if the peer IS sealing this epoch (chunks arrived, just under a
		 * conflicting tuple), a unilateral self-seal replay commits history the peer never
		 * agreed to — soak1 @861 ended in PEER_SNAPSHOT_DIVERGE that way. Fail closed
		 * (fall through to hold/hard-desync below) instead of force-opening the gate.
		 * See docs/bugs/netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md.
		 */
		if ((sSYNetRollbackResimBaselineDigestMatched != FALSE) &&
		    (syNetRollbackEpisodePeerSealActivitySeen() == FALSE) &&
		    (syNetRollbackEpisodeTrySelfSealMissingPeerRows() != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: RESIM_SEAL_ROWS_SELF_SEAL_FALLBACK load_tick=%u missing_was=0x%X (peer absent; sealed from wire-confirmed history)\n",
			    load_tick,
			    missing_seal);
			sSYNetRollbackResimBaselineWaitFrames = 0U;
			sSYNetRollbackBaselineTimeoutStreak = 0U;
			sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
			sSYNetRollbackResimPending = TRUE;
			syNetRollbackTryOpenResimReplayGate();
			return;
		}
		/*
		 * Peer may have sealed under a forked mismatch (same epoch+target) and those chunks
		 * were stashed as compatible — or arrived after our self-seal attempt. Flush and
		 * retry before deepen: deepen widens the tuple fork (soak1 Android 502 vs Linux 504).
		 * See docs/bugs/netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md.
		 */
		syNetRollbackEpisodePumpPendingPeerSealRows();
		missing_seal = syNetRollbackEpisodeGetMissingPeerSealSlotsMask();
		if (syNetRollbackEpisodeAllPeerSealRowsComplete() != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: RESIM_SEAL_ROWS_PUMP_UNBLOCK load_tick=%u (compatible/stashed peer seals completed span)\n",
			    load_tick);
			sSYNetRollbackResimBaselineWaitFrames = 0U;
			sSYNetRollbackBaselineTimeoutStreak = 0U;
			sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
			sSYNetRollbackResimPending = TRUE;
			syNetRollbackTryOpenResimReplayGate();
			return;
		}
		/* Same peer-absent gate as above: never self-seal over a live, disagreeing peer. */
		if ((sSYNetRollbackResimBaselineDigestMatched != FALSE) &&
		    (syNetRollbackEpisodePeerSealActivitySeen() == FALSE) &&
		    (syNetRollbackEpisodeTrySelfSealMissingPeerRows() != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: RESIM_SEAL_ROWS_SELF_SEAL_FALLBACK load_tick=%u missing_was=0x%X (after pending pump; wire-confirmed history)\n",
			    load_tick,
			    missing_seal);
			sSYNetRollbackResimBaselineWaitFrames = 0U;
			sSYNetRollbackBaselineTimeoutStreak = 0U;
			sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
			sSYNetRollbackResimPending = TRUE;
			syNetRollbackTryOpenResimReplayGate();
			return;
		}
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			/*
			 * Baseline OK; only seal exchange is stuck. Hold and retransmit while streak
			 * allows — do not fall through to deeper-load (that forks mismatch further).
			 * When streak is exhausted, fall through to hard desync (not deepen).
			 */
			if (sSYNetRollbackBaselineTimeoutStreak < SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_MAX)
			{
				sSYNetRollbackResimBaselineWaitFrames = 0U;
				sSYNetRollbackResimAwaitingPeerBaseline = TRUE;
				sSYNetRollbackResimPending = TRUE;
				syNetRollbackEpisodePrepareSealRowsRetransmit();
				syNetPeerTrySendEpisodeSealRows();
				port_log(
				    "SSB64 NetRollback: RESIM_SEAL_ROWS_HOLD_NO_DEEPEN load_tick=%u missing_slots=0x%X streak=%u (baseline matched; keep exchanging seals)\n",
				    load_tick,
				    missing_seal,
				    (unsigned int)sSYNetRollbackBaselineTimeoutStreak);
				return;
			}
			port_log(
			    "SSB64 NetRollback: RESIM_SEAL_ROWS_EXHAUSTED load_tick=%u missing_slots=0x%X (baseline matched; no deepen — hard desync path)\n",
			    load_tick,
			    missing_seal);
		}
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
	if (syNetRollbackTryProceedAfterDivergentLoadBaselineTimeout(load_tick) != FALSE)
	{
		return;
	}
	if (syNetRollbackTryFcRecoveryDivergentLoadTimeout(load_tick) != FALSE)
	{
		return;
	}
	if (sSYNetRollbackBaselineTimeoutStreak >= SYNETROLLBACK_BASELINE_TIMEOUT_STREAK_MAX)
	{
		/*
		 * Before tearing the session down, back off to an earlier load-safe slot when the block is
		 * recoverable: either this peer verified its own load (baseline digest matched — the peer is
		 * stuck sealing) or this peer hit item-only self load-fidelity drift at this load_tick. A single
		 * non-idempotent snapshot (Peach's Castle GBumper item hash folding to a resting value on one ISA
		 * after a Firefox collision) poisons baseline agreement at THIS load_tick only; an earlier slot
		 * predates the collision and reproduces cleanly, so a deeper resim converges over the poisoned tick
		 * instead of freezing both peers into VS_SESSION_END. Bounded by DEEPER_MAX_ATTEMPTS and the ring;
		 * if no clean earlier slot exists it falls through to the normal hard-desync teardown below.
		 * See docs/bugs/netplay_castle_bumper_resim_baseline_item_load_fidelity_2026-07-03.md.
		 *
		 * Seal-rows-only blocks with matched baseline are handled above (HOLD_NO_DEEPEN) and must not
		 * reach this deepen path — forked mismatch deepens are what caused soak1 asymmetric stall.
		 */
		if (((sSYNetRollbackResimBaselineDigestMatched != FALSE) ||
		     (sSYNetRollbackBaselineItemOnlySelfDriftLoadTick == load_tick)) &&
		    (sSYNetRollbackResimBaselineDeeperAttempts < SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS) &&
		    (load_tick > 0U) &&
		    ((syNetRollbackEpisodeFsmEnabled() == FALSE) ||
		     (syNetRollbackEpisodeAllPeerSealRowsComplete() != FALSE) ||
		     (sSYNetRollbackResimBaselineDigestMatched == FALSE)))
		{
			sSYNetRollbackResimBaselineDeeperAttempts++;
			sSYNetRollbackResimAwaitingPeerBaseline = FALSE;
			sSYNetRollbackResimBaselineGateOpen = FALSE;
			sSYNetRollbackPeerBaselineSendPending = FALSE;
			sSYNetRollbackResimPending = FALSE;
			if (syNetRollbackTryRestartResimAtDeeperLoad(load_tick - 1U) != FALSE)
			{
				sSYNetRollbackBaselineTimeoutStreak = 0U;
				sSYNetRollbackBaselineItemOnlySelfDriftLoadTick = ~(u32)0;
				port_log(
				    "SSB64 NetRollback: RESIM_BASELINE_TIMEOUT streak — item/seal load-fidelity block, "
				    "deeper-load resync from load_tick=%u attempt=%u (avoiding hard desync)\n",
				    load_tick,
				    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
				return;
			}
		}
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
	u32 slot_r;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL))
	{
		return FALSE;
	}
	load_tick = *io_load_tick;
	live = syNetRollbackCollectHashes();
	slot_f = syNetRbSnapshotGetSlotHashFighter(load_tick);
	slot_r = syNetRbSnapshotGetSlotHashRng(load_tick);
	if (syNetRollbackLoadHashDriftIsPresentationalOnly(load_tick, live.fighter, live.world, live.item, live.weapon,
							 live.map, live.rng, live.camera, live.animation,
							 live.effect) != FALSE)
	{
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	if (syNetRollbackLoadVerifyPerSlotFighDriftOk(load_tick, live.fighter, live.world, live.item, live.weapon,
						      live.map, live.rng, live.animation) != FALSE)
	{
		syNetRbSnapshotRefreshSlotHashFighterWhenPerSlotMatch(load_tick);
		return FALSE;
	}
#endif
	/* syNetRollbackLoadPostTick verify runs before coupling finalize; world/item may drift after eject. */
	if ((live.fighter == slot_f) && (live.rng == slot_r))
	{
		return FALSE;
	}
	if (sSYNetRollbackPreResimDeeperLoadUsed != FALSE)
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
	sSYNetRollbackPreResimDeeperLoadUsed = TRUE;
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
		mismatch_tick = load_tick + 1U;
	}
	if (syNetRollbackPeerBaselineResyncStormLimitReached(load_tick) != FALSE)
	{
		syNetRollbackOnPeerBaselineResyncStormLimit(load_tick);
		return;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Belt-and-suspenders with ComparePeerBaselineToLocal: never queue a deepen whose
	 * onset is already covered by resolved_through while Live (inclusive exclusive end).
	 * Soak seed 3282055674: healed Commit@482 then Arm(473) → 474→485.
	 * Soak seed 214064425: exclusive-end load@776 with resolved=777 must not Arm either.
	 * CoveredThrough includes HealedThrough after FC deepen clears EpisodeResolvedThrough.
	 * Suspend while inputs-agree FC recovery is armed (honest figh compare).
	 * See docs/bugs/netplay_fc_stale_baseline_resync_post_heal_2026-07-28.md,
	 * docs/bugs/netplay_fc_exclusive_end_baseline_diverge_post_heal_2026-07-28.md,
	 * docs/bugs/netplay_fc_healed_frontier_honest_fc_2026-07-28.md.
	 */
	{
		u32 covered;

		covered = syNetRollbackExclusiveEndCoveredThrough();
		if ((covered != 0U) && (mismatch_tick <= covered) && (sSYNetRollbackResimPending == FALSE) &&
		    (syNetRollbackIsResimulating() == FALSE) &&
		    (syNetRollbackFcHonestFighCompareActive() == FALSE))
		{
			port_log(
			    "SSB64 NetRollback: peer baseline resync ignore_stale load_tick=%u mismatch=%u "
			    "resolved_through=%u healed_through=%u sim=%u\n",
			    load_tick,
			    mismatch_tick,
			    sSYNetRollbackEpisodeResolvedThrough,
			    sSYNetRollbackHealedThrough,
			    syNetInputGetTick());
			return;
		}
	}
#endif
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
	         (mismatch_tick < sSYNetRollbackDeferredStateMismatchTick) &&
	         (load_tick + 1U) < sSYNetRollbackDeferredStateMismatchTick)
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

/* Echo-retry alone must not block flushing deferred ROLLBACK_SYNC (follower live-cap hang). */
static sb32 syNetRollbackPeerSymmetricFlushQuiesced(void)
{
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
	    (sSYNetRollbackResimBaselineGateOpen == FALSE))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackQueuePeerSymmetricNotify(s32 slot, u32 mismatch_tick, u32 target_tick, u8 notify_flags)
{
	sb32 follower_local_auth;

	follower_local_auth =
	    ((notify_flags & SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH) != 0U) ? TRUE : FALSE;
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
		sSYNetRollbackPendingPeerSymmetricFlags |= notify_flags;
		return;
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick > sSYNetRollbackPendingPeerSymmetricTick))
	{
		if ((sSYNetRollbackEpisodeResolvedThrough == 0U) ||
		    (mismatch_tick < sSYNetRollbackEpisodeResolvedThrough))
		{
			return;
		}
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback replace stale pending=%u with mismatch_tick=%u target_tick=%u resolved_through=%u sim=%u\n",
		    sSYNetRollbackPendingPeerSymmetricTick,
		    mismatch_tick,
		    target_tick,
		    sSYNetRollbackEpisodeResolvedThrough,
		    syNetInputGetTick());
	}
	if ((sSYNetRollbackPendingPeerSymmetricTick != ~(u32)0) &&
	    (mismatch_tick < sSYNetRollbackPendingPeerSymmetricTick))
	{
		return;
	}
	sSYNetRollbackPendingPeerSymmetricTick = mismatch_tick;
	sSYNetRollbackPendingPeerSymmetricTargetTick = target_tick;
	sSYNetRollbackPendingPeerSymmetricSlot = slot;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = follower_local_auth;
	sSYNetRollbackPendingPeerSymmetricFlags = notify_flags;
	syNetRollbackArmPeerSymmetricRejectLiveCap(mismatch_tick);
	if (syNetRollbackEpisodeAuthorityEnabled() != FALSE)
	{
		syNetRollbackPendingEpisodeSet(slot, mismatch_tick, target_tick, 0U, 0U, notify_flags);
	}
	if (sSYNetRollbackPeerSymmetricLogsRemaining > 0U)
	{
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback queued slot=%d mismatch_tick=%u target_tick=%u flags=0x%02X sim=%u\n",
		    (int)slot,
		    mismatch_tick,
		    target_tick,
		    (unsigned int)notify_flags,
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
	if (syNetRollbackPeerSymmetricFlushQuiesced() == FALSE)
	{
		return;
	}
	mismatch_tick = sSYNetRollbackDeferredPeerSymmetricTick;
	target_tick = sSYNetRollbackDeferredPeerSymmetricTargetTick;
	slot = sSYNetRollbackDeferredPeerSymmetricSlot;
	{
		u8 notify_flags;

		notify_flags = sSYNetRollbackDeferredPeerSymmetricFlags;
		sSYNetRollbackDeferredPeerSymmetricPending = FALSE;
		sSYNetRollbackDeferredPeerSymmetricTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricTargetTick = ~(u32)0;
		sSYNetRollbackDeferredPeerSymmetricSlot = -1;
		sSYNetRollbackDeferredPeerSymmetricFollowerLocalAuth = FALSE;
		sSYNetRollbackDeferredPeerSymmetricFlags = 0U;
		port_log(
		    "SSB64 NetRollback: peer symmetric rollback flush deferred mismatch_tick=%u target_tick=%u flags=0x%02X sim=%u\n",
		    mismatch_tick,
		    target_tick,
		    (unsigned int)notify_flags,
		    syNetInputGetTick());
		syNetRollbackQueuePeerSymmetricNotify(slot, mismatch_tick, target_tick, notify_flags);
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
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * Peer episode load_tick often sits ahead of local resolved_through until the
		 * follower joins/seals. RESIM_BASELINE_ECHO already commits the ring slot; blocking
		 * here only emits BASELINE_ECHO_RETRY_DEFER (soak 1482152420: load=422
		 * resolved=416 after successful echo). Allow compare when the slot is committed.
		 * See docs/bugs/netplay_baseline_echo_retry_ahead_resolved_2026-07-26.md.
		 */
		if (syNetRbSnapshotIsTickCommitted(load_tick) == FALSE)
		{
			return FALSE;
		}
#else
		return FALSE;
#endif
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
	/*
	 * Parked on the load anchor (sim == load_tick) while awaiting peer baseline is ready —
	 * the ring slot is the armed digest. Requiring load_tick < sim forced echo-retry defer
	 * until a forward tick clobbered the slot (soak1 608380406).
	 */
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
	    (load_tick == sSYNetRollbackResimLoadTick) && (load_tick == sim_tick))
	{
		return syNetRbSnapshotIsTickCommitted(load_tick);
	}
	if (load_tick >= sim_tick)
	{
		return FALSE;
	}
	return syNetRbSnapshotIsTickCommitted(load_tick);
}

static void syNetRollbackApplyLoadAnchorFragileWalkback(u32 *io_load_tick, u32 *io_mismatch_tick)
{
	u32 before_load;
	u32 min_load;
	const char *fragile_reason;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL) || (*io_load_tick == 0U))
	{
		return;
	}
	before_load = *io_load_tick;
	min_load = syNetRollbackLoadTickMinBound(syNetInputGetTick());
	fragile_reason = NULL;
	(void)syNetRbSnapshotResolveLoadAnchorAvoidingFragile(io_load_tick,
	                                                      min_load,
	                                                      SYNETROLLBACK_LOAD_TICK_REWIND_MAX,
	                                                      &fragile_reason);
	if (*io_load_tick != before_load)
	{
		if (*io_mismatch_tick > (*io_load_tick + 1U))
		{
			*io_mismatch_tick = *io_load_tick + 1U;
		}
		port_log(
		    "SSB64 NetRollback: RESIM_LOAD_ANCHOR_ADJUST requested=%u resolved=%u mismatch=%u reason=%s\n",
		    before_load,
		    *io_load_tick,
		    *io_mismatch_tick,
		    (fragile_reason != NULL) ? fragile_reason : "fragile");
	}
}

static u32 syNetRollbackResolveDeeperLoadForFidelity(u32 failed_load_tick)
{
	u32 deeper_load;
	u32 min_load;

	if (failed_load_tick == 0U)
	{
		return 0U;
	}
	/* Rescue path (load already failed): ring floor only — the episode floor
	 * pins min_load at failed_load_tick and every Find below returns ~0.
	 * See syNetRollbackLoadTickRingMinBound. */
	min_load = syNetRollbackLoadTickRingMinBound(syNetInputGetTick());
	deeper_load = failed_load_tick - 1U;
	(void)syNetRbSnapshotResolveLoadAnchorAvoidingFragile(&deeper_load,
	                                                      min_load,
	                                                      SYNETROLLBACK_LOAD_TICK_REWIND_MAX,
	                                                      NULL);
	if ((deeper_load == ~(u32)0) || (deeper_load >= failed_load_tick))
	{
		deeper_load = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(failed_load_tick - 1U, min_load);
		if (deeper_load == ~(u32)0)
		{
			deeper_load = syNetRbSnapshotFindLatestValidTickAtOrBefore(failed_load_tick - 1U, min_load);
		}
	}
	if ((deeper_load == ~(u32)0) || (deeper_load >= failed_load_tick))
	{
		return 0U;
	}
	return deeper_load;
}

static sb32 syNetRollbackTryLoadPostTickWithFidelityWalkback(u32 *io_load_tick, u32 *io_mismatch_tick)
{
	u32 walk_attempts;
	u32 min_load;
	u32 episode_min_load;
	u32 before_load;
	u32 deeper;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL) || (*io_load_tick == 0U))
	{
		return FALSE;
	}
	syNetRollbackApplyLoadAnchorFragileWalkback(io_load_tick, io_mismatch_tick);
	if (syNetRollbackLoadPostTick(*io_load_tick) != FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackLoadHashDriftIsResimLoadContext() == FALSE)
	{
		return FALSE;
	}
	/*
	 * Rescue floor: ring capacity only. The load already failed, so the episode
	 * floor (resolved_through - 1) would pin min_load at the failed tick itself
	 * and every Find below returns ~0 — the walkback would break before its
	 * first retry and the caller would arm a terminal BATTLE_SIM_HOLD.
	 * Soak 2026-08-21: light finish invalidated the exclusive frontier, the next
	 * deferred GGPO correction hit LOAD_HASH_DRIFT class=snapshot_fidelity at the
	 * frontier tick, and 0 RESIM_LOAD_FIDELITY_RETRY fired across 8375 ticks.
	 */
	min_load = syNetRollbackLoadTickRingMinBound(syNetInputGetTick());
	episode_min_load = syNetRollbackLoadTickMinBound(syNetInputGetTick());
	walk_attempts = 0U;
	while (walk_attempts < SYNETROLLBACK_LOAD_TICK_REWIND_MAX)
	{
		before_load = *io_load_tick;
		if (before_load == 0U)
		{
			break;
		}
		deeper = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(before_load - 1U, min_load);
		if (deeper == ~(u32)0)
		{
			deeper = syNetRbSnapshotFindLatestValidTickAtOrBefore(before_load - 1U, min_load);
		}
		if ((deeper == ~(u32)0) || (deeper >= before_load))
		{
			break;
		}
		syNetRbSnapshotMarkLoadUnsafe(before_load);
		*io_load_tick = deeper;
		if (*io_mismatch_tick > (*io_load_tick + 1U))
		{
			*io_mismatch_tick = *io_load_tick + 1U;
		}
		port_log(
		    "SSB64 NetRollback: RESIM_LOAD_FIDELITY_RETRY failed=%u deeper=%u mismatch=%u attempt=%u%s\n",
		    before_load,
		    *io_load_tick,
		    *io_mismatch_tick,
		    walk_attempts + 1U,
		    (*io_load_tick < episode_min_load) ? " (below episode floor — rescue)" : "");
		if (syNetRollbackLoadPostTick(*io_load_tick) != FALSE)
		{
			return TRUE;
		}
		if (syNetRollbackLoadHashDriftIsResimLoadContext() == FALSE)
		{
			break;
		}
		walk_attempts++;
	}
	return FALSE;
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
		syNetRollbackApplyLoadAnchorFragileWalkback(io_load_tick, io_mismatch_tick);
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
	syNetRollbackApplyLoadAnchorFragileWalkback(io_load_tick, io_mismatch_tick);
	return TRUE;
}

static sb32 syNetRollbackPeerBaselineWireGameplayMatchArmed(const SYNetRollbackHashSet *peer)
{
	if (peer == NULL)
	{
		return FALSE;
	}
	return ((peer->fighter == sSYNetRollbackPeerBaselineFigh) && (peer->world == sSYNetRollbackPeerBaselineWorld) &&
	        (peer->item == sSYNetRollbackPeerBaselineItem) && (peer->rng == sSYNetRollbackPeerBaselineRng) &&
	        (peer->weapon == sSYNetRollbackPeerBaselineWeapon) && (peer->map == sSYNetRollbackPeerBaselineMap) &&
	        ((sSYNetRollbackLastPeerOutcomeEffectValid == FALSE) ||
	         (peer->effect == sSYNetRollbackPeerBaselineEffect)) &&
	        ((syNetRollbackEpisodeFsmBaselineRequiresAnimMatch() == FALSE) ||
	         (peer->animation == sSYNetRollbackPeerBaselineAnim)))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackPeerBaselineGameplayDigestsMatch(const SYNetRollbackHashSet *peer,
							    const SYNetRollbackHashSet *local)
{
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	return ((peer->fighter == local->fighter) && (peer->world == local->world) && (peer->item == local->item) &&
	        (peer->rng == local->rng) && (peer->animation == local->animation) &&
	        (peer->weapon == local->weapon) && (peer->map == local->map) &&
	        ((sSYNetRollbackLastPeerOutcomeEffectValid == FALSE) || (peer->effect == local->effect)))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackPeerBaselineDriftIsCameraOnlyCosmetic(const SYNetRollbackHashSet *peer,
							       const SYNetRollbackHashSet *local)
{
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	if (peer->camera == local->camera)
	{
		return FALSE;
	}
	return syNetRollbackPeerBaselineGameplayDigestsMatch(peer, local);
}

/*
 * Egg-lay / CaptureYoshi resim: ring aggregate figh can lag post-apply canonicalize while every per-player
 * slot hash still matches live (Linux follower soak2 @519). Baseline wire intentionally used ring aggregate,
 * so one peer can send stale figh while slots and all other partitions agree — false PEER_SNAPSHOT_DIVERGE.
 *
 * Exclusive-end post-heal (soak seed 214064425): SoftLip/topn can leave peers with mismatched ring figh at
 * load == resolved_through - 1 while world/rng/map/anim agree. Slot/live refresh can succeed on one peer
 * and fail on the other — asymmetric hard kill. Covered exclusive-end figh drift is stale aggregate.
 *
 * Follow-on (soak seed 593462826): mid-resim deepen at exclusive-end can also skew camera (cosmetic) while
 * Live peer ignore_stale already soft-continues. Require non-cam gameplay digests; allow cam mismatch when
 * (load+1) <= resolved_through so both peers soft-continue. See
 * docs/bugs/netplay_fc_exclusive_end_baseline_figh_cam_mid_resim_2026-07-28.md.
 *
 * Follow-on (soak seed 2064608876): FC deepen ResetCorrectionEpisode clears EpisodeResolvedThrough;
 * exclusive-end coverage must use durable HealedThrough. See
 * docs/bugs/netplay_fc_healed_frontier_survive_deepen_2026-07-28.md.
 */
static sb32 syNetRollbackPeerBaselineDriftIsStaleAggregateFighOnly(u32 load_tick, const SYNetRollbackHashSet *peer,
								   const SYNetRollbackHashSet *local,
								   const u32 *peer_fighter_slot)
{
	SYNetRollbackHashSet live;
	u32 live_slots[GMCOMMON_PLAYERS_MAX];

#if defined(SSB64_NETMENU)
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	if (peer->fighter == local->fighter)
	{
		return FALSE;
	}
	/* Non-cam gameplay partitions must match; camera is checked below (or waived at exclusive end). */
	if ((peer->world != local->world) || (peer->item != local->item) || (peer->rng != local->rng) ||
	    (peer->animation != local->animation) || (peer->weapon != local->weapon) || (peer->map != local->map) ||
	    ((sSYNetRollbackLastPeerOutcomeEffectValid != FALSE) && (peer->effect != local->effect)))
	{
		return FALSE;
	}
#if defined(PORT)
	/*
	 * Exclusive-end soft-continue uses durable HealedThrough as well as live
	 * EpisodeResolvedThrough — FC deepen ResetCorrectionEpisode zeros the latter
	 * (soak seed 2064608876: load=1004 after Commit@1015 hard-killed without coverage).
	 * Suspend during inputs-agree FC recovery so deepen sees real figh digests.
	 */
	if (syNetRollbackFcHonestFighCompareActive() == FALSE)
	{
		u32 covered;

		covered = syNetRollbackExclusiveEndCoveredThrough();
		if ((covered != 0U) && ((load_tick + 1U) <= covered))
		{
			return TRUE;
		}
	}
#else
	if ((sSYNetRollbackEpisodeResolvedThrough != 0U) &&
	    ((load_tick + 1U) <= sSYNetRollbackEpisodeResolvedThrough))
	{
		return TRUE;
	}
#endif
	if (peer->camera != local->camera)
	{
		return FALSE;
	}
	live = syNetRollbackCollectHashes();
	if ((syNetRollbackLoadVerifyPerSlotFighDriftOk(load_tick, live.fighter, live.world, live.item, live.weapon,
						       live.map, live.rng, live.animation) != FALSE) &&
	    (peer->fighter == live.fighter))
	{
		return TRUE;
	}
	syNetRollbackCollectFighterSlotHashes(live_slots);
	if ((peer_fighter_slot != NULL) &&
	    (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, live_slots) != FALSE) &&
	    (syNetRbSnapshotAllFighterSlotHashesMatchAtTick(load_tick) != FALSE))
	{
		return TRUE;
	}
	return FALSE;
#else
	(void)load_tick;
	(void)peer;
	(void)local;
	(void)peer_fighter_slot;
	return FALSE;
#endif
}

static sb32 syNetRollbackCollectBaselineCompareLocal(u32 load_tick, SYNetRollbackHashSet *out)
{
	if (out == NULL)
	{
		return FALSE;
	}
	if (syNetRollbackCollectRingBaselineAtTick(load_tick, out) == FALSE)
	{
		*out = syNetRollbackCollectHashes();
		return FALSE;
	}
	/* After sim advances past load_tick, ring camera reads can reflect live skew; armed camera is frozen at arm. */
	if ((load_tick != 0U) && (load_tick == sSYNetRollbackPeerBaselineLoadTick))
	{
		out->camera = sSYNetRollbackPeerBaselineSlotCamera;
	}
#if defined(SSB64_NETMENU)
	{
		SYNetRollbackHashSet live_compare;

		live_compare = syNetRollbackCollectHashes();
		if (syNetRollbackLoadVerifyPerSlotFighDriftOk(load_tick, live_compare.fighter, live_compare.world,
							      live_compare.item, live_compare.weapon, live_compare.map,
							      live_compare.rng, live_compare.animation) != FALSE)
		{
			out->fighter = live_compare.fighter;
		}
	}
#endif
	return TRUE;
}

static void syNetRollbackAlignArmedBaselineCameraFromPeerWire(u32 load_tick, u32 peer_camera)
{
	u32 ring_camera;

	if (peer_camera == sSYNetRollbackPeerBaselineCamera)
	{
		return;
	}
	ring_camera = syNetRbSnapshotGetSlotHashCamera(load_tick);
	port_log(
	    "SSB64 NetRollback: BASELINE_CAMERA_WIRE_ALIGN load_tick=%u armed_cam=0x%08X peer_cam=0x%08X ring_cam=0x%08X\n",
	    load_tick,
	    sSYNetRollbackPeerBaselineCamera,
	    peer_camera,
	    ring_camera);
	sSYNetRollbackPeerBaselineCamera = peer_camera;
	if (peer_camera == ring_camera)
	{
		sSYNetRollbackPeerBaselineSlotCamera = ring_camera;
	}
}

#if defined(SSB64_NETMENU)
static void syNetRollbackAlignArmedBaselineWeaponFromPeerWire(u32 load_tick, u32 peer_weapon)
{
	u32 ring_weapon;

	if (peer_weapon == sSYNetRollbackPeerBaselineWeapon)
	{
		return;
	}
	ring_weapon = syNetRbSnapshotGetSlotHashWeapon(load_tick);
	port_log(
	    "SSB64 NetRollback: BASELINE_WEAPON_WIRE_ALIGN load_tick=%u armed_wpn=0x%08X peer_wpn=0x%08X ring_wpn=0x%08X\n",
	    load_tick,
	    sSYNetRollbackPeerBaselineWeapon,
	    peer_weapon,
	    ring_weapon);
	sSYNetRollbackPeerBaselineWeapon = peer_weapon;
	if (peer_weapon == ring_weapon)
	{
		sSYNetRollbackPeerBaselineSlotWeapon = ring_weapon;
	}
	else
	{
		sSYNetRollbackPeerBaselineSlotWeapon = peer_weapon;
	}
}

/*
 * Mid-Hold PK Thunder: peer_vs_armed often shows wpn-only while figh/world/rng/anim agree.
 * Deepening that band storms seal rows (soak after Hold-aim GGPO). Treat weapon as Hold-fragile
 * and open the replay gate after adopting peer wire weapon into the armed baseline.
 */
static sb32 syNetRollbackPeerBaselineDriftIsWeaponOnlyVsArmed(const SYNetRollbackHashSet *peer)
{
	if (peer == NULL)
	{
		return FALSE;
	}
	if (peer->weapon == sSYNetRollbackPeerBaselineWeapon)
	{
		return FALSE;
	}
	if ((peer->fighter != sSYNetRollbackPeerBaselineFigh) || (peer->world != sSYNetRollbackPeerBaselineWorld) ||
	    (peer->item != sSYNetRollbackPeerBaselineItem) || (peer->rng != sSYNetRollbackPeerBaselineRng) ||
	    (peer->map != sSYNetRollbackPeerBaselineMap))
	{
		return FALSE;
	}
	if ((sSYNetRollbackLastPeerOutcomeEffectValid != FALSE) &&
	    (peer->effect != sSYNetRollbackPeerBaselineEffect))
	{
		return FALSE;
	}
	if ((syNetRollbackEpisodeFsmBaselineRequiresAnimMatch() != FALSE) &&
	    (peer->animation != sSYNetRollbackPeerBaselineAnim))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackTryOpenResimReplayGateAfterPkHoldWeaponOnlyAbsorb(u32 load_tick,
									    const SYNetRollbackHashSet *peer,
									    const u32 *peer_fighter_slot)
{
	if ((peer == NULL) || (load_tick != sSYNetRollbackResimLoadTick) ||
	    (sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE) ||
	    (sSYNetRollbackResimBaselineGateOpen != FALSE) || (sSYNetRollbackResimBaselineDigestMatched != FALSE))
	{
		return;
	}
	if (syNetplayNessAnyLiveFighterInPkHoldAimScope() == FALSE)
	{
		return;
	}
	if (syNetRollbackPeerBaselineDriftIsWeaponOnlyVsArmed(peer) == FALSE)
	{
		return;
	}
	if ((peer_fighter_slot != NULL) &&
	    (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, sSYNetRollbackPeerBaselineFighterSlot) ==
	     FALSE))
	{
		return;
	}
	syNetRollbackAlignArmedBaselineWeaponFromPeerWire(load_tick, peer->weapon);
	if (peer->camera != sSYNetRollbackPeerBaselineCamera)
	{
		syNetRollbackAlignArmedBaselineCameraFromPeerWire(load_tick, peer->camera);
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_PK_HOLD_WPN_ONLY_ABSORB load_tick=%u peer_wpn=0x%08X armed_wpn=0x%08X "
	    "— opening replay gate (Hold aim weapon-fragile)\n",
	    load_tick,
	    peer->weapon,
	    sSYNetRollbackPeerBaselineWeapon);
	sSYNetRollbackResimBaselineDigestMatched = TRUE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	syNetRollbackTryOpenResimReplayGate();
}
#endif

static sb32 syNetRollbackCollectRingBaselineAtTick(u32 load_tick, SYNetRollbackHashSet *out)
{
	if ((out == NULL) || (load_tick == 0U))
	{
		return FALSE;
	}
	if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &out->fighter, &out->world, &out->item, &out->rng) == FALSE)
	{
		return FALSE;
	}
	out->weapon = syNetRbSnapshotGetSlotHashWeapon(load_tick);
	out->map = syNetRbSnapshotGetSlotHashMap(load_tick);
	out->camera = syNetRbSnapshotGetSlotHashCamera(load_tick);
	out->animation = syNetRbSnapshotGetSlotHashAnimation(load_tick);
	out->effect = syNetRbSnapshotGetSlotHashEffect(load_tick);
	return TRUE;
}

static sb32 syNetRollbackPeerBaselineWireDigestsMatchArmed(const SYNetRollbackHashSet *peer)
{
	if (peer == NULL)
	{
		return FALSE;
	}
	return ((peer->fighter == sSYNetRollbackPeerBaselineFigh) && (peer->world == sSYNetRollbackPeerBaselineWorld) &&
	        (peer->item == sSYNetRollbackPeerBaselineItem) && (peer->rng == sSYNetRollbackPeerBaselineRng) &&
	        (peer->weapon == sSYNetRollbackPeerBaselineWeapon) && (peer->map == sSYNetRollbackPeerBaselineMap) &&
	        (peer->camera == sSYNetRollbackPeerBaselineCamera) &&
	        ((sSYNetRollbackLastPeerOutcomeEffectValid == FALSE) ||
	         (peer->effect == sSYNetRollbackPeerBaselineEffect)) &&
	        ((syNetRollbackEpisodeFsmBaselineRequiresAnimMatch() == FALSE) ||
	         (peer->animation == sSYNetRollbackPeerBaselineAnim)))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackPeerBaselineSlotSubsystemOk(const SYNetRollbackHashSet *peer)
{
	if (peer == NULL)
	{
		return FALSE;
	}
	return ((peer->fighter == sSYNetRollbackPeerBaselineSlotFigh) &&
	        (peer->world == sSYNetRollbackPeerBaselineSlotWorld) &&
	        (peer->item == sSYNetRollbackPeerBaselineSlotItem) && (peer->rng == sSYNetRollbackPeerBaselineSlotRng) &&
	        (peer->weapon == sSYNetRollbackPeerBaselineSlotWeapon) && (peer->map == sSYNetRollbackPeerBaselineSlotMap) &&
	        (peer->camera == sSYNetRollbackPeerBaselineSlotCamera) &&
	        ((sSYNetRollbackLastPeerOutcomeEffectValid == FALSE) ||
	         (peer->effect == sSYNetRollbackPeerBaselineSlotEffect)))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRollbackPeerBaselineSlotGameplaySubsystemOk(const SYNetRollbackHashSet *peer)
{
	if (peer == NULL)
	{
		return FALSE;
	}
	return ((peer->fighter == sSYNetRollbackPeerBaselineSlotFigh) &&
	        (peer->world == sSYNetRollbackPeerBaselineSlotWorld) &&
	        (peer->item == sSYNetRollbackPeerBaselineSlotItem) && (peer->rng == sSYNetRollbackPeerBaselineSlotRng) &&
	        (peer->weapon == sSYNetRollbackPeerBaselineSlotWeapon) && (peer->map == sSYNetRollbackPeerBaselineSlotMap) &&
	        ((sSYNetRollbackLastPeerOutcomeEffectValid == FALSE) ||
	         (peer->effect == sSYNetRollbackPeerBaselineSlotEffect)))
	           ? TRUE
	           : FALSE;
}

static void syNetRollbackTryOpenResimReplayGateAfterSlotRingResync(u32 load_tick, const SYNetRollbackHashSet *peer,
								   const u32 *peer_fighter_slot)
{
	u32 ring_slots[GMCOMMON_PLAYERS_MAX];
	s32 si;

	if ((peer == NULL) || (peer_fighter_slot == NULL) || (load_tick != sSYNetRollbackResimLoadTick))
	{
		return;
	}
	if (syNetRollbackPeerBaselineWireGameplayMatchArmed(peer) == FALSE)
	{
		return;
	}
	if (syNetRollbackPeerBaselineSlotGameplaySubsystemOk(peer) == FALSE)
	{
		return;
	}
	if (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, sSYNetRollbackPeerBaselineFighterSlot) != FALSE)
	{
		return;
	}
	syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, ring_slots);
	if (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, ring_slots) == FALSE)
	{
		return;
	}
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		if (sSYNetRollbackPeerBaselineFighterSlot[si] != ring_slots[si])
		{
			port_log(
			    "SSB64 NetRollback: RESIM_BASELINE_SLOT_RING_RESYNC load_tick=%u player=%d armed_slot=0x%08X ring_slot=0x%08X peer_slot=0x%08X\n",
			    load_tick,
			    (int)si,
			    sSYNetRollbackPeerBaselineFighterSlot[si],
			    ring_slots[si],
			    peer_fighter_slot[si]);
		}
		sSYNetRollbackPeerBaselineFighterSlot[si] = ring_slots[si];
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_SLOT_RING_RESYNC load_tick=%u — opening replay gate (main digest matched, armed slots realigned to ring)\n",
	    load_tick);
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE))
	{
		return;
	}
	sSYNetRollbackResimBaselineDigestMatched = TRUE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	syNetRollbackTryOpenResimReplayGate();
}

static void syNetRollbackTryOpenResimReplayGateAfterCameraRingResync(u32 load_tick, const SYNetRollbackHashSet *peer,
								     const u32 *peer_fighter_slot)
{
	u32 ring_camera;
	u32 ring_slots[GMCOMMON_PLAYERS_MAX];

	if ((peer == NULL) || (load_tick != sSYNetRollbackResimLoadTick))
	{
		return;
	}
	if (syNetRollbackPeerBaselineWireGameplayMatchArmed(peer) == FALSE)
	{
		return;
	}
	if (peer->camera == sSYNetRollbackPeerBaselineCamera)
	{
		return;
	}
	ring_camera = syNetRbSnapshotGetSlotHashCamera(load_tick);
	if (peer->camera == ring_camera)
	{
		if (sSYNetRollbackPeerBaselineCamera != ring_camera)
		{
			port_log(
			    "SSB64 NetRollback: RESIM_BASELINE_CAMERA_RING_RESYNC load_tick=%u armed_cam=0x%08X ring_cam=0x%08X peer_cam=0x%08X\n",
			    load_tick,
			    sSYNetRollbackPeerBaselineCamera,
			    ring_camera,
			    peer->camera);
		}
		sSYNetRollbackPeerBaselineCamera = ring_camera;
		sSYNetRollbackPeerBaselineSlotCamera = ring_camera;
	}
	else if (syNetRollbackPeerBaselineWireGameplayMatchArmed(peer) != FALSE)
	{
		/* Cross-peer cosmetic camera: gameplay digests agree; adopt peer wire camera. */
		syNetRollbackAlignArmedBaselineCameraFromPeerWire(load_tick, peer->camera);
	}
	else
	{
		return;
	}
	if (peer_fighter_slot != NULL)
	{
		syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, ring_slots);
		if (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, ring_slots) == FALSE)
		{
			return;
		}
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_CAMERA_RING_RESYNC load_tick=%u — opening replay gate (gameplay digest matched, armed camera realigned)\n",
	    load_tick);
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE))
	{
		return;
	}
	sSYNetRollbackResimBaselineDigestMatched = TRUE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	syNetRollbackTryOpenResimReplayGate();
}

static sb32 syNetRollbackPeerBaselineDriftIsAnimOnly(const SYNetRollbackHashSet *peer, const SYNetRollbackHashSet *local)
{
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	if ((peer->fighter != local->fighter) || (peer->world != local->world) || (peer->item != local->item) ||
	    (peer->rng != local->rng) || (peer->weapon != local->weapon) || (peer->map != local->map) ||
	    (peer->camera != local->camera) || (peer->effect != local->effect))
	{
		return FALSE;
	}
	if (peer->animation == local->animation)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRollbackPeerBaselineAnimOnlyVsArmedLive(const SYNetRollbackHashSet *peer)
{
	SYNetRollbackHashSet local;

	if (peer == NULL)
	{
		return FALSE;
	}
	local.fighter = sSYNetRollbackPeerBaselineFigh;
	local.world = sSYNetRollbackPeerBaselineWorld;
	local.item = sSYNetRollbackPeerBaselineItem;
	local.rng = sSYNetRollbackPeerBaselineRng;
	local.weapon = sSYNetRollbackPeerBaselineWeapon;
	local.map = sSYNetRollbackPeerBaselineMap;
	local.camera = sSYNetRollbackPeerBaselineCamera;
	local.animation = sSYNetRollbackPeerBaselineAnim;
	local.effect = sSYNetRollbackPeerBaselineEffect;
	return syNetRollbackPeerBaselineDriftIsAnimOnly(peer, &local);
}

static void syNetRollbackTryOpenResimReplayGateAfterAnimResync(u32 load_tick, const SYNetRollbackHashSet *peer)
{
	if (syNetRollbackPeerBaselineAnimOnlyVsArmedLive(peer) == FALSE)
	{
		return;
	}
	syNetRbSnapshotReapplyJointAnimAtTick(load_tick);
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_ANIM_ONLY load_tick=%u peer anim=0x%08X local anim=0x%08X — opening replay gate\n",
	    load_tick,
	    peer->animation,
	    sSYNetRollbackPeerBaselineAnim);
	if ((sSYNetRollbackResimPending == FALSE) || (sSYNetRollbackResimAwaitingPeerBaseline == FALSE))
	{
		return;
	}
	sSYNetRollbackResimBaselineDigestMatched = TRUE;
	sSYNetRollbackResimBaselineWaitFrames = 0U;
	sSYNetRollbackPeerBaselineSendPending = FALSE;
	sSYNetRollbackBaselineTimeoutStreak = 0U;
	sSYNetRollbackFcDeepenInFlight = FALSE;
	syNetRollbackTryOpenResimReplayGate();
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
	/*
	 * Camera is presentational (see DriftIsCameraOnlyCosmetic). Map+cam forks used to miss this
	 * path and hard-stop PEER_SNAPSHOT_DIVERGE while fighters matched (soak 1309587627 @1959).
	 * Treat cam mismatch as noise so map-only resync/deeper still runs.
	 */
	if ((peer->fighter != local->fighter) || (peer->world != local->world) || (peer->item != local->item) ||
	    (peer->rng != local->rng) || (peer->animation != local->animation) || (peer->weapon != local->weapon) ||
	    (peer->effect != local->effect))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRollbackFillArmedBaselineHashSet(SYNetRollbackHashSet *out)
{
	if (out == NULL)
	{
		return;
	}
	memset(out, 0, sizeof(*out));
	out->fighter = sSYNetRollbackPeerBaselineFigh;
	out->world = sSYNetRollbackPeerBaselineWorld;
	out->item = sSYNetRollbackPeerBaselineItem;
	out->rng = sSYNetRollbackPeerBaselineRng;
	out->animation = sSYNetRollbackPeerBaselineAnim;
	out->weapon = sSYNetRollbackPeerBaselineWeapon;
	out->map = sSYNetRollbackPeerBaselineMap;
	out->camera = sSYNetRollbackPeerBaselineCamera;
	out->effect = sSYNetRollbackPeerBaselineEffect;
}

/*
 * Hard gameplay partitions that deeper-load cannot invent agreement for once the deepen budget
 * is spent. Cam/anim-alone soft drifts are intentionally excluded (cosmetic / joint resync paths).
 */
static sb32 syNetRollbackPeerBaselineHardGameplayDiverge(const SYNetRollbackHashSet *peer,
							  const SYNetRollbackHashSet *local)
{
	if ((peer == NULL) || (local == NULL))
	{
		return FALSE;
	}
	if ((peer->fighter != local->fighter) || (peer->world != local->world) || (peer->item != local->item) ||
	    (peer->rng != local->rng) || (peer->map != local->map))
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * After state-diverge deepen exhaust: fail closed on map-only or figh/world/map hard diverge.
 * Returns TRUE if the session was stopped (or diverge path consumed the mismatch).
 */
static sb32 syNetRollbackTryFailClosedAfterStateDeepenExhaust(u32 failed_load_tick)
{
	SYNetRollbackHashSet local_cmp;
	const SYNetRollbackHashSet *peer;
	const u32 *slots;

	if (sSYNetRollbackLastPeerOutcomeValid == FALSE)
	{
		return FALSE;
	}
	/*
	 * Input-poisoned loads still prefer arming peer baseline resync / GGPO rather than hard-stop
	 * here — deepen exhaust is for the inputs-agree-through-load class.
	 */
	if (syNetRollbackUniverseMismatchInputPoisonedAtLoad(failed_load_tick, NULL, NULL) != FALSE)
	{
		return FALSE;
	}
	peer = &sSYNetRollbackLastPeerOutcomeHash;
	/*
	 * Prefer the armed / resim load digests for failed_load_tick — CollectHashes() may already
	 * reflect live ticks past the diverge point after abort.
	 */
	if ((sSYNetRollbackPeerBaselineLoadTick == failed_load_tick) ||
	    (sSYNetRollbackResimLoadTick == failed_load_tick))
	{
		syNetRollbackFillArmedBaselineHashSet(&local_cmp);
	}
	else
	{
		local_cmp = syNetRollbackCollectHashes();
	}
	slots = (sSYNetRollbackLastPeerOutcomeFighterSlotsValid != FALSE) ? sSYNetRollbackLastPeerOutcomeFighterSlot
									 : NULL;
	if (syNetRollbackPeerBaselineDriftIsGameplayOnlyMap(peer, &local_cmp) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH map-only deeper exhausted failed_load=%u peer_map=0x%08X local_map=0x%08X — PEER_SNAPSHOT_DIVERGE\n",
		    failed_load_tick,
		    peer->map,
		    local_cmp.map);
		syNetRollbackFailPeerSnapshotDiverge(failed_load_tick, peer, &local_cmp, slots);
		return TRUE;
	}
	if (syNetRollbackPeerBaselineHardGameplayDiverge(peer, &local_cmp) == FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetRollback: RESIM_BASELINE_MISMATCH state deepen exhausted failed_load=%u peer figh=0x%08X map=0x%08X world=0x%08X | local figh=0x%08X map=0x%08X world=0x%08X — PEER_SNAPSHOT_DIVERGE\n",
	    failed_load_tick,
	    peer->fighter,
	    peer->map,
	    peer->world,
	    local_cmp.fighter,
	    local_cmp.map,
	    local_cmp.world);
	syNetRollbackFailPeerSnapshotDiverge(failed_load_tick, peer, &local_cmp, slots);
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
	u32 frontier;
	u32 frontier_floor;
	u32 effective;

	if (load_tick == 0U)
	{
		return load_tick;
	}
	remote_hr = syNetPeerGetHighestRemoteTick();
	if ((remote_hr == 0U) || (load_tick <= remote_hr))
	{
		return load_tick;
	}
	effective = remote_hr;
#if defined(SSB64_NETMENU)
	/*
	 * Do not advertise a baseline load behind the shared correction frontier — peer will
	 * clamp/skip and seal-wait forever (soak 1734260705). Floor at frontier-1 when the
	 * original load was at/after that floor.
	 */
	frontier = syNetRollbackGetSharedCorrectionFrontier();
	frontier_floor = (frontier > 0U) ? (frontier - 1U) : 0U;
	if ((frontier_floor > 0U) && (effective < frontier_floor) && (frontier_floor <= load_tick) &&
	    (sSYNetRollbackFcStateRecoveryActive == FALSE))
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_LOAD_CLAMP_FRONTIER_FLOOR orig_load_tick=%u remote_hr=%u "
		    "floored_load=%u frontier=%u sim=%u\n",
		    load_tick,
		    remote_hr,
		    frontier_floor,
		    frontier,
		    syNetInputGetTick());
		effective = frontier_floor;
	}
	else
#endif
	{
		port_log(
		    "SSB64 NetRollback: BASELINE_LOAD_CLAMP orig_load_tick=%u effective_load_tick=%u remote_hr=%u sim=%u\n",
		    load_tick,
		    effective,
		    remote_hr,
		    syNetInputGetTick());
	}
	return effective;
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
	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		static u32 sLastHoldBlockedBaselineCompareLogTick = ~(u32)0;

		if (load_tick != sLastHoldBlockedBaselineCompareLogTick)
		{
			port_log(
			    "SSB64 NetRollback: PEER_BASELINE_COMPARE ignored (BATTLE_SIM_HOLD) load_tick=%u peer figh=0x%08X\n",
			    load_tick,
			    peer->fighter);
			sLastHoldBlockedBaselineCompareLogTick = load_tick;
		}
		return;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Live after Commit→Live: peer baselines for loads already covered by resolved_through
	 * (inclusive exclusive end: load+1 == resolved) must not re-arm FC deepen or
	 * PEER_SNAPSHOT_DIVERGE. In-flight AwaitingBaseline still compares.
	 * Soak seed 3282055674: healed @482, stale peer baseline@473 → Arm 474→485.
	 * Soak seed 214064425: exclusive-end @776 with resolved=777 → Linux hard kill.
	 * CoveredThrough survives FC deepen ResetCorrectionEpisode (seed 2064608876).
	 * Suspend while inputs-agree FC recovery needs honest figh compare (soak1 313936707).
	 * See docs/bugs/netplay_fc_healed_frontier_honest_fc_2026-07-28.md.
	 */
	{
		u32 covered;

		covered = syNetRollbackExclusiveEndCoveredThrough();
		if ((covered != 0U) && ((load_tick + 1U) <= covered) && (sSYNetRollbackResimPending == FALSE) &&
		    (syNetRollbackIsResimulating() == FALSE) &&
		    (sSYNetRollbackDeferredStateMismatchPending == FALSE) &&
		    (syNetRollbackFcHonestFighCompareActive() == FALSE))
		{
			port_log(
			    "SSB64 NetRollback: PEER_BASELINE_COMPARE ignore_stale_behind_resolved load_tick=%u "
			    "resolved_through=%u healed_through=%u peer figh=0x%08X sim=%u\n",
			    load_tick,
			    sSYNetRollbackEpisodeResolvedThrough,
			    sSYNetRollbackHealedThrough,
			    peer->fighter,
			    syNetInputGetTick());
			return;
		}
	}
#endif
	if (syNetRollbackCollectBaselineCompareLocal(load_tick, &local) == FALSE)
	{
		/* live fallback already applied inside CollectBaselineCompareLocal */
	}
	if ((peer->fighter == local.fighter) && (peer->world == local.world) && (peer->item == local.item) &&
	    (peer->rng == local.rng) && (peer->animation == local.animation) && (peer->weapon == local.weapon) &&
	    (peer->map == local.map) && (peer->camera == local.camera) &&
	    ((sSYNetRollbackLastPeerOutcomeEffectValid == FALSE) || (peer->effect == local.effect)))
	{
		return;
	}
	if (syNetRollbackPeerBaselineDriftIsCameraOnlyCosmetic(peer, &local) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_CAMERA_COSMETIC_OK load_tick=%u peer cam=0x%08X local cam=0x%08X armed cam=0x%08X — continuing\n",
		    load_tick,
		    peer->camera,
		    local.camera,
		    sSYNetRollbackPeerBaselineCamera);
		if ((load_tick == sSYNetRollbackPeerBaselineLoadTick) && (peer->camera != sSYNetRollbackPeerBaselineCamera))
		{
			syNetRollbackAlignArmedBaselineCameraFromPeerWire(load_tick, peer->camera);
		}
		if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
		    (load_tick == sSYNetRollbackResimLoadTick))
		{
			syNetRollbackTryOpenResimReplayGateAfterCameraRingResync(load_tick, peer, peer_fighter_slot);
		}
		return;
	}
	if (syNetRollbackPeerBaselineDriftIsStaleAggregateFighOnly(load_tick, peer, &local, peer_fighter_slot) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_FIGH_STALE_AGGREGATE_OK load_tick=%u peer figh=0x%08X local figh=0x%08X — continuing\n",
		    load_tick,
		    peer->fighter,
		    local.fighter);
		syNetRbSnapshotRefreshSlotHashFighterWhenPerSlotMatch(load_tick);
		if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE) &&
		    (load_tick == sSYNetRollbackResimLoadTick))
		{
			sSYNetRollbackResimBaselineDigestMatched = TRUE;
			sSYNetRollbackResimBaselineWaitFrames = 0U;
			sSYNetRollbackPeerBaselineSendPending = FALSE;
			sSYNetRollbackBaselineTimeoutStreak = 0U;
			sSYNetRollbackFcDeepenInFlight = FALSE;
			syNetRollbackTryOpenResimReplayGate();
		}
		return;
	}
	if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackEpisodeInputsSealed() != FALSE) &&
	    (syNetRollbackEpisodeAllPeerSealRowsComplete() != FALSE) && (peer->world == local.world) &&
	    (peer->item == local.item) && (peer->rng == local.rng) && (peer->fighter != local.fighter))
	{
		port_log(
		    "SSB64 NetRollback: EPISODE_FSM seal_authority_mismatch load_tick=%u peer figh=0x%08X local figh=0x%08X\n",
		    load_tick,
		    peer->fighter,
		    local.fighter);
		syNetSyncLogBaselineUniverseDiff(load_tick, peer->fighter, local.fighter, peer->world, local.world,
						 peer->rng, local.rng);
		syNetRbSnapshotLogFighterFieldDiffAtTick(load_tick, "seal_authority_mismatch");
		{
			s32 i;
			s32 n;
			s32 remote_slot;
			u32 auth_mismatch;

			n = syNetPeerGetRemoteHumanSlotCount();
			for (i = 0; i < n; i++)
			{
				if (syNetPeerGetRemoteHumanSlotByIndex(i, &remote_slot) == FALSE)
				{
					continue;
				}
				auth_mismatch = syNetInputFindEarliestRemoteAuthorityMismatch(remote_slot, load_tick, load_tick + 4U);
				if (auth_mismatch != ~(u32)0)
				{
					port_log(
					    "SSB64 NetRollback: seal_authority_mismatch remote_auth player=%d earliest_tick=%u load_tick=%u\n",
					    (int)remote_slot, (unsigned int)auth_mismatch, load_tick);
				}
			}
		}
		/*
		 * AbortPending → deeper-load was uncapped from this path (DEEPER_MAX only gated
		 * timeout/fidelity sites). Soak1 Linux: hundreds of 656↔657 seal/echo cycles then
		 * SIGSEGV in mmIcePoll on the GObj 64 KB stack. Once deeper budget is spent, treat
		 * as hard peer diverge instead of restarting forever.
		 */
		if (sSYNetRollbackResimBaselineDeeperAttempts >= SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS)
		{
			port_log(
			    "SSB64 NetRollback: seal_authority_mismatch deeper exhausted load_tick=%u attempts=%u — PEER_SNAPSHOT_DIVERGE\n",
			    load_tick,
			    (unsigned int)sSYNetRollbackResimBaselineDeeperAttempts);
			syNetRollbackFailPeerSnapshotDiverge(load_tick, peer, &local, peer_fighter_slot);
			return;
		}
		/*
		 * Sealed inputs already exchanged; figh diverge with matching world/rng is state
		 * recovery (deeper load), not a fresh GGPO. AbortToInputCorrection here re-queued
		 * the episode's own post-load stick mismatch and hung (soak1 1160137450).
		 * See docs/bugs/netplay_baseline_universe_state_vs_input_routing_2026-07-13.md.
		 */
		syNetRollbackAbortPendingResimForBaselineMismatch(load_tick);
		return;
	}
	if (syNetRollbackPeerBaselineDriftIsAnimOnly(peer, &local) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_ANIM_ONLY load_tick=%u peer anim=0x%08X local anim=0x%08X — continuing\n",
		    load_tick,
		    peer->animation,
		    local.animation);
		syNetRbSnapshotReapplyJointAnimAtTick(load_tick);
		if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE))
		{
			sSYNetRollbackResimBaselineDigestMatched = TRUE;
			sSYNetRollbackResimBaselineWaitFrames = 0U;
			sSYNetRollbackPeerBaselineSendPending = FALSE;
			sSYNetRollbackBaselineTimeoutStreak = 0U;
			sSYNetRollbackFcDeepenInFlight = FALSE;
			syNetRollbackTryOpenResimReplayGate();
		}
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
	if ((sSYNetRollbackResimPending != FALSE) && (sSYNetRollbackResimAwaitingPeerBaseline != FALSE))
	{
		syNetRollbackTryOpenResimReplayGateAfterCameraRingResync(load_tick, peer, peer_fighter_slot);
		if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
		{
			return;
		}
		if (peer_fighter_slot != NULL)
		{
			syNetRollbackTryOpenResimReplayGateAfterSlotRingResync(load_tick, peer, peer_fighter_slot);
			if (sSYNetRollbackResimBaselineDigestMatched != FALSE)
			{
				return;
			}
		}
		if (syNetRollbackPeerBaselineWireGameplayMatchArmed(peer) != FALSE)
		{
			u32 ring_slots[GMCOMMON_PLAYERS_MAX];
			sb32 ring_slots_ok;

			ring_slots_ok = TRUE;
			if (peer_fighter_slot != NULL)
			{
				syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, ring_slots);
				ring_slots_ok =
				    (syNetRollbackBaselineFighterSlotsMatch(peer_fighter_slot, ring_slots) != FALSE) ? TRUE
				                                                                                      : FALSE;
			}
			if (ring_slots_ok != FALSE)
			{
				if (syNetRollbackPeerBaselineGameplayDigestsMatch(peer, &local) != FALSE)
				{
					port_log(
					    "SSB64 NetRollback: PEER_BASELINE_COSMETIC_RING_OK load_tick=%u peer cam=0x%08X local cam=0x%08X — continuing\n",
					    load_tick,
					    peer->camera,
					    local.camera);
					return;
				}
			}
		}
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
	const char *proof_class;
	sb32 input_poisoned;

	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE suppressed (BATTLE_SIM_HOLD) load_tick=%u peer figh=0x%08X local figh=0x%08X\n",
		    load_tick,
		    (peer != NULL) ? peer->fighter : 0U,
		    (local != NULL) ? local->fighter : 0U);
		return;
	}
	input_poisoned = syNetRollbackUniverseMismatchInputPoisonedAtLoad(load_tick, NULL, NULL);
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Stick-onset GGPO / EPISODE_SEAL_ROWS_WAIT (soak1 871504438): peer baseline can land
	 * while seal rows / stick absorb are still open — fail-closed mid-heal. Defer kill until
	 * defer-interface wait clears; deeper exhaust still fail-closes after. See
	 * docs/bugs/netplay_zero_onset_predict_runway_peer_2026-07-20.md.
	 *
	 * Hang-exit contract (2026-07-27, soak 891751718 @960): the defer applies only to the
	 * protocol class (a pending input correction can still heal) and only within a bounded
	 * budget per load_tick. inputs_agree ∧ state_disagree (replay_determinism) reaches this
	 * point with deepen already exhausted — no wait can heal it; context windows
	 * (seal wait on host, stick absorb on guest) re-suppressed every retransmitted peer
	 * baseline until session timeout. That class falls through to the fail-closed abort.
	 * See docs/bugs/netplay_close_wire_cap_diverge_hang_exit_2026-07-27.md.
	 */
	if (input_poisoned != FALSE)
	{
		static u32 sSuppressLoadTick = ~(u32)0;
		static u32 sSuppressCount = 0U;
		const char *suppress_reason;

		suppress_reason = NULL;
		if (syNetRollbackShouldDeferInterfaceDuringResimWait() != FALSE)
		{
			suppress_reason = "resim_seal_wait";
		}
		else if ((sSYNetRollbackStickAbsorbUntilSim != 0U) &&
			 (syNetInputGetTick() <= sSYNetRollbackStickAbsorbUntilSim))
		{
			suppress_reason = "stick_absorb";
		}
		if (suppress_reason != NULL)
		{
			if (load_tick != sSuppressLoadTick)
			{
				sSuppressLoadTick = load_tick;
				sSuppressCount = 0U;
			}
			if (sSuppressCount < SYNETROLLBACK_PEER_DIVERGE_SUPPRESS_BUDGET)
			{
				sSuppressCount++;
				port_log(
				    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE suppressed (%s) load_tick=%u budget=%u/%u peer figh=0x%08X local figh=0x%08X\n",
				    suppress_reason,
				    load_tick,
				    (unsigned int)sSuppressCount,
				    (unsigned int)SYNETROLLBACK_PEER_DIVERGE_SUPPRESS_BUDGET,
				    (peer != NULL) ? peer->fighter : 0U,
				    (local != NULL) ? local->fighter : 0U);
				return;
			}
			port_log(
			    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE suppress budget exhausted (%s) load_tick=%u — fail closed\n",
			    suppress_reason,
			    load_tick);
		}
	}
#endif
	if ((peer != NULL) && (local != NULL) &&
	    (syNetRollbackPeerBaselineDriftIsCameraOnlyCosmetic(peer, local) != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE suppressed (camera cosmetic) load_tick=%u peer cam=0x%08X local cam=0x%08X\n",
		    load_tick,
		    peer->camera,
		    local->camera);
		return;
	}
	/*
	 * Map drift with matching fighters is Pupupu-class: do not suppress — fail closed after
	 * deeper exhaust. Camera may also differ (cosmetic); GameplayOnlyMap ignores cam so
	 * ComparePeerBaseline arms map resync before this path.
	 */
	if ((peer != NULL) && (local != NULL) &&
	    (syNetRollbackPeerBaselineDriftIsStaleAggregateFighOnly(load_tick, peer, local, peer_fighter_slot) != FALSE))
	{
		port_log(
		    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE suppressed (figh stale aggregate) load_tick=%u peer figh=0x%08X local figh=0x%08X\n",
		    load_tick,
		    peer->fighter,
		    local->fighter);
		return;
	}
	proof_class = (input_poisoned == FALSE) ? "replay_determinism" : "protocol";
	port_log(
	    "SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE load_tick=%u peer figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X wpn=0x%08X map=0x%08X cam=0x%08X | local figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X anim=0x%08X wpn=0x%08X map=0x%08X cam=0x%08X class=%s\n",
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
	    local->camera,
	    proof_class);
	syNetRollbackLogEpisodeProofDiverge(load_tick, "peer_snapshot_diverge", proof_class);
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
			u32 ring_slots[GMCOMMON_PLAYERS_MAX];

			syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, ring_slots);
			for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
			{
				local_slot[si] = ring_slots[si];
			}
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
				     u32 map, u32 camera, u32 effect, sb32 peer_effect_valid, const u32 *fighter_slot)
{
	SYNetRollbackHashSet peer;
	SYNetRollbackHashSet local;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: PEER_BASELINE_RECV ignored (BATTLE_SIM_HOLD) load_tick=%u figh=0x%08X\n",
		    load_tick,
		    figh);
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
	peer.effect = effect;
	sSYNetRollbackLastPeerOutcomeEffectValid = peer_effect_valid;
	sSYNetRollbackLastPeerOutcomeHash = peer;
	sSYNetRollbackLastPeerOutcomeTick = load_tick;
	sSYNetRollbackLastPeerOutcomeValid = TRUE;
	sSYNetRollbackLastPeerOutcomeFighterSlotsValid = FALSE;
	if (fighter_slot != NULL)
	{
		s32 slot_si;

		for (slot_si = 0; slot_si < GMCOMMON_PLAYERS_MAX; slot_si++)
		{
			sSYNetRollbackLastPeerOutcomeFighterSlot[slot_si] = fighter_slot[slot_si];
		}
		sSYNetRollbackLastPeerOutcomeFighterSlotsValid = TRUE;
	}
	if (syNetRollbackTryNegotiateResimLoadTickWithPeer(load_tick) != FALSE)
	{
		return;
	}
	{
		u32 sim_tick;

		sim_tick = syNetInputGetTick();
		if ((load_tick > 0U) && (load_tick < sim_tick) && (sSYNetRollbackResimPending == FALSE))
		{
			if (syNetRollbackPreemptiveBaselineCapIsStale(load_tick) != FALSE)
			{
				syNetRollbackMaybeClearStalePeerSymmetricRejectLiveCap();
				port_log(
				    "SSB64 NetRollback: BASELINE_PREEMPTIVE_LIVE_CAP_SKIP stale load_tick=%u sim=%u resolved_through=%u figh=0x%08X\n",
				    load_tick,
				    sim_tick,
				    sSYNetRollbackEpisodeResolvedThrough,
				    figh);
			}
			else
			{
				u32 preempt_mismatch;

				preempt_mismatch = load_tick + 1U;
				syNetRollbackArmPeerSymmetricRejectLiveCap(preempt_mismatch);
				port_log(
				    "SSB64 NetRollback: BASELINE_PREEMPTIVE_LIVE_CAP load_tick=%u mismatch=%u sim=%u figh=0x%08X\n",
				    load_tick,
				    preempt_mismatch,
				    sim_tick,
				    figh);
			}
		}
	}
	syNetRollbackTryOpenResimBaselineGateFromPeerDigest(load_tick, &peer, fighter_slot);
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick != sSYNetRollbackResimLoadTick))
	{
		/*
		 * Foreign baseline while a local resim episode is in flight: remember the peer's load_tick
		 * (divergent-load stall detection for the baseline gate timeout) but do not act on it.
		 */
		sSYNetRollbackPeerBaselineForeignLoadTick = load_tick;
		return;
	}
	if ((sSYNetRollbackResimPending != FALSE) && (load_tick == sSYNetRollbackResimLoadTick) &&
	    (sSYNetRollbackResimAwaitingPeerBaseline != FALSE))
	{
		return;
	}
	if (sSYNetRollbackResimPending == FALSE)
	{
		if (syNetRollbackBaselineEchoAllowed(load_tick) != FALSE)
		{
			(void)syNetRollbackTryEchoBaselineResponse(load_tick);
		}
		else
		{
			/* Peer awaits a deeper/older load_tick (e.g. below resolved_through): hash-only reply. */
			(void)syNetRollbackTryHashOnlyBaselineEcho(load_tick);
		}
	}
	if (syNetRollbackSnapshotReadyForBaselineCompare(load_tick) == FALSE)
	{
		u32 sim_tick;
		sb32 ring_hashes_ready;

		sim_tick = syNetInputGetTick();
		ring_hashes_ready = FALSE;
#if defined(PORT) && defined(SSB64_NETMENU)
		{
			u32 probe_f;
			u32 probe_w;
			u32 probe_i;
			u32 probe_r;

			/*
			 * Echo just armed slot hashes but readiness still false (e.g. other gates):
			 * compare immediately from ring — do not arm EchoRetryLoadTick (blocks
			 * BaselineCompareQuiesced / follower flush). See
			 * docs/bugs/netplay_baseline_echo_retry_ahead_resolved_2026-07-26.md.
			 */
			if (syNetRbSnapshotGetStoredSubsystemHashes(load_tick, &probe_f, &probe_w, &probe_i,
								     &probe_r) != FALSE)
			{
				ring_hashes_ready = TRUE;
				port_log(
				    "SSB64 NetRollback: BASELINE_COMPARE_RING_READY load_tick=%u sim=%u "
				    "(bypass snapshot_not_ready; hashes present)\n",
				    load_tick,
				    sim_tick);
			}
		}
#endif
		if ((ring_hashes_ready == FALSE) &&
		    (sSYNetRollbackBaselineEchoRetryAttempts < SYNETROLLBACK_BASELINE_SNAPSHOT_RETRY_MAX))
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
					      u32 *out_camera, u32 *out_effect, u32 *out_fighter_slot,
					      s32 fighter_slot_count)
{
	s32 si;

#if defined(SSB64_NETMENU)
	if (syNetRollbackLightEpisodeIsActive() != FALSE)
	{
		sSYNetRollbackPeerBaselineSendPending = FALSE;
		return FALSE;
	}
#endif
	if ((sSYNetRollbackPeerBaselineSendPending == FALSE) || (out_load_tick == NULL) || (out_figh == NULL) ||
	    (out_world == NULL) || (out_item == NULL) || (out_rng == NULL) || (out_anim == NULL) ||
	    (out_weapon == NULL) || (out_map == NULL) || (out_camera == NULL) || (out_effect == NULL))
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
	*out_effect = sSYNetRollbackPeerBaselineEffect;
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
	sb32 light;

	if ((mismatch_tick >= target_tick) || (mismatch_tick == 0))
	{
		return FALSE;
	}
	from_peer_notify = sSYNetRollbackResimFromPeerSymmetric;
	/*
	 * Light classifier: pure GGPO input correction — local initiator, no FC state
	 * recovery, no peer-symmetric authority. These resim locally with no peer
	 * coordination (GGPO-proper). State recovery / peer-sym keep the full protocol.
	 * Deferred FC pending also blocks light: the arm flag can race-clear if a prior
	 * light Finish covered past FcStateRecoveryTarget before deferred Begin runs.
	 */
	light = FALSE;
#if defined(SSB64_NETMENU)
	if ((syNetRollbackLightInputEpisodesEnabled() != FALSE) && (from_peer_notify == FALSE) &&
	    (sSYNetRollbackFcStateRecoveryActive == FALSE) &&
	    (sSYNetRollbackDeferredStateMismatchPending == FALSE) &&
	    (syNetRollbackPeerSymmetricAuthoritySlotForPlayer(correction_player) < 0))
	{
		light = TRUE;
		/*
		 * Cap light span to contiguous remote-confirmed wires so the episode does not
		 * invent hold_last past the ledger (PRED_CAP micro-cascade SoftLip fork).
		 */
		syNetRollbackAlignTargetToWireReady(mismatch_tick, correction_player, &target_tick,
						    "BEGIN_LIGHT");
	}
#endif
	sSYNetRollbackAuthoritativeEpisodeActive = FALSE;
	memset(&sSYNetRollbackExecutingEpisode, 0, sizeof(sSYNetRollbackExecutingEpisode));
	{
		sb32 used_notify_episode;

		used_notify_episode = FALSE;
		if ((from_peer_notify != FALSE) && (syNetRollbackEpisodeAuthorityEnabled() != FALSE))
		{
			s32 ep_slot;
			SYNetRollbackPendingEpisode notify_ep;

			ep_slot = correction_player;
			if (sSYNetRollbackPendingPeerSymmetricSlot >= 0)
			{
				ep_slot = sSYNetRollbackPendingPeerSymmetricSlot;
			}
			if (syNetRollbackPendingEpisodeCopyValid(ep_slot, &notify_ep) != FALSE)
			{
				sSYNetRollbackExecutingEpisode = notify_ep;
				sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
				mismatch_tick = sSYNetRollbackExecutingEpisode.mismatch_tick;
				target_tick = sSYNetRollbackExecutingEpisode.target_tick;
				load_tick = sSYNetRollbackExecutingEpisode.load_tick;
				syNetRollbackPendingEpisodeClearSlot(ep_slot);
				used_notify_episode = TRUE;
			}
		}
		if (used_notify_episode == FALSE)
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
	}
	sSYNetRollbackPredictionRecoveryUntilSim = 0U;
	sSYNetRollbackResimCorrectionPlayer = correction_player;
	sSYNetRollbackResimBaselineDeeperAttempts = 0U;
	sSYNetRollbackPreResimDeeperLoadUsed = FALSE;
	sSYNetRollbackPeerBaselineRetransmitCount = 0U;
	if (load_tick == 0U)
	{
		load_tick = mismatch_tick - 1U;
	}
	{
		u32 clamped_load = syNetRollbackClampLoadTickForPeerSend(load_tick);

		/*
		 * ClampLoadTickForPeerSend floors at shared frontier-1 so remote_hr cannot pull
		 * mismatch behind resolved_through (soak 1734260705). See
		 * docs/bugs/netplay_baseline_load_clamp_frontier_2026-07-19.md.
		 */
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
		syNetRollbackArmBattleSimHoldAfterLoadFail(load_tick);
		syNetRollbackResetCorrectionEpisode();
#endif
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	if (sSYNetRollbackFcStateRecoveryActive != FALSE)
	{
		if (syNetplayNessClampFcRecoveryLoadTick(&load_tick, &mismatch_tick) != FALSE)
		{
			sSYNetRollbackExecutingEpisode.load_tick = load_tick;
			sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
		}
		if (syNetplayPikachuClampFcRecoveryLoadTick(&load_tick, &mismatch_tick) != FALSE)
		{
			sSYNetRollbackExecutingEpisode.load_tick = load_tick;
			sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
		}
	}
#endif
#ifdef PORT
	sSYNetRollbackBeginResimInitialLoad = TRUE;
	port_log(
	    "SSB64 NetRollback: resim initial load tick=%u mismatch_tick=%u target_tick=%u fc_recovery=%d episode_valid=%d\n",
	    load_tick,
	    mismatch_tick,
	    target_tick,
	    (int)sSYNetRollbackFcStateRecoveryActive,
	    (int)sSYNetRollbackExecutingEpisode.valid);
#endif
	if (syNetRollbackTryLoadPostTickWithFidelityWalkback(&load_tick, &mismatch_tick) == FALSE)
	{
#ifdef PORT
		u32 rescue_load;

		sSYNetRollbackBeginResimInitialLoad = FALSE;
		port_log(
		    "SSB64 NetRollback: load post tick %u failed (need earlier snapshots; ring=%u scan=%u)\n",
		    load_tick,
		    (unsigned int)syNetRbSnapshotRingCapacity(),
		    (unsigned int)SYNETROLLBACK_SCAN_WINDOW);
		/*
		 * Last resort before the terminal hold: restart this episode from the
		 * newest loadable snapshot strictly below the failed tick. The drift
		 * path deliberately defers session stop here ("caller may walk back");
		 * without this the caller never did, and one poisoned frontier killed
		 * the match (soak 2026-08-21, BATTLE_SIM_HOLD sim=8382 load_tick=8381).
		 */
		rescue_load = syNetRollbackResolveDeeperLoadForFidelity(load_tick);
		if ((rescue_load != 0U) && (rescue_load < load_tick) &&
		    (syNetRollbackTryRestartResimAtDeeperLoad(rescue_load) != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: RESIM_LOAD_FAIL_RESCUE failed=%u restarted_at=%u "
			    "(deeper load instead of BATTLE_SIM_HOLD)\n",
			    load_tick,
			    rescue_load);
			return TRUE;
		}
		sSYNetRollbackLoadFailCount++;
		syNetRollbackArmBattleSimHoldAfterLoadFail(load_tick);
		syNetRollbackResetCorrectionEpisode();
#endif
		return FALSE;
	}
	sSYNetRollbackExecutingEpisode.load_tick = load_tick;
	sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
#if defined(SSB64_NETMENU)
	/* Resim rewrites snapshots from load_tick up: re-walk retroactive load-safe promotion. */
	if (sSYNetRollbackLoadSafePromotedThrough > load_tick)
	{
		sSYNetRollbackLoadSafePromotedThrough = load_tick;
	}
	/* Ness PK Thunder hold/jibaku: rebuild ephemeral tracking + canonicalize after every
	 * resim load (not only FC recovery). Force-rebuild ThrowEntryTick once here — not on
	 * every replay tick — so Canonical fall vel keeps accelerating (vanilla air drift). */
	syNetplayNessResimHardeningAfterSnapshotLoad(load_tick);
#endif
#ifdef PORT
	if (syNetRollbackTryDeeperLoadBeforeResim(&load_tick, &mismatch_tick) != FALSE)
	{
		port_log(
		    "SSB64 NetRollback: resim begin after deeper pre-load mismatch_tick=%u load_tick=%u target_tick=%u\n",
		    mismatch_tick,
		    load_tick,
		    target_tick);
	}
#if defined(SSB64_NETMENU)
	/*
	 * After deeper-load walkback, refuse ordinary GGPO episodes that landed behind the
	 * shared frontier (FC state recovery may still deepen).
	 */
	{
		u32 frontier;

		frontier = syNetRollbackGetSharedCorrectionFrontier();
		if ((frontier > 0U) && (mismatch_tick < frontier) &&
		    (sSYNetRollbackFcStateRecoveryActive == FALSE))
		{
			u32 floor_load;
			u32 pre_floor_load;
			u32 pre_floor_mismatch;

			/*
			 * Remember what the fidelity walkback settled on. The clamp below can push load
			 * back onto a tick that already failed to load, and if that happens the deeper
			 * anchor is the only one known to work.
			 */
			pre_floor_load = load_tick;
			pre_floor_mismatch = mismatch_tick;

			floor_load = frontier - 1U;
			port_log(
			    "SSB64 NetRollback: FRONTIER_BEGIN_FLOOR mismatch=%u->%u load=%u->%u frontier=%u target=%u\n",
			    mismatch_tick,
			    frontier,
			    load_tick,
			    floor_load,
			    frontier,
			    target_tick);
			mismatch_tick = frontier;
			load_tick = floor_load;
			sSYNetRollbackExecutingEpisode.load_tick = load_tick;
			sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
			if (syNetRollbackLoadPostTick(load_tick) == FALSE)
			{
				/*
				 * Floor load failed. Soak 2026-08-25 deadlocked exactly here: the fidelity
				 * walkback rescued load 1039 -> 1038, this clamp forced it back to 1039
				 * (frontier 1040), and 1039 failed again on the same
				 * LOAD_HASH_DRIFT reason=fighter_mismatch — three attempts, then a terminal
				 * BATTLE_SIM_HOLD and a ~6 s freeze. Two floors fighting: the rescue says go
				 * deeper, the frontier says never below the frontier, neither yields.
				 *
				 * Prefer the deeper anchor over a dead match. Opening behind the shared
				 * frontier is what this clamp exists to prevent, so this is a real relaxation
				 * — accepted only on the already-failed path, where the alternative is a hold
				 * that ends the session anyway. Replay re-derives every tick from the deeper
				 * load, and a resulting mismatch is detectable/healable by the FC layer,
				 * whereas the hold is not recoverable.
				 * See docs/bugs/netplay_frontier_floor_vs_rescue_deadlock_2026-08-25.md.
				 */
				if ((pre_floor_load < load_tick) &&
				    (syNetRollbackLoadPostTick(pre_floor_load) != FALSE))
				{
					port_log(
					    "SSB64 NetRollback: FRONTIER_BEGIN_FLOOR_YIELD floor_load=%u failed; "
					    "keeping rescue load=%u mismatch=%u frontier=%u (below frontier by %u)\n",
					    load_tick,
					    pre_floor_load,
					    pre_floor_mismatch,
					    frontier,
					    frontier - pre_floor_mismatch);
					load_tick = pre_floor_load;
					mismatch_tick = pre_floor_mismatch;
					sSYNetRollbackExecutingEpisode.load_tick = load_tick;
					sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
					syNetInputSetTick(load_tick);
				}
				else
				{
					sSYNetRollbackBeginResimInitialLoad = FALSE;
					port_log(
					    "SSB64 NetRollback: FRONTIER_BEGIN_FLOOR load failed load_tick=%u\n",
					    load_tick);
					sSYNetRollbackLoadFailCount++;
					syNetRollbackArmBattleSimHoldAfterLoadFail(load_tick);
					syNetRollbackResetCorrectionEpisode();
					return FALSE;
				}
			}
			else
			{
				syNetInputSetTick(load_tick);
			}
		}
	}
#endif
	syNetSyncLogRollbackWorldDetail("rollback_load", load_tick);
	syNetSyncLogFighterDetail("rollback_load", load_tick);
#if defined(SSB64_NETMENU)
	/*
	 * Arm light before baseline arm so ArmResimBaselineAfterLoad skips the synchronous
	 * wire send (pending-clear after Arm was too late).
	 */
	if (light != FALSE)
	{
		sSYNetRollbackLightEpisodeActive = TRUE;
	}
#endif
	syNetRollbackArmResimBaselineAfterLoad(load_tick);
	{
		u32 probe_attempts;
		sb32 final_load_ok;
		sb32 anchor_probe_unresolved;

		sSYNetRollbackResimAnchorProbeLastMismatch = FALSE;
		anchor_probe_unresolved = FALSE;
		probe_attempts = 0U;
		while ((syNetRollbackMaybeResimAnchorProbe(load_tick) != FALSE) &&
		       (probe_attempts < SYNETROLLBACK_LOAD_TICK_REWIND_MAX) && (load_tick > 0U))
		{
			u32 before_load;
			u32 min_load;

			anchor_probe_unresolved = TRUE;
			before_load = load_tick;
			min_load = syNetRollbackLoadTickMinBound(syNetInputGetTick());
			if (syNetRollbackAnchorProbeTryWalkbackLoad(before_load, min_load, &load_tick, &mismatch_tick) ==
			    FALSE)
			{
				port_log(
				    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_WALKBACK exhausted from=%u min=%u attempt=%u\n",
				    before_load,
				    min_load,
				    probe_attempts + 1U);
				load_tick = before_load;
				mismatch_tick = load_tick + 1U;
				sSYNetRollbackExecutingEpisode.load_tick = load_tick;
				sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
				(void)syNetRollbackLoadPostTick(load_tick);
				syNetInputSetTick(load_tick);
				break;
			}
			sSYNetRollbackExecutingEpisode.load_tick = load_tick;
			sSYNetRollbackExecutingEpisode.mismatch_tick = mismatch_tick;
			port_log(
			    "SSB64 NetRollback: RESIM_ANCHOR_PROBE_WALKBACK from=%u to=%u mismatch=%u attempt=%u\n",
			    before_load,
			    load_tick,
			    mismatch_tick,
			    probe_attempts + 1U);
			syNetRollbackArmResimBaselineAfterLoad(load_tick);
			probe_attempts++;
		}
		if ((syNetRollbackResimAnchorProbeEnabled() == FALSE) || (sSYNetRollbackResimAnchorProbeLastMismatch == FALSE))
		{
			anchor_probe_unresolved = FALSE;
		}
		final_load_ok = syNetRollbackLoadPostTick(load_tick);
		syNetInputSetTick(load_tick);
		sSYNetRollbackBeginResimInitialLoad = FALSE;
		if ((final_load_ok == FALSE) ||
		    ((syNetRollbackResimAnchorProbeEnabled() != FALSE) && (anchor_probe_unresolved != FALSE)))
		{
			return syNetRollbackFailBeginResimOnUnsafeLoad(load_tick, final_load_ok, anchor_probe_unresolved);
		}
	}
#endif
	syNetInputRollbackPrepareForResim(mismatch_tick);
#ifdef PORT
	/*
	 * Light episodes skip the FSM (no SealInputs, whose seal path normally reconciles) —
	 * reconcile the span from wire/ledger here, like the non-FSM legacy path.
	 */
	if ((syNetRollbackEpisodeFsmEnabled() == FALSE) || (light != FALSE))
	{
		syNetInputRollbackReconcileResimSpan(mismatch_tick, target_tick, correction_player);
		if ((light == FALSE) &&
		    ((from_peer_notify != FALSE) ||
		     (syNetRollbackPeerSymmetricAuthoritySlotForPlayer(correction_player) >= 0)))
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
	syNetRollbackClearStaleShallowPeerSymmetricNotify(mismatch_tick);
	port_log(
	    "SSB64 NetRollback: resim begin epoch=%u mismatch_tick=%u load_tick=%u target_tick=%u span=%u budget=%u/frame burst_max=%u owner=%s\n",
	    sSYNetRollbackEpochId,
	    mismatch_tick,
	    load_tick,
	    target_tick,
	    (unsigned int)(target_tick - mismatch_tick),
	    (unsigned int)sSYNetRollbackResimTicksPerFrame,
	    (unsigned int)sSYNetRollbackResimMaxBurstTicks,
	    (light != FALSE) ? "local_light" : (from_peer_notify != FALSE ? "peer_follower" : "local_initiator"));
	sSYNetRollbackResimBudgetedCatchUpLogged = FALSE;
	/*
	 * Light: no symmetric notify — the peer never mirrors a follower episode for a pure
	 * input correction. See docs/bugs/netplay_light_input_episodes_2026-07-27.md.
	 */
	if ((light == FALSE) && (from_peer_notify == FALSE) && (syNetRollbackSymmetricWireLockActive() != FALSE))
	{
		sb32 follower_local_auth;
		s32 notify_slot;

		notify_slot = syNetRollbackResolveRemoteHumanPlayer(correction_player);
		if (notify_slot < 0)
		{
			notify_slot = 0;
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
	                          from_peer_notify,
	                          light);
	syNetRollbackMaybeLogEpisodeExec(from_peer_notify, light);
	syNetRollbackLogEpisodeProofBegin(correction_player,
					  mismatch_tick,
					  load_tick,
					  target_tick,
					  sSYNetRollbackExecutingEpisode.valid != FALSE
					      ? sSYNetRollbackExecutingEpisode.epoch_id
					      : sSYNetRollbackEpochId);
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
	syNetSyncLogRngHashWalkTrace(tick);
	syNetSyncLogFighterSlotHashes(tick);
	syNetSyncLogPKThunderHoldDiag(tick);
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
	/*
	 * Name gameplay LCG draws when resim advances hashed rng with matched figh/map possible
	 * (soak 1790844706: initiator D993→D993 vs follower D993→6FE7). Sites always in ring.
	 */
	if (sSYNetRollbackResimPreHashes.rng != post.rng)
	{
		syNetSyncLogRngHashDriftDiag(sSYNetRollbackResimMismatchTick, post.rng,
		                             sSYNetRollbackResimPreHashes.rng, "resim_rng_burn");
	}
	{
		u32 live_sim;
		u32 next_wire;

		live_sim = syNetInputGetTick();
		next_wire = syNetPeerGetBaseRequiredWireTick(live_sim + 1U);
		port_log(
		    "SSB64 NetRollback: POST_RESIM_LIVE sim=%u target=%u hr=%u next_wire=%u wire_gap=%u commit_gen=%u\n",
		    (unsigned int)live_sim,
		    (unsigned int)sSYNetRollbackResimTargetTick,
		    (unsigned int)syNetPeerGetHighestRemoteTick(),
		    (unsigned int)next_wire,
		    (unsigned int)((syNetPeerGetHighestRemoteTick() < next_wire)
		                       ? (next_wire - syNetPeerGetHighestRemoteTick())
		                       : 0U),
		    (unsigned int)syNetPeerGetGlobalCommitGen());
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

static u32 syNetRollbackComputeResimTickLimit(void)
{
	u32 remaining;

	if (sSYNetRollbackResimPending == FALSE)
	{
		return 0U;
	}
	if ((sSYNetRollbackResimAwaitingPeerBaseline != FALSE) && (sSYNetRollbackResimBaselineGateOpen == FALSE))
	{
		return 0U;
	}
	if ((sSYNetRollbackResimNextTick >= sSYNetRollbackResimTargetTick) ||
	    (sSYNetRollbackResimTargetTick == ~(u32)0))
	{
		return 0U;
	}
	remaining = sSYNetRollbackResimTargetTick - sSYNetRollbackResimNextTick;
	if ((sSYNetRollbackResimMaxBurstTicks > 0U) && (remaining <= sSYNetRollbackResimMaxBurstTicks))
	{
		return remaining;
	}
	return sSYNetRollbackResimTicksPerFrame;
}

static void syNetRollbackAdvanceResimBudgetEx(u32 max_ticks_this_call)
{
#ifdef PORT
	u32 ran;
	u32 t;
	u32 limit;
	u32 remaining_at_start;
	sb32 burst_path;

	if (sSYNetRollbackResimPending == FALSE)
	{
		return;
	}
	if ((sSYNetRollbackResimAwaitingPeerBaseline != FALSE) && (sSYNetRollbackResimBaselineGateOpen == FALSE))
	{
		syNetRollbackPumpResimBaselineSend();
		sSYNetRollbackResimBaselineWaitFrames++;
		if (sSYNetRollbackResimBaselineWaitFrames >= syNetRollbackGetResimBaselineGateTimeoutFrames())
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
	limit = max_ticks_this_call;
	if (limit == 0U)
	{
		limit = syNetRollbackComputeResimTickLimit();
	}
	if (limit == 0U)
	{
		return;
	}
	remaining_at_start = sSYNetRollbackResimTargetTick - sSYNetRollbackResimNextTick;
	burst_path =
	    (sSYNetRollbackResimMaxBurstTicks > 0U) && (remaining_at_start <= sSYNetRollbackResimMaxBurstTicks) &&
	    (limit >= remaining_at_start);
	if ((burst_path == FALSE) && (sSYNetRollbackResimMaxBurstTicks > 0U) &&
	    (remaining_at_start > sSYNetRollbackResimMaxBurstTicks) && (sSYNetRollbackResimBudgetedCatchUpLogged == FALSE))
	{
		port_log(
		    "SSB64 NetRollback: resim budgeted catch-up span=%u limit=%u/frame burst_max=%u\n",
		    remaining_at_start,
		    (unsigned int)sSYNetRollbackResimTicksPerFrame,
		    (unsigned int)sSYNetRollbackResimMaxBurstTicks);
		sSYNetRollbackResimBudgetedCatchUpLogged = TRUE;
	}
	ran = 0;
	t = sSYNetRollbackResimNextTick;
	while ((t < sSYNetRollbackResimTargetTick) && (ran < limit))
	{
#if defined(SSB64_NETMENU)
		/* Ness PK Thunder hold/jibaku: weapon rebind + hold canonicalize each replayed tick
		 * (not only FC recovery) so cross-ISA gravity drift cannot fork jibaku launch pose. */
		syNetplayNessResimReplayHardeningAfterLoadStep();
		syNetplayResimReplayHangDiagNoteReplayTickBegin(t, ran, limit);
#endif
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * BattleSimHold makes BattleSimOnly a no-op, but the old loop still SnapshotSave(t)
		 * — that commits load world under the mismatch index (+1 map phase). Stop the burst.
		 * See docs/bugs/netplay_deepen_resim_map_save_phase_skew_peer_2026-07-28.md.
		 */
		if (syNetRollbackIsBattleSimHoldActive() != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: RESIM_ADVANCE_BLOCKED battle_sim_hold tick=%u load=%u\n",
			    (unsigned int)t,
			    (unsigned int)sSYNetRollbackResimLoadTick);
			break;
		}
#endif
		syNetInputSetTick(t);
		syNetInputPublishSynchronizedTick(t);
		scVSBattleFuncUpdateBattleSimOnly();
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * BattleSimOnly must Advance past t. If GetTick stayed at t, interface/Advance did not
		 * complete — never SavePostTick (would publish pre-battle load as T).
		 */
		if (syNetInputGetTick() <= t)
		{
			port_log(
			    "SSB64 NetRollback: RESIM_SAVE_SKIP no_tick_advance tick=%u get=%u load=%u\n",
			    (unsigned int)t,
			    (unsigned int)syNetInputGetTick(),
			    (unsigned int)sSYNetRollbackResimLoadTick);
			break;
		}
		sSYNetRollbackResimBattleProvenThrough = t;
		/*
		 * Pupupu: after a real Advance, map must leave the load slot. Equal hash means the
		 * "battle" did not consume a Whispy wait tick — refuse to publish that phase as T.
		 */
		if ((t > sSYNetRollbackResimLoadTick) && (sSYNetRollbackResimLoadTick != 0U) &&
		    (sSYNetRollbackResimLoadTick != ~(u32)0) && (gSCManagerBattleState != NULL) &&
		    (gSCManagerBattleState->gkind == nGRKindPupupu) &&
		    (syNetRbSnapshotComputeMapHashLive() ==
		     syNetRbSnapshotGetSlotHashMap(sSYNetRollbackResimLoadTick)))
		{
			port_log(
			    "SSB64 NetRollback: RESIM_SAVE_PHASE_STALE tick=%u load=%u map=0x%08X — skip save\n",
			    (unsigned int)t,
			    (unsigned int)sSYNetRollbackResimLoadTick,
			    (unsigned int)syNetRbSnapshotComputeMapHashLive());
			/* Leave a hole rather than a +1-skewed baseline slot; do not advance NextTick. */
			syNetInputSetTick(t);
			break;
		}
#endif
#if defined(SSB64_NETMENU)
		/*
		 * Resim must reproduce the forward sim's *canonicalized* per-tick state. Accepted forward ticks
		 * grid-snap fighters/items/camera in syNetRollbackAfterBattleUpdate (quantize active) before the
		 * post-tick snapshot save, so committed history is on-grid -- the fix that keeps the movable
		 * Castle bumper (the one item fighters can knock; itgbumper.c) deterministic. During resim,
		 * AfterBattleUpdate early-returns (resim pending) and never canonicalizes, yet this loop still
		 * saves snapshots and collects hashes below. So the bumper and any fighter it contacts integrate
		 * un-snapped and drift cross-ISA, diverging resim output from the canonicalized forward baseline:
		 * soak2 @357459419 committed figh,item,rng at tick 600 after a FORCE_MISMATCH resim (519->522) --
		 * bumper contact pulled Fox's Firefox-setup position off, so the host drew hit RNG @533/539 that
		 * the guest never did, and item stayed diverged through 600 (again at 2040). Snap here, at the
		 * same point relative to the save/hash as the accepted path, so both peers' replay is bit-
		 * identical to their canonicalized forward sim.
		 * See docs/bugs/netplay_castle_bumper_resim_uncanonicalized_drift_2026-07-02.md.
		 */
		if (syNetplaySimQuantizeActive() != FALSE)
		{
			syNetplayCanonicalizeActiveFightersForNetplay();
			syNetRbSnapshotCanonicalizeActiveItemsForNetplay();
			syNetRbSnapshotAfterSimLinkBombForwardRepair();
		}
		if (syNetRollbackResimSnapshotRefreshEnabled() != FALSE)
		{
			(void)syNetRbSnapshotSave(t);
		}
		if ((sSYNetRollbackResimLoadTick != ~(u32)0) && (sSYNetRollbackResimLoadTick != 0U))
		{
			syNetRbSnapshotRefreshIntroPresentationAfterForwardResimTick(t);
		}
#else
		if (syNetRollbackResimSnapshotRefreshEnabled() != FALSE)
		{
			(void)syNetRbSnapshotSave(t);
		}
#endif
		/* Light episodes have no seal/verify — skip per-tick replay-log hashing. */
		if ((syNetRollbackEpisodeFsmEnabled() != FALSE) && (syNetRollbackLightEpisodeIsActive() == FALSE))
		{
			SYNetRollbackHashSet tick_h;
			u32 tick_inp;

			tick_h = syNetRollbackCollectHashes();
			tick_inp = syNetRollbackEpisodeReplayLogTickInputDigest(t);
			syNetRollbackEpisodeReplayLogAppend(t, tick_inp, tick_h.fighter, tick_h.item, tick_h.rng);
		}
#if defined(SSB64_NETMENU)
		/* Per-replayed-tick AObj probe: AfterBattleUpdate's trace never fires during resim, so the
		 * forward-vs-resim comparison was blind to the replay window. phase=resim here. */
		syNetplayTraceActiveFighterAObj(t);
		syNetplayFoxFirefoxStateForkTraceTick(t, "resim_post_sim");
#endif
		syNetRollbackLogResimTickTrace(t);
#if defined(SSB64_NETMENU)
		syNetplayResimReplayHangDiagNoteReplayTickEnd(t);
#endif
		t++;
		ran++;
	}
	sSYNetRollbackResimNextTick = t;
	if (t >= sSYNetRollbackResimTargetTick)
	{
		u32 completed_mismatch;

		if ((burst_path != FALSE) && (ran > 0U))
		{
			port_log(
			    "SSB64 NetRollback: resim burst complete span=%u ticks=%u\n",
			    remaining_at_start,
			    ran);
		}
		completed_mismatch = sSYNetRollbackResimMismatchTick;
		if (completed_mismatch == 0U)
		{
			completed_mismatch = sSYNetRollbackEpisode.mismatch_tick;
		}
		/*
		 * Pin exclusive frontier before Live. Resim saves [mismatch, target) then GetTick must
		 * equal target. If the last BattleSimOnly AdvanceAuthoritative was wire/hr-capped
		 * (follower), GetTick stays at target-1; live then re-sims that tick while
		 * AfterBattleUpdate skips save (completed < resolved_through) and the next save is
		 * permanently +1 ahead of the peer (soak 1133978048 Android POST sim=420 target=421).
		 * See docs/bugs/netplay_post_resim_exclusive_tick_wire_cap_skew_2026-07-15.md.
		 */
		if ((sSYNetRollbackResimTargetTick != 0U) && (sSYNetRollbackResimTargetTick != ~(u32)0U) &&
		    (syNetInputGetTick() != sSYNetRollbackResimTargetTick))
		{
			port_log(
			    "SSB64 NetRollback: POST_RESIM_EXCLUSIVE_TICK_PIN from=%u to=%u (Advance was capped mid-resim)\n",
			    (unsigned int)syNetInputGetTick(),
			    (unsigned int)sSYNetRollbackResimTargetTick);
			syNetInputSetTick(sSYNetRollbackResimTargetTick);
		}
		if (syNetRollbackFinishForwardResim() == FALSE)
		{
			/* Mid-pass defer: emit complete logs when OnLiveFuncUpdateBegin flushes Finish. */
			sSYNetRollbackResimCompleteLogPending = TRUE;
			sSYNetRollbackResimCompleteLogMismatch = completed_mismatch;
		}
		else
		{
			syNetRollbackEmitResimCompleteAfterFinish(completed_mismatch);
		}
	}
#else
	(void)max_ticks_this_call;
#endif
}

static void syNetRollbackAdvanceResimBudget(void)
{
	syNetRollbackAdvanceResimBudgetEx(0U);
}

#ifdef PORT
/*
 * Drain queued ROLLBACK_SYNC into resim begin before tick-commit gate. Preemptive
 * live-cap can block battle sim while pending notify is already valid; follower must
 * still load snapshot and join seal rendezvous (episode boundary hang, soak STRICT_INPUT ep10).
 * See docs/bugs/netplay_episode_boundary_seal_hang_2026-07-12.md.
 */
static sb32 syNetRollbackTryBeginResimFromPendingPeerSymmetric(u32 frontier, u32 scan_mismatch)
{
	u32 mismatch;
	u32 resim_target_tick;
	u32 peer_symmetric_target_tick;
	s32 mismatch_player;
	sb32 mismatch_from_peer_symmetric;

	if ((sSYNetRollbackSymmetricEnabled == FALSE) || (sSYNetRollbackSymmetricDiagOnly != FALSE))
	{
		return FALSE;
	}
	if (sSYNetRollbackPendingPeerSymmetricTick == ~(u32)0)
	{
		return FALSE;
	}

	mismatch = scan_mismatch;
	mismatch_player = -1;
	mismatch_from_peer_symmetric = FALSE;
	peer_symmetric_target_tick = ~(u32)0;
	resim_target_tick = frontier;

	if ((sSYNetRollbackSymmetricEnabled != FALSE) && (sSYNetRollbackSymmetricDiagOnly == FALSE))
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
			SYNetRollbackPendingEpisode sym_ep;

			sym_slot = sSYNetRollbackPendingPeerSymmetricSlot;
			if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
			    (syNetRollbackPendingEpisodeCopyValid(sym_slot, &sym_ep) != FALSE))
			{
				peer_tick = sym_ep.mismatch_tick;
				peer_target_tick = sym_ep.target_tick;
			}
			else if ((syNetRollbackEpisodeAuthorityEnabled() == FALSE) &&
				 (syNetRollbackPeerSymmetricUseFollowerLocalAuthority(
					 sym_slot, sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth) != FALSE))
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
				sSYNetRollbackPendingPeerSymmetricFlags = 0U;
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
					return TRUE;
				}
				return FALSE;
			}
			mismatch = peer_tick;
			resim_target_tick = peer_target_tick;
			peer_symmetric_target_tick = peer_target_tick;
			mismatch_player = sym_slot;
			mismatch_from_peer_symmetric = TRUE;
			sSYNetRollbackResimFromPeerSymmetric = TRUE;
		}
	}

	if (mismatch_from_peer_symmetric == FALSE)
	{
		return FALSE;
	}
	if (peer_symmetric_target_tick != ~(u32)0)
	{
		resim_target_tick = peer_symmetric_target_tick;
	}
	if (syNetRollbackCorrectionAllowedAtTick(mismatch) == FALSE)
	{
		syNetRollbackLogTryBeginFail("peer_sym_not_allowed", mismatch, resim_target_tick, mismatch_player);
		return FALSE;
	}
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
	syNetDesyncClassifierOnRollbackInputMismatch(mismatch);
	{
		SYNetRollbackPendingEpisode sym_resim_ep;
		s32 sym_resim_slot;

		sym_resim_slot = sSYNetRollbackPendingPeerSymmetricSlot;
		if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) && (sym_resim_slot >= 0) &&
		    (syNetRollbackPendingEpisodeCopyValid(sym_resim_slot, &sym_resim_ep) != FALSE))
		{
			sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
			resim_target_tick =
			    syNetRollbackClampResimTargetTickAuthoritative(mismatch, sym_resim_ep.target_tick);
		}
		else
		{
			resim_target_tick = syNetRollbackClampResimTargetTickEx(mismatch, resim_target_tick, frontier, TRUE);
		}
	}
	{
		SYNetRollbackCorrectionCommitSnap commit_snap;
		s32 correction_player;

		correction_player = mismatch_player;
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
		if (syNetRollbackTryCommitCorrectionBegin(mismatch, mismatch - 1U, resim_target_tick, &commit_snap) == FALSE)
		{
			return FALSE;
		}
		if (syNetRollbackBeginResim(mismatch, resim_target_tick, correction_player) == FALSE)
		{
			syNetRollbackLogTryBeginFail("peer_sym_begin_resim", mismatch, resim_target_tick,
						     correction_player);
			syNetRollbackAbortCorrectionCommit(&commit_snap);
			return FALSE;
		}
	}
	if ((sSYNetRollbackPendingPeerSymmetricSlot >= 0) &&
	    (sSYNetRollbackPendingPeerSymmetricSlot < MAXCONTROLLERS))
	{
		sSYNetRollbackPeerSymmetricAppliedTick[sSYNetRollbackPendingPeerSymmetricSlot] = mismatch;
	}
	sSYNetRollbackPendingPeerSymmetricTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricTargetTick = ~(u32)0;
	sSYNetRollbackPendingPeerSymmetricSlot = -1;
	sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth = FALSE;
	sSYNetRollbackPendingPeerSymmetricFlags = 0U;
	sSYNetRollbackRollbackCount++;
	sSYNetRollbackResimOrdinal = sSYNetRollbackRollbackCount;
	sSYNetRollbackLastCommittedMismatchTick = mismatch;
	syNetRollbackAdvanceResimBudget();
	return TRUE;
}
#endif

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
	if ((syNetPeerIsVSSessionActive() == FALSE) && (syNetReplayIsDiagnosticPlaybackActive() == FALSE))
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
	/* Solo diagnostic playback: only forced resim / budget advance — no peer mismatch scan. */
	if ((syNetPeerIsVSSessionActive() == FALSE) && (syNetReplayIsDiagnosticPlaybackActive() != FALSE))
	{
		return;
	}
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
	{
		u32 frontier_pre;

		frontier_pre = syNetInputGetTick();
		if (frontier_pre < ~(u32)0)
		{
			frontier_pre++;
		}
		if (syNetRollbackTryBeginResimFromPendingPeerSymmetric(frontier_pre, ~(u32)0) != FALSE)
		{
			return;
		}
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
			SYNetRollbackPendingEpisode sym_ep;

			sym_slot = sSYNetRollbackPendingPeerSymmetricSlot;
			if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
			    (syNetRollbackPendingEpisodeCopyValid(sym_slot, &sym_ep) != FALSE))
			{
				peer_tick = sym_ep.mismatch_tick;
				peer_target_tick = sym_ep.target_tick;
			}
			else if ((syNetRollbackEpisodeAuthorityEnabled() == FALSE) &&
				 (syNetRollbackPeerSymmetricUseFollowerLocalAuthority(
					 sym_slot, sSYNetRollbackPendingPeerSymmetricFollowerLocalAuth) != FALSE))
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
				sSYNetRollbackPendingPeerSymmetricFlags = 0U;
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
#ifdef PORT
		if (mismatch_from_peer_symmetric != FALSE)
		{
			syNetRollbackLogTryBeginFail("peer_sym_not_allowed", mismatch, resim_target_tick,
						     mismatch_player);
		}
#endif
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
		SYNetRollbackPendingEpisode sym_resim_ep;
		s32 sym_resim_slot;

		sym_resim_slot = sSYNetRollbackPendingPeerSymmetricSlot;
		if ((syNetRollbackEpisodeAuthorityEnabled() != FALSE) &&
		    (sym_resim_slot >= 0) &&
		    (syNetRollbackPendingEpisodeCopyValid(sym_resim_slot, &sym_resim_ep) != FALSE))
		{
			sSYNetRollbackAuthoritativeEpisodeActive = TRUE;
			resim_target_tick =
			    syNetRollbackClampResimTargetTickAuthoritative(mismatch, sym_resim_ep.target_tick);
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
			syNetRollbackLogTryBeginFail(
			    (mismatch_from_peer_symmetric != FALSE) ? "peer_sym_begin_resim" : "scan_begin_resim",
			    mismatch,
			    resim_target_tick,
			    correction_player);
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
		sSYNetRollbackPendingPeerSymmetricFlags = 0U;
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

#if defined(PORT) && defined(SSB64_NETMENU)
void syNetRollbackResimReplayHangDiagExportEpisode(u32 *out_pending, u32 *out_load, u32 *out_mismatch, u32 *out_target,
                                                   u32 *out_next)
{
	if (out_pending != NULL)
	{
		*out_pending = (sSYNetRollbackResimPending != FALSE) ? 1U : 0U;
	}
	if (out_load != NULL)
	{
		*out_load = sSYNetRollbackResimLoadTick;
	}
	if (out_mismatch != NULL)
	{
		*out_mismatch = sSYNetRollbackResimMismatchTick;
	}
	if (out_target != NULL)
	{
		*out_target = sSYNetRollbackResimTargetTick;
	}
	if (out_next != NULL)
	{
		*out_next = sSYNetRollbackResimNextTick;
	}
}
#endif
