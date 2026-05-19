#include <sys/netpeer.h>

#include <ft/fighter.h>
#include <gm/gmdef.h>
#include <gr/ground.h>
#include <mp/map.h>
#include <sc/scmanager.h>
#include <sys/netinput.h>
#include <sys/netreplay.h>
#include <sys/netsession_params.h>
#include <sys/netrollback.h>
#ifdef PORT
#include <sys/netdesyncclassifier.h>
#include <sys/netpeer_frame_commit.h>
#endif
#include <sys/netsync.h>
#include <sys/objman.h>
#include <sys/objman_gcport.h>
#if defined(SSB64_NETMENU)
#include <sys/netfighterphase.h>
#endif
#include <sys/utils.h>
#include <sys/taskman.h>
#include <sys/netphase.h>
#include <sys/nettickgridlock.h>

#ifdef SSB64_NETMENU
#include "bootstrap/mm_server_barrier.h"
extern sb32 mnVSNetLevelPrefsMapsCheckLocked(s32 gkind);
extern s32 mnVSNetLevelPrefsMapsGetGroundKind(s32 slot);
extern void mnVSNetAutomatchForceRequeueAfterBarrierTimeout(void);
#endif

#ifdef PORT
#include "gameloop.h"
#include "port_watchdog.h"
#include <stdio.h>
#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
extern void port_coroutine_yield(void);

static s32 syNetPeerGetPrimaryLocalHardwareDeviceIndex(void)
{
	const char *env;
	s32 hw;

	env = getenv("SSB64_NETPLAY_LOCAL_HARDWARE");
	if ((env != NULL) && (env[0] != '\0'))
	{
		hw = atoi(env);
		if ((hw >= 0) && (hw < MAXCONTROLLERS))
		{
			return hw;
		}
	}
	return 0;
}

static void syNetPeerResetSkewPacingSessionStats(void);
static void syNetPeerRefreshSkewPacingLeadMaxFromEnv(void);
static void syNetPeerRefreshSkewBehindMaxFromEnv(void);
static void syNetPeerRefreshSkewGapEwmaPacingFromEnv(void);
static void syNetPeerResetDesyncTraceSession(void);
static void syNetPeerFrameCommitReset(void);
#if defined(PORT)
static void syNetPeerResetMatchBufferMinSlackEnv(void);
static void syNetPeerResetMatchDelayStarvationSession(void);
sb32 syNetPeerShouldHoldSimTickForSkewPacing(u32 tick, s32 *out_skew);
#endif
static u32 sSYNetPeerDesyncTracePrevFigh;
static sb32 sSYNetPeerDesyncTracePrevValid;
static int sSYNetPeerDesyncTraceLevelCache = -999;
static int sSYNetPeerMpTicDiagAssertEnvCache = -999;
static int sSYNetPeerStateDetailDiagEnvCache = -999;

extern sb32 sSYNetPeerIsActive;

/*
 * Effective tick/frame diag level: `SSB64_NETPLAY_TICK_DIAG` env (cached) with a floor of 1 while a VS UDP
 * session is active so match logs always include tick_diag snapshots, NetSync extras (including extended input
 * diagnostics when `SSB64_NETPLAY_NETSYNC_INPUT_DIAG` is not set to 0), and INPUT endpoint
 * routing logs without requiring env setup. `clock_sync_sample` and `tick_diag tag=barrier_release` are
 * always emitted on Linux UDP (not gated by this level).
 */
static int sSYNetPeerTickDiagEnvCache = -999;

static int syNetPeerTickDiagLevel(void)
{
	int effective;
	char *e;

	if (sSYNetPeerTickDiagEnvCache == -999)
	{
		e = getenv("SSB64_NETPLAY_TICK_DIAG");
		if ((e != NULL) && (e[0] != '\0'))
		{
			sSYNetPeerTickDiagEnvCache = atoi(e);
			if (sSYNetPeerTickDiagEnvCache < 0)
			{
				sSYNetPeerTickDiagEnvCache = 0;
			}
		}
		else
		{
			sSYNetPeerTickDiagEnvCache = 0;
		}
	}
	effective = sSYNetPeerTickDiagEnvCache;
	if (sSYNetPeerIsActive != FALSE)
	{
		if (effective < 1)
		{
			effective = 1;
		}
	}
	return effective;
}

s32 syNetPeerGetTickDiagLevel(void)
{
	return (s32)syNetPeerTickDiagLevel();
}

static int syNetPeerGetStateDetailDiagLevel(void)
{
	char *e;

	if (sSYNetPeerStateDetailDiagEnvCache != -999)
	{
		return sSYNetPeerStateDetailDiagEnvCache;
	}
	e = getenv("SSB64_NETPLAY_STATE_DETAIL_DIAG");
	sSYNetPeerStateDetailDiagEnvCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (sSYNetPeerStateDetailDiagEnvCache < 0)
	{
		sSYNetPeerStateDetailDiagEnvCache = 0;
	}
	if (sSYNetPeerStateDetailDiagEnvCache > 2)
	{
		sSYNetPeerStateDetailDiagEnvCache = 2;
	}
	return sSYNetPeerStateDetailDiagEnvCache;
}
#endif

#if defined(PORT)
#include <stdint.h>
#include <string.h>
#include <sys/netpeer_socket_platform.h>
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <time.h>
#else
#include <stdio.h>
#endif
#endif

#define SYNETPEER_MAGIC 0x53534E50 // SSNP
/* Legacy INPUT wire ids (no peer_connect_status block). Kept for recv compatibility. */
#define SYNETPEER_WIRE_LEGACY_INPUT_SINGLE 2
#define SYNETPEER_WIRE_LEGACY_INPUT_DUAL 3
/*
 * Current protocol version for control plane + INPUT:
 *   4 = single-local INPUT bundle + GGPO-style peer_connect_status[4]
 *   5 = dual-local INPUT + peer_connect_status
 */
#define SYNETPEER_VERSION 4
#define SYNETPEER_VERSION_DUAL_LOCAL 5
#define SYNETPEER_MAX_PACKET_FRAMES 16
#define SYNETPEER_FRAME_BYTES 8
/* Per slot: last_confirmed u32, disconnect u8, symmetric rollback mismatch tick u24, resim target u24. */
#define SYNETPEER_CONNECT_BLOCK_BYTES ((MAXCONTROLLERS)*11)
/* Base INPUT header before frame payloads (magic…remote_player); legacy wire 2/3 stopped here. */
#define SYNETPEER_INPUT_HEADER_BASE_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 1 + 1 + 1 + 1)
/* Wire 4/5: append peer_connect_status (last_confirmed tick + disconnect + pad) per slot. */
#define SYNETPEER_INPUT_HEADER_BYTES (SYNETPEER_INPUT_HEADER_BASE_BYTES + SYNETPEER_CONNECT_BLOCK_BYTES)
#define SYNETPEER_PACKET_BYTES_LEGACY_V2                                                                               \
	((SYNETPEER_INPUT_HEADER_BASE_BYTES) + ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 4)
#define SYNETPEER_PACKET_BYTES_LEGACY_V3                                                                               \
	((SYNETPEER_INPUT_HEADER_BASE_BYTES) + ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 1 + 1 +        \
	 ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 4)
#define SYNETPEER_PACKET_BYTES_V4                                                                                      \
	((SYNETPEER_INPUT_HEADER_BYTES) + ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 4)
#define SYNETPEER_PACKET_BYTES_V5                                                                                      \
	((SYNETPEER_INPUT_HEADER_BYTES) + ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 1 + 1 +             \
	 ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 4)
#define SYNETPEER_PACKET_RECV_MAX                                                                                      \
	(((SYNETPEER_PACKET_BYTES_LEGACY_V3) > (SYNETPEER_PACKET_BYTES_V5)) ? (SYNETPEER_PACKET_BYTES_LEGACY_V3)        \
	                                                                      : (SYNETPEER_PACKET_BYTES_V5))
#define SYNETPEER_MAX_REMOTE_PLAYLIST 4
#define SYNETPEER_SECONDARY_SLOT_ABSENT 255
#define SYNETPEER_VALIDATION_INPUT_WINDOW 120
#define SYNETPEER_METADATA_BYTES (11 * 4 + 8 + (MAXCONTROLLERS * 7) + 2)
#define SYNETPEER_BOOTSTRAP_PACKET_BYTES (4 + 2 + 2 + 4 + SYNETPEER_METADATA_BYTES + 4)
#define SYNETPEER_CONTROL_PACKET_BYTES (4 + 2 + 2 + 4 + 4)
#define SYNETPEER_TIME_PING_BYTES (4 + 2 + 2 + 4 + 4 + 8 + 4)
#define SYNETPEER_TIME_PONG_BYTES (4 + 2 + 2 + 4 + 4 + 8 + 8 + 8 + 4)
/* Host wall-clock start + median offset; extended adds authoritative VI grid (Hz + align flag). */
#define SYNETPEER_BATTLE_START_TIME_BYTES_LEGACY (4 + 2 + 2 + 4 + 8 + 8 + 4)
#define SYNETPEER_BATTLE_START_TIME_BYTES ((SYNETPEER_BATTLE_START_TIME_BYTES_LEGACY) + 4 + 4)
#define SYNETPEER_CLOCK_SYNC_SAMPLES_DEFAULT 12U
#define SYNETPEER_CLOCK_SYNC_SAMPLES_MAX 64U
#define SYNETPEER_MIN_START_LEAD_MS 200U
#define SYNETPEER_START_MARGIN_MS 40U
#define SYNETPEER_START_JITTER_SLACK_MS 30U
#define SYNETPEER_CLOCK_OUTLIER_RTT_K_NUM 2U
#define SYNETPEER_CLOCK_OUTLIER_RTT_C_MS 30U
#define SYNETPEER_CLOCK_OUTLIER_OFF_W_MS 120LL
#define SYNETPEER_CLOCK_FILTER_MIN_KEEP 4U
#define SYNETPEER_CLOCK_FALLBACK_EXTRA_LEAD_MS 80U
#define SYNETPEER_SYNC_OFFSET_SPREAD_THRESH_MS 80LL
#define SYNETPEER_SYNC_UNCERTAINTY_SLACK_MS 40U
#define SYNETPEER_DEADLINE_PAST_SLACK_MS 24U
#define SYNETPEER_BARRIER_CONSERVATIVE_EXTRA_MS 120U
#define SYNETPEER_DEFAULT_INPUT_DELAY 2
/*
 * Online default: committed wire delay `D` is at least 1 unless the match explicitly pins `D==0` via
 * `SSB64_NETPLAY_MATCH_INPUT_DELAY=0`, or lab override `SSB64_NETPLAY_ALLOW_INPUT_DELAY_ZERO=1`.
 */
#define SYNETPEER_ONLINE_COMMITTED_DELAY_MIN_DEFAULT 1U
#define SYNETPEER_DEFAULT_SESSION_ID 1
#define SYNETPEER_DEFAULT_BOOTSTRAP_SEED 12345
#define SYNETPEER_LOG_INTERVAL 120
/*
 * Sim skew pacing (lead cap): suppress full `scVSBattleFuncUpdate` while local tick leads `HighestRemoteTick` by more
 * than this many frames — default when `SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS` is unset (see docs/netplay_pacing.md).
 */
#define SYNETPEER_SKEW_PACING_LEAD_MAX_TICKS_DEFAULT 4U
/*
 * Optional EWMA of (sim_tick - remote_sim_frontier) after first inbound wire (`hr > 0`): tightens skew pacing by
 * lowering the effective lead cap toward `SSB64_NETPLAY_SKEW_GAP_EWMA_MIN_LEAD_TICKS` (see `syNetPeerShouldHoldSimTickForSkewPacing`).
 */
#define SYNETPEER_SKEW_GAP_EWMA_CAP_TICKS_DEFAULT 4U
#define SYNETPEER_SKEW_GAP_EWMA_MIN_EFFECTIVE_LEAD_DEFAULT 1U
#define SYNETPEER_SKEW_GAP_EWMA_STORE_MAX 48
/* Host adaptive delay: run policy + broadcast on sim ticks (decoupled from stats logging interval). */
#define SYNETPEER_ADAPT_DELAY_SIM_INTERVAL 120U
#if defined(PORT)
/*
 * Host auto runway (UDP): local sim persistently leads remote ingress frontier -> queue +1 INPUT_DELAY_SYNC.
 * Still clamped by `syNetPeerClampInputDelayToContract` (match-linked floor/ceiling when active).
 */
#define SYNETPEER_AUTO_RUNWAY_DEFICIT_MIN_TICKS 3U
#define SYNETPEER_AUTO_RUNWAY_DEFICIT_EMERGENCY_TICKS 6U
#define SYNETPEER_AUTO_RUNWAY_SUSTAIN_SIM_TICKS 8U
/* Max sim - remote_sim_frontier while predicting without ring at wire_base (see EvaluateSharedCommitStep). */
#define SYNETPEER_RUNWAY_PREDICT_MAX_SIM_DEFICIT_DEFAULT 2U
/* Max expansion of prediction window when hr lags required_wire (replaces unbounded 2× phase_lock bump). */
#define SYNETPEER_RUNWAY_PREDICT_INGRESS_SLACK_DEFAULT 2U
#endif
/* Bootstrap pacing defaults sized for ~200–400 ms RTT (override via env). */
#define SYNETPEER_BOOTSTRAP_RETRY_COUNT_DEFAULT 360U
#define SYNETPEER_BOOTSTRAP_RETRY_USECS_DEFAULT 16666U
#define SYNETPEER_BOOTSTRAP_RETRY_COUNT_MIN 60U
#define SYNETPEER_BOOTSTRAP_RETRY_COUNT_MAX 900U
#define SYNETPEER_BOOTSTRAP_RETRY_USECS_MIN 4000U
#define SYNETPEER_BOOTSTRAP_RETRY_USECS_MAX 50000U
#define SYNETPEER_STAGE_SCENE_GO_HOLD_MS_DEFAULT 2500
#define SYNETPEER_BOOTSTRAP_START_BURST_DEFAULT 60U
#define SYNETPEER_STAGE_SCENE_GO_REPEAT_DEFAULT 60U
#define SYNETPEER_BOOTSTRAP_PAUSE_BETWEEN_MS_DEFAULT 500U
#define SYNETPEER_BARRIER_LOG_INTERVAL 30
#define SYNETPEER_BATTLE_START_REPEAT_FRAMES 30

#define SYNETPEER_PACKET_INPUT 0
#define SYNETPEER_PACKET_MATCH_CONFIG 1
#define SYNETPEER_PACKET_READY 2
#define SYNETPEER_PACKET_START 3
#define SYNETPEER_PACKET_BATTLE_READY 4
#define SYNETPEER_PACKET_BATTLE_START 5
#define SYNETPEER_PACKET_TIME_PING 6
#define SYNETPEER_PACKET_TIME_PONG 7
#define SYNETPEER_PACKET_BATTLE_START_TIME 8
#define SYNETPEER_PACKET_AUTOMATCH_OFFER 9
/* Packet type ids 10 and 11 are reserved (retired warmup handshake). */
#define SYNETPEER_PACKET_INPUT_BIND 12
#define SYNETPEER_PACKET_BATTLE_EXEC_SYNC 13
#define SYNETPEER_PACKET_INPUT_DELAY_SYNC 14
#define SYNETPEER_PACKET_UDP_SYNC_REQ 15
#define SYNETPEER_PACKET_UDP_SYNC_REP 16
#define SYNETPEER_PACKET_FRAME_COMMIT 17
#define SYNETPEER_PACKET_STAGE_SCENE_READY 18
#define SYNETPEER_PACKET_STAGE_SCENE_GO 19
#define SYNETPEER_PACKET_ROLLBACK_BASELINE 20
#define SYNETPEER_PACKET_VS_SESSION_END  21
#define SYNETPEER_PACKET_SESSION_PARAMS 22
#define SYNETPEER_PACKET_SESSION_PARAMS_ACK 23
#define SYNETPEER_PACKET_ROLLBACK_SYNC 24
#define SYNETPEER_PACKET_RESIM_POST 25
/* header(12) + rtt_ms(4) + nine u8 knobs(9) + checksum(4) = 29 (wire v2) */
#define SYNETPEER_SESSION_PARAMS_WIRE_BYTES (4 + 2 + 2 + 4 + 4 + 9 + 4)
#define SYNETPEER_UDP_SYNC_PACKET_BYTES (4 + 2 + 2 + 4 + 2 + 2 + 4)
#define SYNETPEER_UDP_LINK_SYNC_ROUNDS 5
/* Retransmit the same REQ token at this interval while awaiting REP (must exceed path RTT). */
#define SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_DEFAULT 300U
#define SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MIN 80U
#define SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MAX 2000U
/* INPUT layout predicates (peer_connect_status introduced at wire 4/5). */
#define SYNETPEER_WIRE_HAS_CONNECT_STATUS(W) (((u16)(W) == (u16)SYNETPEER_VERSION) || ((u16)(W) == (u16)SYNETPEER_VERSION_DUAL_LOCAL))
#define SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(W)                                                                           \
	(((u16)(W) == (u16)SYNETPEER_WIRE_LEGACY_INPUT_DUAL) || ((u16)(W) == (u16)SYNETPEER_VERSION_DUAL_LOCAL))

#define SYNETPEER_AUTOMATCH_OFFER_BYTES (4 + 2 + 2 + 4 + 2 + 1 + 1 + 4 + 4)
#define SYNETPEER_INPUT_BIND_BYTES (4 + 2 + 2 + 4 + 1 + 1 + 1 + 1 + 4)
#define SYNETPEER_BATTLE_EXEC_SYNC_BYTES_LEGACY (4 + 2 + 2 + 4 + 4 + 4 + 4)
#define SYNETPEER_BATTLE_EXEC_SYNC_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 4 + 4)
#define SYNETPEER_INPUT_DELAY_SYNC_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 4)
/* Default local sim ticks before applying queued `INPUT_DELAY_SYNC` / host ramp commits (`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`). */
#define SYNETPEER_DELAY_SYNC_COMMIT_LEAD_TICKS_DEFAULT 2U
/* Frame commit token (NetSync validation cadence). */
#define SYNETPEER_FRAME_COMMIT_BYTES (56)
#define SYNETPEER_ROLLBACK_BASELINE_BYTES (68)
#define SYNETPEER_ROLLBACK_BASELINE_BYTES_LEGACY (56)
#define SYNETPEER_ROLLBACK_SYNC_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 1 + 1 + 2 + 4)
#define SYNETPEER_RESIM_POST_BYTES (12 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4)
#define SYNETPEER_BARRIER_SKEW_RETRY_MAX 3U
#define SYNETPEER_STRICT_DISCONNECT_LATCH_K 4U

typedef struct SYNetPeerPacketFrame
{
	u32 tick;
	u16 buttons;
	s8 stick_x;
	s8 stick_y;

} SYNetPeerPacketFrame;

static void syNetPeerConfigureRemoteReceiveSlots(void);
static void syNetPeerConfigurePeerSenderSlots(void);
static void syNetPeerConfigureExtraLocalSender(void);
static sb32 syNetPeerValidateRemoteReceiveList(void);
static sb32 syNetPeerValidatePeerSenderList(void);
static sb32 syNetPeerGatherHistoryBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *out_frame_count);
static void syNetPeerStagePacketBundle(s32 target_player, const SYNetPeerPacketFrame *frames, s32 frame_count,
                                       u32 current_tick, u32 packet_seq);

sb32 sSYNetPeerIsEnabled;
sb32 sSYNetPeerIsConfigured;
sb32 sSYNetPeerIsActive;
#if defined(PORT)
static sb32 sSYNetPeerBootstrapRunInProgress;
#endif
s32 sSYNetPeerLocalPlayer;
s32 sSYNetPeerRemotePlayer;
u32 sSYNetPeerInputDelay;
static u8 sSYNetPeerRemoteReceiveSlots[SYNETPEER_MAX_REMOTE_PLAYLIST] = { 1 };
static s32 sSYNetPeerRemoteReceiveCount = 1;
static u8 sSYNetPeerPeerSenderSlots[SYNETPEER_MAX_REMOTE_PLAYLIST] = { 1 };
static s32 sSYNetPeerPeerSenderCount = 1;
static s32 sSYNetPeerExtraLocalSenderSlot = -1;
u32 sSYNetPeerSessionID;
u32 sSYNetPeerHighestRemoteTick;
u32 sSYNetPeerPacketsSent;
u32 sSYNetPeerPacketsReceived;
u32 sSYNetPeerPacketsDropped;
u32 sSYNetPeerFramesStaged;
u32 sSYNetPeerLateFrames;
u32 sSYNetPeerInputChecksum;
u32 sSYNetPeerLastLogTick;
u32 sSYNetPeerSendSeq;
u32 sSYNetPeerRecvSeqHighWater;
sb32 sSYNetPeerRecvSeqInitialized;
u32 sSYNetPeerSeqGaps;
u32 sSYNetPeerSeqDuplicates;
u32 sSYNetPeerSeqOutOfOrder;
u32 sSYNetPeerLastPeerAckTick;
u32 sSYNetPeerLastPacketOldestTick;
u32 sSYNetPeerLastPacketNewestTick;
sb32 sSYNetPeerLastPacketTicksValid;
static s32 sSYNetPeerMergedConnectLastTick[MAXCONTROLLERS];
static u8 sSYNetPeerMergedConnectDisc[MAXCONTROLLERS];
static sb32 sSYNetPeerVsSessionEndReceived;
static u32 sSYNetPeerPeerDisconnectConsec;
sb32 sSYNetPeerBootstrapIsEnabled;
sb32 sSYNetPeerBootstrapIsHost;
sb32 sSYNetPeerBootstrapMetadataApplied;
static sb32 sSYNetPeerBootstrapMetadataStaged;
sb32 sSYNetPeerBootstrapPeerReady;
sb32 sSYNetPeerBootstrapStartReceived;
u32 sSYNetPeerBootstrapSeed;
SYNetInputReplayMetadata sSYNetPeerBootstrapMetadata;
sb32 sSYNetPeerBattleBarrierEnabled;
sb32 sSYNetPeerBattleLocalReady;
sb32 sSYNetPeerBattlePeerReady;
sb32 sSYNetPeerBattleStartSent;
sb32 sSYNetPeerBattleStartReceived;
sb32 sSYNetPeerBattleBarrierReleased;
u32 sSYNetPeerBattleBarrierWaitFrames;
u32 sSYNetPeerBattleStartRepeatFrames;
u32 sSYNetPeerExecutionHoldFrames;
sb32 sSYNetPeerExecutionBeginLogged;
sb32 sSYNetPeerClockAlignEnabled;

#ifdef PORT
static u32 sSYNetPeerInputDelayFloor;
static u32 sSYNetPeerInputDelayCeil;
static const char *sSYNetPeerInputDelaySource;
static sb32 sSYNetPeerAdaptiveDelayEnabled;
static sb32 sSYNetPeerAdaptivePrimed;
static u32 sSYNetPeerAdaptivePrevLateFrames;
static u32 sSYNetPeerAdaptivePrevLoadFail;
static u32 sSYNetPeerAdaptiveStableIntervals;
static u32 sSYNetPeerAdaptNextSimTick;
static u32 sSYNetPeerDelaySyncPending;
static u32 sSYNetPeerDelaySyncEffectiveTick;
static sb32 sSYNetPeerDelaySyncPendingValid;
static sb32 sSYNetPeerTickGridExecGate;
static sb32 sSYNetPeerStartupDelayAlignDone;
static sb32 sSYNetPeerStartupMatchDelayPendingValid;
static u32 sSYNetPeerStartupMatchDelayTarget;
static u32 sSYNetPeerAdmissionBiasLastAdjustTick;
static s32 sSYNetPeerAdmissionWireBiasTicks;

static sb32 sSYNetPeerOptionalWallCalFromExecHoldStarted;
static u32 sSYNetPeerDelaySyncDiagExecReadyMark = ~(u32)0;
static int sSYNetPeerDelaySyncDiagEnvCache = -999;
static u32 sSYNetPeerAutoRunwayConsec;
static u32 sSYNetPeerAutoRunwayLastSimTick;
static int sSYNetPeerRunwayFrontierLogIntervalEnv = -999;
static int sSYNetPeerBootstrapIngressSymEnv = -999;
static sb32 sSYNetPeerBootstrapIngressWarmupOutboundSent;
static sb32 sSYNetPeerBootstrapIngressWarmupLoggedStart;
static sb32 sSYNetPeerBootstrapIngressWarmupLoggedDone;
static u32 sSYNetPeerGlobalCommitGen;
static int sSYNetPeerPhaseLockPredictionWindowEnv = -999;
static int sSYNetPeerStrictRingFuzzTicksEnvCache = -999;
static sb32 sSYNetPeerSessionStrictRingFuzzOverrideValid = FALSE;
static int sSYNetPeerSessionStrictRingFuzzOverride = 0;
static sb32 sSYNetPeerSessionParamsHostSent;
static sb32 sSYNetPeerSessionParamsHostGotAck;
static SYNetSessionParams sSYNetPeerSessionParamsHostProposal;
#define SYNETPEER_SESSION_RTT_PROBE_SAMPLES 4U
static u32 sSYNetPeerSessionRttProbeTarget;
static u32 sSYNetPeerSessionRttProbeSampleCount;
static u32 sSYNetPeerSessionRttProbeRttMs[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
static sb32 sSYNetPeerSessionRttProbeAwaitingAck;
static u64 sSYNetPeerSessionRttProbePingT0;
static void syNetPeerSessionParamsResetTransport(void);
static void syNetPeerRefreshCachedNetplayEnvCachesOnly(void);
static void syNetPeerSessionParamsServiceNegotiation(void);
static void syNetPeerSessionRttProbeOnPong(u32 seq, u64 h0, u32 rtt_ms);
static u32 sSYNetPeerBootstrapRetryCountCached;
static u32 sSYNetPeerBootstrapRetrySleepUsCached;
static sb32 sSYNetPeerBootstrapTimingCached;
static void syNetPeerRefreshBootstrapTimingFromEnv(void);
static u32 syNetPeerBootstrapRetryCount(void);
static u32 syNetPeerBootstrapRetrySleepUs(void);
static u32 syNetPeerBootstrapStartBurstCount(void);
static u32 syNetPeerBootstrapPauseBetweenAttempts(void);
static void syNetPeerHostFinalizeSessionParamsFromRtt(u32 rtt_ms);
static void syNetPeerHostFinalizeSessionParamsFromRttProbe(void);
static void syNetPeerSendSessionParamsPacket(const SYNetSessionParams *params, u16 packet_type);
static void syNetPeerHandleSessionParamsPacket(const u8 *buffer, s32 size, u16 expected_type);

static int syNetPeerGetDelaySyncDiagLevel(void)
{
	char *e;

	if (sSYNetPeerDelaySyncDiagEnvCache != -999)
	{
		return sSYNetPeerDelaySyncDiagEnvCache;
	}
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_DIAG");
	sSYNetPeerDelaySyncDiagEnvCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (sSYNetPeerDelaySyncDiagEnvCache < 0)
	{
		sSYNetPeerDelaySyncDiagEnvCache = 0;
	}
	if (sSYNetPeerDelaySyncDiagEnvCache > 2)
	{
		sSYNetPeerDelaySyncDiagEnvCache = 2;
	}
	return sSYNetPeerDelaySyncDiagEnvCache;
}

static void syNetPeerMaybeLogDelaySyncDiagOnDelayMutation(const char *kind, u32 prev_d, u32 new_d, u32 effective_tick)
{
	if (syNetPeerGetDelaySyncDiagLevel() < 1)
	{
		return;
	}
	if (prev_d == new_d)
	{
		return;
	}
	port_log(
	    "SSB64 NetPeer: delay_sync_diag apply=%s prev_D=%u new_D=%u eff_tick=%u sim_tick=%u host=%d\n",
	    kind,
	    (unsigned int)prev_d,
	    (unsigned int)new_d,
	    (unsigned int)effective_tick,
	    (unsigned int)syNetInputGetTick(),
	    (sSYNetPeerBootstrapIsHost != FALSE) ? 1 : 0);
}

static sb32 sSYNetPeerDelaySyncCommitLeadEnvLoaded;
static u32 sSYNetPeerDelaySyncCommitLeadTicksCached;

static void syNetPeerResetDelaySyncCommitLeadEnv(void)
{
	sSYNetPeerDelaySyncCommitLeadEnvLoaded = FALSE;
}

static u32 syNetPeerDelaySyncCommitLeadTicks(void)
{
	const char *e;
	int v;

	if (sSYNetPeerDelaySyncCommitLeadEnvLoaded != FALSE)
	{
		return sSYNetPeerDelaySyncCommitLeadTicksCached;
	}
	v = (int)SYNETPEER_DELAY_SYNC_COMMIT_LEAD_TICKS_DEFAULT;
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
	}
	if (v < 1)
	{
		v = 1;
	}
	if (v > 16)
	{
		v = 16;
	}
	sSYNetPeerDelaySyncCommitLeadTicksCached = (u32)v;
	sSYNetPeerDelaySyncCommitLeadEnvLoaded = TRUE;
	return sSYNetPeerDelaySyncCommitLeadTicksCached;
}

/*
 * Host adaptive delay must not commit before `effective_tick` — same boundary as INPUT_DELAY_SYNC on the client.
 * Otherwise wire ticks from `GatherHistoryBundle` (history_tick + delay) skew vs peer labels until the client applies.
 */
static sb32 sSYNetPeerHostDelayRampPendingValid;
static u32 sSYNetPeerHostDelayRampTarget;
static u32 sSYNetPeerHostDelayRampEffectiveTick;

static u32 syNetPeerClampInputDelayToContract(u32 delay);
static void syNetPeerSendInputDelaySyncPacket(u32 delay, u32 effective_tick);
static void syNetPeerResetDelaySyncPending(void);
static void syNetPeerApplyPendingInputDelaySync(void);
static void syNetPeerApplyOnlineCommittedInputDelayMinToFloorAndDelay(void);
static void syNetPeerLogCommittedInputDelay(const char *tag, u32 requested_delay, u32 caller_delay);
static void syNetPeerResetHostDelayRampPending(void);
static void syNetPeerApplyHostDelayRampPending(void);

static void syNetPeerResetAdaptiveDelayTracking(void)
{
	sSYNetPeerAdaptivePrimed = FALSE;
	sSYNetPeerAdaptivePrevLateFrames = 0;
	sSYNetPeerAdaptivePrevLoadFail = 0;
	sSYNetPeerAdaptiveStableIntervals = 0;
	sSYNetPeerAdaptNextSimTick = 0U;
	syNetPeerResetDelaySyncPending();
	sSYNetPeerAutoRunwayConsec = 0U;
	sSYNetPeerAutoRunwayLastSimTick = ~(u32)0;
	sSYNetPeerRunwayFrontierLogIntervalEnv = -999;
}

static u32 syNetPeerClampInputDelayToContract(u32 delay)
{
	u32 d;

	d = delay;
	if (d < sSYNetPeerInputDelayFloor)
	{
		d = sSYNetPeerInputDelayFloor;
	}
	if (d > sSYNetPeerInputDelayCeil)
	{
		d = sSYNetPeerInputDelayCeil;
	}
	return d;
}

static u32 syNetPeerEffectiveOnlineCommittedDelayMin(void)
{
	const char *e;
	int md;

	e = getenv("SSB64_NETPLAY_ALLOW_INPUT_DELAY_ZERO");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0))
	{
		return 0U;
	}
	md = syNetInputEnvGetMatchInputDelayOrNeg1();
	if (md == 0)
	{
		return 0U;
	}
	return SYNETPEER_ONLINE_COMMITTED_DELAY_MIN_DEFAULT;
}

static void syNetPeerApplyOnlineCommittedInputDelayMinToFloorAndDelay(void)
{
	u32 lo;

	lo = syNetPeerEffectiveOnlineCommittedDelayMin();
	if (lo == 0U)
	{
		return;
	}
	if (sSYNetPeerInputDelayFloor < lo)
	{
		sSYNetPeerInputDelayFloor = lo;
	}
	if (sSYNetPeerInputDelay < lo)
	{
		sSYNetPeerInputDelay = lo;
	}
	if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
	{
		sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
	}
}

static void syNetPeerLogCommittedInputDelay(const char *tag, u32 requested_delay, u32 caller_delay)
{
	port_log("SSB64 NetPeer: committed_input_delay tag=%s D=%u source=%s requested=%u caller=%u floor=%u ceil=%u\n",
	         (tag != NULL) ? tag : "?",
	         (unsigned int)sSYNetPeerInputDelay,
	         (sSYNetPeerInputDelaySource != NULL) ? sSYNetPeerInputDelaySource : "?",
	         (unsigned int)requested_delay,
	         (unsigned int)caller_delay,
	         (unsigned int)sSYNetPeerInputDelayFloor,
	         (unsigned int)sSYNetPeerInputDelayCeil);
}

static u32 syNetPeerSaturatingAddU32(u32 a, u32 b);

static void syNetPeerResetDelaySyncPending(void)
{
	sSYNetPeerDelaySyncPendingValid = FALSE;
	sSYNetPeerDelaySyncPending = 0U;
	sSYNetPeerDelaySyncEffectiveTick = 0U;
	syNetPeerResetHostDelayRampPending();
}

static void syNetPeerResetHostDelayRampPending(void)
{
	sSYNetPeerHostDelayRampPendingValid = FALSE;
	sSYNetPeerHostDelayRampTarget = 0U;
	sSYNetPeerHostDelayRampEffectiveTick = 0U;
}

static void syNetPeerApplyHostDelayRampPending(void)
{
	u32 t;

	if (sSYNetPeerHostDelayRampPendingValid == FALSE)
	{
		return;
	}
	t = syNetInputGetTick();
	if (t >= sSYNetPeerHostDelayRampEffectiveTick)
	{
		u32 prev_d;
		u32 eff_tick;

		prev_d = sSYNetPeerInputDelay;
		eff_tick = sSYNetPeerHostDelayRampEffectiveTick;
		sSYNetPeerInputDelay = syNetPeerClampInputDelayToContract(sSYNetPeerHostDelayRampTarget);
		syNetPeerMaybeLogDelaySyncDiagOnDelayMutation("ramp_commit", prev_d, sSYNetPeerInputDelay, eff_tick);
		syNetPeerResetHostDelayRampPending();
	}
}

static void syNetPeerApplyPendingInputDelaySync(void)
{
	u32 t;

	if (sSYNetPeerDelaySyncPendingValid == FALSE)
	{
		return;
	}
	t = syNetInputGetTick();
	if (t >= sSYNetPeerDelaySyncEffectiveTick)
	{
		u32 prev_d;
		u32 eff_tick;

		prev_d = sSYNetPeerInputDelay;
		eff_tick = sSYNetPeerDelaySyncEffectiveTick;
		sSYNetPeerInputDelay = syNetPeerClampInputDelayToContract(sSYNetPeerDelaySyncPending);
		if ((sSYNetPeerStartupMatchDelayPendingValid != FALSE) &&
		    (sSYNetPeerInputDelay == sSYNetPeerStartupMatchDelayTarget))
		{
			sSYNetPeerStartupMatchDelayPendingValid = FALSE;
			sSYNetPeerInputDelayFloor = sSYNetPeerStartupMatchDelayTarget;
			if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
			{
				sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
			}
		}
		syNetPeerMaybeLogDelaySyncDiagOnDelayMutation("delay_sync_commit", prev_d, sSYNetPeerInputDelay, eff_tick);
		syNetPeerApplyOnlineCommittedInputDelayMinToFloorAndDelay();
		syNetPeerResetDelaySyncPending();
	}
}

void syNetPeerApplyPendingDelayContract(void)
{
	syNetPeerApplyHostDelayRampPending();
	syNetPeerApplyPendingInputDelaySync();
}

static sb32 syNetPeerRequireInputBindStrict(void);
static sb32 syNetPeerInputBindIsComplete(void);

static void syNetPeerMaybeApplyStartupDelaySkewAlignment(void)
{
	u32 local_sim_tick;
	u32 eff_tick;

	if (sSYNetPeerStartupDelayAlignDone != FALSE)
	{
		return;
	}
	if ((sSYNetPeerIsActive == FALSE) || (sSYNetPeerBootstrapIsHost == FALSE))
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	local_sim_tick = syNetInputGetTick();
	if (local_sim_tick > 120U)
	{
		return;
	}
	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return;
	}
	if (sSYNetPeerStartupMatchDelayPendingValid != FALSE)
	{
		eff_tick = syNetPeerSaturatingAddU32(local_sim_tick, syNetPeerDelaySyncCommitLeadTicks());
		sSYNetPeerDelaySyncPending = sSYNetPeerStartupMatchDelayTarget;
		sSYNetPeerDelaySyncEffectiveTick = eff_tick;
		sSYNetPeerDelaySyncPendingValid = TRUE;
		syNetPeerSendInputDelaySyncPacket(sSYNetPeerStartupMatchDelayTarget, eff_tick);
		sSYNetPeerStartupMatchDelayPendingValid = FALSE;
		sSYNetPeerStartupDelayAlignDone = TRUE;
		sSYNetPeerInputDelayFloor = sSYNetPeerStartupMatchDelayTarget;
		if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
		{
			sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
		}
		syNetPeerApplyOnlineCommittedInputDelayMinToFloorAndDelay();
		port_log(
		    "SSB64 NetPeer: phase_lock_startup_delay apply_match_D=%u local_sim=%u eff_tick=%u\n",
		    (unsigned int)sSYNetPeerStartupMatchDelayTarget, (unsigned int)local_sim_tick, (unsigned int)eff_tick);
		return;
	}
	sSYNetPeerStartupDelayAlignDone = TRUE;
}

u32 syNetPeerGetDelaySyncDiagExecReadySimTick(void)
{
	return sSYNetPeerDelaySyncDiagExecReadyMark;
}
#endif

#if defined(PORT) && defined(SSB64_NETMENU)
sb32 gSYNetPeerSuppressBootstrapSceneAdvance;
static sb32 sSYNetPeerAutomatchHandshakeActive;
static u16 sAutoLocalBanMask;
static u8 sAutoLocalFkind;
static u8 sAutoLocalCostume;
static u32 sAutoLocalNonce;
static u16 sAutoPeerBanMask;
static u8 sAutoPeerFkind;
static u8 sAutoPeerCostume;
static u32 sAutoPeerNonce;
static sb32 sSYAutoGotPeerOffer;
static sb32 sSYNetPeerStageSceneRendezvousArmed;
static sb32 sSYNetPeerStageSceneLocalReadySent;
static sb32 sSYNetPeerStageScenePeerReady;
static sb32 sSYNetPeerStageScenePeerReadyLogged;
static sb32 sSYNetPeerStageSceneGoSent;
static sb32 sSYNetPeerStageSceneGoReceived;
static sb32 sSYNetPeerStageSceneGoReceivedLogged;
static sb32 sSYNetPeerStageSceneGoDeadlineValid;
static u64 sSYNetPeerStageSceneGoDeadlineUnixMs;
static u32 sSYNetPeerStageSceneGoSendRepeatFrames;
#endif

#if defined(PORT)
syNetPeerOsSocket sSYNetPeerSocket = SY_NETPEER_OS_SOCKET_INVALID;
struct sockaddr_in sSYNetPeerBindAddress;
struct sockaddr_in sSYNetPeerPeerAddress;

static u32 sSYNetPeerTimePingSeq;
static u64 sSYNetPeerTimePingT0Sent;
static sb32 sSYNetPeerTimePingAwaitingAck;
static s64 sSYNetPeerClockOffsetSamples[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
static u32 sSYNetPeerClockRttSamples[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
static u32 sSYNetPeerClockSyncSampleCount;
static u32 sSYNetPeerClockSyncTargetTotal;
static sb32 sSYNetPeerBarrierViAlign;
static u32 sSYNetPeerBarrierViHz;
static sb32 sSYNetPeerBarrierConservative;
static u64 sSYNetPeerBattleStartUnixMs;
static s64 sSYNetPeerBattleStartOffsetMs;
static sb32 sSYNetPeerBattleStartTimeSent;
static sb32 sSYNetPeerBattleStartTimeReceived;
static sb32 sSYNetPeerBarrierDeadlineValid;
static u64 sSYNetPeerBarrierDeadlineUnixMs;
static sb32 sSYNetPeerInputBindSent;
static sb32 sSYNetPeerInputBindPeerOk;
static sb32 sSYNetPeerInputBindAckLogged;
static u8 sSYNetPeerInputBindPeerPrimaryDev;
static u32 sSYNetPeerInputBindGraceUntilFrame;
static sb32 sSYNetPeerBattleStartTimeDupLogOnce;
static sb32 sSYNetPeerBattleStartTimeConflictLogged;
static sb32 sSYNetPeerBattleStartViWireUsesExtended;
static u32 sSYNetPeerBattleStartViWireHz;
static u32 sSYNetPeerBattleStartViWireAlign;
static u32 sSYNetPeerClockSyncTargetBaseline;
static u32 sSYNetPeerBarrierSkewRetryCount;
static u32 sSYNetPeerBarrierEpochExtraLeadMs;
static s64 sSYNetPeerLastBarrierContractOffsetSpreadMs;
static u32 sSYNetPeerBarrierSkewRetriesLatchedForLog;
static u64 sSYNetPeerBarrierWallClockStartMs;
static sb32 sSYNetPeerBarrierEscapeApplied;
static sb32 sSYNetPeerBarrierRequeueApplied;

static u64 syNetPeerNowUnixMs(void);
static void syNetPeerWriteU64(u8 **cursor, u64 value);
static u64 syNetPeerReadU64(const u8 **cursor);
static void syNetPeerResetClockAlignState(void);
static void syNetPeerSendTimePingPacket(u32 seq, u64 t0_ms);
static void syNetPeerSendTimePongPacket(u32 seq, u64 h0_echo, u64 c1_ms, u64 c2_ms);
static void syNetPeerSendBattleStartTimePacket(u64 start_unix_ms, s64 offset_host_minus_client_ms);
static void syNetPeerHandleTimePingPacket(const u8 *buffer, s32 size);
static void syNetPeerHandleTimePongPacket(const u8 *buffer, s32 size);
static void syNetPeerHandleBattleStartTimePacket(const u8 *buffer, s32 size);
static s64 syNetPeerMedianS64(s64 *values, u32 count);
static u32 syNetPeerMedianU32Copy(const u32 *vals, u32 n);
static s64 syNetPeerAbsS64(s64 x);
static void syNetPeerLoadBarrierTimingEnvFromConfig(void);
static void syNetPeerBarrierViApplyContractFromHost(u32 hz, u32 align_flag);
static u32 syNetPeerBarrierFrameGranularityMs(void);
static u32 syNetPeerBarrierDeadlineViPhaseBucket(void);
static u64 syNetPeerQuantizeCeilUnixMs(u64 ms, u32 gran_ms);
static void syNetPeerPickClockSyncMedians(u32 sample_n, s64 *out_median_o, u32 *out_min_rtt_kept, u32 *out_rtt_for_lead,
                                          u32 *out_uncertainty_slack, u32 *out_fallback_extra_lead,
                                          u32 *out_kept_count, sb32 *out_used_fallback, s64 *out_offset_spread_ms);
static void syNetPeerHostFinishClockSyncAndSendStart(void);
static sb32 syNetPeerCheckBarrierDeadlineReached(void);
static void syNetPeerLogTickFrameSnapshot(const char *tag, sb32 gated_by_tick_diag);
static void syNetPeerLogClockSyncSampleDone(u32 seq, s64 o_ms, u32 rtt_ms);
static void syNetPeerInputBindReset(void);
static void syNetPeerResetBootstrapIngressSymmetryState(void);
static void syNetPeerGetInputBindExpectedSims(u8 *out_host_sim, u8 *out_guest_sim);
static void syNetPeerSendInputBindPacket(void);
static void syNetPeerHandleInputBindPacket(const u8 *buffer, s32 size);
static void syNetPeerInputBindMaybeLogAck(void);
static void syNetPeerInputBindServiceTransport(void);
static sb32 syNetPeerRequireBattleExecSync(void);
static void syNetPeerBattleExecSyncReset(void);
static sb32 syNetPeerBattleExecSyncIsComplete(void);
static void syNetPeerSendBattleExecSyncPacket(u32 agreed_sim_tick, u32 vi_phase_bucket);
static void syNetPeerHandleBattleExecSyncPacket(const u8 *buffer, s32 size);
static void syNetPeerBattleExecSyncServiceTransport(void);
static void syNetPeerPollBarrierWallTimeouts(void);
#if defined(PORT) && defined(SSB64_NETMENU)
static void syNetPeerResetStageSceneRendezvousState(void);
static u32 syNetPeerStageSceneGoHoldMs(void);
#endif
#endif

u32 syNetPeerChecksumAccumulateU32(u32 checksum, u32 value)
{
	checksum ^= value;
	checksum *= 16777619U;

	return checksum;
}

u32 syNetPeerChecksumAccumulateFrame(u32 checksum, const SYNetPeerPacketFrame *frame)
{
	checksum = syNetPeerChecksumAccumulateU32(checksum, frame->tick);
	checksum = syNetPeerChecksumAccumulateU32(checksum, frame->buttons);
	checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)frame->stick_x);
	checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)frame->stick_y);

	return checksum;
}

u32 syNetPeerChecksumInputPacket(u32 session_id, u32 ack_tick, u32 packet_seq, u16 wire_version, u8 player, u8 frame_count,
                                 const SYNetPeerPacketFrame *frames, u8 secondary_slot, u8 sec_frame_count,
                                 const SYNetPeerPacketFrame *sec_frames, const s32 *connect_last_tick,
                                 const u8 *connect_disconnected, const s32 *symmetric_mismatch_tick,
                                 const s32 *symmetric_target_tick)
{
	u32 checksum = 2166136261U;
	s32 i;

	checksum = syNetPeerChecksumAccumulateU32(checksum, SYNETPEER_MAGIC);
	checksum = syNetPeerChecksumAccumulateU32(checksum, wire_version);
	checksum = syNetPeerChecksumAccumulateU32(checksum, SYNETPEER_PACKET_INPUT);
	checksum = syNetPeerChecksumAccumulateU32(checksum, session_id);
	checksum = syNetPeerChecksumAccumulateU32(checksum, ack_tick);
	checksum = syNetPeerChecksumAccumulateU32(checksum, packet_seq);
	checksum = syNetPeerChecksumAccumulateU32(checksum, player);
	checksum = syNetPeerChecksumAccumulateU32(checksum, frame_count);

	if (SYNETPEER_WIRE_HAS_CONNECT_STATUS(wire_version) != FALSE)
	{
		if ((connect_last_tick != NULL) && (connect_disconnected != NULL))
		{
			for (i = 0; i < MAXCONTROLLERS; i++)
			{
				u32 sym_tick;
				u32 sym_target;

				checksum = syNetPeerChecksumAccumulateU32(checksum, (u32)connect_last_tick[i]);
				checksum = syNetPeerChecksumAccumulateU32(checksum, connect_disconnected[i]);
				sym_tick = 0U;
				sym_target = 0U;
				if (symmetric_mismatch_tick != NULL)
				{
					sym_tick =
					    (symmetric_mismatch_tick[i] > 0) ? (u32)symmetric_mismatch_tick[i] : 0U;
				}
				if (symmetric_target_tick != NULL)
				{
					sym_target =
					    (symmetric_target_tick[i] > 0) ? (u32)symmetric_target_tick[i] : 0U;
				}
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)((sym_tick >> 16) & 0xFF));
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)((sym_tick >> 8) & 0xFF));
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)(sym_tick & 0xFF));
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)((sym_target >> 16) & 0xFF));
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)((sym_target >> 8) & 0xFF));
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)(sym_target & 0xFF));
			}
		}
	}
	for (i = 0; i < frame_count; i++)
	{
		checksum = syNetPeerChecksumAccumulateFrame(checksum, &frames[i]);
	}
	if ((SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(wire_version) != FALSE) &&
	    (secondary_slot != SYNETPEER_SECONDARY_SLOT_ABSENT))
	{
		checksum = syNetPeerChecksumAccumulateU32(checksum, secondary_slot);
		checksum = syNetPeerChecksumAccumulateU32(checksum, sec_frame_count);
		for (i = 0; i < sec_frame_count; i++)
		{
			checksum = syNetPeerChecksumAccumulateFrame(checksum, &sec_frames[i]);
		}
	}
	return checksum;
}

u32 syNetPeerChecksumBytes(const u8 *bytes, u32 size)
{
	u32 checksum = 2166136261U;
	u32 i;

	for (i = 0; i < size; i++)
	{
		checksum ^= bytes[i];
		checksum *= 16777619U;
	}
	return checksum;
}

void syNetPeerWriteU8(u8 **cursor, u8 value)
{
	*(*cursor)++ = value;
}

void syNetPeerWriteU16(u8 **cursor, u16 value)
{
	*(*cursor)++ = (value >> 8) & 0xFF;
	*(*cursor)++ = value & 0xFF;
}

void syNetPeerWriteU32(u8 **cursor, u32 value)
{
	*(*cursor)++ = (value >> 24) & 0xFF;
	*(*cursor)++ = (value >> 16) & 0xFF;
	*(*cursor)++ = (value >> 8) & 0xFF;
	*(*cursor)++ = value & 0xFF;
}

u8 syNetPeerReadU8(const u8 **cursor)
{
	return *(*cursor)++;
}

u16 syNetPeerReadU16(const u8 **cursor)
{
	u16 value = (u16)(*(*cursor)++) << 8;

	value |= *(*cursor)++;

	return value;
}

u32 syNetPeerReadU32(const u8 **cursor)
{
	u32 value = (u32)(*(*cursor)++) << 24;

	value |= (u32)(*(*cursor)++) << 16;
	value |= (u32)(*(*cursor)++) << 8;
	value |= *(*cursor)++;

	return value;
}

void syNetPeerWriteMetadata(u8 **cursor, const SYNetInputReplayMetadata *metadata)
{
	s32 player;

	syNetPeerWriteU32(cursor, metadata->magic);
	syNetPeerWriteU32(cursor, metadata->version);
	syNetPeerWriteU32(cursor, metadata->scene_kind);
	syNetPeerWriteU32(cursor, metadata->player_count);
	syNetPeerWriteU32(cursor, metadata->stage_kind);
	syNetPeerWriteU32(cursor, metadata->stocks);
	syNetPeerWriteU32(cursor, metadata->time_limit);
	syNetPeerWriteU32(cursor, metadata->item_switch);
	syNetPeerWriteU32(cursor, metadata->item_toggles);
	syNetPeerWriteU32(cursor, metadata->rng_seed);
	syNetPeerWriteU32(cursor, metadata->game_type);
	syNetPeerWriteU8(cursor, metadata->game_rules);
	syNetPeerWriteU8(cursor, metadata->is_team_battle);
	syNetPeerWriteU8(cursor, metadata->handicap);
	syNetPeerWriteU8(cursor, metadata->is_team_attack);
	syNetPeerWriteU8(cursor, metadata->is_stage_select);
	syNetPeerWriteU8(cursor, metadata->damage_ratio);
	syNetPeerWriteU8(cursor, metadata->item_appearance_rate);
	syNetPeerWriteU8(cursor, metadata->is_not_teamshadows);

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		syNetPeerWriteU8(cursor, metadata->player_kinds[player]);
		syNetPeerWriteU8(cursor, metadata->fighter_kinds[player]);
		syNetPeerWriteU8(cursor, metadata->costumes[player]);
		syNetPeerWriteU8(cursor, metadata->teams[player]);
		syNetPeerWriteU8(cursor, metadata->handicaps[player]);
		syNetPeerWriteU8(cursor, metadata->levels[player]);
		syNetPeerWriteU8(cursor, metadata->shades[player]);
	}
	syNetPeerWriteU8(cursor, metadata->netplay_sim_slot_host_hw);
	syNetPeerWriteU8(cursor, metadata->netplay_sim_slot_client_hw);
}

void syNetPeerReadMetadata(const u8 **cursor, SYNetInputReplayMetadata *metadata)
{
	s32 player;

	metadata->magic = syNetPeerReadU32(cursor);
	metadata->version = syNetPeerReadU32(cursor);
	metadata->scene_kind = syNetPeerReadU32(cursor);
	metadata->player_count = syNetPeerReadU32(cursor);
	metadata->stage_kind = syNetPeerReadU32(cursor);
	metadata->stocks = syNetPeerReadU32(cursor);
	metadata->time_limit = syNetPeerReadU32(cursor);
	metadata->item_switch = syNetPeerReadU32(cursor);
	metadata->item_toggles = syNetPeerReadU32(cursor);
	metadata->rng_seed = syNetPeerReadU32(cursor);
	metadata->game_type = syNetPeerReadU32(cursor);
	metadata->game_rules = syNetPeerReadU8(cursor);
	metadata->is_team_battle = syNetPeerReadU8(cursor);
	metadata->handicap = syNetPeerReadU8(cursor);
	metadata->is_team_attack = syNetPeerReadU8(cursor);
	metadata->is_stage_select = syNetPeerReadU8(cursor);
	metadata->damage_ratio = syNetPeerReadU8(cursor);
	metadata->item_appearance_rate = syNetPeerReadU8(cursor);
	metadata->is_not_teamshadows = syNetPeerReadU8(cursor);

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		metadata->player_kinds[player] = syNetPeerReadU8(cursor);
		metadata->fighter_kinds[player] = syNetPeerReadU8(cursor);
		metadata->costumes[player] = syNetPeerReadU8(cursor);
		metadata->teams[player] = syNetPeerReadU8(cursor);
		metadata->handicaps[player] = syNetPeerReadU8(cursor);
		metadata->levels[player] = syNetPeerReadU8(cursor);
		metadata->shades[player] = syNetPeerReadU8(cursor);
	}
	metadata->netplay_sim_slot_host_hw = syNetPeerReadU8(cursor);
	metadata->netplay_sim_slot_client_hw = syNetPeerReadU8(cursor);
}

sb32 syNetPeerCheckMetadata(const SYNetInputReplayMetadata *metadata)
{
	if ((metadata->magic != SYNETINPUT_REPLAY_MAGIC) ||
		(metadata->version != SYNETINPUT_REPLAY_VERSION) ||
		(metadata->scene_kind != nSCKindVSBattle) ||
		(metadata->player_count == 0) ||
		(metadata->player_count > MAXCONTROLLERS))
	{
		return FALSE;
	}
	return TRUE;
}

#if defined(PORT)
void syNetPeerSendBytes(const u8 *buffer, u32 size);

static void syNetPeerRefreshBootstrapTimingFromEnv(void)
{
	const char *e;
	s32 v;

	if (sSYNetPeerBootstrapTimingCached != FALSE)
	{
		return;
	}
	sSYNetPeerBootstrapTimingCached = TRUE;
	sSYNetPeerBootstrapRetryCountCached = SYNETPEER_BOOTSTRAP_RETRY_COUNT_DEFAULT;
	e = getenv("SSB64_NETPLAY_BOOTSTRAP_RETRY_COUNT");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v >= (s32)SYNETPEER_BOOTSTRAP_RETRY_COUNT_MIN)
		{
			sSYNetPeerBootstrapRetryCountCached = (u32)v;
		}
	}
	if (sSYNetPeerBootstrapRetryCountCached < SYNETPEER_BOOTSTRAP_RETRY_COUNT_MIN)
	{
		sSYNetPeerBootstrapRetryCountCached = SYNETPEER_BOOTSTRAP_RETRY_COUNT_MIN;
	}
	if (sSYNetPeerBootstrapRetryCountCached > SYNETPEER_BOOTSTRAP_RETRY_COUNT_MAX)
	{
		sSYNetPeerBootstrapRetryCountCached = SYNETPEER_BOOTSTRAP_RETRY_COUNT_MAX;
	}
	sSYNetPeerBootstrapRetrySleepUsCached = SYNETPEER_BOOTSTRAP_RETRY_USECS_DEFAULT;
	e = getenv("SSB64_NETPLAY_BOOTSTRAP_RETRY_SLEEP_US");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v >= (s32)SYNETPEER_BOOTSTRAP_RETRY_USECS_MIN)
		{
			sSYNetPeerBootstrapRetrySleepUsCached = (u32)v;
		}
	}
	if (sSYNetPeerBootstrapRetrySleepUsCached < SYNETPEER_BOOTSTRAP_RETRY_USECS_MIN)
	{
		sSYNetPeerBootstrapRetrySleepUsCached = SYNETPEER_BOOTSTRAP_RETRY_USECS_MIN;
	}
	if (sSYNetPeerBootstrapRetrySleepUsCached > SYNETPEER_BOOTSTRAP_RETRY_USECS_MAX)
	{
		sSYNetPeerBootstrapRetrySleepUsCached = SYNETPEER_BOOTSTRAP_RETRY_USECS_MAX;
	}
	port_log(
	    "SSB64 NetPeer: bootstrap timing retries=%u sleep_us=%u (~%.1fs/phase) stage_hold_ms_default=%u\n",
	    (unsigned int)sSYNetPeerBootstrapRetryCountCached, (unsigned int)sSYNetPeerBootstrapRetrySleepUsCached,
	    (double)((u64)sSYNetPeerBootstrapRetryCountCached * (u64)sSYNetPeerBootstrapRetrySleepUsCached) / 1000000.0,
	    (unsigned int)SYNETPEER_STAGE_SCENE_GO_HOLD_MS_DEFAULT);
}

static u32 syNetPeerBootstrapRetryCount(void)
{
	syNetPeerRefreshBootstrapTimingFromEnv();
	return sSYNetPeerBootstrapRetryCountCached;
}

static u32 syNetPeerBootstrapRetrySleepUs(void)
{
	syNetPeerRefreshBootstrapTimingFromEnv();
	return sSYNetPeerBootstrapRetrySleepUsCached;
}

static u32 syNetPeerBootstrapStartBurstCount(void)
{
	const char *e;
	s32 v;

	v = (s32)SYNETPEER_BOOTSTRAP_START_BURST_DEFAULT;
	e = getenv("SSB64_NETPLAY_BOOTSTRAP_START_BURST");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
	}
	if (v < 10)
	{
		v = 10;
	}
	if (v > 240)
	{
		v = 240;
	}
	return (u32)v;
}

static u32 syNetPeerBootstrapPauseBetweenAttempts(void)
{
	const char *e;
	s32 ms;
	u32 slices;

	ms = (s32)SYNETPEER_BOOTSTRAP_PAUSE_BETWEEN_MS_DEFAULT;
	e = getenv("SSB64_NETPLAY_BOOTSTRAP_PAUSE_BETWEEN_MS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		ms = atoi(e);
	}
	if (ms < 0)
	{
		ms = 0;
	}
	if (ms > 5000)
	{
		ms = 5000;
	}
	slices = ((u32)ms * 1000U) / syNetPeerBootstrapRetrySleepUs();
	if (slices < 1U)
	{
		slices = 1U;
	}
	return slices;
}

void syNetPeerSleepBootstrapRetry(void)
{
	u32 remain;

	remain = syNetPeerBootstrapRetrySleepUs();
	while (remain > 0U)
	{
		u32 slice = (remain > 1000U) ? 1000U : remain;

		syNetPeerOsSleepMicros(slice);
		port_watchdog_note_yield();
		port_coroutine_yield();
		remain -= slice;
	}
}

const char *syNetPeerFindPortSeparator(const char *text)
{
	const char *separator = NULL;

	while (*text != '\0')
	{
		if (*text == ':')
		{
			separator = text;
		}
		text++;
	}
	return separator;
}

sb32 syNetPeerStringEquals(const char *a, const char *b)
{
	while ((*a != '\0') && (*b != '\0'))
	{
		if (*a != *b)
		{
			return FALSE;
		}
		a++;
		b++;
	}
	return (*a == *b) ? TRUE : FALSE;
}

sb32 syNetPeerParseIPv4Address(const char *text, struct sockaddr_in *out_address)
{
	const char *colon;
	s32 host_length;
	s32 port;
	char host[64];

	if ((text == NULL) || (out_address == NULL))
	{
		return FALSE;
	}
	colon = syNetPeerFindPortSeparator(text);

	if ((colon == NULL) || (colon == text) || (*(colon + 1) == '\0'))
	{
		return FALSE;
	}
	host_length = colon - text;

	if (host_length >= (s32)sizeof(host))
	{
		return FALSE;
	}
	memcpy(host, text, host_length);
	host[host_length] = '\0';

	port = atoi(colon + 1);

	if ((port <= 0) || (port > 65535))
	{
		return FALSE;
	}
	memset(out_address, 0, sizeof(*out_address));
	out_address->sin_family = AF_INET;
	out_address->sin_port = htons((u16)port);

	if ((syNetPeerStringEquals(host, "*") != FALSE) || (syNetPeerStringEquals(host, "0.0.0.0") != FALSE))
	{
		out_address->sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else if (inet_pton(AF_INET, host, &out_address->sin_addr) != 1)
	{
		return FALSE;
	}
	return TRUE;
}

void syNetPeerCloseSocket(void)
{
	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) != FALSE)
	{
		syNetPeerOsSocketDestroy(&sSYNetPeerSocket);
	}
}

static sb32 syNetPeerDatagramSocketIsUsable(void)
{
	return ((sSYNetPeerIsActive != FALSE) && (syNetPeerOsSocketIsValid(sSYNetPeerSocket) != FALSE)) ? TRUE : FALSE;
}

sb32 syNetPeerOpenSocket(void)
{
	int reuse = 1;

	/* Always bind a fresh socket per bootstrap attempt (back-to-back LAN/reflexive retries). */
	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) != FALSE)
	{
		syNetPeerCloseSocket();
	}
	syNetPeerSocketOsStartup();
	sSYNetPeerSocket = syNetPeerOsSocketCreateDgram();

	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) == FALSE)
	{
		port_log("SSB64 NetPeer: socket failed err=%d\n", syNetPeerOsSocketLastError());
		return FALSE;
	}
	if (syNetPeerOsSetsockoptReuseAddr(sSYNetPeerSocket, reuse) != 0)
	{
		port_log("SSB64 NetPeer: SO_REUSEADDR failed err=%d\n", syNetPeerOsSocketLastError());
		syNetPeerCloseSocket();
		return FALSE;
	}
	if (syNetPeerOsBind(sSYNetPeerSocket, &sSYNetPeerBindAddress) != 0)
	{
		port_log("SSB64 NetPeer: bind failed err=%d\n", syNetPeerOsSocketLastError());
		syNetPeerCloseSocket();
		return FALSE;
	}
	if (syNetPeerOsSetNonBlocking(sSYNetPeerSocket) != 0)
	{
		port_log("SSB64 NetPeer: nonblocking setup failed err=%d\n", syNetPeerOsSocketLastError());
		syNetPeerCloseSocket();
		return FALSE;
	}
	return TRUE;
}

/*--------------------------------------------------------------------
 * Wall-clock ms for barrier deadlines + TIME_PING/TIME_PONG samples.
 *-------------------------------------------------------------------*/
static u64 syNetPeerNowUnixMs(void)
{
	return syNetPeerOsWallClockUnixMs();
}

static void syNetPeerWriteU64(u8 **cursor, u64 value)
{
	syNetPeerWriteU32(cursor, (u32)(value >> 32));
	syNetPeerWriteU32(cursor, (u32)(value & 0xFFFFFFFFU));
}

static u64 syNetPeerReadU64(const u8 **cursor)
{
	u64 hi = syNetPeerReadU32(cursor);
	u64 lo = syNetPeerReadU32(cursor);

	return (hi << 32) | lo;
}

static void syNetPeerLoadBarrierTimingEnvFromConfig(void)
{
	u32 base;
	u32 extra;
	u32 settle;
	char *e;

	base = SYNETPEER_CLOCK_SYNC_SAMPLES_DEFAULT;
	e = getenv("SSB64_NETPLAY_CLOCK_SYNC_SAMPLES");
	if ((e != NULL) && (e[0] != '\0'))
	{
		s32 v = atoi(e);

		if (v >= 4)
		{
			base = (u32)v;
		}
	}
	if (base < 4U)
	{
		base = 4U;
	}
	if (base > SYNETPEER_CLOCK_SYNC_SAMPLES_MAX)
	{
		base = SYNETPEER_CLOCK_SYNC_SAMPLES_MAX;
	}
	extra = 0U;
	e = getenv("SSB64_NETPLAY_CLOCK_EXTRA_SAMPLES");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) > 0))
	{
		extra = (u32)atoi(e);
	}
	settle = 0U;
	e = getenv("SSB64_NETPLAY_CLOCK_SETTLE_ROUNDS");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) > 0))
	{
		settle = (u32)atoi(e);
	}
	extra += settle;
	if (extra > SYNETPEER_CLOCK_SYNC_SAMPLES_MAX - base)
	{
		extra = SYNETPEER_CLOCK_SYNC_SAMPLES_MAX - base;
	}
	sSYNetPeerClockSyncTargetTotal = base + extra;

	sSYNetPeerBarrierViAlign = TRUE;
	e = getenv("SSB64_NETPLAY_BARRIER_VI_ALIGN");
	if ((e != NULL) && (atoi(e) == 0))
	{
		sSYNetPeerBarrierViAlign = FALSE;
	}
	sSYNetPeerBarrierViHz = 60U;
	e = getenv("SSB64_NETPLAY_BARRIER_VI_HZ");
	if ((e != NULL) && (atoi(e) > 0))
	{
		sSYNetPeerBarrierViHz = (u32)atoi(e);
	}
	if (sSYNetPeerBarrierViHz < 1U)
	{
		sSYNetPeerBarrierViHz = 1U;
	}
	if (sSYNetPeerBarrierViHz > 480U)
	{
		sSYNetPeerBarrierViHz = 480U;
	}

	sSYNetPeerBarrierConservative = FALSE;
	e = getenv("SSB64_NETPLAY_BARRIER_CONSERVATIVE");
	if ((e != NULL) && (atoi(e) != 0))
	{
		sSYNetPeerBarrierConservative = TRUE;
	}
	if (sSYNetPeerClockSyncTargetTotal < 4U)
	{
		sSYNetPeerClockSyncTargetTotal = SYNETPEER_CLOCK_SYNC_SAMPLES_DEFAULT;
	}
}

/* Guest: host-selected barrier VI grid overwrites local env for this session (BATTLE_START_TIME v2). */
static void syNetPeerBarrierViApplyContractFromHost(u32 hz, u32 align_flag)
{
	u32 h;

	h = hz;
	if (h < 1U)
	{
		h = 1U;
	}
	if (h > 480U)
	{
		h = 480U;
	}
	sSYNetPeerBarrierViHz = h;
	sSYNetPeerBarrierViAlign = (align_flag != 0U) ? TRUE : FALSE;
}

static u32 syNetPeerBarrierFrameGranularityMs(void)
{
	u32 hz;

	hz = sSYNetPeerBarrierViHz;
	if (hz < 1U)
	{
		hz = 60U;
	}
	if (hz > 480U)
	{
		hz = 480U;
	}
	return (1000U + hz - 1U) / hz;
}

static u64 syNetPeerQuantizeCeilUnixMs(u64 ms, u32 gran_ms)
{
	if (gran_ms == 0U)
	{
		return ms;
	}
	return ((ms + (u64)gran_ms - 1ULL) / (u64)gran_ms) * (u64)gran_ms;
}

static u32 syNetPeerBarrierDeadlineViPhaseBucket(void)
{
	u32 gran;

	if (sSYNetPeerBarrierDeadlineValid == FALSE)
	{
		return 0U;
	}
	gran = syNetPeerBarrierFrameGranularityMs();
	if (gran == 0U)
	{
		return 0U;
	}
	return (u32)(sSYNetPeerBarrierDeadlineUnixMs / (u64)gran);
}

static u32 syNetPeerCurrentViPhaseBucketNow(void)
{
	u32 gran;
	u64 now_ms;

	gran = syNetPeerBarrierFrameGranularityMs();
	if (gran == 0U)
	{
		return 0U;
	}
	now_ms = syNetPeerNowUnixMs();
	return (u32)(now_ms / (u64)gran);
}

/* Drops ping/pong progress and start-time/barrier bookkeeping (VS session start). */
static void syNetPeerResetClockAlignState(void)
{
	s32 i;

	/* Guard startup/early-service paths that can query VI phase before env contract parse. */
	if (sSYNetPeerBarrierViHz < 1U)
	{
		sSYNetPeerBarrierViHz = 60U;
	}
	sSYNetPeerTimePingSeq = 0;
	sSYNetPeerTimePingT0Sent = 0;
	for (i = 0; i < (s32)SYNETPEER_CLOCK_SYNC_SAMPLES_MAX; i++)
	{
		sSYNetPeerClockOffsetSamples[i] = 0;
		sSYNetPeerClockRttSamples[i] = 0;
	}
	sSYNetPeerClockSyncSampleCount = 0;
	sSYNetPeerBattleStartUnixMs = 0;
	sSYNetPeerBattleStartOffsetMs = 0;
	sSYNetPeerBattleStartTimeSent = FALSE;
	sSYNetPeerBattleStartTimeReceived = FALSE;
	sSYNetPeerBarrierDeadlineValid = FALSE;
	sSYNetPeerBarrierDeadlineUnixMs = 0;
	sSYNetPeerTimePingAwaitingAck = FALSE;
	sSYNetPeerBattleStartTimeDupLogOnce = FALSE;
	sSYNetPeerBattleStartTimeConflictLogged = FALSE;
	sSYNetPeerBattleStartViWireUsesExtended = FALSE;
	sSYNetPeerBattleStartViWireHz = 0U;
	sSYNetPeerBattleStartViWireAlign = 0U;
}

static void syNetPeerSendTimePingPacket(u32 seq, u64 t0_ms)
{
	u8 buffer[SYNETPEER_TIME_PING_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_TIME_PING);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, seq);
	syNetPeerWriteU64(&cursor, t0_ms);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_TIME_PING_BYTES);
}

static void syNetPeerSendTimePongPacket(u32 seq, u64 h0_echo, u64 c1_ms, u64 c2_ms)
{
	u8 buffer[SYNETPEER_TIME_PONG_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_TIME_PONG);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, seq);
	syNetPeerWriteU64(&cursor, h0_echo);
	syNetPeerWriteU64(&cursor, c1_ms);
	syNetPeerWriteU64(&cursor, c2_ms);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_TIME_PONG_BYTES);
}

static void syNetPeerSendBattleStartTimePacket(u64 start_unix_ms, s64 offset_host_minus_client_ms)
{
	u8 buffer[SYNETPEER_BATTLE_START_TIME_BYTES];
	u8 *cursor = buffer;
	u32 checksum;
	u64 off_u;
	u32 vi_hz;
	u32 vi_al;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_BATTLE_START_TIME);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU64(&cursor, start_unix_ms);
	off_u = (u64)offset_host_minus_client_ms;
	syNetPeerWriteU64(&cursor, off_u);
	vi_hz = sSYNetPeerBarrierViHz;
	vi_al = (sSYNetPeerBarrierViAlign != FALSE) ? 1U : 0U;
	syNetPeerWriteU32(&cursor, vi_hz);
	syNetPeerWriteU32(&cursor, vi_al);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_BATTLE_START_TIME_BYTES);
}

static s64 syNetPeerMedianS64(s64 *values, u32 count)
{
	s64 tmp[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	u32 a;
	u32 b;
	s64 swap;
	s32 mid;

	if ((count == 0) || (count > SYNETPEER_CLOCK_SYNC_SAMPLES_MAX))
	{
		return 0;
	}
	for (a = 0; a < count; a++)
	{
		tmp[a] = values[a];
	}
	for (a = 0; a < count; a++)
	{
		for (b = a + 1; b < count; b++)
		{
			if (tmp[b] < tmp[a])
			{
				swap = tmp[a];
				tmp[a] = tmp[b];
				tmp[b] = swap;
			}
		}
	}
	mid = (s32)(count / 2U);
	if ((count % 2U) != 0U)
	{
		return tmp[(u32)mid];
	}
	return (tmp[(u32)(mid - 1)] + tmp[(u32)mid]) / 2;
}

static s64 syNetPeerAbsS64(s64 x)
{
	return (x < 0) ? -x : x;
}

static u32 syNetPeerMedianU32Copy(const u32 *vals, u32 n)
{
	u32 tmp[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	u32 a;
	u32 b;
	u32 swap;
	s32 mid;

	if ((n == 0) || (n > SYNETPEER_CLOCK_SYNC_SAMPLES_MAX))
	{
		return 0;
	}
	for (a = 0; a < n; a++)
	{
		tmp[a] = vals[a];
	}
	for (a = 0; a < n; a++)
	{
		for (b = a + 1; b < n; b++)
		{
			if (tmp[b] < tmp[a])
			{
				swap = tmp[a];
				tmp[a] = tmp[b];
				tmp[b] = swap;
			}
		}
	}
	mid = (s32)(n / 2U);
	if ((n % 2U) != 0U)
	{
		return tmp[(u32)mid];
	}
	return (tmp[(u32)(mid - 1)] + tmp[(u32)mid]) / 2U;
}

/* Aggregate many RTT/offset samples; drop outliers; median offset + max RTT for conservative lead */
static void syNetPeerPickClockSyncMedians(u32 sample_n, s64 *out_median_o, u32 *out_min_rtt_kept, u32 *out_rtt_for_lead,
                                          u32 *out_uncertainty_slack, u32 *out_fallback_extra_lead,
                                          u32 *out_kept_count, sb32 *out_used_fallback, s64 *out_offset_spread_ms)
{
	u32 all_rtt[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	s64 all_off[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	u32 prov_med_rtt;
	s64 prov_med_off;
	u64 rtt_bnd;
	u32 keep_idx[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	u32 nkeep;
	u32 i;
	u32 j;
	u32 a;
	u32 b;
	u32 r;
	s64 o;
	s64 offs_work[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	u32 rtts_work[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];
	s64 omin;
	s64 omax;
	s64 spread;
	u32 min_r;
	u32 max_r;

	if (out_offset_spread_ms != NULL)
	{
		*out_offset_spread_ms = 0;
	}
	if ((sample_n == 0U) || (sample_n > SYNETPEER_CLOCK_SYNC_SAMPLES_MAX))
	{
		sample_n = SYNETPEER_CLOCK_SYNC_SAMPLES_DEFAULT;
	}

	for (i = 0; i < sample_n; i++)
	{
		all_rtt[i] = sSYNetPeerClockRttSamples[i];
		all_off[i] = sSYNetPeerClockOffsetSamples[i];
	}
	prov_med_rtt = syNetPeerMedianU32Copy(all_rtt, sample_n);
	prov_med_off = syNetPeerMedianS64(all_off, sample_n);
	rtt_bnd = (u64)prov_med_rtt * (u64)SYNETPEER_CLOCK_OUTLIER_RTT_K_NUM + (u64)SYNETPEER_CLOCK_OUTLIER_RTT_C_MS;

	nkeep = 0U;
	for (i = 0U; i < sample_n; i++)
	{
		r = sSYNetPeerClockRttSamples[i];
		o = sSYNetPeerClockOffsetSamples[i];
		/* RTT==0: degenerate sample (host recv before send path); exclude from inlier set */
		if (r == 0U)
		{
			continue;
		}
		if ((u64)r > rtt_bnd)
		{
			continue;
		}
		if (syNetPeerAbsS64(o - prov_med_off) > SYNETPEER_CLOCK_OUTLIER_OFF_W_MS)
		{
			continue;
		}
		keep_idx[nkeep++] = i;
	}

	*out_used_fallback = FALSE;
	*out_fallback_extra_lead = 0U;
	if (nkeep < SYNETPEER_CLOCK_FILTER_MIN_KEEP)
	{
		nkeep = sample_n;
		for (i = 0U; i < nkeep; i++)
		{
			keep_idx[i] = i;
		}
		*out_used_fallback = TRUE;
		*out_fallback_extra_lead = SYNETPEER_CLOCK_FALLBACK_EXTRA_LEAD_MS;
#ifdef PORT
		port_log(
		    "SSB64 NetPeer: clock sync outlier filter kept <%u samples; using raw set + extra_lead=%u ms\n",
		    (unsigned int)SYNETPEER_CLOCK_FILTER_MIN_KEEP,
		    (unsigned int)SYNETPEER_CLOCK_FALLBACK_EXTRA_LEAD_MS);
#endif
	}

	for (j = 0U; j < nkeep; j++)
	{
		offs_work[j] = sSYNetPeerClockOffsetSamples[keep_idx[j]];
	}
	*out_median_o = syNetPeerMedianS64(offs_work, nkeep);

	for (j = 0U; j < nkeep; j++)
	{
		rtts_work[j] = sSYNetPeerClockRttSamples[keep_idx[j]];
	}
	for (a = 0U; a < nkeep; a++)
	{
		for (b = a + 1U; b < nkeep; b++)
		{
			if (rtts_work[b] < rtts_work[a])
			{
				u32 sw = rtts_work[a];
				rtts_work[a] = rtts_work[b];
				rtts_work[b] = sw;
			}
		}
	}
	if (nkeep == 0U)
	{
		*out_min_rtt_kept = 0U;
		*out_rtt_for_lead = 0U;
	}
	else
	{
		min_r = rtts_work[0];
		max_r = rtts_work[nkeep - 1U];
		*out_min_rtt_kept = min_r;
		/* Lead uses the worst kept RTT so late inliers do not trigger early release. */
		*out_rtt_for_lead = max_r;
	}

	*out_uncertainty_slack = 0U;
	if (nkeep >= 2U)
	{
		for (j = 0U; j < nkeep; j++)
		{
			offs_work[j] = sSYNetPeerClockOffsetSamples[keep_idx[j]];
		}
		for (a = 0U; a < nkeep; a++)
		{
			for (b = a + 1U; b < nkeep; b++)
			{
				if (offs_work[b] < offs_work[a])
				{
					s64 sws = offs_work[a];
					offs_work[a] = offs_work[b];
					offs_work[b] = sws;
				}
			}
		}
		omin = offs_work[0];
		omax = offs_work[nkeep - 1U];
		spread = omax - omin;
		if (out_offset_spread_ms != NULL)
		{
			*out_offset_spread_ms = spread;
		}
		if (spread > SYNETPEER_SYNC_OFFSET_SPREAD_THRESH_MS)
		{
			*out_uncertainty_slack = SYNETPEER_SYNC_UNCERTAINTY_SLACK_MS;
#ifdef PORT
			port_log("SSB64 NetPeer: clock sync offset spread=%lld ms -> uncertainty slack +%u ms\n",
			         (long long)spread, (unsigned int)SYNETPEER_SYNC_UNCERTAINTY_SLACK_MS);
#endif
		}
	}
	*out_kept_count = nkeep;
}

/* After all TIME_PONG samples: compute start_ms, arm host deadline, send first BATTLE_START_TIME */
static void syNetPeerHostFinishClockSyncAndSendStart(void)
{
	u64 now_ms;
	u64 start_ms;
	u64 start_ms_raw;
	u64 lead_ms;
	u64 half_rtt_plus;
	u32 max_rtt_diag;
	u32 min_rtt_kept;
	u32 i;
	s64 median_o;
	u32 rtt_for_lead;
	u32 uncertainty_slack;
	u32 fallback_extra;
	u32 kept_count;
	sb32 used_fallback;
	char *env_lead;
	s32 env_add;
	u32 gran_ms;

	s64 offset_spread_ms;
	s64 max_skew_contract;
	char *env_skew;
	u32 bump_total;

	max_rtt_diag = 0U;
	for (i = 0U; i < sSYNetPeerClockSyncTargetTotal; i++)
	{
		if (sSYNetPeerClockRttSamples[i] > max_rtt_diag)
		{
			max_rtt_diag = sSYNetPeerClockRttSamples[i];
		}
	}
	syNetPeerPickClockSyncMedians(sSYNetPeerClockSyncTargetTotal, &median_o, &min_rtt_kept, &rtt_for_lead, &uncertainty_slack,
	                              &fallback_extra, &kept_count, &used_fallback, &offset_spread_ms);
	max_skew_contract = 10;
	env_skew = getenv("SSB64_NETPLAY_BARRIER_MAX_CONTRACT_SKEW_MS");
	if ((env_skew != NULL) && (env_skew[0] != '\0'))
	{
		s32 vsk;

		vsk = atoi(env_skew);
		if (vsk > 0)
		{
			max_skew_contract = (s64)vsk;
		}
	}
	if (offset_spread_ms > max_skew_contract)
	{
		if (sSYNetPeerBarrierSkewRetryCount < SYNETPEER_BARRIER_SKEW_RETRY_MAX)
		{
			sSYNetPeerBarrierSkewRetryCount++;
			sSYNetPeerBarrierEpochExtraLeadMs += 30U;
			bump_total = sSYNetPeerClockSyncTargetBaseline + 4U * sSYNetPeerBarrierSkewRetryCount;
			if (bump_total > SYNETPEER_CLOCK_SYNC_SAMPLES_MAX)
			{
				bump_total = SYNETPEER_CLOCK_SYNC_SAMPLES_MAX;
			}
			sSYNetPeerClockSyncTargetTotal = bump_total;
			syNetPeerResetClockAlignState();
#ifdef PORT
			port_log(
			    "SSB64 NetPeer: barrier skew retry epoch=%u offset_spread=%lld ms max_contract=%lld -> extra_lead_ms=%u samples_total=%u\n",
			    (unsigned int)sSYNetPeerBarrierSkewRetryCount, (long long)offset_spread_ms, (long long)max_skew_contract,
			    (unsigned int)sSYNetPeerBarrierEpochExtraLeadMs, (unsigned int)sSYNetPeerClockSyncTargetTotal);
#endif
			return;
		}
#ifdef PORT
		port_log(
		    "SSB64 NetPeer: barrier skew WARN offset_spread=%lld ms exceeds max_contract=%lld after %u retries; latching start_time anyway\n",
		    (long long)offset_spread_ms, (long long)max_skew_contract, (unsigned int)SYNETPEER_BARRIER_SKEW_RETRY_MAX);
#endif
	}
	now_ms = syNetPeerNowUnixMs();
	lead_ms = (u64)SYNETPEER_MIN_START_LEAD_MS;
	half_rtt_plus = (u64)(rtt_for_lead / 2U) + (u64)SYNETPEER_START_MARGIN_MS;
	if (sSYNetPeerBarrierConservative != FALSE)
	{
		half_rtt_plus = (half_rtt_plus * 3U + 1U) / 2U;
		lead_ms += (u64)SYNETPEER_BARRIER_CONSERVATIVE_EXTRA_MS;
	}
	if (half_rtt_plus > lead_ms)
	{
		lead_ms = half_rtt_plus;
	}
	lead_ms += (u64)SYNETPEER_START_JITTER_SLACK_MS;
	lead_ms += (u64)uncertainty_slack;
	lead_ms += (u64)fallback_extra;
	lead_ms += (u64)sSYNetPeerBarrierEpochExtraLeadMs;
	env_lead = getenv("SSB64_NETPLAY_BARRIER_EXTRA_LEAD_MS");
	if (env_lead != NULL)
	{
		env_add = atoi(env_lead);
		if (env_add > 0)
		{
			lead_ms += (u64)env_add;
		}
	}
	start_ms_raw = now_ms + lead_ms;
	gran_ms = syNetPeerBarrierFrameGranularityMs();
	start_ms = (sSYNetPeerBarrierViAlign != FALSE) ? syNetPeerQuantizeCeilUnixMs(start_ms_raw, gran_ms) : start_ms_raw;
#ifdef PORT
	(void)mmServerBarrierTryApplyHostSchedule(&start_ms_raw, &start_ms);
#endif
	sSYNetPeerBattleStartUnixMs = start_ms;
	sSYNetPeerBattleStartOffsetMs = median_o;
	sSYNetPeerBarrierDeadlineUnixMs = start_ms;
	sSYNetPeerBarrierDeadlineValid = TRUE;
	syNetPeerSendBattleStartTimePacket(start_ms, median_o);
	sSYNetPeerBattleStartTimeSent = TRUE;
	/* Pretend START both ways so the legacy BATTLE_READY / ack path does not stall waiting for BATTLE_START control */
	sSYNetPeerBattleStartSent = TRUE;
	sSYNetPeerBattleStartReceived = TRUE;
	sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
	sSYNetPeerLastBarrierContractOffsetSpreadMs = offset_spread_ms;
	sSYNetPeerBarrierSkewRetriesLatchedForLog = sSYNetPeerBarrierSkewRetryCount;
	sSYNetPeerBarrierSkewRetryCount = 0U;
	sSYNetPeerBarrierEpochExtraLeadMs = 0U;
	sSYNetPeerClockSyncTargetTotal = sSYNetPeerClockSyncTargetBaseline;
#ifdef PORT
	if ((syNetSessionParamsAutoNegotiationEnabled() != FALSE) && (sSYNetPeerBootstrapIsEnabled != FALSE) &&
	    (syNetSessionParamsAreNegotiated() == FALSE))
	{
		sSYNetPeerSessionRttProbeTarget = 0U;
		syNetPeerHostFinalizeSessionParamsFromRtt(rtt_for_lead);
	}
	port_log(
	    "SSB64 NetPeer: barrier schedule host max_rtt=%u min_kept_rtt=%u lead_rtt=%u kept=%u fallback=%d median_o=%lld offset_spread=%lld start_ms=%llu start_ms_raw=%llu lead_wall_ms=%llu jitter=%u uns_sl=%u fall_extra=%u samples_target=%u vi_hz=%u gran_ms=%u vi_align=%d conservative=%d deadline_ms=%llu deadline_vi_ph=%u\n",
	    max_rtt_diag, min_rtt_kept, rtt_for_lead, kept_count, used_fallback != FALSE, (long long)median_o,
	    (long long)offset_spread_ms, (unsigned long long)start_ms, (unsigned long long)start_ms_raw,
	    (unsigned long long)lead_ms, (unsigned int)SYNETPEER_START_JITTER_SLACK_MS, uncertainty_slack, fallback_extra,
	    (unsigned int)sSYNetPeerClockSyncTargetTotal, (unsigned int)sSYNetPeerBarrierViHz,
	    (unsigned int)gran_ms, (sSYNetPeerBarrierViAlign != FALSE) ? 1 : 0,
	    (sSYNetPeerBarrierConservative != FALSE) ? 1 : 0, (unsigned long long)start_ms,
	    (unsigned int)syNetPeerBarrierDeadlineViPhaseBucket());
#endif
}

static sb32 syNetPeerCheckBarrierDeadlineReached(void)
{
	u64 now_ms;

	if (sSYNetPeerBarrierDeadlineValid == FALSE)
	{
		return FALSE;
	}
	now_ms = syNetPeerNowUnixMs();
	return (now_ms >= sSYNetPeerBarrierDeadlineUnixMs) ? TRUE : FALSE;
}

static void syNetPeerLogClockSyncSampleDone(u32 seq, s64 o_ms, u32 rtt_ms)
{
	port_log("SSB64 NetPeer: clock_sync_sample role=host seq=%u offset_ms=%lld rtt_ms=%u\n", (unsigned int)seq,
	         (long long)o_ms, (unsigned int)rtt_ms);
}

static void syNetPeerLogTickFrameSnapshot(const char *tag, sb32 gated_by_tick_diag)
{
	u32 vi_deadline_ph;
	u64 deadline_ms_disp;

	if ((gated_by_tick_diag != FALSE) && (syNetPeerTickDiagLevel() < 1))
	{
		return;
	}
	vi_deadline_ph = syNetPeerBarrierDeadlineViPhaseBucket();
	deadline_ms_disp = (sSYNetPeerBarrierDeadlineValid != FALSE) ? sSYNetPeerBarrierDeadlineUnixMs : 0ULL;
	port_log(
	    "SSB64 NetPeer: tick_diag tag=%s role=%s sim_tick=%u push=%d tm_up=%u tm_fr=%u scene=%u exec_rdy=%d bar_rel=%d unix_ms=%llu deadline_valid=%d deadline_ms=%llu deadline_vi_ph=%u delay=%u hr=%u late=%u\n",
	    tag, (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", syNetInputGetTick(), port_get_push_frame_count(),
	    dSYTaskmanUpdateCount, dSYTaskmanFrameCount, (unsigned int)(u32)gSCManagerSceneData.scene_curr,
	    (syNetPeerCheckBattleExecutionReady() != FALSE) ? 1 : 0, (sSYNetPeerBattleBarrierReleased != FALSE) ? 1 : 0,
	    (unsigned long long)syNetPeerNowUnixMs(), (sSYNetPeerBarrierDeadlineValid != FALSE) ? 1 : 0,
	    (unsigned long long)deadline_ms_disp, (unsigned int)vi_deadline_ph, (unsigned int)sSYNetPeerInputDelay,
	    (unsigned int)sSYNetPeerHighestRemoteTick, (unsigned int)sSYNetPeerLateFrames);
}

static void syNetPeerHandleTimePingPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u32 seq;
	u64 h0;
	u64 c1;
	u64 c2;
	u32 checksum;
	u32 expected_checksum;

	if (size != SYNETPEER_TIME_PING_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_TIME_PING_BYTES - 4);
	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	seq = syNetPeerReadU32(&cursor);
	h0 = syNetPeerReadU64(&cursor);
	checksum = syNetPeerReadU32(&cursor);
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_TIME_PING) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		return;
	}
	/* NTP-style t2/t3: client recv time, then client tx time (second sample right before send). */
	c1 = syNetPeerNowUnixMs();
	c2 = syNetPeerNowUnixMs();
	syNetPeerSendTimePongPacket(seq, h0, c1, c2);
}

static void syNetPeerHandleTimePongPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u32 seq;
	u64 h0;
	u64 c1;
	u64 c2;
	u64 h3;
	u32 checksum;
	u32 expected_checksum;
	s64 o_sample;
	s64 rtt_sample;
	u32 rtt_ms;

	if (size != SYNETPEER_TIME_PONG_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_TIME_PONG_BYTES - 4);
	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	seq = syNetPeerReadU32(&cursor);
	h0 = syNetPeerReadU64(&cursor);
	c1 = syNetPeerReadU64(&cursor);
	c2 = syNetPeerReadU64(&cursor);
	checksum = syNetPeerReadU32(&cursor);
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_TIME_PONG) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
	if (sSYNetPeerClockSyncSampleCount >= sSYNetPeerClockSyncTargetTotal)
	{
		return;
	}
	if ((seq != sSYNetPeerClockSyncSampleCount) && (syNetSessionParamsAutoNegotiationEnabled() == FALSE))
	{
		return;
	}
	h3 = syNetPeerNowUnixMs();
	o_sample = ((s64)h0 - (s64)c1 + (s64)h3 - (s64)c2) / 2;
	rtt_sample = ((s64)h3 - (s64)h0) - ((s64)c2 - (s64)c1);
	if (rtt_sample > 0)
	{
		rtt_ms = (u32)rtt_sample;
	}
	else
	{
		rtt_ms = 0;
	}
	if ((syNetSessionParamsAutoNegotiationEnabled() != FALSE) && (syNetSessionParamsAreNegotiated() == FALSE) &&
	    (sSYNetPeerSessionRttProbeTarget > 0U) && (sSYNetPeerSessionRttProbeSampleCount < sSYNetPeerSessionRttProbeTarget) &&
	    (seq == sSYNetPeerSessionRttProbeSampleCount) && (h0 == sSYNetPeerSessionRttProbePingT0))
	{
		syNetPeerSessionRttProbeOnPong(seq, h0, rtt_ms);
		return;
	}
	if ((seq != sSYNetPeerClockSyncSampleCount) || (h0 != sSYNetPeerTimePingT0Sent))
	{
		return;
	}
	sSYNetPeerClockOffsetSamples[sSYNetPeerClockSyncSampleCount] = o_sample;
	sSYNetPeerClockRttSamples[sSYNetPeerClockSyncSampleCount] = rtt_ms;
	syNetPeerLogClockSyncSampleDone(sSYNetPeerClockSyncSampleCount, o_sample, rtt_ms);
	sSYNetPeerTimePingAwaitingAck = FALSE;
	sSYNetPeerClockSyncSampleCount++;
	if (sSYNetPeerClockSyncSampleCount >= sSYNetPeerClockSyncTargetTotal)
	{
		syNetPeerHostFinishClockSyncAndSendStart();
	}
}

/*
 * Guest-only: installs the host's wall-clock start instant and offset estimate.
 * Extended payloads carry host-authoritative VI hz + align (guest aligns `gran_ms` / quantization to host).
 * The barrier deadline must be latched once — duplicates are ignored earlier in this function.
 */
static void syNetPeerHandleBattleStartTimePacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u64 start_ms;
	u64 offset_u;
	s64 offset_ms;
	u64 raw_deadline_ms;
	u64 deadline_ms;
	u64 now_ms;
	u32 gran_ms;
	u32 checksum;
	u32 expected_checksum;
	sb32 uses_extended;
	u32 wire_vi_hz;
	u32 wire_vi_align;

	if ((size != (s32)SYNETPEER_BATTLE_START_TIME_BYTES_LEGACY) && (size != (s32)SYNETPEER_BATTLE_START_TIME_BYTES))
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, (u32)size - 4U);
	uses_extended = (size == (s32)SYNETPEER_BATTLE_START_TIME_BYTES) ? TRUE : FALSE;
	wire_vi_hz = 0U;
	wire_vi_align = 0U;
	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	start_ms = syNetPeerReadU64(&cursor);
	offset_u = syNetPeerReadU64(&cursor);
	if (uses_extended != FALSE)
	{
		wire_vi_hz = syNetPeerReadU32(&cursor);
		wire_vi_align = syNetPeerReadU32(&cursor);
	}
	checksum = syNetPeerReadU32(&cursor);
	offset_ms = (s64)offset_u;
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_BATTLE_START_TIME) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		return;
	}
	/* Host re-sends identical BATTLE_START_TIME for UDP loss (see syNetPeerUpdate repeat counter).
	 * Only the first decode may set barrier_deadline — re-applying clamps would slide the deadline forward every frame.
	 */
	if ((sSYNetPeerBattleStartTimeReceived != FALSE) && (sSYNetPeerBattleStartUnixMs == start_ms) &&
	    (sSYNetPeerBattleStartOffsetMs == offset_ms))
	{
		if ((uses_extended != FALSE) && (sSYNetPeerBattleStartViWireUsesExtended != FALSE))
		{
			if ((wire_vi_hz != sSYNetPeerBattleStartViWireHz) || (wire_vi_align != sSYNetPeerBattleStartViWireAlign))
			{
#ifdef PORT
				if (sSYNetPeerBattleStartTimeConflictLogged == FALSE)
				{
					sSYNetPeerBattleStartTimeConflictLogged = TRUE;
					port_log(
					    "SSB64 NetPeer: barrier schedule client ignore conflicting VI contract (keep latched start_ms=%llu off=%lld vi_hz=%u vi_align=%u) got vi_hz=%u vi_align=%u\n",
					    (unsigned long long)sSYNetPeerBattleStartUnixMs, (long long)sSYNetPeerBattleStartOffsetMs,
					    (unsigned int)sSYNetPeerBattleStartViWireHz,
					    (unsigned int)sSYNetPeerBattleStartViWireAlign, (unsigned int)wire_vi_hz,
					    (unsigned int)wire_vi_align);
				}
#endif
				return;
			}
		}
		else if ((uses_extended != FALSE) != (sSYNetPeerBattleStartViWireUsesExtended != FALSE))
		{
#ifdef PORT
			if (sSYNetPeerBattleStartTimeConflictLogged == FALSE)
			{
				sSYNetPeerBattleStartTimeConflictLogged = TRUE;
				port_log(
				    "SSB64 NetPeer: barrier schedule client ignore conflicting start_time layout (latched extended=%d got extended=%d) start_ms=%llu\n",
				    (sSYNetPeerBattleStartViWireUsesExtended != FALSE) ? 1 : 0, (uses_extended != FALSE) ? 1 : 0,
				    (unsigned long long)start_ms);
			}
#endif
			return;
		}
#ifdef PORT
		if (sSYNetPeerBattleStartTimeDupLogOnce == FALSE)
		{
			sSYNetPeerBattleStartTimeDupLogOnce = TRUE;
			port_log(
			    "SSB64 NetPeer: barrier schedule client ignore duplicate start_time (latched deadline) start_ms=%llu off=%lld\n",
			    (unsigned long long)start_ms, (long long)offset_ms);
		}
#endif
		return;
	}
	if ((sSYNetPeerBattleStartTimeReceived != FALSE) &&
	    ((sSYNetPeerBattleStartUnixMs != start_ms) || (sSYNetPeerBattleStartOffsetMs != offset_ms)))
	{
#ifdef PORT
		if (sSYNetPeerBattleStartTimeConflictLogged == FALSE)
		{
			sSYNetPeerBattleStartTimeConflictLogged = TRUE;
			port_log(
			    "SSB64 NetPeer: barrier schedule client ignore conflicting start_time (keep latched start_ms=%llu off=%lld) got start_ms=%llu off=%lld\n",
			    (unsigned long long)sSYNetPeerBattleStartUnixMs, (long long)sSYNetPeerBattleStartOffsetMs,
			    (unsigned long long)start_ms, (long long)offset_ms);
		}
#endif
		return;
	}
	if (uses_extended != FALSE)
	{
		syNetPeerBarrierViApplyContractFromHost(wire_vi_hz, wire_vi_align);
	}
	sSYNetPeerBattleStartUnixMs = start_ms;
	sSYNetPeerBattleStartOffsetMs = offset_ms;
	sSYNetPeerBattleStartViWireUsesExtended = uses_extended;
	if (uses_extended != FALSE)
	{
		sSYNetPeerBattleStartViWireHz = wire_vi_hz;
		sSYNetPeerBattleStartViWireAlign = wire_vi_align;
	}
	else
	{
		sSYNetPeerBattleStartViWireHz = 0U;
		sSYNetPeerBattleStartViWireAlign = 0U;
	}
	raw_deadline_ms = (u64)((s64)start_ms - offset_ms);
	now_ms = syNetPeerNowUnixMs();
	deadline_ms = raw_deadline_ms;
	if (deadline_ms < now_ms)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetPeer: barrier client deadline in past raw_deadline=%llu now=%llu start_ms=%llu off=%lld -> clamp now+%u ms\n",
		    (unsigned long long)raw_deadline_ms, (unsigned long long)now_ms, (unsigned long long)start_ms,
		    (long long)offset_ms, (unsigned int)SYNETPEER_DEADLINE_PAST_SLACK_MS);
#endif
		deadline_ms = now_ms + (u64)SYNETPEER_DEADLINE_PAST_SLACK_MS;
	}
	gran_ms = syNetPeerBarrierFrameGranularityMs();
	if (sSYNetPeerBarrierViAlign != FALSE)
	{
		deadline_ms = syNetPeerQuantizeCeilUnixMs(deadline_ms, gran_ms);
	}
	sSYNetPeerBarrierDeadlineUnixMs = deadline_ms;
	sSYNetPeerBarrierDeadlineValid = TRUE;
	sSYNetPeerBattleStartTimeReceived = TRUE;
	sSYNetPeerBattleStartReceived = TRUE;
	sSYNetPeerBattleStartSent = TRUE;
	sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
#ifdef PORT
	port_log(
	    "SSB64 NetPeer: barrier schedule client start_ms=%llu off=%lld deadline_ms=%llu now=%llu gran_ms=%u vi_hz=%u vi_align=%d host_contract=%d deadline_vi_ph=%u\n",
	    (unsigned long long)start_ms, (long long)offset_ms, (unsigned long long)deadline_ms,
	    (unsigned long long)now_ms, (unsigned int)gran_ms, (unsigned int)sSYNetPeerBarrierViHz,
	    (sSYNetPeerBarrierViAlign != FALSE) ? 1 : 0, (uses_extended != FALSE) ? 1 : 0,
	    (unsigned int)syNetPeerBarrierDeadlineViPhaseBucket());
#endif
}

#endif

static sb32 syNetPeerApplyInputSlotsFromMetadata(const SYNetInputReplayMetadata *m);

void syNetPeerMakeBootstrapMetadata(SYNetInputReplayMetadata *metadata)
{
	s32 player;

	memset(metadata, 0, sizeof(*metadata));
	metadata->magic = SYNETINPUT_REPLAY_MAGIC;
	metadata->version = SYNETINPUT_REPLAY_VERSION;
	metadata->scene_kind = nSCKindVSBattle;
	metadata->player_count = 2;
	metadata->stage_kind = nGRKindCastle;
	metadata->stocks = 3;
	metadata->time_limit = SCBATTLE_TIMELIMIT_INFINITE;
	metadata->item_switch = nSCBattleItemSwitchNone;
	metadata->item_toggles = 0;
	metadata->rng_seed = sSYNetPeerBootstrapSeed;
	metadata->game_type = nSCBattleGameTypeRoyal;
	metadata->game_rules = SCBATTLE_GAMERULE_STOCK;
	metadata->is_team_battle = FALSE;
	metadata->handicap = nSCBattleHandicapOff;
	metadata->is_team_attack = FALSE;
	metadata->is_stage_select = FALSE;
	metadata->damage_ratio = 100;
	metadata->item_appearance_rate = nSCBattleItemSwitchNone;
	metadata->is_not_teamshadows = TRUE;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		metadata->player_kinds[player] = nFTPlayerKindNot;
		metadata->fighter_kinds[player] = nFTKindNull;
		metadata->costumes[player] = 0;
		metadata->teams[player] = player;
		metadata->handicaps[player] = 9;
		metadata->levels[player] = 3;
		metadata->shades[player] = 0;
	}
	metadata->player_kinds[0] = nFTPlayerKindMan;
	metadata->fighter_kinds[0] = nFTKindMario;
	metadata->player_kinds[1] = nFTPlayerKindMan;
	metadata->fighter_kinds[1] = nFTKindFox;
	metadata->netplay_sim_slot_host_hw = 0U;
	metadata->netplay_sim_slot_client_hw = 1U;
}

static void syNetPeerStageBootstrapMetadata(const SYNetInputReplayMetadata *metadata)
{
#ifdef PORT
	if ((sSYNetPeerIsConfigured != FALSE) && (syNetPeerApplyInputSlotsFromMetadata(metadata) == FALSE))
	{
		port_log("SSB64 NetPeer: refusing bootstrap metadata (invalid netplay input slot binding)\n");
		return;
	}
#endif
	sSYNetPeerBootstrapMetadata = *metadata;
	sSYNetPeerBootstrapMetadataStaged = TRUE;
}

static void syNetPeerCommitStagedBootstrapMetadataNow(sb32 ignore_barrier_guard)
{
	if (sSYNetPeerBootstrapMetadataApplied != FALSE)
	{
		sSYNetPeerBootstrapMetadataStaged = FALSE;
		return;
	}
	if (sSYNetPeerBootstrapMetadataStaged == FALSE)
	{
		return;
	}
	if ((ignore_barrier_guard == FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE) &&
	    (sSYNetPeerBattleBarrierReleased == FALSE))
	{
		return;
	}
	syNetReplayApplyBattleMetadata(&sSYNetPeerBootstrapMetadata);
	syUtilsSetRandomSeed(sSYNetPeerBootstrapMetadata.rng_seed);
	gSCManagerSceneData.scene_prev = nSCKindVSMode;
#if defined(SSB64_NETMENU) && defined(PORT)
	if (gSYNetPeerSuppressBootstrapSceneAdvance != FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetPeer: bootstrap metadata staged (suppress scene_curr) host=%d stage=%u seed=%u players=%u "
		    "p0=%u/%u p1=%u/%u\n",
		    sSYNetPeerBootstrapIsHost, sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed,
		    sSYNetPeerBootstrapMetadata.player_count, sSYNetPeerBootstrapMetadata.player_kinds[0],
		    sSYNetPeerBootstrapMetadata.fighter_kinds[0], sSYNetPeerBootstrapMetadata.player_kinds[1],
		    sSYNetPeerBootstrapMetadata.fighter_kinds[1]);
#endif
	}
	else
	{
		gSCManagerSceneData.scene_curr = nSCKindVSBattle;
#ifdef PORT
		port_log(
		    "SSB64 NetPeer: bootstrap metadata applied host=%d stage=%u seed=%u players=%u p0=%u/%u p1=%u/%u scene->VSBattle\n",
		    sSYNetPeerBootstrapIsHost, sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed,
		    sSYNetPeerBootstrapMetadata.player_count, sSYNetPeerBootstrapMetadata.player_kinds[0],
		    sSYNetPeerBootstrapMetadata.fighter_kinds[0], sSYNetPeerBootstrapMetadata.player_kinds[1],
		    sSYNetPeerBootstrapMetadata.fighter_kinds[1]);
#endif
	}
#else
	gSCManagerSceneData.scene_curr = nSCKindVSBattle;
#ifdef PORT
	port_log("SSB64 NetPeer: bootstrap metadata applied host=%d stage=%u seed=%u players=%u p0=%u/%u p1=%u/%u\n",
	         sSYNetPeerBootstrapIsHost, sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed,
	         sSYNetPeerBootstrapMetadata.player_count, sSYNetPeerBootstrapMetadata.player_kinds[0],
	         sSYNetPeerBootstrapMetadata.fighter_kinds[0], sSYNetPeerBootstrapMetadata.player_kinds[1],
	         sSYNetPeerBootstrapMetadata.fighter_kinds[1]);
#endif
#endif
	sSYNetPeerBootstrapMetadataApplied = TRUE;
	sSYNetPeerBootstrapMetadataStaged = FALSE;
}

void syNetPeerApplyBootstrapMetadata(const SYNetInputReplayMetadata *metadata)
{
	syNetPeerStageBootstrapMetadata(metadata);
	syNetPeerCommitStagedBootstrapMetadataNow(TRUE);
}

#ifdef PORT
void syNetPeerCommitStagedBootstrapMetadataForBattleStart(void)
{
	/* StartBattle runs before the VS tick loop; barrier only freezes updates. Must not wait on barrier. */
	syNetPeerCommitStagedBootstrapMetadataNow(TRUE);
}
#endif

#if defined(PORT)
void syNetPeerSendBytes(const u8 *buffer, u32 size)
{
	if ((buffer == NULL) || (size == 0U) || (syNetPeerDatagramSocketIsUsable() == FALSE))
	{
		return;
	}
	(void)syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)size, &sSYNetPeerPeerAddress);
}
#endif

#ifdef PORT
/*
 * Queue host delay changes at local_sim + commit lead (same contract as INPUT_DELAY_SYNC; see `syNetPeerDelaySyncCommitLeadTicks`).
 * Committed delay used for wire tagging (`GatherHistoryBundle`) advances only in `syNetPeerApplyHostDelayRampPending`.
 */
static void syNetPeerMaybeAdaptInputDelay(u32 tick_now)
{
	u32 late;
	u32 lf;
	u32 d_late;
	u32 d_lf;

	if ((sSYNetPeerAdaptiveDelayEnabled == FALSE) || (sSYNetPeerIsActive == FALSE))
	{
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
#if defined(PORT)
	if (sSYNetPeerDelaySyncPendingValid != FALSE)
	{
		return;
	}
	if (sSYNetPeerHostDelayRampPendingValid != FALSE)
	{
		return;
	}
#endif
	late = sSYNetPeerLateFrames;
	lf = syNetRollbackGetLoadFailCount();
	if (sSYNetPeerAdaptivePrimed == FALSE)
	{
		sSYNetPeerAdaptivePrimed = TRUE;
		sSYNetPeerAdaptivePrevLateFrames = late;
		sSYNetPeerAdaptivePrevLoadFail = lf;
		return;
	}
	d_late = late - sSYNetPeerAdaptivePrevLateFrames;
	d_lf = lf - sSYNetPeerAdaptivePrevLoadFail;
	sSYNetPeerAdaptivePrevLateFrames = late;
	sSYNetPeerAdaptivePrevLoadFail = lf;

	if ((d_lf > 0U) || (d_late > 8U) || ((late >= 24U) && (d_late > 0U)))
	{
		sSYNetPeerAdaptiveStableIntervals = 0;
		/*
		 * Rollback-first matches: do not ramp committed D on late frames — prediction + resim mask spikes;
		 * raising D only adds input lag without engaging rollback.
		 */
		if ((syNetSessionParamsAreNegotiated() != FALSE) && (syNetSessionParamsRollbackEnabled() != FALSE))
		{
			return;
		}
		if (sSYNetPeerInputDelay < sSYNetPeerInputDelayCeil)
		{
#if defined(PORT)
			u32 proposed;

			proposed = syNetPeerClampInputDelayToContract(sSYNetPeerInputDelay + 1U);
			sSYNetPeerHostDelayRampTarget = proposed;
			sSYNetPeerHostDelayRampEffectiveTick =
			    syNetPeerSaturatingAddU32(tick_now, syNetPeerDelaySyncCommitLeadTicks());
			sSYNetPeerHostDelayRampPendingValid = TRUE;
			port_log("SSB64 NetPeer: adaptive delay up (queued) -> %u eff_tick=%u (late_delta=%u lf_delta=%u ceil=%u "
			         "late=%u)\n",
			         proposed, sSYNetPeerHostDelayRampEffectiveTick, d_late, d_lf, sSYNetPeerInputDelayCeil, late);
#else
			sSYNetPeerInputDelay++;
			port_log("SSB64 NetPeer: adaptive delay up -> %u (late_delta=%u lf_delta=%u ceil=%u late=%u)\n",
			         sSYNetPeerInputDelay, d_late, d_lf, sSYNetPeerInputDelayCeil, late);
#endif
		}
	}
	else
	{
		sSYNetPeerAdaptiveStableIntervals++;
		if (sSYNetPeerAdaptiveStableIntervals >= 5U)
		{
			sSYNetPeerAdaptiveStableIntervals = 0;
			if (sSYNetPeerInputDelay > sSYNetPeerInputDelayFloor)
			{
#if defined(PORT)
				u32 proposed;

				proposed = syNetPeerClampInputDelayToContract(sSYNetPeerInputDelay - 1U);
				sSYNetPeerHostDelayRampTarget = proposed;
				sSYNetPeerHostDelayRampEffectiveTick =
				    syNetPeerSaturatingAddU32(tick_now, syNetPeerDelaySyncCommitLeadTicks());
				sSYNetPeerHostDelayRampPendingValid = TRUE;
				port_log("SSB64 NetPeer: adaptive delay down (queued) -> %u eff_tick=%u floor=%u\n", proposed,
				         sSYNetPeerHostDelayRampEffectiveTick, sSYNetPeerInputDelayFloor);
#else
				sSYNetPeerInputDelay--;
				port_log("SSB64 NetPeer: adaptive delay down -> %u floor=%u\n", sSYNetPeerInputDelay,
				         sSYNetPeerInputDelayFloor);
#endif
			}
		}
	}
}
#endif

#if defined(PORT)
/*
 * `effective_tick` on the wire is host-local metadata only; guests must not compare it to their own
 * `syNetInputGetTick()`. Both sides schedule commits at local_sim + `syNetPeerDelaySyncCommitLeadTicks()`.
 */
static u32 syNetPeerDelaySyncLocalEffectiveApplyTick(void)
{
	return syNetPeerSaturatingAddU32(syNetInputGetTick(), syNetPeerDelaySyncCommitLeadTicks());
}

static void syNetPeerSendInputDelaySyncPacket(u32 delay, u32 effective_tick)
{
	u8 buffer[SYNETPEER_INPUT_DELAY_SYNC_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_INPUT_DELAY_SYNC);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, delay);
	syNetPeerWriteU32(&cursor, effective_tick);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	if (syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)SYNETPEER_INPUT_DELAY_SYNC_BYTES, &sSYNetPeerPeerAddress) ==
	    (int)SYNETPEER_INPUT_DELAY_SYNC_BYTES)
	{
		sSYNetPeerPacketsSent++;
	}
}

static void syNetPeerHandleInputDelaySyncPacket(const u8 *buffer, s32 size)
{
	const u8 *c = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u32 delay_wire;
	u32 effective_tick;
	u32 checksum;
	u32 expected_checksum;

	if (size != (s32)SYNETPEER_INPUT_DELAY_SYNC_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_INPUT_DELAY_SYNC_BYTES - 4U);
	magic = syNetPeerReadU32(&c);
	version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	delay_wire = syNetPeerReadU32(&c);
	effective_tick = syNetPeerReadU32(&c);
	checksum = syNetPeerReadU32(&c);
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_INPUT_DELAY_SYNC) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		return;
	}
	sSYNetPeerPacketsReceived++;
	delay_wire = syNetPeerClampInputDelayToContract(delay_wire);
	(void)effective_tick;
	{
		u32 local_eff;

		local_eff = syNetPeerDelaySyncLocalEffectiveApplyTick();
		sSYNetPeerDelaySyncPending = delay_wire;
		sSYNetPeerDelaySyncEffectiveTick = local_eff;
		sSYNetPeerDelaySyncPendingValid = TRUE;
	}
}

#if defined(PORT)
static u32 syNetPeerSessionRttMedianMs(void)
{
	u32 i;
	u32 n;
	u32 tmp;
	u32 sorted[SYNETPEER_CLOCK_SYNC_SAMPLES_MAX];

	n = sSYNetPeerSessionRttProbeSampleCount;
	if (n == 0U)
	{
		return 0U;
	}
	for (i = 0U; i < n; i++)
	{
		sorted[i] = sSYNetPeerSessionRttProbeRttMs[i];
	}
	for (i = 0U; i < n; i++)
	{
		u32 j;

		for (j = i + 1U; j < n; j++)
		{
			if (sorted[j] < sorted[i])
			{
				tmp = sorted[i];
				sorted[i] = sorted[j];
				sorted[j] = tmp;
			}
		}
	}
	return sorted[n / 2U];
}

static void syNetPeerSessionParamsResetTransport(void)
{
	sSYNetPeerSessionParamsHostSent = FALSE;
	sSYNetPeerSessionParamsHostGotAck = FALSE;
	memset(&sSYNetPeerSessionParamsHostProposal, 0, sizeof(sSYNetPeerSessionParamsHostProposal));
	sSYNetPeerSessionRttProbeTarget = 0U;
	sSYNetPeerSessionRttProbeSampleCount = 0U;
	sSYNetPeerSessionRttProbeAwaitingAck = FALSE;
	sSYNetPeerSessionRttProbePingT0 = 0ULL;
	memset(sSYNetPeerSessionRttProbeRttMs, 0, sizeof(sSYNetPeerSessionRttProbeRttMs));
}

static void syNetPeerHostFinalizeSessionParamsFromRtt(u32 rtt_ms)
{
	SYNetSessionParams params;

	if (syNetSessionParamsAreNegotiated() != FALSE)
	{
		return;
	}
	if (rtt_ms == 0U)
	{
		rtt_ms = 32U;
	}
	syNetSessionParamsComputeFromRttMs(rtt_ms, &params);
	sSYNetPeerSessionParamsHostProposal = params;
	syNetSessionParamsApplyNegotiated(&params, "host_compute");
	sSYNetPeerStartupMatchDelayTarget = (u32)params.input_delay;
	sSYNetPeerStartupMatchDelayPendingValid = TRUE;
	sSYNetPeerSessionParamsHostSent = TRUE;
	syNetPeerSendSessionParamsPacket(&params, SYNETPEER_PACKET_SESSION_PARAMS);
	port_log(
	    "SSB64 NetPeer: session_params host propose rtt_ms=%u D=%u phase_lock=%u redundancy=%u pumps=%u fuzz=%u "
	    "rb_snap=%u rb_resim=%u rb_flags=0x%02X\n",
	    (unsigned int)rtt_ms, (unsigned int)params.input_delay, (unsigned int)params.phase_lock_ticks,
	    (unsigned int)params.bundle_redundancy, (unsigned int)params.ingress_extra_pumps,
	    (unsigned int)params.strict_ring_fuzz_ticks, (unsigned int)params.rollback_snapshot_frames,
	    (unsigned int)params.rollback_resim_ticks_per_frame, (unsigned int)params.rollback_flags);
}

static void syNetPeerHostFinalizeSessionParamsFromRttProbe(void)
{
	syNetPeerHostFinalizeSessionParamsFromRtt(syNetPeerSessionRttMedianMs());
}

static void syNetPeerSessionRttProbeOnPong(u32 seq, u64 h0, u32 rtt_ms)
{
	(void)seq;
	(void)h0;

	if (sSYNetPeerSessionRttProbeSampleCount >= SYNETPEER_SESSION_RTT_PROBE_SAMPLES)
	{
		return;
	}
	sSYNetPeerSessionRttProbeRttMs[sSYNetPeerSessionRttProbeSampleCount] = rtt_ms;
	sSYNetPeerSessionRttProbeSampleCount++;
	sSYNetPeerSessionRttProbeAwaitingAck = FALSE;
	port_log("SSB64 NetPeer: session_rtt_probe sample=%u/%u rtt_ms=%u\n",
	         (unsigned int)sSYNetPeerSessionRttProbeSampleCount, (unsigned int)sSYNetPeerSessionRttProbeTarget,
	         (unsigned int)rtt_ms);
	if (sSYNetPeerSessionRttProbeSampleCount >= sSYNetPeerSessionRttProbeTarget)
	{
		syNetPeerHostFinalizeSessionParamsFromRttProbe();
	}
}

static void syNetPeerSendSessionParamsPacket(const SYNetSessionParams *params, u16 packet_type)
{
	u8 buffer[SYNETPEER_SESSION_PARAMS_WIRE_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	if (params == NULL)
	{
		return;
	}
	memset(buffer, 0, sizeof(buffer));
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, packet_type);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, params->rtt_ms);
	syNetPeerWriteU8(&cursor, params->input_delay);
	syNetPeerWriteU8(&cursor, params->phase_lock_ticks);
	syNetPeerWriteU8(&cursor, params->bundle_redundancy);
	syNetPeerWriteU8(&cursor, params->ingress_extra_pumps);
	syNetPeerWriteU8(&cursor, params->delay_adaptive_headroom);
	syNetPeerWriteU8(&cursor, params->rollback_snapshot_frames);
	syNetPeerWriteU8(&cursor, params->rollback_resim_ticks_per_frame);
	syNetPeerWriteU8(&cursor, params->strict_ring_fuzz_ticks);
	syNetPeerWriteU8(&cursor, params->rollback_flags);
	checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_SESSION_PARAMS_WIRE_BYTES - 4U);
	syNetPeerWriteU32(&cursor, checksum);
	if (syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)SYNETPEER_SESSION_PARAMS_WIRE_BYTES, &sSYNetPeerPeerAddress) ==
	    (int)SYNETPEER_SESSION_PARAMS_WIRE_BYTES)
	{
		sSYNetPeerPacketsSent++;
	}
}

static void syNetPeerHandleSessionParamsPacket(const u8 *buffer, s32 size, u16 expected_type)
{
	const u8 *c = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	SYNetSessionParams params;
	u32 checksum;
	u32 expected_checksum;

	if (size != (s32)SYNETPEER_SESSION_PARAMS_WIRE_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_SESSION_PARAMS_WIRE_BYTES - 4U);
	magic = syNetPeerReadU32(&c);
	version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	params.rtt_ms = syNetPeerReadU32(&c);
	params.input_delay = (u8)syNetPeerReadU8(&c);
	params.phase_lock_ticks = (u8)syNetPeerReadU8(&c);
	params.bundle_redundancy = (u8)syNetPeerReadU8(&c);
	params.ingress_extra_pumps = (u8)syNetPeerReadU8(&c);
	params.delay_adaptive_headroom = (u8)syNetPeerReadU8(&c);
	params.rollback_snapshot_frames = (u8)syNetPeerReadU8(&c);
	params.rollback_resim_ticks_per_frame = (u8)syNetPeerReadU8(&c);
	params.strict_ring_fuzz_ticks = (u8)syNetPeerReadU8(&c);
	params.rollback_flags = (u8)syNetPeerReadU8(&c);
	checksum = syNetPeerReadU32(&c);
	params.version = SYNETSESSION_PARAMS_WIRE_VERSION;
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) || (packet_type != expected_type) ||
	    (session_id != sSYNetPeerSessionID) || (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		port_log(
		    "SSB64 NetPeer: session_params drop type=%u magic_ok=%d ver_ok=%d sess_ok=%d csum_ok=%d\n",
		    (unsigned int)expected_type, (magic == SYNETPEER_MAGIC) ? 1 : 0, (version == SYNETPEER_VERSION) ? 1 : 0,
		    (session_id == sSYNetPeerSessionID) ? 1 : 0, (checksum == expected_checksum) ? 1 : 0);
		return;
	}
	sSYNetPeerPacketsReceived++;
	if (expected_type == SYNETPEER_PACKET_SESSION_PARAMS)
	{
		if (sSYNetPeerBootstrapIsHost != FALSE)
		{
			return;
		}
		if (syNetSessionParamsAreNegotiated() == FALSE)
		{
			syNetSessionParamsApplyNegotiated(&params, "guest_recv");
			sSYNetPeerStartupMatchDelayTarget = (u32)params.input_delay;
			sSYNetPeerStartupMatchDelayPendingValid = TRUE;
		}
		syNetPeerSendSessionParamsPacket(&params, SYNETPEER_PACKET_SESSION_PARAMS_ACK);
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
	if (sSYNetPeerSessionParamsHostGotAck == FALSE)
	{
		port_log("SSB64 NetPeer: session_params host_recv_ack rtt_ms=%u D=%u\n", (unsigned int)params.rtt_ms,
		         (unsigned int)params.input_delay);
	}
	sSYNetPeerSessionParamsHostGotAck = TRUE;
}

static void syNetPeerSessionParamsServiceNegotiation(void)
{
	if ((syNetSessionParamsAutoNegotiationEnabled() == FALSE) || (sSYNetPeerIsActive == FALSE) ||
	    (sSYNetPeerBootstrapIsEnabled == FALSE))
	{
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		if (sSYNetPeerSessionParamsHostGotAck != FALSE)
		{
			return;
		}
	}
	else if (syNetSessionParamsAreNegotiated() != FALSE)
	{
		return;
	}
	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		if (sSYNetPeerSessionRttProbeTarget == 0U)
		{
			sSYNetPeerSessionRttProbeTarget = SYNETPEER_SESSION_RTT_PROBE_SAMPLES;
		}
		if (sSYNetPeerSessionRttProbeSampleCount < sSYNetPeerSessionRttProbeTarget)
		{
			if (sSYNetPeerSessionRttProbeAwaitingAck == FALSE)
			{
				sSYNetPeerSessionRttProbePingT0 = syNetPeerNowUnixMs();
				syNetPeerSendTimePingPacket(sSYNetPeerSessionRttProbeSampleCount, sSYNetPeerSessionRttProbePingT0);
				sSYNetPeerSessionRttProbeAwaitingAck = TRUE;
			}
			else if ((dSYTaskmanFrameCount % 8U) == 0U)
			{
				syNetPeerSendTimePingPacket(sSYNetPeerSessionRttProbeSampleCount, sSYNetPeerSessionRttProbePingT0);
			}
			return;
		}
		if (sSYNetPeerSessionParamsHostSent == FALSE)
		{
			syNetPeerHostFinalizeSessionParamsFromRttProbe();
		}
		if (sSYNetPeerSessionParamsHostGotAck == FALSE)
		{
			if ((dSYTaskmanFrameCount % 10U) == 0U)
			{
				syNetPeerSendSessionParamsPacket(&sSYNetPeerSessionParamsHostProposal, SYNETPEER_PACKET_SESSION_PARAMS);
			}
		}
		return;
	}
}

sb32 syNetPeerSessionParamsNegotiationSatisfied(void)
{
	if (syNetSessionParamsAutoNegotiationEnabled() == FALSE)
	{
		return TRUE;
	}
	if (sSYNetPeerBootstrapIsEnabled == FALSE)
	{
		return TRUE;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		/*
		 * Host applies params locally when proposing, but must not open the battle execution gate or
		 * advance shared sim until the guest has applied the same contract and ACKed.
		 */
		return (syNetSessionParamsAreNegotiated() != FALSE) && (sSYNetPeerSessionParamsHostGotAck != FALSE);
	}
	return syNetSessionParamsAreNegotiated();
}

void syNetPeerApplyAutoNegotiatedDelayContract(u32 delay, u32 delay_ceil, const char *tag)
{
	u32 d;

	(void)tag;
	d = syNetPeerClampInputDelayToContract(delay);
	sSYNetPeerInputDelay = d;
	sSYNetPeerInputDelayFloor = d;
	sSYNetPeerInputDelayCeil = delay_ceil;
	if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
	{
		sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
	}
	sSYNetPeerAdaptiveDelayEnabled = TRUE;
}

void syNetPeerApplyAutoNegotiatedTransportParams(u32 phase_lock_ticks, u32 bundle_redundancy, u32 ingress_extra_pumps,
                                               u32 strict_ring_fuzz_ticks)
{
	if (phase_lock_ticks > 16U)
	{
		phase_lock_ticks = 16U;
	}
	sSYNetPeerPhaseLockPredictionWindowEnv = (int)phase_lock_ticks;
	if (bundle_redundancy > 8U)
	{
		bundle_redundancy = 8U;
	}
	if (ingress_extra_pumps > 4U)
	{
		ingress_extra_pumps = 4U;
	}
	if (strict_ring_fuzz_ticks > 2U)
	{
		strict_ring_fuzz_ticks = 2U;
	}
	syNetInputSetSessionIngressExtraPumpsOverride((s32)ingress_extra_pumps);
	syNetInputSetSessionBundleRedundancyOverride((s32)bundle_redundancy);
	sSYNetPeerSessionStrictRingFuzzOverride = (int)strict_ring_fuzz_ticks;
	sSYNetPeerSessionStrictRingFuzzOverrideValid = TRUE;
	sSYNetPeerStrictRingFuzzTicksEnvCache = (int)strict_ring_fuzz_ticks;
}

u32 syNetPeerGetInputDelayCeil(void)
{
	return sSYNetPeerInputDelayCeil;
}

u32 syNetPeerGetSessionIngressExtraPumps(void)
{
	return syNetSessionParamsGetEffectiveIngressExtraPumps();
}
#endif /* PORT */

static void syNetPeerRunAdaptiveInputDelaySimStep(u32 tick)
{
	u32 prev_delay;

	if ((sSYNetPeerAdaptiveDelayEnabled == FALSE) || (sSYNetPeerIsActive == FALSE))
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
	if (tick < sSYNetPeerAdaptNextSimTick)
	{
		return;
	}
	sSYNetPeerAdaptNextSimTick = tick + SYNETPEER_ADAPT_DELAY_SIM_INTERVAL;
	prev_delay = sSYNetPeerInputDelay;
	syNetPeerMaybeAdaptInputDelay(tick);
	(void)prev_delay;
	if (sSYNetPeerHostDelayRampPendingValid != FALSE)
	{
		syNetPeerSendInputDelaySyncPacket(sSYNetPeerHostDelayRampTarget, syNetPeerDelaySyncLocalEffectiveApplyTick());
	}
	else
	{
		syNetPeerSendInputDelaySyncPacket(sSYNetPeerInputDelay, syNetPeerDelaySyncLocalEffectiveApplyTick());
	}
}

static int syNetPeerRunwayFrontierLogIntervalTicks(void)
{
	const char *e;
	int v;

	if (sSYNetPeerRunwayFrontierLogIntervalEnv != -999)
	{
		return sSYNetPeerRunwayFrontierLogIntervalEnv;
	}
	e = getenv("SSB64_NETPLAY_RUNWAY_FRONTIER_LOG_TICKS");
	if ((e == NULL) || (e[0] == '\0'))
	{
		v = 60;
	}
	else
	{
		v = atoi(e);
	}
	if (v < 0)
	{
		v = 0;
	}
	if (v > 600)
	{
		v = 600;
	}
	sSYNetPeerRunwayFrontierLogIntervalEnv = v;
	return v;
}

static void syNetPeerMaybeLogRunwayFrontierSample(u32 tick_now)
{
	u32 hr;
	u32 frontier_sim;
	u32 deficit;
	s32 gap_wire;
	int interval;

	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	interval = syNetPeerRunwayFrontierLogIntervalTicks();
	if (interval <= 0)
	{
		return;
	}
	if ((tick_now == 0U) || ((tick_now % (u32)interval) != 0U))
	{
		return;
	}
	hr = sSYNetPeerHighestRemoteTick;
	if (hr == 0U)
	{
		return;
	}
	frontier_sim = syNetPeerDelaySimTickFromWire(hr);
	if (tick_now > frontier_sim)
	{
		deficit = tick_now - frontier_sim;
	}
	else
	{
		deficit = 0U;
	}
	if (hr >= tick_now)
	{
		gap_wire = (s32)(hr - tick_now);
	}
	else
	{
		gap_wire = -(s32)(tick_now - hr);
	}
	port_log(
	    "SSB64 NetPeer: runway_frontier sim=%u hr=%u frontier_sim=%u D=%u deficit=%u gap_hr_minus_sim=%d host=%d\n",
	    (unsigned int)tick_now,
	    (unsigned int)hr,
	    (unsigned int)frontier_sim,
	    (unsigned int)syNetPeerGetCommittedInputDelay(),
	    (unsigned int)deficit,
	    (int)gap_wire,
	    (sSYNetPeerBootstrapIsHost != FALSE) ? 1 : 0);
}

/*
 * When the host's committed sim tick leads the remote input frontier by several frames, committed input delay is
 * too low for the ingress cadence — raise D by one via the same INPUT_DELAY_SYNC contract as startup skew alignment.
 * Independent of SSB64_NETPLAY_ADAPTIVE_DELAY and of whether SSB64_NETPLAY_MATCH_INPUT_DELAY is set; proposed delay
 * is still clamped by `syNetPeerClampInputDelayToContract` (linked floor/ceiling when match contract is active).
 */
static void syNetPeerMaybeAutoRunwayDelayBump(u32 tick_now)
{
	u32 hr;
	u32 frontier_sim;
	u32 deficit;
	u32 proposed;
	u32 eff_tick;

	(void)tick_now;
	return;
	if ((sSYNetPeerIsActive == FALSE) || (sSYNetPeerBootstrapIsHost == FALSE))
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if ((sSYNetPeerStartupDelayAlignDone == FALSE) && (tick_now <= 120U))
	{
		return;
	}
	if (sSYNetPeerStartupMatchDelayPendingValid != FALSE)
	{
		return;
	}
	if (sSYNetPeerDelaySyncPendingValid != FALSE)
	{
		return;
	}
	if (sSYNetPeerHostDelayRampPendingValid != FALSE)
	{
		return;
	}
	hr = sSYNetPeerHighestRemoteTick;
	if (hr == 0U)
	{
		return;
	}
	frontier_sim = syNetPeerDelaySimTickFromWire(hr);
	if (tick_now <= frontier_sim)
	{
		sSYNetPeerAutoRunwayConsec = 0U;
		sSYNetPeerAutoRunwayLastSimTick = tick_now;
		return;
	}
	deficit = tick_now - frontier_sim;
	if (deficit < SYNETPEER_AUTO_RUNWAY_DEFICIT_MIN_TICKS)
	{
		sSYNetPeerAutoRunwayConsec = 0U;
		sSYNetPeerAutoRunwayLastSimTick = tick_now;
		return;
	}
	if (tick_now != sSYNetPeerAutoRunwayLastSimTick)
	{
		if ((sSYNetPeerAutoRunwayLastSimTick != ~(u32)0) && (tick_now == sSYNetPeerAutoRunwayLastSimTick + 1U))
		{
			if (sSYNetPeerAutoRunwayConsec < 0xFFFFU)
			{
				sSYNetPeerAutoRunwayConsec++;
			}
		}
		else
		{
			sSYNetPeerAutoRunwayConsec = 1U;
		}
		sSYNetPeerAutoRunwayLastSimTick = tick_now;
	}
	if ((deficit < SYNETPEER_AUTO_RUNWAY_DEFICIT_EMERGENCY_TICKS) &&
	    (sSYNetPeerAutoRunwayConsec < SYNETPEER_AUTO_RUNWAY_SUSTAIN_SIM_TICKS))
	{
		return;
	}
	if (sSYNetPeerInputDelay >= sSYNetPeerInputDelayCeil)
	{
		return;
	}
	proposed = syNetPeerClampInputDelayToContract(sSYNetPeerInputDelay + 1U);
	if (proposed <= sSYNetPeerInputDelay)
	{
		return;
	}
	eff_tick = syNetPeerSaturatingAddU32(tick_now, syNetPeerDelaySyncCommitLeadTicks());
	sSYNetPeerDelaySyncPending = proposed;
	sSYNetPeerDelaySyncEffectiveTick = eff_tick;
	sSYNetPeerDelaySyncPendingValid = TRUE;
	syNetPeerSendInputDelaySyncPacket(proposed, eff_tick);
	sSYNetPeerAutoRunwayConsec = 0U;
	port_log(
	    "SSB64 NetPeer: auto_runway_delay bump queued D=%u eff_tick=%u deficit=%u hr=%u frontier_sim=%u sim=%u\n",
	    (unsigned int)proposed, (unsigned int)eff_tick, (unsigned int)deficit, (unsigned int)hr,
	    (unsigned int)frontier_sim, (unsigned int)tick_now);
}
#endif

void syNetPeerSendControlPacket(u16 packet_type)
{
#if defined(PORT)
	u8 buffer[SYNETPEER_CONTROL_PACKET_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, packet_type);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	checksum = syNetPeerChecksumBytes(buffer, cursor - buffer);
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_CONTROL_PACKET_BYTES);
#endif
}

#if defined(PORT)
void syNetPeerSendVsSessionEndNotifyPeer(void)
{
	u8 buffer[SYNETPEER_CONTROL_PACKET_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_VS_SESSION_END);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	checksum = syNetPeerChecksumBytes(buffer, cursor - buffer);
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_CONTROL_PACKET_BYTES);
}
#endif

#if defined(PORT)
void syNetPeerSendMatchConfigPacket(void)
{
	u8 buffer[SYNETPEER_BOOTSTRAP_PACKET_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_MATCH_CONFIG);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteMetadata(&cursor, &sSYNetPeerBootstrapMetadata);
	checksum = syNetPeerChecksumBytes(buffer, cursor - buffer);
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_BOOTSTRAP_PACKET_BYTES);
}
#endif /* defined(PORT) */

#if defined(PORT) && defined(SSB64_NETMENU)
static u32 syNetPeerAutomix32(u32 a, u32 b, u32 c)
{
	a ^= (b ^ 2166136261U);
	a *= 16777619U;
	a ^= (c ^ 2166136261U);
	a *= 16777619U;
	a ^= (sSYNetPeerSessionID + 7919U);
	a *= 16777619U;
	return a != 0U ? a : 1U;
}

static void syNetPeerSendAutomatchOfferPacketMaybe(void)
{
	u8 buf[SYNETPEER_AUTOMATCH_OFFER_BYTES];
	u8 *cursor = buf;
	u32 chk;

	memset(buf, 0, sizeof(buf));
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_AUTOMATCH_OFFER);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU16(&cursor, sAutoLocalBanMask);
	syNetPeerWriteU8(&cursor, sAutoLocalFkind);
	syNetPeerWriteU8(&cursor, sAutoLocalCostume);
	syNetPeerWriteU32(&cursor, sAutoLocalNonce);
	chk = syNetPeerChecksumBytes(buf, SYNETPEER_AUTOMATCH_OFFER_BYTES - 4);
	syNetPeerWriteU32(&cursor, chk);
	syNetPeerSendBytes(buf, SYNETPEER_AUTOMATCH_OFFER_BYTES);
}

static void syNetPeerHandleAutomatchOfferPacket(const u8 *buffer, s32 size)
{
	const u8 *c = buffer;
	u32 magic;
	u32 session_id;
	u32 checksum;
	u32 expected_checksum;
	u16 wire_version;
	u16 packet_type;
	u16 ban;
	u8 fk;
	u8 costume;
	u32 nonce;

	if (size != SYNETPEER_AUTOMATCH_OFFER_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_AUTOMATCH_OFFER_BYTES - 4);
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	ban = syNetPeerReadU16(&c);
	fk = syNetPeerReadU8(&c);
	costume = syNetPeerReadU8(&c);
	nonce = syNetPeerReadU32(&c);
	checksum = syNetPeerReadU32(&c);
	if ((magic != SYNETPEER_MAGIC) || (wire_version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_AUTOMATCH_OFFER) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	sAutoPeerBanMask = ban;
	sAutoPeerFkind = fk;
	sAutoPeerCostume = costume;
	sAutoPeerNonce = nonce;
	sSYAutoGotPeerOffer = TRUE;
}

static sb32 syNetPeerComposeAutomatchMatchMetadata(void)
{
	SYNetInputReplayMetadata *m = &sSYNetPeerBootstrapMetadata;
	u16 combo;
	u32 pool[10];
	u32 npool;
	u32 seed_pick;
	u32 slot;
	u8 hfk;
	u8 hcost;
	u8 gfk;
	u8 gcost;

	combo = (u16)(((u32)sAutoLocalBanMask) | ((u32)sAutoPeerBanMask));
	npool = 0U;
	for (slot = 0U; slot < 9U; slot++)
	{
		s32 gkind;

		if ((((u32)1U << slot) & (u32)combo) != 0U)
		{
			continue;
		}
		gkind = mnVSNetLevelPrefsMapsGetGroundKind((s32)slot);
		if (gkind == 0xDE)
		{
			continue;
		}
		if (mnVSNetLevelPrefsMapsCheckLocked(gkind) != FALSE)
		{
			continue;
		}
		pool[npool++] = (u32)gkind;
	}
	if (npool == 0U)
	{
		pool[0] = (u32)nGRKindCastle;
		npool = 1U;
	}

	seed_pick = syNetPeerAutomix32(sAutoLocalNonce, sAutoPeerNonce, (u32)combo);
	hfk = sAutoLocalFkind;
	hcost = sAutoLocalCostume;
	gfk = sAutoPeerFkind;
	gcost = sAutoPeerCostume;

	syNetPeerMakeBootstrapMetadata(m);

	m->player_count = 2;
	m->stage_kind = (u32)pool[(seed_pick ^ sSYNetPeerSessionID ^ npool) % npool];
	m->fighter_kinds[0] = hfk;
	m->fighter_kinds[1] = gfk;
	m->costumes[0] = hcost;
	m->costumes[1] = gcost;
	m->scene_kind = nSCKindVSBattle;

	m->stocks = (u32)3;
	m->time_limit = (u32)SCBATTLE_TIMELIMIT_INFINITE;
	m->item_toggles = ~(u32)0;
	m->item_appearance_rate = (u8)nSCBattleItemSwitchMiddle;
#ifdef PORT
	/* Host-only: MATCH_CONFIG carries this to the peer. `SSB64_NETPLAY_AUTOMATCH_NO_ITEMS=0` or unset = default (items on). */
	{
		const char *no_items_env;

		no_items_env = getenv("SSB64_NETPLAY_AUTOMATCH_NO_ITEMS");
		if ((no_items_env != NULL) && (no_items_env[0] != '\0') && (atoi(no_items_env) != 0))
		{
			m->item_appearance_rate = (u8)nSCBattleItemSwitchNone;
			m->item_toggles = 0U;
			m->item_switch = (u32)nSCBattleItemSwitchNone;
		}
	}
#endif
	m->game_type = (u8)nSCBattleGameTypeRoyal;
	m->game_rules = SCBATTLE_GAMERULE_STOCK;
	m->rng_seed = syNetPeerAutomix32(
	    seed_pick, (((u32)hfk) << 24) ^ (((u32)gfk) << 16) ^ (((u32)combo)), m->stage_kind ^ sSYNetPeerSessionID);
	m->netplay_sim_slot_host_hw = 0U;
	m->netplay_sim_slot_client_hw = 1U;
	return TRUE;
}

void syNetPeerReceiveBootstrapPackets(void);

static sb32 syNetPeerAutomatchExchangeOffers(void)
{
	s32 i;

	sSYAutoGotPeerOffer = FALSE;
	for (i = 0; i < (s32)syNetPeerBootstrapRetryCount(); i++)
	{
		syNetPeerSendAutomatchOfferPacketMaybe();
		syNetPeerReceiveBootstrapPackets();
		if ((sSYNetPeerBootstrapIsHost != FALSE) && (sSYAutoGotPeerOffer != FALSE))
		{
			return TRUE;
		}
		if ((sSYNetPeerBootstrapIsHost == FALSE) &&
		    ((sSYNetPeerBootstrapMetadataApplied != FALSE) || (sSYNetPeerBootstrapMetadataStaged != FALSE)))
		{
			return TRUE;
		}
		syNetPeerSleepBootstrapRetry();
	}
	port_log("SSB64 NetPeer: automatch offer exchange timed out role=%s\n",
	         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client");
	return FALSE;
}
#endif /* defined(PORT) && defined(SSB64_NETMENU) */

#if defined(PORT)
void syNetPeerHandleControlPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u32 session_id;
	u32 checksum;
	u32 expected_checksum;
	u16 version;
	u16 packet_type;

	if (size != SYNETPEER_CONTROL_PACKET_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_CONTROL_PACKET_BYTES - 4);

	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	checksum = syNetPeerReadU32(&cursor);

	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
		(session_id != sSYNetPeerSessionID) || (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (packet_type == SYNETPEER_PACKET_READY)
	{
		sSYNetPeerBootstrapPeerReady = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_START)
	{
		sSYNetPeerBootstrapStartReceived = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_BATTLE_READY)
	{
		if (sSYNetPeerBattlePeerReady == FALSE)
		{
			port_log("SSB64 NetPeer: received BATTLE_READY role=%s tick=%u local=%d remote=%d\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         syNetInputGetTick(), sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer);
		}
		sSYNetPeerBattlePeerReady = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_BATTLE_START)
	{
		if (sSYNetPeerBattleStartReceived == FALSE)
		{
			port_log("SSB64 NetPeer: received BATTLE_START role=%s tick=%u local=%d remote=%d\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         syNetInputGetTick(), sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer);
		}
		sSYNetPeerBattleStartReceived = TRUE;
	}
#if defined(SSB64_NETMENU)
	else if (packet_type == SYNETPEER_PACKET_STAGE_SCENE_READY)
	{
		if (sSYNetPeerStageScenePeerReadyLogged == FALSE)
		{
			port_log("SSB64 NetPeer: staging_ready recv role=%s sim=%u\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         (unsigned int)syNetInputGetTick());
			sSYNetPeerStageScenePeerReadyLogged = TRUE;
		}
		sSYNetPeerStageScenePeerReady = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_STAGE_SCENE_GO)
	{
		if (sSYNetPeerStageSceneGoReceivedLogged == FALSE)
		{
			port_log("SSB64 NetPeer: staging_go recv role=%s sim=%u\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         (unsigned int)syNetInputGetTick());
			sSYNetPeerStageSceneGoReceivedLogged = TRUE;
		}
		sSYNetPeerStageSceneGoReceived = TRUE;
	}
#endif
	else if (packet_type == SYNETPEER_PACKET_VS_SESSION_END)
	{
		if (sSYNetPeerIsActive != FALSE)
		{
			sSYNetPeerVsSessionEndReceived = TRUE;
			port_log("SSB64 NetPeer: received VS_SESSION_END role=%s tick=%u — stopping session\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         (unsigned int)syNetInputGetTick());
			syNetPeerStopVSSession();
		}
	}
}

static sb32 sSYNetPeerUdpLinkSyncEnvEnabled = TRUE;
static sb32 sSYNetPeerUdpLinkSyncRequiredOnFail = FALSE;
static sb32 sSYNetPeerUdpLinkEnvLoaded;
static sb32 sSYNetPeerUdpLinkComplete = TRUE;
static sb32 sSYNetPeerUdpLinkSyncSkippedFallback;
static u32 sSYNetPeerUdpLinkRoundsRemaining;
static u16 sSYNetPeerUdpLinkPendingToken;
static u16 sSYNetPeerUdpLinkRepConsumedToken;
static u32 sSYNetPeerUdpLinkNonce;

static void syNetPeerMergedConnectReset(void)
{
	s32 i;

	for (i = 0; i < MAXCONTROLLERS; i++)
	{
		sSYNetPeerMergedConnectLastTick[i] = -1;
		sSYNetPeerMergedConnectDisc[i] = 0;
	}
	sSYNetPeerVsSessionEndReceived = FALSE;
	sSYNetPeerPeerDisconnectConsec = 0U;
}

static void syNetPeerLoadUdpLinkSyncEnvOnce(void)
{
	char *e;

	if (sSYNetPeerUdpLinkEnvLoaded != FALSE)
	{
		return;
	}
	sSYNetPeerUdpLinkEnvLoaded = TRUE;
	e = getenv("SSB64_NETPLAY_UDP_LINK_SYNC");
	sSYNetPeerUdpLinkSyncEnvEnabled = ((e == NULL) || (atoi(e) != 0)) ? TRUE : FALSE;
	e = getenv("SSB64_NETPLAY_UDP_LINK_SYNC_REQUIRED");
	sSYNetPeerUdpLinkSyncRequiredOnFail = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
}

static u32 syNetPeerUdpLinkSyncRetransmitMs(void)
{
	static u32 cached_ms = SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_DEFAULT;
	static sb32 loaded = FALSE;
	char *e;
	s32 v;

	if (loaded != FALSE)
	{
		return cached_ms;
	}
	loaded = TRUE;
	cached_ms = SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_DEFAULT;
	e = getenv("SSB64_NETPLAY_UDP_LINK_SYNC_RETRANSMIT_MS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v >= (s32)SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MIN)
		{
			cached_ms = (u32)v;
		}
	}
	if (cached_ms < SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MIN)
	{
		cached_ms = SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MIN;
	}
	if (cached_ms > SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MAX)
	{
		cached_ms = SYNETPEER_UDP_LINK_SYNC_RETRANSMIT_MS_MAX;
	}
	return cached_ms;
}

static u32 syNetPeerUdpLinkSyncRetransmitSlices(void)
{
	u32 slices;

	slices = (syNetPeerUdpLinkSyncRetransmitMs() * 1000U) / syNetPeerBootstrapRetrySleepUs();
	if (slices < 4U)
	{
		slices = 4U;
	}
	return slices;
}

static void syNetPeerUdpSyncSendPayload(u16 kind, u16 a, u16 b)
{
	u8 buf[SYNETPEER_UDP_SYNC_PACKET_BYTES];
	u8 *cursor = buf;
	u32 chk;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, kind);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU16(&cursor, a);
	syNetPeerWriteU16(&cursor, b);
	chk = syNetPeerChecksumBytes(buf, (u32)(cursor - buf));
	syNetPeerWriteU32(&cursor, chk);
	syNetPeerSendBytes(buf, SYNETPEER_UDP_SYNC_PACKET_BYTES);
}

static void syNetPeerUdpSyncSendRequest(void)
{
	u32 mix;

	sSYNetPeerUdpLinkNonce++;
	mix = (u32)(sSYNetPeerSessionID ^ (sSYNetPeerUdpLinkNonce * 1664525U) ^ 1013904223U);
	sSYNetPeerUdpLinkPendingToken = (u16)((mix ^ (mix >> 16)) & 0xFFFFU);
	if (sSYNetPeerUdpLinkPendingToken == 0)
	{
		sSYNetPeerUdpLinkPendingToken = 1;
	}
	sSYNetPeerUdpLinkRepConsumedToken = 0U;
	syNetPeerUdpSyncSendPayload(SYNETPEER_PACKET_UDP_SYNC_REQ, sSYNetPeerUdpLinkPendingToken, 0);
}

static void syNetPeerUdpSyncRetransmitPendingRequest(void)
{
	if (sSYNetPeerUdpLinkPendingToken == 0U)
	{
		return;
	}
	syNetPeerUdpSyncSendPayload(SYNETPEER_PACKET_UDP_SYNC_REQ, sSYNetPeerUdpLinkPendingToken, 0);
}

static void syNetPeerUdpSyncSendReplyEcho(u16 challenge)
{
	syNetPeerUdpSyncSendPayload(SYNETPEER_PACKET_UDP_SYNC_REP, challenge, 0);
}

static void syNetPeerHandleUdpSyncIngress(const u8 *buffer, int size)
{
	const u8 *c = buffer;
	u32 magic;
	u16 ver;
	u16 kind;
	u32 session_id;
	u16 fld_a;
	u16 fld_b;
	u32 chk;
	u32 exp;

	if (size != (int)SYNETPEER_UDP_SYNC_PACKET_BYTES)
	{
		return;
	}
	exp = syNetPeerChecksumBytes(buffer, SYNETPEER_UDP_SYNC_PACKET_BYTES - 4U);
	magic = syNetPeerReadU32(&c);
	ver = syNetPeerReadU16(&c);
	kind = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	fld_a = syNetPeerReadU16(&c);
	fld_b = syNetPeerReadU16(&c);
	chk = syNetPeerReadU32(&c);
	(void)fld_b;
	if ((magic != SYNETPEER_MAGIC) || (ver != SYNETPEER_VERSION) || (session_id != sSYNetPeerSessionID) ||
	    (chk != exp))
	{
		return;
	}
	if (kind == SYNETPEER_PACKET_UDP_SYNC_REQ)
	{
		syNetPeerUdpSyncSendReplyEcho(fld_a);
		return;
	}
	if (kind == SYNETPEER_PACKET_UDP_SYNC_REP)
	{
		if (sSYNetPeerUdpLinkRoundsRemaining == 0U)
		{
			return;
		}
		if (fld_a != sSYNetPeerUdpLinkPendingToken)
		{
			return;
		}
		if (fld_a == sSYNetPeerUdpLinkRepConsumedToken)
		{
			return;
		}
		sSYNetPeerUdpLinkRepConsumedToken = fld_a;
		if (sSYNetPeerUdpLinkRoundsRemaining > 0U)
		{
			sSYNetPeerUdpLinkRoundsRemaining--;
		}
		if (sSYNetPeerUdpLinkRoundsRemaining > 0U)
		{
			syNetPeerUdpSyncSendRequest();
		}
		return;
	}
}

static void syNetPeerPumpUdpLinkSyncRecv(void)
{
	u8 buf[256];

	if (syNetPeerDatagramSocketIsUsable() == FALSE)
	{
		return;
	}
	for (;;)
	{
		sb32 wb = FALSE;
		int n = syNetPeerOsRecvFrom(sSYNetPeerSocket, buf, sizeof(buf), &wb);

		if (n < 0)
		{
			break;
		}
		if (n == (int)SYNETPEER_UDP_SYNC_PACKET_BYTES)
		{
			syNetPeerHandleUdpSyncIngress(buf, n);
		}
	}
}

static sb32 syNetPeerRunUdpLinkSync(void)
{
	s32 i;
	u32 retransmit_slices;
	u32 since_retransmit;

	syNetPeerLoadUdpLinkSyncEnvOnce();
	if (sSYNetPeerUdpLinkSyncEnvEnabled == FALSE)
	{
		sSYNetPeerUdpLinkComplete = TRUE;
		sSYNetPeerUdpLinkRoundsRemaining = 0U;
		return TRUE;
	}
	sSYNetPeerUdpLinkComplete = FALSE;
	sSYNetPeerUdpLinkSyncSkippedFallback = FALSE;
	sSYNetPeerUdpLinkRoundsRemaining = SYNETPEER_UDP_LINK_SYNC_ROUNDS;
	sSYNetPeerUdpLinkRepConsumedToken = 0U;
	sSYNetPeerUdpLinkNonce = (u32)(sSYNetPeerSessionID ^ 0x9E3779B9U);
	retransmit_slices = syNetPeerUdpLinkSyncRetransmitSlices();
	since_retransmit = retransmit_slices;
	syNetPeerUdpSyncSendRequest();
	for (i = 0; i < (s32)(syNetPeerBootstrapRetryCount() * 8U); i++)
	{
		syNetPeerPumpUdpLinkSyncRecv();
		if (sSYNetPeerUdpLinkRoundsRemaining == 0U)
		{
			sSYNetPeerUdpLinkComplete = TRUE;
			port_log("SSB64 NetPeer: UDP link sync OK (%u echo rounds)\n",
			         (unsigned int)SYNETPEER_UDP_LINK_SYNC_ROUNDS);
			return TRUE;
		}
		syNetPeerSleepBootstrapRetry();
		since_retransmit++;
		if (since_retransmit >= retransmit_slices)
		{
			syNetPeerUdpSyncRetransmitPendingRequest();
			since_retransmit = 0U;
		}
	}
	if (sSYNetPeerUdpLinkSyncRequiredOnFail != FALSE)
	{
		port_log("SSB64 NetPeer: UDP link sync FAILED (timeout) — bootstrap aborted (SSB64_NETPLAY_UDP_LINK_SYNC_REQUIRED=1)\n");
		return FALSE;
	}
	sSYNetPeerUdpLinkComplete = TRUE;
	sSYNetPeerUdpLinkRoundsRemaining = 0U;
	sSYNetPeerUdpLinkSyncSkippedFallback = TRUE;
	port_log(
	    "SSB64 NetPeer: UDP link sync FAILED (timeout) — continuing without link proof (fallback; set UDP_LINK_SYNC=0 to skip probe)\n");
	return TRUE;
}

static sb32 syNetPeerRequireInputBindStrict(void)
{
	char *e;

	e = getenv("SSB64_NETPLAY_REQUIRE_INPUT_BIND");
	if ((e != NULL) && (atoi(e) == 0))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetPeerGetInputBindExpectedSims(u8 *out_host_sim, u8 *out_guest_sim)
{
	u8 eh;
	u8 eg;

	eh = sSYNetPeerBootstrapMetadata.netplay_sim_slot_host_hw;
	eg = sSYNetPeerBootstrapMetadata.netplay_sim_slot_client_hw;
	if (((sSYNetPeerBootstrapMetadataApplied == FALSE) && (sSYNetPeerBootstrapMetadataStaged == FALSE)) ||
	    (eh >= (u8)MAXCONTROLLERS) || (eg >= (u8)MAXCONTROLLERS) || (eh == eg))
	{
		eh = 0U;
		eg = 1U;
	}
	*out_host_sim = eh;
	*out_guest_sim = eg;
}

static sb32 syNetPeerInputBindIsComplete(void)
{
	if (syNetPeerRequireInputBindStrict() == FALSE)
	{
		return TRUE;
	}
	return (sSYNetPeerInputBindSent != FALSE) && (sSYNetPeerInputBindPeerOk != FALSE);
}

static void syNetPeerInputBindReset(void)
{
	sSYNetPeerInputBindSent = FALSE;
	sSYNetPeerInputBindPeerOk = FALSE;
	sSYNetPeerInputBindAckLogged = FALSE;
	sSYNetPeerInputBindPeerPrimaryDev = (u8)MAXCONTROLLERS;
	sSYNetPeerInputBindGraceUntilFrame = 0U;
}

static void syNetPeerInputBindMaybeLogAck(void)
{
	u8 eh;
	u8 eg;

	if ((sSYNetPeerInputBindAckLogged != FALSE) || (syNetPeerInputBindIsComplete() == FALSE))
	{
		return;
	}
	syNetPeerGetInputBindExpectedSims(&eh, &eg);
	port_log(
	    "SSB64 NetPeer: input_bind_ack session=%u host_sim=%u guest_sim=%u primary_dev=%u role=%s ok=1 peer_primary_dev=%u\n",
	    sSYNetPeerSessionID, (u32)eh, (u32)eg, (u32)syNetPeerGetPrimaryLocalHardwareDeviceIndex(),
	    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", (u32)sSYNetPeerInputBindPeerPrimaryDev);
	/* Keep retransmitting bind for a short grace window so a late/lost peer-ack path can still converge. */
	sSYNetPeerInputBindGraceUntilFrame = dSYTaskmanFrameCount + 180U;
	sSYNetPeerInputBindAckLogged = TRUE;
#ifdef PORT
	syNetInputClearRemoteSlotPredictionState();
#endif
}

static void syNetPeerSendInputBindPacket(void)
{
	u8 buffer[SYNETPEER_INPUT_BIND_BYTES];
	u8 *cursor = buffer;
	u32 checksum;
	u8 eh;
	u8 eg;

	syNetPeerGetInputBindExpectedSims(&eh, &eg);
	memset(buffer, 0, sizeof(buffer));
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_INPUT_BIND);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU8(&cursor, eh);
	syNetPeerWriteU8(&cursor, eg);
	syNetPeerWriteU8(&cursor, (u8)syNetPeerGetPrimaryLocalHardwareDeviceIndex());
	syNetPeerWriteU8(&cursor, 0);
	checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_INPUT_BIND_BYTES - 4);
	syNetPeerWriteU32(&cursor, checksum);
	if (syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)SYNETPEER_INPUT_BIND_BYTES, &sSYNetPeerPeerAddress) ==
	    (int)SYNETPEER_INPUT_BIND_BYTES)
	{
		sSYNetPeerInputBindSent = TRUE;
		syNetPeerInputBindMaybeLogAck();
	}
}

static void syNetPeerHandleInputBindPacket(const u8 *buffer, s32 size)
{
	const u8 *c = buffer;
	u32 magic;
	u16 wire_version;
	u16 packet_type;
	u32 session_id;
	u8 rx_host_sim;
	u8 rx_guest_sim;
	u8 rx_primary;
	u8 reserved;
	u32 checksum;
	u32 expected_checksum;
	u8 eh;
	u8 eg;

	if (size != SYNETPEER_INPUT_BIND_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_INPUT_BIND_BYTES - 4);
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	rx_host_sim = syNetPeerReadU8(&c);
	rx_guest_sim = syNetPeerReadU8(&c);
	rx_primary = syNetPeerReadU8(&c);
	reserved = syNetPeerReadU8(&c);
	checksum = syNetPeerReadU32(&c);
	(void)reserved;
	if ((magic != SYNETPEER_MAGIC) || (wire_version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_INPUT_BIND) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (rx_primary >= (u8)MAXCONTROLLERS)
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	syNetPeerGetInputBindExpectedSims(&eh, &eg);
	if ((rx_host_sim != eh) || (rx_guest_sim != eg))
	{
		port_log(
		    "SSB64 NetPeer: input_bind mismatch session=%u expected host_sim=%u guest_sim=%u got host_sim=%u guest_sim=%u role=%s\n",
		    sSYNetPeerSessionID, (u32)eh, (u32)eg, (u32)rx_host_sim, (u32)rx_guest_sim,
		    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client");
		syNetDesyncClassifierOnFrameIdentityMismatch(syNetInputGetTick());
		sSYNetPeerPacketsDropped++;
		return;
	}
	sSYNetPeerInputBindPeerOk = TRUE;
	sSYNetPeerInputBindPeerPrimaryDev = rx_primary;
	if (syNetPeerInputBindIsComplete() == FALSE)
	{
		syNetPeerSendInputBindPacket();
	}
	syNetPeerInputBindMaybeLogAck();
}

static void syNetPeerInputBindServiceTransport(void)
{
	u32 frame_now;

	if ((sSYNetPeerIsActive == FALSE) || (syNetPeerRequireInputBindStrict() == FALSE))
	{
		return;
	}
	frame_now = dSYTaskmanFrameCount;
	if ((syNetPeerInputBindIsComplete() != FALSE) && (frame_now > sSYNetPeerInputBindGraceUntilFrame))
	{
		return;
	}
	if ((frame_now % 15U) != 0U)
	{
		return;
	}
	syNetPeerSendInputBindPacket();
}

static sb32 syNetPeerRequireBattleExecSync(void)
{
	char *e;

	e = getenv("SSB64_NETPLAY_BATTLE_EXEC_SYNC");
	if ((e != NULL) && (atoi(e) == 0))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 sSYNetPeerExecSyncHostSent;
static sb32 sSYNetPeerExecSyncHostPeerEcho;
static u32 sSYNetPeerExecSyncHostProposedTick;
static sb32 sSYNetPeerExecSyncClientGotHost;
static sb32 sSYNetPeerExecSyncClientEchoSent;
static u32 sSYNetPeerExecSyncAgreedTick;
static u32 sSYNetPeerExecSyncPumpCount;
static u32 sSYNetPeerExecSyncHostViPhase;
static u32 sSYNetPeerExecSyncPeerViPhaseLatch;
/*
 * One-shot symmetric startup: set when BATTLE_EXEC_SYNC completes for bootstrap (including no battle barrier).
 * Skipped when `SSB64_NETPLAY_BATTLE_EXEC_SYNC=0` or non-bootstrap VS. Never mutates `syNetInputSetTick` from readiness paths.
 */
static sb32 sSYNetPeerBothSidesLatchedStartup;
static u32 sSYNetPeerLatchedStartupTick;

static void syNetPeerBattleExecSyncReset(void)
{
	sSYNetPeerExecSyncHostSent = FALSE;
	sSYNetPeerExecSyncHostPeerEcho = FALSE;
	sSYNetPeerExecSyncHostProposedTick = ~(u32)0;
	sSYNetPeerExecSyncClientGotHost = FALSE;
	sSYNetPeerExecSyncClientEchoSent = FALSE;
	sSYNetPeerExecSyncAgreedTick = ~(u32)0;
	sSYNetPeerExecSyncPumpCount = 0U;
	sSYNetPeerExecSyncHostViPhase = 0U;
	sSYNetPeerExecSyncPeerViPhaseLatch = 0U;
	sSYNetPeerBothSidesLatchedStartup = FALSE;
	sSYNetPeerLatchedStartupTick = ~(u32)0;
}

static sb32 syNetPeerBattleExecSyncIsComplete(void)
{
	if (syNetPeerRequireBattleExecSync() == FALSE)
	{
		return TRUE;
	}
	if (sSYNetPeerBootstrapIsEnabled == FALSE)
	{
		return TRUE;
	}
	/* When the battle barrier is enabled, wait for wall/NTP release before bind/exec sync. */
	if ((sSYNetPeerBattleBarrierEnabled != FALSE) && (sSYNetPeerBattleBarrierReleased == FALSE))
	{
		return FALSE;
	}
	if (syNetPeerInputBindIsComplete() == FALSE)
	{
		return FALSE;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		return (sSYNetPeerExecSyncHostSent != FALSE) && (sSYNetPeerExecSyncHostPeerEcho != FALSE);
	}
	return (sSYNetPeerExecSyncClientGotHost != FALSE) && (sSYNetPeerExecSyncClientEchoSent != FALSE);
}

static void syNetPeerMaybeLatchBothSidesStartupAfterExecSync(void)
{
	u32 agreed;

	if (syNetPeerRequireBattleExecSync() == FALSE)
	{
		return;
	}
	if (sSYNetPeerBootstrapIsEnabled == FALSE)
	{
		return;
	}
	if (syNetPeerBattleExecSyncIsComplete() == FALSE)
	{
		return;
	}
	if (sSYNetPeerBothSidesLatchedStartup != FALSE)
	{
		return;
	}
	agreed = (sSYNetPeerBootstrapIsHost != FALSE) ? sSYNetPeerExecSyncHostProposedTick : sSYNetPeerExecSyncAgreedTick;
	sSYNetPeerBothSidesLatchedStartup = TRUE;
	sSYNetPeerLatchedStartupTick = agreed;
	/*
	 * Steady-state diag: `syTaskmanLoadScene` already zeros push at VS load, but peers can accumulate different
	 * PortPushFrame / decouple skew between scene entry and this symmetric handshake. Re-latch here so
	 * `port_get_push_frame_count` + decouple deadlines share a common post-epoch origin for automatch.
	 */
	port_reset_push_frame_count_for_net_barrier();
	port_reset_vs_decouple_pacing_for_net_barrier();
	port_log("SSB64 NetPeer: both_sides_latched_startup agreed_tick=%u role=%s (push+decouple epoch reset)\n",
	         (unsigned int)agreed, (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client");
}

static SYNetPeerSyncPipelinePhase syNetPeerDeriveSyncPipelinePhase(void)
{
	if (sSYNetPeerIsEnabled == FALSE)
	{
		return nSYNetPeerSyncPipeline_Disabled;
	}
	if ((sSYNetPeerIsConfigured == FALSE) || (sSYNetPeerIsActive == FALSE))
	{
		return nSYNetPeerSyncPipeline_Inactive;
	}
	if ((sSYNetPeerUdpLinkSyncEnvEnabled != FALSE) && (sSYNetPeerUdpLinkComplete == FALSE))
	{
		return nSYNetPeerSyncPipeline_UdpLink;
	}
	if ((sSYNetPeerBootstrapIsEnabled != FALSE) &&
	    ((sSYNetPeerBootstrapPeerReady == FALSE) || (sSYNetPeerBootstrapStartReceived == FALSE)))
	{
		return nSYNetPeerSyncPipeline_Bootstrap;
	}
	if ((sSYNetPeerBootstrapIsEnabled != FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE) &&
	    (sSYNetPeerBattleBarrierReleased == FALSE))
	{
		return nSYNetPeerSyncPipeline_ClockBarrier;
	}
	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return nSYNetPeerSyncPipeline_InputBind;
	}
	if ((syNetPeerRequireBattleExecSync() != FALSE) && (syNetPeerBattleExecSyncIsComplete() == FALSE))
	{
		return nSYNetPeerSyncPipeline_BattleExecSync;
	}
	return nSYNetPeerSyncPipeline_Running;
}

SYNetPeerSyncPipelinePhase syNetPeerGetSyncPipelinePhase(void)
{
	return syNetPeerDeriveSyncPipelinePhase();
}

void syNetPeerGetSyncPipelineProgress(u32 *out_step, u32 *out_total)
{
	SYNetPeerSyncPipelinePhase ph;

	if ((out_step == NULL) || (out_total == NULL))
	{
		return;
	}
	*out_step = 0U;
	*out_total = 1U;
	ph = syNetPeerDeriveSyncPipelinePhase();
	if (ph == nSYNetPeerSyncPipeline_UdpLink)
	{
		*out_total = (u32)SYNETPEER_UDP_LINK_SYNC_ROUNDS;
		if (*out_total == 0U)
		{
			*out_total = 1U;
		}
		*out_step = (*out_total)-sSYNetPeerUdpLinkRoundsRemaining;
	}
}

sb32 syNetPeerShouldHardAbortOnNetplayInputMismatch(void)
{
	if (syNetInputGetAbortOnInputMismatchFatal() == FALSE)
	{
		return FALSE;
	}
	return (syNetPeerGetSyncPipelinePhase() == nSYNetPeerSyncPipeline_Running) ? TRUE : FALSE;
}

sb32 syNetPeerGetMergedMinConfirmedSimTick(s32 *out_min_tick)
{
	s32 i;
	s32 best;
	s32 v;

	if (out_min_tick == NULL)
	{
		return FALSE;
	}
	best = 0x7FFFFFFF;
	for (i = 0; i < MAXCONTROLLERS; i++)
	{
		if (sSYNetPeerMergedConnectDisc[i] != 0)
		{
			continue;
		}
		v = sSYNetPeerMergedConnectLastTick[i];
		if (v < 0)
		{
			continue;
		}
		if (v < best)
		{
			best = v;
		}
	}
	if (best == 0x7FFFFFFF)
	{
		return FALSE;
	}
	*out_min_tick = best;
	return TRUE;
}

#if defined(PORT)
u32 syNetPeerGetPhaseLockPredictionWindowTicksFromEnv(void)
{
	const char *e;
	int v;

	if (sSYNetPeerPhaseLockPredictionWindowEnv != -999)
	{
		return (u32)sSYNetPeerPhaseLockPredictionWindowEnv;
	}
	v = 2;
	e = getenv("SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
	}
	if (v < 0)
	{
		v = 0;
	}
	if (v > 16)
	{
		v = 16;
	}
	sSYNetPeerPhaseLockPredictionWindowEnv = v;
	return (u32)v;
}

u32 syNetPeerGetPhaseLockPredictionWindowTicks(void)
{
	return syNetSessionParamsGetEffectivePhaseLockTicks();
}

u32 syNetPeerGetGlobalCommitGen(void)
{
	return sSYNetPeerGlobalCommitGen;
}

void syNetPeerNoteSharedCommitAdvanced(u32 completed_sim_tick)
{
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	sSYNetPeerGlobalCommitGen++;
	if (syNetPeerTickDiagLevel() >= 1)
	{
		port_log("SSB64 NetPeer: phase_lock_commit gen=%u completed_sim=%u next_sim=%u hr=%u D=%u\n",
		         (unsigned int)sSYNetPeerGlobalCommitGen,
		         (unsigned int)completed_sim_tick,
		         (unsigned int)syNetInputGetTick(),
		         (unsigned int)sSYNetPeerHighestRemoteTick,
		         (unsigned int)syNetPeerGetCommittedInputDelay());
	}
}

static sb32 syNetPeerGetSharedConfirmedSimFrontier(u32 *out_tick)
{
	s32 best;
	s32 v;
	u32 remote_frontier;

	if (out_tick == NULL)
	{
		return FALSE;
	}
	best = 0x7FFFFFFF;
	if (sSYNetPeerHighestRemoteTick != 0U)
	{
		remote_frontier = syNetPeerDelaySimTickFromWire(sSYNetPeerHighestRemoteTick);
		if ((s32)remote_frontier < best)
		{
			best = (s32)remote_frontier;
		}
	}
	if ((sSYNetPeerLocalPlayer >= 0) && (sSYNetPeerLocalPlayer < MAXCONTROLLERS) &&
	    (sSYNetPeerMergedConnectDisc[sSYNetPeerLocalPlayer] == 0U))
	{
		v = sSYNetPeerMergedConnectLastTick[sSYNetPeerLocalPlayer];
		if ((v >= 0) && (v < best))
		{
			best = v;
		}
	}
	if ((sSYNetPeerExtraLocalSenderSlot >= 0) && (sSYNetPeerExtraLocalSenderSlot < MAXCONTROLLERS) &&
	    (sSYNetPeerMergedConnectDisc[sSYNetPeerExtraLocalSenderSlot] == 0U))
	{
		v = sSYNetPeerMergedConnectLastTick[sSYNetPeerExtraLocalSenderSlot];
		if ((v >= 0) && (v < best))
		{
			best = v;
		}
	}
	if (best == 0x7FFFFFFF)
	{
		return FALSE;
	}
	*out_tick = (u32)best;
	return TRUE;
}

static sb32 syNetPeerRemoteInputsPresentForWireTick(u32 wire_tick)
{
	s32 i;
	s32 n;
	s32 slot;

	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputHasRemoteInputForWireTick(slot, wire_tick) == FALSE)
		{
			return FALSE;
		}
	}
	return TRUE;
}

static u32 sSYNetPeerRunwayPredictMaxSimDeficit = SYNETPEER_RUNWAY_PREDICT_MAX_SIM_DEFICIT_DEFAULT;
static u32 sSYNetPeerRunwayPredictIngressSlack = SYNETPEER_RUNWAY_PREDICT_INGRESS_SLACK_DEFAULT;
static u32 sSYNetPeerRunwayPredictHoldLogsRemaining = 8U;

static void syNetPeerRefreshRunwayPredictLimitsFromEnv(void)
{
	const char *e;
	s32 v;

	sSYNetPeerRunwayPredictMaxSimDeficit = SYNETPEER_RUNWAY_PREDICT_MAX_SIM_DEFICIT_DEFAULT;
	e = getenv("SSB64_NETPLAY_RUNWAY_PREDICT_MAX_SIM_DEFICIT");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v >= 0)
		{
			sSYNetPeerRunwayPredictMaxSimDeficit = (u32)v;
		}
	}
	if (sSYNetPeerRunwayPredictMaxSimDeficit > 16U)
	{
		sSYNetPeerRunwayPredictMaxSimDeficit = 16U;
	}
	sSYNetPeerRunwayPredictIngressSlack = SYNETPEER_RUNWAY_PREDICT_INGRESS_SLACK_DEFAULT;
	e = getenv("SSB64_NETPLAY_RUNWAY_PREDICT_INGRESS_SLACK");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v >= 0)
		{
			sSYNetPeerRunwayPredictIngressSlack = (u32)v;
		}
	}
	if (sSYNetPeerRunwayPredictIngressSlack > 16U)
	{
		sSYNetPeerRunwayPredictIngressSlack = 16U;
	}
}

static u32 syNetPeerRunwaySimDeficitFromHr(u32 sim_tick, u32 hr)
{
	u32 remote_sim_frontier;

	if (hr == 0U)
	{
		return 0U;
	}
	remote_sim_frontier = syNetPeerDelaySimTickFromWire(hr);
	if (sim_tick <= remote_sim_frontier)
	{
		return 0U;
	}
	return sim_tick - remote_sim_frontier;
}

static sb32 syNetPeerShouldHoldCommitForRunwayDeficit(u32 sim_tick, u32 hr, u32 *out_deficit)
{
	u32 deficit;

	deficit = syNetPeerRunwaySimDeficitFromHr(sim_tick, hr);
	if (out_deficit != NULL)
	{
		*out_deficit = deficit;
	}
	if (deficit > sSYNetPeerRunwayPredictMaxSimDeficit)
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetPeerMaybeLogRunwayPredictHold(u32 sim_tick, u32 hr, u32 deficit, const char *reason)
{
	if (sSYNetPeerRunwayPredictHoldLogsRemaining == 0U)
	{
		return;
	}
	port_log(
	    "SSB64 NetPeer: runway_predict_hold sim=%u hr=%u deficit=%u max_deficit=%u ingress_slack=%u reason=%s\n",
	    sim_tick,
	    hr,
	    deficit,
	    (unsigned int)sSYNetPeerRunwayPredictMaxSimDeficit,
	    (unsigned int)sSYNetPeerRunwayPredictIngressSlack,
	    reason);
	sSYNetPeerRunwayPredictHoldLogsRemaining--;
}

static u32 syNetPeerRollbackEffectivePredictionWindow(u32 sim_tick, u32 base_window)
{
	u32 hr;
	u32 required_wire;
	u32 ingress_deficit;
	u32 slack;

	if ((base_window == 0U) || (sSYNetPeerHighestRemoteTick == 0U))
	{
		return base_window;
	}
	required_wire = syNetPeerGetBaseRequiredWireTick(sim_tick);
	hr = sSYNetPeerHighestRemoteTick;
	if (hr >= required_wire)
	{
		return base_window;
	}
	ingress_deficit = required_wire - hr;
	slack = sSYNetPeerRunwayPredictIngressSlack;
	if (ingress_deficit > slack)
	{
		ingress_deficit = slack;
	}
	return base_window + ingress_deficit;
}

void syNetPeerEvaluateSharedCommitStep(u32 sim_tick, SYNetPeerSharedCommitStep *out)
{
	u32 shared_confirmed;
	u32 prediction_window;
	u32 required_wire;
	sb32 have_shared_frontier;
	sb32 ring_ready;

	if (out == NULL)
	{
		return;
	}
	out->advance = TRUE;
	out->uses_prediction = FALSE;
	out->hold_reason = 'P';
	out->sim_tick = sim_tick;
	out->required_wire = syNetPeerGetBaseRequiredWireTick(sim_tick);
	out->shared_confirmed_sim = 0U;
	out->prediction_window = syNetPeerGetPhaseLockPredictionWindowTicks();
	out->commit_gen = sSYNetPeerGlobalCommitGen;

	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		out->advance = FALSE;
		out->hold_reason = 'E';
		return;
	}
	if (syNetPeerBootstrapIngressSymmetrySatisfied() == FALSE)
	{
		out->advance = FALSE;
		out->hold_reason = 'E';
		return;
	}
	required_wire = out->required_wire;
	ring_ready = syNetPeerRemoteInputsPresentForWireTick(required_wire);
	if (ring_ready != FALSE)
	{
		return;
	}
	prediction_window = out->prediction_window;
	have_shared_frontier = syNetPeerGetSharedConfirmedSimFrontier(&shared_confirmed);
	if (have_shared_frontier != FALSE)
	{
		out->shared_confirmed_sim = shared_confirmed;
	}
	if (sSYNetPeerHighestRemoteTick != 0U)
	{
		u32 hr;
		u32 runway_deficit;

		hr = sSYNetPeerHighestRemoteTick;
		if (syNetPeerShouldHoldCommitForRunwayDeficit(sim_tick, hr, &runway_deficit) != FALSE)
		{
			syNetPeerMaybeLogRunwayPredictHold(sim_tick, hr, runway_deficit, "sim_deficit");
			syNetPeerPumpIngressTransport("runway_hold");
			out->advance = FALSE;
			out->hold_reason = 'R';
			return;
		}
		if (syNetPeerShouldHoldSimTickForSkewPacing(sim_tick, NULL) != FALSE)
		{
			runway_deficit = syNetPeerRunwaySimDeficitFromHr(sim_tick, hr);
			syNetPeerMaybeLogRunwayPredictHold(sim_tick, hr, runway_deficit, "skew_lead");
			syNetPeerPumpIngressTransport("runway_hold");
			out->advance = FALSE;
			out->hold_reason = 'R';
			return;
		}
	}
	if (syNetSessionParamsRollbackEnabled() != FALSE)
	{
		u32 epoch_cap;
		u32 cap_source;

		if ((syNetRollbackIsResimulating() == FALSE) &&
		    (syNetRollbackGetLiveSimCap(&epoch_cap, &cap_source) != FALSE) && (epoch_cap != ~(u32)0) &&
		    (sim_tick > epoch_cap))
		{
			(void)syNetRollbackShouldBlockLiveBattleAdvance(sim_tick);
			syNetPeerPumpIngressTransport("rollback_epoch_hold");
			out->advance = FALSE;
			out->hold_reason = 'B';
			return;
		}
	}
	if ((syNetSessionParamsRollbackEnabled() != FALSE) && (sSYNetPeerHighestRemoteTick != 0U))
	{
		u32 remote_sim_frontier;
		u32 rollback_sim_cap;
		u32 epoch_cap;
		u32 effective_cap;

		remote_sim_frontier = syNetPeerDelaySimTickFromWire(sSYNetPeerHighestRemoteTick);
		rollback_sim_cap = remote_sim_frontier + syNetPeerGetCommittedInputDelay() + prediction_window;
		effective_cap = rollback_sim_cap;
		if ((syNetRollbackIsResimulating() == FALSE) &&
		    (syNetRollbackGetLiveSimCap(&epoch_cap, NULL) != FALSE) && (epoch_cap != ~(u32)0) &&
		    (epoch_cap < effective_cap))
		{
			effective_cap = epoch_cap;
		}
		if (sim_tick > effective_cap)
		{
			if (effective_cap == epoch_cap)
			{
				(void)syNetRollbackShouldBlockLiveBattleAdvance(sim_tick);
			}
			syNetPeerPumpIngressTransport(
			    (effective_cap == epoch_cap) ? "rollback_epoch_hold" : "rollback_frontier_cap");
			out->advance = FALSE;
			out->hold_reason = (effective_cap == epoch_cap) ? 'B' : 'R';
			return;
		}
	}
	if ((syNetInputGetUseInputPrediction() != FALSE) && (prediction_window > 0U))
	{
		sb32 predict_ok = FALSE;

		/*
		 * Rollback sessions: gate prediction on observed remote wire frontier (hr), not the connect-ack
		 * min() frontier — connect rows often lag hr by >phase_lock and forced STRICT stalls despite rollback.
		 */
		if ((syNetSessionParamsRollbackEnabled() != FALSE) && (sSYNetPeerHighestRemoteTick != 0U))
		{
			u32 remote_sim_frontier;
			u32 effective_window;

			remote_sim_frontier = syNetPeerDelaySimTickFromWire(sSYNetPeerHighestRemoteTick);
			out->shared_confirmed_sim = remote_sim_frontier;
			effective_window = syNetPeerRollbackEffectivePredictionWindow(sim_tick, prediction_window);
			if ((u64)sim_tick <= ((u64)remote_sim_frontier + (u64)effective_window))
			{
				predict_ok = TRUE;
			}
		}
		else if ((have_shared_frontier != FALSE) &&
		         ((u64)sim_tick <= ((u64)shared_confirmed + (u64)prediction_window)))
		{
			predict_ok = TRUE;
		}
		if (predict_ok != FALSE)
		{
			out->uses_prediction = TRUE;
			return;
		}
	}
	out->advance = FALSE;
	out->hold_reason = 'R';
}
#endif

static u32 syNetPeerExecSyncComputeViPhaseBucket(void)
{
	return syNetPeerCurrentViPhaseBucketNow();
}

static void syNetPeerSendBattleExecSyncPacket(u32 agreed_sim_tick, u32 vi_phase_bucket)
{
	u8 buffer[SYNETPEER_BATTLE_EXEC_SYNC_BYTES];
	u8 *cursor = buffer;
	u32 checksum;
	int push_diag;
	int sent;

	memset(buffer, 0, sizeof(buffer));
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_BATTLE_EXEC_SYNC);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, agreed_sim_tick);
	push_diag = port_get_push_frame_count();
	syNetPeerWriteU32(&cursor, (u32)push_diag);
	syNetPeerWriteU32(&cursor, vi_phase_bucket);
	checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_BATTLE_EXEC_SYNC_BYTES - 4);
	syNetPeerWriteU32(&cursor, checksum);
	sent = syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)SYNETPEER_BATTLE_EXEC_SYNC_BYTES,
				 &sSYNetPeerPeerAddress);
	if (sent != (int)SYNETPEER_BATTLE_EXEC_SYNC_BYTES)
	{
		port_log(
		    "SSB64 NetPeer: battle_exec_sync send_fail role=%s bytes=%d sent=%d err=%d peer=%s:%u tick=%u vi_phase=%u push=%d\n",
		    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", (int)SYNETPEER_BATTLE_EXEC_SYNC_BYTES, (int)sent,
		    syNetPeerOsSocketLastError(), inet_ntoa(sSYNetPeerPeerAddress.sin_addr),
		    (unsigned int)ntohs(sSYNetPeerPeerAddress.sin_port), (unsigned int)agreed_sim_tick,
		    (unsigned int)vi_phase_bucket, push_diag);
		return;
	}
	port_log(
	    "SSB64 NetPeer: battle_exec_sync send_ok role=%s bytes=%d peer=%s:%u tick=%u vi_phase=%u push=%d\n",
	    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", (int)SYNETPEER_BATTLE_EXEC_SYNC_BYTES,
	    inet_ntoa(sSYNetPeerPeerAddress.sin_addr), (unsigned int)ntohs(sSYNetPeerPeerAddress.sin_port),
	    (unsigned int)agreed_sim_tick, (unsigned int)vi_phase_bucket, push_diag);
	sSYNetPeerPacketsSent++;
}

static void syNetPeerHandleBattleExecSyncPacket(const u8 *buffer, s32 size)
{
	const u8 *c = buffer;
	u32 magic;
	u16 wire_version;
	u16 packet_type;
	u32 session_id;
	u32 agreed_tick;
	u32 peer_push_diag;
	u32 vi_phase_wire;
	u32 local_vi_phase;
	u32 checksum;
	u32 expected_checksum;

	if ((size != (s32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES) && (size != (s32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES_LEGACY))
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, (u32)size - 4U);
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	agreed_tick = syNetPeerReadU32(&c);
	peer_push_diag = syNetPeerReadU32(&c);
	if ((u32)size == (u32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES_LEGACY)
	{
		vi_phase_wire = 0U;
	}
	else
	{
		vi_phase_wire = syNetPeerReadU32(&c);
	}
	checksum = syNetPeerReadU32(&c);
	if ((magic != SYNETPEER_MAGIC) || (wire_version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_BATTLE_EXEC_SYNC) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		port_log(
		    "SSB64 NetPeer: battle_exec_sync drop role=%s size=%d magic=0x%08X wire=%u type=%u sess=%u expect_sess=%u csum=0x%08X expect=0x%08X\n",
		    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", size, (unsigned int)magic,
		    (unsigned int)wire_version, (unsigned int)packet_type, (unsigned int)session_id,
		    (unsigned int)sSYNetPeerSessionID, (unsigned int)checksum, (unsigned int)expected_checksum);
		sSYNetPeerPacketsDropped++;
		return;
	}
	sSYNetPeerPacketsReceived++;
	port_log(
	    "SSB64 NetPeer: battle_exec_sync recv role=%s agreed_tick=%u peer_push=%u vi_phase=%u sim=%u host_sent=%d host_echo=%d client_got=%d client_echo=%d\n",
	    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
	    (unsigned int)agreed_tick, (unsigned int)peer_push_diag, (unsigned int)vi_phase_wire,
	    (unsigned int)syNetInputGetTick(),
	    (sSYNetPeerExecSyncHostSent != FALSE) ? 1 : 0,
	    (sSYNetPeerExecSyncHostPeerEcho != FALSE) ? 1 : 0,
	    (sSYNetPeerExecSyncClientGotHost != FALSE) ? 1 : 0,
	    (sSYNetPeerExecSyncClientEchoSent != FALSE) ? 1 : 0);
	local_vi_phase = syNetPeerExecSyncComputeViPhaseBucket();
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		if ((sSYNetPeerExecSyncHostSent != FALSE) && (agreed_tick == sSYNetPeerExecSyncHostProposedTick))
		{
			if (sSYNetPeerExecSyncHostPeerEcho == FALSE)
			{
				if ((vi_phase_wire != 0U) && (vi_phase_wire != sSYNetPeerExecSyncHostViPhase))
				{
					port_log(
					    "SSB64 NetPeer: battle_exec_sync host echo WARN vi_phase_wire=%u host_vi_phase=%u tick=%u peer_push_diag=%u\n",
					    vi_phase_wire, sSYNetPeerExecSyncHostViPhase, agreed_tick, peer_push_diag);
				}
				port_log(
				    "SSB64 NetPeer: battle_exec_sync host echo ok tick=%u vi_phase=%u peer_push_diag=%u local_tick=%u local_push=%d local_vi_phase=%u\n",
				    agreed_tick, vi_phase_wire, peer_push_diag, syNetInputGetTick(), port_get_push_frame_count(),
				    local_vi_phase);
			}
			sSYNetPeerExecSyncHostPeerEcho = TRUE;
		}
	}
	else
	{
		if (sSYNetPeerExecSyncClientGotHost == FALSE)
		{
			if (agreed_tick != syNetInputGetTick())
			{
				port_log(
				    "SSB64 NetPeer: battle_exec_sync client WARN host tick=%u local_sim=%u (expected frozen match pre-exec)\n",
				    agreed_tick, syNetInputGetTick());
				syNetDesyncClassifierOnFrameIdentityMismatch(syNetInputGetTick());
			}
			sSYNetPeerExecSyncAgreedTick = agreed_tick;
			sSYNetPeerExecSyncPeerViPhaseLatch = vi_phase_wire;
			if ((vi_phase_wire != 0U) && (local_vi_phase != 0U) && (vi_phase_wire != local_vi_phase))
			{
				port_log(
				    "SSB64 NetPeer: battle_exec_sync client WARN host_vi_phase=%u local_vi_phase=%u tick=%u (deadline bucket mismatch)\n",
				    vi_phase_wire, local_vi_phase, agreed_tick);
			}
			sSYNetPeerExecSyncClientGotHost = TRUE;
			port_log(
			    "SSB64 NetPeer: battle_exec_sync client latched tick=%u host_vi_phase=%u local_vi_phase=%u host_push_diag=%u local_push=%d local_tm=%u\n",
			    agreed_tick, vi_phase_wire, local_vi_phase, peer_push_diag, port_get_push_frame_count(),
			    (u32)dSYTaskmanFrameCount);
		}
		else if (agreed_tick != sSYNetPeerExecSyncAgreedTick)
		{
			port_log("SSB64 NetPeer: battle_exec_sync client ignore conflicting tick=%u (latched %u)\n", agreed_tick,
			         sSYNetPeerExecSyncAgreedTick);
		}
	}
	syNetPeerMaybeLatchBothSidesStartupAfterExecSync();
}

static void syNetPeerBattleExecSyncServiceTransport(void)
{
	u32 tick_now;

	if (syNetPeerRequireBattleExecSync() == FALSE)
	{
		return;
	}
	if ((sSYNetPeerIsActive == FALSE) || (sSYNetPeerBattleBarrierReleased == FALSE))
	{
		return;
	}
	if (syNetPeerInputBindIsComplete() == FALSE)
	{
		return;
	}
	if (syNetPeerBattleExecSyncIsComplete() != FALSE)
	{
		return;
	}
	sSYNetPeerExecSyncPumpCount++;
	if ((sSYNetPeerExecSyncPumpCount & 63U) == 0U)
	{
		port_log("SSB64 NetPeer: battle_exec_sync pump role=%s sim=%u bind=%d host_sent=%d host_echo=%d client_got=%d client_echo=%d peer_ready=%d\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         (unsigned int)syNetInputGetTick(),
		         (syNetPeerInputBindIsComplete() != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncHostSent != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncHostPeerEcho != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncClientGotHost != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncClientEchoSent != FALSE) ? 1 : 0,
		         (sSYNetPeerBattlePeerReady != FALSE) ? 1 : 0);
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		tick_now = syNetInputGetTick();
		if (sSYNetPeerExecSyncHostSent == FALSE)
		{
			u32 vi_ph;

			vi_ph = syNetPeerExecSyncComputeViPhaseBucket();
			sSYNetPeerExecSyncHostViPhase = vi_ph;
			syNetPeerSendBattleExecSyncPacket(tick_now, vi_ph);
			sSYNetPeerExecSyncHostProposedTick = tick_now;
			sSYNetPeerExecSyncHostSent = TRUE;
			port_log("SSB64 NetPeer: battle_exec_sync host propose tick=%u vi_phase=%u local_push=%d taskman=%u\n", tick_now,
			         vi_ph, port_get_push_frame_count(), (u32)dSYTaskmanFrameCount);
		}
		else if ((sSYNetPeerExecSyncPumpCount & 3U) == 0U)
		{
			syNetPeerSendBattleExecSyncPacket(sSYNetPeerExecSyncHostProposedTick, sSYNetPeerExecSyncHostViPhase);
		}
	}
	else
	{
		if (sSYNetPeerExecSyncClientGotHost == FALSE)
		{
			return;
		}
		if (sSYNetPeerExecSyncClientEchoSent == FALSE)
		{
			syNetPeerSendBattleExecSyncPacket(sSYNetPeerExecSyncAgreedTick, sSYNetPeerExecSyncPeerViPhaseLatch);
			sSYNetPeerExecSyncClientEchoSent = TRUE;
			port_log("SSB64 NetPeer: battle_exec_sync client echo tick=%u vi_phase=%u local_push=%d taskman=%u\n",
			         sSYNetPeerExecSyncAgreedTick, sSYNetPeerExecSyncPeerViPhaseLatch, port_get_push_frame_count(),
			         (u32)dSYTaskmanFrameCount);
		}
		else if ((sSYNetPeerExecSyncPumpCount & 3U) == 0U)
		{
			syNetPeerSendBattleExecSyncPacket(sSYNetPeerExecSyncAgreedTick, sSYNetPeerExecSyncPeerViPhaseLatch);
		}
	}
	syNetPeerMaybeLatchBothSidesStartupAfterExecSync();
}

static const char *syNetPeerAbbrevSlotSource(SYNetInputSource s)
{
	switch (s)
	{
	case nSYNetInputSourceLocal:
		return "Loc";
	case nSYNetInputSourceRemoteConfirmed:
		return "RConf";
	case nSYNetInputSourceRemotePredicted:
		return "RPred";
	case nSYNetInputSourceSaved:
		return "Saved";
	default:
		return "?";
	}
}

void syNetPeerHandleMatchConfigPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	SYNetInputReplayMetadata metadata;
	u32 magic;
	u32 session_id;
	u32 checksum;
	u32 expected_checksum;
	u16 version;
	u16 packet_type;

	if (size != SYNETPEER_BOOTSTRAP_PACKET_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_BOOTSTRAP_PACKET_BYTES - 4);

	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	syNetPeerReadMetadata(&cursor, &metadata);
	checksum = syNetPeerReadU32(&cursor);

	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
		(packet_type != SYNETPEER_PACKET_MATCH_CONFIG) ||
		(session_id != sSYNetPeerSessionID) || (checksum != expected_checksum) ||
		(syNetPeerCheckMetadata(&metadata) == FALSE))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
#if defined(SSB64_NETMENU)
	if ((sSYNetPeerAutomatchHandshakeActive != FALSE) && (sSYNetPeerBootstrapIsHost == FALSE))
	{
		if ((metadata.scene_kind != (u32)nSCKindVSBattle) || (metadata.player_count != 2U) ||
		    (metadata.stocks != 3U) || ((s32)metadata.time_limit != SCBATTLE_TIMELIMIT_INFINITE) ||
		    (metadata.game_rules != SCBATTLE_GAMERULE_STOCK) ||
		    (metadata.game_type != (u8)nSCBattleGameTypeRoyal) ||
		    (metadata.stage_kind == (u32)(0xDE)) || (metadata.item_toggles != ~(u32)0) ||
		    (metadata.netplay_sim_slot_host_hw != 0U) || (metadata.netplay_sim_slot_client_hw != 1U))
		{
			sSYNetPeerPacketsDropped++;
			return;
		}
	}
#endif /* SSB64_NETMENU */
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		syNetPeerStageBootstrapMetadata(&metadata);
	}
}

void syNetPeerHandleBootstrapPacket(const u8 *buffer, s32 size)
{
#if defined(PORT)
	if (size >= (s32)(4 + 2 + 2 + 4))
	{
		const u8 *h = buffer;
		u32 magic = syNetPeerReadU32(&h);
		(void)syNetPeerReadU16(&h);
		u16 packet_type = syNetPeerReadU16(&h);
		u32 session_id = syNetPeerReadU32(&h);

		if ((magic == SYNETPEER_MAGIC) && (session_id == sSYNetPeerSessionID) &&
		    ((packet_type == SYNETPEER_PACKET_UDP_SYNC_REQ) || (packet_type == SYNETPEER_PACKET_UDP_SYNC_REP)) &&
		    (size == (s32)SYNETPEER_UDP_SYNC_PACKET_BYTES))
		{
			syNetPeerHandleUdpSyncIngress(buffer, size);
			return;
		}
	}
#endif
	if (size == SYNETPEER_CONTROL_PACKET_BYTES)
	{
		syNetPeerHandleControlPacket(buffer, size);
	}
	else if (size == SYNETPEER_BOOTSTRAP_PACKET_BYTES)
	{
		syNetPeerHandleMatchConfigPacket(buffer, size);
	}
#if defined(SSB64_NETMENU)
	else if (size == SYNETPEER_AUTOMATCH_OFFER_BYTES)
	{
		syNetPeerHandleAutomatchOfferPacket(buffer, size);
	}
#endif
	else
	{
		sSYNetPeerPacketsDropped++;
	}
}

void syNetPeerReceiveBootstrapPackets(void)
{
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];

	if (syNetPeerDatagramSocketIsUsable() == FALSE)
	{
		return;
	}
	while (TRUE)
	{
		sb32 wb = FALSE;
		int size = syNetPeerOsRecvFrom(sSYNetPeerSocket, buffer, sizeof(buffer), &wb);

		if (size < 0)
		{
			if (wb == FALSE)
			{
				sSYNetPeerPacketsDropped++;
			}
			break;
		}
		syNetPeerHandleBootstrapPacket(buffer, (s32)size);
	}
}

#if defined(PORT) && defined(SSB64_NETMENU)
static void syNetPeerResetAutomatchBootstrapAttemptState(void)
{
	sSYNetPeerBootstrapRunInProgress = FALSE;
	sSYNetPeerIsActive = FALSE;
	sSYNetPeerBootstrapPeerReady = FALSE;
	sSYNetPeerBootstrapStartReceived = FALSE;
	sSYNetPeerBootstrapMetadataApplied = FALSE;
	sSYNetPeerBootstrapMetadataStaged = FALSE;
	sSYAutoGotPeerOffer = FALSE;
	sAutoPeerNonce = 0U;
	sAutoPeerFkind = 0U;
	sAutoPeerBanMask = 0U;
	sAutoPeerCostume = 0U;
	sSYNetPeerUdpLinkComplete = TRUE;
	sSYNetPeerUdpLinkRoundsRemaining = 0U;
	sSYNetPeerUdpLinkPendingToken = 0;
	sSYNetPeerUdpLinkRepConsumedToken = 0U;
	sSYNetPeerUdpLinkSyncSkippedFallback = FALSE;
	sSYNetPeerBootstrapTimingCached = FALSE;
	syNetPeerResetStageSceneRendezvousState();
	syNetPeerInputBindReset();
	syNetPeerBattleExecSyncReset();
	syNetPeerResetBootstrapIngressSymmetryState();
}

static void syNetPeerResetAutomatchBootstrapTransportState(void)
{
	syNetPeerCloseSocket();
	syNetPeerResetAutomatchBootstrapAttemptState();
}

void syNetPeerCancelAutomatchBootstrap(void)
{
	syNetPeerResetAutomatchBootstrapTransportState();
}

void syNetPeerPauseBetweenBootstrapAttempts(void)
{
	u32 i;
	u32 n;

	n = syNetPeerBootstrapPauseBetweenAttempts();
	for (i = 0U; i < n; i++)
	{
		syNetPeerSleepBootstrapRetry();
	}
}
#endif /* PORT && SSB64_NETMENU */

static void syNetPeerBootstrapFailTeardown(void)
{
#if defined(PORT)
	sSYNetPeerBootstrapRunInProgress = FALSE;
#endif
#if defined(PORT) && defined(SSB64_NETMENU)
	syNetPeerResetAutomatchBootstrapTransportState();
#else
	syNetPeerCloseSocket();
	sSYNetPeerIsActive = FALSE;
	sSYNetPeerBootstrapPeerReady = FALSE;
	sSYNetPeerBootstrapStartReceived = FALSE;
	sSYNetPeerBootstrapMetadataApplied = FALSE;
	sSYNetPeerBootstrapMetadataStaged = FALSE;
#endif
}

sb32 syNetPeerRunBootstrap(void)
{
	s32 i;
#ifdef SSB64_NETMENU
	sb32 handshake;

	handshake = (sSYNetPeerAutomatchHandshakeActive != FALSE) ? TRUE : FALSE;
#else
	sb32 handshake = FALSE;
#endif

	if (sSYNetPeerIsConfigured == FALSE)
	{
		return FALSE;
	}
	if (sSYNetPeerBootstrapIsEnabled == FALSE)
	{
		return TRUE;
	}
	syNetPeerRefreshBootstrapTimingFromEnv();
#if defined(PORT) && defined(SSB64_NETMENU)
	syNetPeerResetAutomatchBootstrapTransportState();
#endif
	if (syNetPeerOpenSocket() == FALSE)
	{
		return FALSE;
	}
#if defined(PORT)
	sSYNetPeerBootstrapRunInProgress = TRUE;
#endif
	sSYNetPeerIsActive = TRUE;
#if defined(PORT)
	if (syNetPeerRunUdpLinkSync() == FALSE)
	{
		syNetPeerBootstrapFailTeardown();
		return FALSE;
	}
#endif

#if defined(SSB64_NETMENU)
	if (handshake != FALSE)
	{
		if (syNetPeerAutomatchExchangeOffers() == FALSE)
		{
			syNetPeerBootstrapFailTeardown();
			return FALSE;
		}
	}
#endif

	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
#if defined(SSB64_NETMENU)
		if (handshake != FALSE)
		{
			(void)syNetPeerComposeAutomatchMatchMetadata();
			syNetPeerStageBootstrapMetadata(&sSYNetPeerBootstrapMetadata);
		}
		else
#endif
		{
			syNetPeerMakeBootstrapMetadata(&sSYNetPeerBootstrapMetadata);
			syNetPeerStageBootstrapMetadata(&sSYNetPeerBootstrapMetadata);
		}

		for (i = 0; i < (s32)syNetPeerBootstrapRetryCount(); i++)
		{
			syNetPeerSendMatchConfigPacket();
			syNetPeerReceiveBootstrapPackets();

			if (sSYNetPeerBootstrapPeerReady != FALSE)
			{
				break;
			}
			syNetPeerSleepBootstrapRetry();
		}
		if (sSYNetPeerBootstrapPeerReady == FALSE)
		{
			port_log("SSB64 NetPeer: bootstrap host timed out waiting for READY\n");
			syNetPeerBootstrapFailTeardown();
			return FALSE;
		}
		{
			u32 burst;
			u32 b;

			burst = syNetPeerBootstrapStartBurstCount();
			for (b = 0U; b < burst; b++)
			{
				syNetPeerSendControlPacket(SYNETPEER_PACKET_START);
				syNetPeerSleepBootstrapRetry();
			}
		}
		port_log("SSB64 NetPeer: bootstrap host sent START stage=%u seed=%u\n",
		         sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed);
#if defined(PORT)
		sSYNetPeerBootstrapRunInProgress = FALSE;
		sSYNetPeerIsActive = FALSE;
#endif
		return TRUE;
	}

#if defined(SSB64_NETMENU)
	if (handshake != FALSE)
	{
		syNetPeerSendControlPacket(SYNETPEER_PACKET_READY);
	}
#endif
#if defined(SSB64_NETMENU)
	if (handshake == FALSE)
#endif
	{
		for (i = 0; i < (s32)syNetPeerBootstrapRetryCount(); i++)
		{
			syNetPeerReceiveBootstrapPackets();

			if ((sSYNetPeerBootstrapMetadataApplied != FALSE) || (sSYNetPeerBootstrapMetadataStaged != FALSE))
			{
				syNetPeerSendControlPacket(SYNETPEER_PACKET_READY);
				break;
			}
			syNetPeerSleepBootstrapRetry();
		}
	}

	if ((sSYNetPeerBootstrapMetadataApplied == FALSE) && (sSYNetPeerBootstrapMetadataStaged == FALSE))
	{
		port_log("SSB64 NetPeer: bootstrap client timed out waiting for MATCH_CONFIG\n");
		syNetPeerBootstrapFailTeardown();
		return FALSE;
	}
	for (i = 0; i < (s32)syNetPeerBootstrapRetryCount(); i++)
	{
		syNetPeerSendControlPacket(SYNETPEER_PACKET_READY);
		syNetPeerReceiveBootstrapPackets();

		if (sSYNetPeerBootstrapStartReceived != FALSE)
		{
			break;
		}
		syNetPeerSleepBootstrapRetry();
	}
	if (sSYNetPeerBootstrapStartReceived == FALSE)
	{
		port_log("SSB64 NetPeer: bootstrap client timed out waiting for START\n");
		syNetPeerBootstrapFailTeardown();
		return FALSE;
	}
	port_log("SSB64 NetPeer: bootstrap client received START stage=%u seed=%u\n",
	         sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed);
#if defined(PORT)
	sSYNetPeerBootstrapRunInProgress = FALSE;
	sSYNetPeerIsActive = FALSE;
#endif
	return TRUE;
}

#if defined(SSB64_NETMENU)

static void syNetPeerResetStageSceneRendezvousState(void)
{
	sSYNetPeerStageSceneRendezvousArmed = FALSE;
	sSYNetPeerStageSceneLocalReadySent = FALSE;
	sSYNetPeerStageScenePeerReady = FALSE;
	sSYNetPeerStageScenePeerReadyLogged = FALSE;
	sSYNetPeerStageSceneGoSent = FALSE;
	sSYNetPeerStageSceneGoReceived = FALSE;
	sSYNetPeerStageSceneGoReceivedLogged = FALSE;
	sSYNetPeerStageSceneGoDeadlineValid = FALSE;
	sSYNetPeerStageSceneGoDeadlineUnixMs = 0ULL;
	sSYNetPeerStageSceneGoSendRepeatFrames = 0U;
}

static u32 syNetPeerStageSceneGoHoldMs(void)
{
	const char *e;
	int v;

	v = (s32)SYNETPEER_STAGE_SCENE_GO_HOLD_MS_DEFAULT;
	e = getenv("SSB64_NETPLAY_STAGE_SCENE_GO_HOLD_MS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
	}
	if (v < 100)
	{
		v = 100;
	}
	if (v > 8000)
	{
		v = 8000;
	}
	return (u32)v;
}

sb32 syNetPeerBeginStageSceneRendezvous(void)
{
	if ((sSYNetPeerIsActive == FALSE) || (sSYNetPeerBootstrapIsEnabled == FALSE))
	{
		return FALSE;
	}
	syNetPeerResetStageSceneRendezvousState();
	sSYNetPeerStageSceneRendezvousArmed = TRUE;
	return TRUE;
}

sb32 syNetPeerUpdateStageSceneRendezvous(void)
{
	u64 now_ms;

	if ((sSYNetPeerIsActive == FALSE) || (sSYNetPeerStageSceneRendezvousArmed == FALSE))
	{
		return FALSE;
	}
	syNetPeerPumpIngressTransport("staging");
	if (sSYNetPeerStageSceneGoDeadlineValid == FALSE)
	{
		syNetPeerSendControlPacket(SYNETPEER_PACKET_STAGE_SCENE_READY);
		sSYNetPeerStageSceneLocalReadySent = TRUE;
	}
	if ((sSYNetPeerBootstrapIsHost != FALSE) && (sSYNetPeerStageScenePeerReady != FALSE) &&
	    (sSYNetPeerStageSceneGoSent == FALSE))
	{
		now_ms = syNetPeerNowUnixMs();
		sSYNetPeerStageSceneGoDeadlineUnixMs = now_ms + (u64)syNetPeerStageSceneGoHoldMs();
		sSYNetPeerStageSceneGoDeadlineValid = TRUE;
		sSYNetPeerStageSceneGoSent = TRUE;
		{
			u32 hold_ms;
			u32 repeat;

			hold_ms = syNetPeerStageSceneGoHoldMs();
			repeat = SYNETPEER_STAGE_SCENE_GO_REPEAT_DEFAULT;
			if (hold_ms > 1500U)
			{
				repeat = hold_ms / 50U;
			}
			if (repeat < 30U)
			{
				repeat = 30U;
			}
			if (repeat > 120U)
			{
				repeat = 120U;
			}
			sSYNetPeerStageSceneGoSendRepeatFrames = repeat;
		}
		syNetPeerSendControlPacket(SYNETPEER_PACKET_STAGE_SCENE_GO);
		port_log("SSB64 NetPeer: staging_go host deadline_ms=%llu hold_ms=%u\n",
		         (unsigned long long)sSYNetPeerStageSceneGoDeadlineUnixMs,
		         (unsigned int)syNetPeerStageSceneGoHoldMs());
	}
	if ((sSYNetPeerBootstrapIsHost != FALSE) && (sSYNetPeerStageSceneGoSendRepeatFrames > 0U))
	{
		syNetPeerSendControlPacket(SYNETPEER_PACKET_STAGE_SCENE_GO);
		sSYNetPeerStageSceneGoSendRepeatFrames--;
	}
	if ((sSYNetPeerBootstrapIsHost == FALSE) && (sSYNetPeerStageSceneGoReceived != FALSE) &&
	    (sSYNetPeerStageSceneGoDeadlineValid == FALSE))
	{
		now_ms = syNetPeerNowUnixMs();
		sSYNetPeerStageSceneGoDeadlineUnixMs = now_ms + (u64)syNetPeerStageSceneGoHoldMs();
		sSYNetPeerStageSceneGoDeadlineValid = TRUE;
		port_log("SSB64 NetPeer: staging_go client deadline_ms=%llu hold_ms=%u\n",
		         (unsigned long long)sSYNetPeerStageSceneGoDeadlineUnixMs,
		         (unsigned int)syNetPeerStageSceneGoHoldMs());
	}
	if (sSYNetPeerStageSceneGoDeadlineValid == FALSE)
	{
		return FALSE;
	}
	now_ms = syNetPeerNowUnixMs();
	if (now_ms < sSYNetPeerStageSceneGoDeadlineUnixMs)
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetPeerSetAutomatchNegotiation(sb32 enabled)
{
	sSYNetPeerAutomatchHandshakeActive = (enabled != FALSE) ? TRUE : FALSE;
	return TRUE;
}

void syNetPeerSetAutomatchLocalOffer(u16 ban_mask, u8 fkind, u8 costume, u32 nonce_opt)
{
	sAutoLocalBanMask = ban_mask;
	sAutoLocalFkind = fkind;
	sAutoLocalCostume = costume;
	if (nonce_opt != 0U)
	{
		sAutoLocalNonce = nonce_opt;
	}
	else
	{
		u32 hi = ((u32)syUtilsRandUShort() << 16) | (u32)syUtilsRandUShort();
		u32 lo = ((u32)syUtilsRandUShort() << 16) | (u32)syUtilsRandUShort();

		sAutoLocalNonce = hi ^ (lo >> 16);
		sAutoLocalNonce = (sAutoLocalNonce != 0U) ? sAutoLocalNonce : 7919U;
	}
	sSYAutoGotPeerOffer = FALSE;
	sAutoPeerNonce = 0U;
	sAutoPeerFkind = 0U;
	sAutoPeerBanMask = 0U;
	sAutoPeerCostume = 0U;
}

sb32 syNetPeerConfigureUdpForAutomatch(const char *bind_hostport, const char *peer_hostport, u32 session_id,
                                       sb32 you_are_host, u32 input_delay)
{
	syNetPeerCloseSocket();

	if ((bind_hostport == NULL) || (peer_hostport == NULL))
	{
		return FALSE;
	}
	{
		u32 effective_input_delay = input_delay;
		u32 requested_input_delay = input_delay;
		int md_match_delay;
		char *delay_env;

		md_match_delay = syNetInputEnvGetMatchInputDelayOrNeg1();
		delay_env = getenv("SSB64_NETPLAY_DELAY");
		sSYNetPeerInputDelaySource = "automatch";
		if (md_match_delay >= 0)
		{
			effective_input_delay = (u32)md_match_delay;
			requested_input_delay = effective_input_delay;
			sSYNetPeerInputDelaySource = "match";
			port_log("SSB64 NetPeer automatch: SSB64_NETPLAY_MATCH_INPUT_DELAY=%d overrides caller input_delay=%u\n",
			         md_match_delay, (unsigned int)input_delay);
		}
		else if ((delay_env != NULL) && (delay_env[0] != '\0'))
		{
			s32 delay = atoi(delay_env);

			if (delay >= 0)
			{
				effective_input_delay = (u32)delay;
				requested_input_delay = effective_input_delay;
				sSYNetPeerInputDelaySource = "env";
				port_log("SSB64 NetPeer automatch: SSB64_NETPLAY_DELAY=%d overrides caller input_delay=%u\n",
				         delay, (unsigned int)input_delay);
			}
		}
		{
			char *adapt_env = getenv("SSB64_NETPLAY_ADAPTIVE_DELAY");
			char *delay_max_env = getenv("SSB64_NETPLAY_DELAY_MAX");
			u32 committed_d;

			committed_d = (effective_input_delay > 99U) ? SYNETPEER_DEFAULT_INPUT_DELAY : effective_input_delay;
			sSYNetPeerAdaptiveDelayEnabled = FALSE;
			sSYNetPeerInputDelayFloor = committed_d;
			sSYNetPeerInputDelayCeil = 12U;
			if ((adapt_env != NULL) && (atoi(adapt_env) != 0))
			{
				sSYNetPeerAdaptiveDelayEnabled = TRUE;
			}
			if ((delay_max_env != NULL) && (atoi(delay_max_env) > 0))
			{
				sSYNetPeerInputDelayCeil = (u32)atoi(delay_max_env);
			}
			if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
			{
				sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
			}
		}
		if ((syNetSessionParamsAutoNegotiationEnabled() != FALSE) &&
		    (syNetSessionParamsManualDelayOverrideActive() == FALSE))
		{
			sSYNetPeerInputDelay = 1U;
			sSYNetPeerInputDelayFloor = 1U;
			sSYNetPeerInputDelayCeil = 20U;
			sSYNetPeerAdaptiveDelayEnabled = TRUE;
			sSYNetPeerInputDelaySource = "auto_pending";
		}
		sSYNetPeerIsEnabled = TRUE;
		sSYNetPeerBootstrapIsEnabled = TRUE;
		sSYNetPeerBootstrapIsHost = (you_are_host != FALSE) ? TRUE : FALSE;
		sSYNetPeerSessionID = (session_id != 0U) ? session_id : SYNETPEER_DEFAULT_SESSION_ID;
		sSYNetPeerInputDelay = (effective_input_delay > 99U) ? SYNETPEER_DEFAULT_INPUT_DELAY : effective_input_delay;
		syNetPeerApplyOnlineCommittedInputDelayMinToFloorAndDelay();
		syNetPeerLogCommittedInputDelay("automatch_config", requested_input_delay, input_delay);
	}

	if ((sSYNetPeerBootstrapIsHost != FALSE))
	{
		sSYNetPeerLocalPlayer = 0;
		sSYNetPeerRemotePlayer = 1;
	}
	else
	{
		sSYNetPeerLocalPlayer = 1;
		sSYNetPeerRemotePlayer = 0;
	}

	syNetPeerConfigureRemoteReceiveSlots();
	syNetPeerConfigurePeerSenderSlots();
	syNetPeerConfigureExtraLocalSender();
	if ((sSYNetPeerLocalPlayer < 0) || (sSYNetPeerLocalPlayer >= MAXCONTROLLERS) ||
	    (sSYNetPeerRemotePlayer < 0) || (sSYNetPeerRemotePlayer >= MAXCONTROLLERS) ||
	    (sSYNetPeerLocalPlayer == sSYNetPeerRemotePlayer))
	{
		port_log("SSB64 NetPeer automatch: invalid players local=%d remote=%d\n", sSYNetPeerLocalPlayer,
		         sSYNetPeerRemotePlayer);
		return FALSE;
	}
	if ((syNetPeerValidateRemoteReceiveList() == FALSE) || (syNetPeerValidatePeerSenderList() == FALSE))
	{
		return FALSE;
	}
	if ((syNetPeerParseIPv4Address(bind_hostport, &sSYNetPeerBindAddress) == FALSE) ||
	    (syNetPeerParseIPv4Address(peer_hostport, &sSYNetPeerPeerAddress) == FALSE))
	{
		port_log("SSB64 NetPeer automatch: invalid bind or peer IPv4 host:port\n");
		return FALSE;
	}

	sSYNetPeerClockAlignEnabled = TRUE;

	syNetPeerResetStageSceneRendezvousState();
	sSYNetPeerIsConfigured = TRUE;
	port_log("SSB64 NetPeer automatch: configured bind=%s peer=%s session=%u host=%d delay=%u\n", bind_hostport,
	         peer_hostport, sSYNetPeerSessionID, sSYNetPeerBootstrapIsHost, sSYNetPeerInputDelay);

	return TRUE;
}

s32 syNetPeerGetUdpSocketFd(void)
{
	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) == FALSE)
	{
		return -1;
	}
#if defined(_WIN32)
	return (s32)(intptr_t)sSYNetPeerSocket;
#else
	return (s32)sSYNetPeerSocket;
#endif
}
#endif /* SSB64_NETMENU */

void syNetPeerInitDebugEnv(void)
{
#ifdef PORT
	char *netplay_env = getenv("SSB64_NETPLAY");
	char *local_player_env;
	char *remote_player_env;
	char *delay_env;
	char *session_env;
	char *bind_env;
	char *peer_env;
	char *bootstrap_env;
	char *bootstrap_host_env;
	char *bootstrap_seed_env;

	sSYNetPeerIsEnabled = FALSE;
	sSYNetPeerIsConfigured = FALSE;
	sSYNetPeerLocalPlayer = 0;
	sSYNetPeerRemotePlayer = 1;
	sSYNetPeerInputDelay = SYNETPEER_DEFAULT_INPUT_DELAY;
	sSYNetPeerInputDelaySource = "default";
	sSYNetPeerSessionID = SYNETPEER_DEFAULT_SESSION_ID;
	sSYNetPeerBootstrapIsEnabled = FALSE;
	sSYNetPeerBootstrapIsHost = FALSE;
	sSYNetPeerBootstrapMetadataApplied = FALSE;
	sSYNetPeerBootstrapMetadataStaged = FALSE;
	sSYNetPeerBootstrapPeerReady = FALSE;
	sSYNetPeerBootstrapStartReceived = FALSE;
	sSYNetPeerBootstrapSeed = SYNETPEER_DEFAULT_BOOTSTRAP_SEED;
	sSYNetPeerBattleBarrierEnabled = FALSE;
	sSYNetPeerBattleLocalReady = FALSE;
	sSYNetPeerBattlePeerReady = FALSE;
	sSYNetPeerBattleStartSent = FALSE;
	sSYNetPeerBattleStartReceived = FALSE;
	sSYNetPeerBattleBarrierReleased = TRUE;
	sSYNetPeerBattleBarrierWaitFrames = 0;
	sSYNetPeerBattleStartRepeatFrames = 0;
	sSYNetPeerExecutionHoldFrames = 0;
	sSYNetPeerExecutionBeginLogged = FALSE;
	sSYNetPeerClockAlignEnabled = FALSE;
	sSYNetPeerDelaySyncDiagExecReadyMark = ~(u32)0;
	sSYNetPeerDelaySyncDiagEnvCache = -999;

	syNetRollbackInit();

	if ((netplay_env == NULL) || (atoi(netplay_env) == 0))
	{
		return;
	}
	sSYNetPeerIsEnabled = TRUE;

	local_player_env = getenv("SSB64_NETPLAY_LOCAL_PLAYER");
	remote_player_env = getenv("SSB64_NETPLAY_REMOTE_PLAYER");
	delay_env = getenv("SSB64_NETPLAY_DELAY");
	session_env = getenv("SSB64_NETPLAY_SESSION");
	bind_env = getenv("SSB64_NETPLAY_BIND");
	peer_env = getenv("SSB64_NETPLAY_PEER");
	bootstrap_env = getenv("SSB64_NETPLAY_BOOTSTRAP");
	bootstrap_host_env = getenv("SSB64_NETPLAY_HOST");
	bootstrap_seed_env = getenv("SSB64_NETPLAY_SEED");

	if (local_player_env != NULL)
	{
		sSYNetPeerLocalPlayer = atoi(local_player_env);
	}
	if (remote_player_env != NULL)
	{
		sSYNetPeerRemotePlayer = atoi(remote_player_env);
	}
	{
		int md_match_delay_d = syNetInputEnvGetMatchInputDelayOrNeg1();

		if (md_match_delay_d >= 0)
		{
			sSYNetPeerInputDelay = (u32)md_match_delay_d;
			sSYNetPeerInputDelaySource = "match";
			port_log("SSB64 NetPeer: SSB64_NETPLAY_MATCH_INPUT_DELAY=%d overrides SSB64_NETPLAY_DELAY for wire delay\n",
			         md_match_delay_d);
		}
		else if (delay_env != NULL)
		{
			s32 delay = atoi(delay_env);

			if (delay >= 0)
			{
				sSYNetPeerInputDelay = (u32)delay;
				sSYNetPeerInputDelaySource = "env";
			}
		}
	}
#ifdef PORT
	{
		char *adapt_env;
		char *delay_max_env;

		sSYNetPeerAdaptiveDelayEnabled = FALSE;
		sSYNetPeerInputDelayFloor = sSYNetPeerInputDelay;
		sSYNetPeerInputDelayCeil = 12;
		adapt_env = getenv("SSB64_NETPLAY_ADAPTIVE_DELAY");
		if ((adapt_env != NULL) && (atoi(adapt_env) != 0))
		{
			sSYNetPeerAdaptiveDelayEnabled = TRUE;
		}
		delay_max_env = getenv("SSB64_NETPLAY_DELAY_MAX");
		if ((delay_max_env != NULL) && (atoi(delay_max_env) > 0))
		{
			sSYNetPeerInputDelayCeil = (u32)atoi(delay_max_env);
		}
		if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
		{
			sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
		}
		syNetPeerApplyOnlineCommittedInputDelayMinToFloorAndDelay();
		syNetPeerLogCommittedInputDelay("debug_env", sSYNetPeerInputDelay, SYNETPEER_DEFAULT_INPUT_DELAY);
	}
#endif
	if (session_env != NULL)
	{
		s32 session_id = atoi(session_env);

		if (session_id > 0)
		{
			sSYNetPeerSessionID = session_id;
		}
	}
	if ((bootstrap_env != NULL) && (atoi(bootstrap_env) != 0))
	{
		sSYNetPeerBootstrapIsEnabled = TRUE;
	}
	if ((bootstrap_host_env != NULL) && (atoi(bootstrap_host_env) != 0))
	{
		sSYNetPeerBootstrapIsHost = TRUE;
	}
	if (bootstrap_seed_env != NULL)
	{
		s32 seed = atoi(bootstrap_seed_env);

		if (seed > 0)
		{
			sSYNetPeerBootstrapSeed = seed;
		}
	}
	if ((sSYNetPeerLocalPlayer < 0) || (sSYNetPeerLocalPlayer >= MAXCONTROLLERS) ||
		(sSYNetPeerRemotePlayer < 0) || (sSYNetPeerRemotePlayer >= MAXCONTROLLERS) ||
		(sSYNetPeerLocalPlayer == sSYNetPeerRemotePlayer))
	{
		port_log("SSB64 NetPeer: invalid players local=%d remote=%d\n",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer);
		return;
	}
	syNetPeerConfigureRemoteReceiveSlots();
	syNetPeerConfigurePeerSenderSlots();
	syNetPeerConfigureExtraLocalSender();
	if ((syNetPeerValidateRemoteReceiveList() == FALSE) || (syNetPeerValidatePeerSenderList() == FALSE))
	{
		return;
	}
	if ((syNetPeerParseIPv4Address(bind_env, &sSYNetPeerBindAddress) == FALSE) ||
	    (syNetPeerParseIPv4Address(peer_env, &sSYNetPeerPeerAddress) == FALSE))
	{
		port_log("SSB64 NetPeer: invalid bind/peer; expected IPv4 host:port\n");
		return;
	}
	sSYNetPeerIsConfigured = TRUE;
	port_log("SSB64 NetPeer: configured local=%d remote=%d delay=%u session=%u bootstrap=%d host=%d seed=%u bind=%s peer=%s\n",
	         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, sSYNetPeerInputDelay,
	         sSYNetPeerSessionID, sSYNetPeerBootstrapIsEnabled, sSYNetPeerBootstrapIsHost,
	         sSYNetPeerBootstrapSeed, bind_env, peer_env);
	{
		char *sync_start_env = getenv("SSB64_NETPLAY_SYNC_START_MS");

		sSYNetPeerClockAlignEnabled = FALSE;
		if ((sSYNetPeerBootstrapIsEnabled != FALSE) && ((sync_start_env == NULL) || (atoi(sync_start_env) != 0)))
		{
			sSYNetPeerClockAlignEnabled = TRUE;
		}
	}
	if (syNetPeerRunBootstrap() == FALSE)
	{
		port_log("SSB64 NetPeer: bootstrap failed (env path)\n");
	}
#endif
}

#if defined(PORT)
/*
 * Wire sim slots for P2P: local HID vs remote ring. Must run after `syNetInputStartVSSession` (reset) on battle
 * entry when staging already activated the UDP session — `syNetPeerStartVSSession` is idempotent and would
 * otherwise skip this block.
 */
static void syNetPeerApplySimSlotInputSources(void)
{
	s32 ri;

	syNetInputSetSlotSource(sSYNetPeerLocalPlayer, nSYNetInputSourceLocal);
	for (ri = 0; ri < sSYNetPeerRemoteReceiveCount; ri++)
	{
		syNetInputSetSlotSource((s32)sSYNetPeerRemoteReceiveSlots[ri], nSYNetInputSourceRemotePredicted);
	}
	syNetInputClearRemoteSlotPredictionState();
}
#endif

#if defined(PORT)
static void syNetPeerRefreshTickGridExecGateFromEnv(void);
static sb32 syNetPeerCheckTickGridSimReady(void);
#endif

void syNetPeerStartVSSession(void)
{
#if defined(PORT)
	if ((sSYNetPeerIsEnabled == FALSE) || (sSYNetPeerIsConfigured == FALSE))
	{
		return;
	}
	if (syNetPeerOpenSocket() == FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive != FALSE)
	{
		/*
		 * Automatch staging may call StartVSSession before VSBattle, then VSBattle calls it again after
		 * syNetInputStartVSSession resets the match-local sim tick to 0. Keep startup latch monotonic
		 * relative to current match-local tick so execution-ready cannot deadlock at frame 0 on re-entry.
		 * Do not reset negotiated session params / RTT-probe transport on this idempotent path — that
		 * cleared guest negotiation and stopped host retransmit after scVSBattleStartBattle().
		 */
		syNetPeerRefreshCachedNetplayEnvCachesOnly();
		if ((sSYNetPeerLatchedStartupTick != ~(u32)0U) && (syNetInputGetTick() < sSYNetPeerLatchedStartupTick))
		{
			sSYNetPeerLatchedStartupTick = syNetInputGetTick();
		}
		syNetPeerApplySimSlotInputSources();
		syNetPeerRefreshSkewPacingLeadMaxFromEnv();
		syNetPeerRefreshSkewBehindMaxFromEnv();
		syNetPeerRefreshSkewGapEwmaPacingFromEnv();
		syNetPeerRefreshRunwayPredictLimitsFromEnv();
		syNetPeerRefreshTickGridExecGateFromEnv();
		return;
	}
	syNetPeerRefreshCachedNetplayEnvForNewMatch();
	sSYNetPeerHighestRemoteTick = 0;
	sSYNetPeerGlobalCommitGen = 0U;
	sSYNetPeerPacketsSent = 0;
	sSYNetPeerPacketsReceived = 0;
	sSYNetPeerPacketsDropped = 0;
	sSYNetPeerFramesStaged = 0;
	sSYNetPeerLateFrames = 0;
	sSYNetPeerInputChecksum = 2166136261U;
	sSYNetPeerLastLogTick = 0;
	sSYNetPeerSendSeq = 0;
	sSYNetPeerRecvSeqInitialized = FALSE;
	sSYNetPeerRecvSeqHighWater = 0;
	sSYNetPeerSeqGaps = 0;
	sSYNetPeerSeqDuplicates = 0;
	sSYNetPeerSeqOutOfOrder = 0;
	sSYNetPeerLastPeerAckTick = 0;
	sSYNetPeerLastPacketTicksValid = FALSE;
	syNetPeerInputBindReset();
	syNetPeerBattleExecSyncReset();
	/* Startup barrier is hard-disabled: this architecture runs without barrier-phase gating. */
	sSYNetPeerBattleBarrierEnabled = FALSE;
	sSYNetPeerIsActive = TRUE;
	syNetDesyncClassifierReset();
	sSYNetPeerBattleLocalReady = sSYNetPeerBattleBarrierEnabled;
	sSYNetPeerBattlePeerReady = FALSE;
	sSYNetPeerBattleStartSent = FALSE;
	sSYNetPeerBattleStartReceived = FALSE;
	sSYNetPeerBattleBarrierReleased = (sSYNetPeerBattleBarrierEnabled == FALSE) ? TRUE : FALSE;
	syNetPhaseOnVSSessionStart((sSYNetPeerBattleBarrierEnabled != FALSE) ? TRUE : FALSE);
	sSYNetPeerBattleBarrierWaitFrames = 0;
	sSYNetPeerBattleStartRepeatFrames = 0;
	if (sSYNetPeerBattleBarrierEnabled != FALSE)
	{
		sSYNetPeerBarrierWallClockStartMs = syNetPeerNowUnixMs();
		sSYNetPeerBarrierEscapeApplied = FALSE;
		sSYNetPeerBarrierRequeueApplied = FALSE;
	}
	else
	{
		sSYNetPeerBarrierWallClockStartMs = 0ULL;
		sSYNetPeerBarrierEscapeApplied = FALSE;
		sSYNetPeerBarrierRequeueApplied = FALSE;
	}
	sSYNetPeerExecutionHoldFrames = 0;
	sSYNetPeerExecutionBeginLogged = (sSYNetPeerBattleBarrierEnabled == FALSE) ? TRUE : FALSE;
	if (sSYNetPeerExecutionBeginLogged != FALSE)
	{
		sSYNetPeerDelaySyncDiagExecReadyMark = syNetInputGetTick();
	}
	else
	{
		sSYNetPeerDelaySyncDiagExecReadyMark = ~(u32)0;
	}

	syNetPeerLoadBarrierTimingEnvFromConfig();
	sSYNetPeerClockSyncTargetBaseline = sSYNetPeerClockSyncTargetTotal;
	sSYNetPeerBarrierSkewRetryCount = 0U;
	sSYNetPeerBarrierEpochExtraLeadMs = 0U;
	sSYNetPeerLastBarrierContractOffsetSpreadMs = 0;
	sSYNetPeerBarrierSkewRetriesLatchedForLog = 0U;
	syNetPeerResetClockAlignState();
#ifdef PORT
	syNetPeerResetAdaptiveDelayTracking();
	syNetPeerResetBootstrapIngressSymmetryState();
	syNetSessionParamsResetForNewMatch();
	syNetPeerSessionParamsResetTransport();
	syNetInputClearSessionTransportOverrides();
	sSYNetPeerStartupDelayAlignDone = FALSE;
	sSYNetPeerOptionalWallCalFromExecHoldStarted = FALSE;
	sSYNetPeerStartupMatchDelayPendingValid = FALSE;
	sSYNetPeerStartupMatchDelayTarget = 0U;
	sSYNetPeerAdmissionBiasLastAdjustTick = 0U;
	sSYNetPeerAdmissionWireBiasTicks = 0;
	syNetPeerResetSkewPacingSessionStats();
	syNetPeerRefreshSkewPacingLeadMaxFromEnv();
	syNetPeerRefreshSkewBehindMaxFromEnv();
	syNetPeerRefreshSkewGapEwmaPacingFromEnv();
	syNetPeerRefreshRunwayPredictLimitsFromEnv();
	sSYNetPeerRunwayPredictHoldLogsRemaining = 8U;
	syNetPeerResetDesyncTraceSession();
	syNetPeerFrameCommitReset();
	syNetPeerRefreshTickGridExecGateFromEnv();
#endif

	syNetPeerMergedConnectReset();
#if defined(PORT)
	syNetPeerLoadUdpLinkSyncEnvOnce();
	if (sSYNetPeerBootstrapIsEnabled == FALSE)
	{
		sSYNetPeerUdpLinkComplete = TRUE;
	}
#endif

	syNetPeerApplySimSlotInputSources();

	syNetRollbackStartVSSession();
#if defined(PORT)
	syNetPeerLogCommittedInputDelay("vs_start", sSYNetPeerInputDelay, sSYNetPeerInputDelay);
#endif

	{
		const char *hw_env;
		sb32 hw_from_env;
		s32 hw_resolved;

		hw_env = getenv("SSB64_NETPLAY_LOCAL_HARDWARE");
		hw_from_env = (hw_env != NULL) && (hw_env[0] != '\0');
		hw_resolved = syNetPeerResolveLocalHardwareDevice(sSYNetPeerLocalPlayer);
		port_log(
		    "SSB64 NetPeer: local_hardware local_sim=%d samples_gSYControllerDevices[%d] source=%s (unset = device 0 "
		    "= settings player 1; override if your pad is on another port)\n",
		    sSYNetPeerLocalPlayer, hw_resolved, hw_from_env ? "SSB64_NETPLAY_LOCAL_HARDWARE" : "default(device_0)");
	}

	port_log("SSB64 NetPeer: VS session start role=%s local=%d remote=%d delay=%u barrier=%d tick=%u recv_n=%d recv=%u,%u,%u,%u peer_snd_n=%d peer_snd=%u,%u,%u,%u extra_local=%d\n",
	         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
	         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, sSYNetPeerInputDelay,
	         sSYNetPeerBattleBarrierEnabled, syNetInputGetTick(),
	         sSYNetPeerRemoteReceiveCount,
	         (sSYNetPeerRemoteReceiveCount > 0) ? (u32)sSYNetPeerRemoteReceiveSlots[0] : 255U,
	         (sSYNetPeerRemoteReceiveCount > 1) ? (u32)sSYNetPeerRemoteReceiveSlots[1] : 255U,
	         (sSYNetPeerRemoteReceiveCount > 2) ? (u32)sSYNetPeerRemoteReceiveSlots[2] : 255U,
	         (sSYNetPeerRemoteReceiveCount > 3) ? (u32)sSYNetPeerRemoteReceiveSlots[3] : 255U,
	         sSYNetPeerPeerSenderCount,
	         (sSYNetPeerPeerSenderCount > 0) ? (u32)sSYNetPeerPeerSenderSlots[0] : 255U,
	         (sSYNetPeerPeerSenderCount > 1) ? (u32)sSYNetPeerPeerSenderSlots[1] : 255U,
	         (sSYNetPeerPeerSenderCount > 2) ? (u32)sSYNetPeerPeerSenderSlots[2] : 255U,
	         (sSYNetPeerPeerSenderCount > 3) ? (u32)sSYNetPeerPeerSenderSlots[3] : 255U,
	         sSYNetPeerExtraLocalSenderSlot);
	port_log(
	    "SSB64 NetPeer: slot_map role=%s local_sim=%d remote_sim=%d meta_host_sim=%u meta_guest_sim=%u primary_dev=%d src0=%s src1=%s recv=%u,%u,%u,%u recv_n=%d snd=%u,%u,%u,%u snd_n=%d\n",
	    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
	    (u32)sSYNetPeerBootstrapMetadata.netplay_sim_slot_host_hw,
	    (u32)sSYNetPeerBootstrapMetadata.netplay_sim_slot_client_hw,
	    (s32)syNetPeerResolveLocalHardwareDevice(sSYNetPeerLocalPlayer), syNetPeerAbbrevSlotSource(syNetInputGetSlotSource(0)),
	    syNetPeerAbbrevSlotSource(syNetInputGetSlotSource(1)),
	    (sSYNetPeerRemoteReceiveCount > 0) ? (u32)sSYNetPeerRemoteReceiveSlots[0] : 255U,
	    (sSYNetPeerRemoteReceiveCount > 1) ? (u32)sSYNetPeerRemoteReceiveSlots[1] : 255U,
	    (sSYNetPeerRemoteReceiveCount > 2) ? (u32)sSYNetPeerRemoteReceiveSlots[2] : 255U,
	    (sSYNetPeerRemoteReceiveCount > 3) ? (u32)sSYNetPeerRemoteReceiveSlots[3] : 255U, sSYNetPeerRemoteReceiveCount,
	    (sSYNetPeerPeerSenderCount > 0) ? (u32)sSYNetPeerPeerSenderSlots[0] : 255U,
	    (sSYNetPeerPeerSenderCount > 1) ? (u32)sSYNetPeerPeerSenderSlots[1] : 255U,
	    (sSYNetPeerPeerSenderCount > 2) ? (u32)sSYNetPeerPeerSenderSlots[2] : 255U,
	    (sSYNetPeerPeerSenderCount > 3) ? (u32)sSYNetPeerPeerSenderSlots[3] : 255U, sSYNetPeerPeerSenderCount);
	if (syNetPeerRequireInputBindStrict() != FALSE)
	{
		syNetPeerSendInputBindPacket();
	}
#endif
}

#if defined(PORT)
/* SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1 gates battle sim until syNetTickGridLockIsLocked() (guest); default off. */
static void syNetPeerRefreshTickGridExecGateFromEnv(void)
{
	char *e;

	e = getenv("SSB64_NETPLAY_TICK_GRID_EXEC_GATE");
	sSYNetPeerTickGridExecGate = ((e != NULL) && (atoi(e) != 0)) ? TRUE : FALSE;
}

static sb32 syNetPeerCheckTickGridSimReady(void)
{
	if (sSYNetPeerTickGridExecGate == FALSE)
	{
		return TRUE;
	}
	/* Exec gate applies only after RUNNING: calibration may run without full tick-grid lock. */
	if (syNetPhaseIsRunning() == FALSE)
	{
		return TRUE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	return syNetTickGridLockIsLocked();
}

static sb32 syNetPeerHardLockstepClockGateEnabled(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_HARD_LOCKSTEP_CLOCK");
	if (e == NULL || e[0] == '\0')
	{
		return TRUE;
	}
	return (atoi(e) != 0) ? TRUE : FALSE;
}

static u32 syNetPeerHardLockstepClockSlackTicks(void)
{
	const char *e;
	int v;

	e = getenv("SSB64_NETPLAY_HARD_LOCKSTEP_CLOCK_SLACK_TICKS");
	v = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (v < 0)
	{
		v = 0;
	}
	if (v > 2)
	{
		v = 2;
	}
	return (u32)v;
}
#endif

sb32 syNetPeerCheckBattleExecutionReady(void)
{
	if (sSYNetPeerIsEnabled == FALSE)
	{
		return TRUE;
	}
	/* Menus / idle: no VS UDP session — do not hold taskman or netinput tick on bind/exec state. */
	if (sSYNetPeerIsActive == FALSE)
	{
		return TRUE;
	}
	/* Automatch / bootstrap clock barrier (optional): must release before post-barrier gates. */
	if ((sSYNetPeerBootstrapIsEnabled != FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE))
	{
		if (sSYNetPeerBattleBarrierReleased == FALSE)
		{
			return FALSE;
		}
	}
#if defined(PORT)
	/*
	 * Apply INPUT_BIND + battle_exec_sync for every active UDP VS session, not only when the clock barrier
	 * is enabled. The old `BattleBarrierEnabled == FALSE` early-return skipped these gates so execution
	 * could begin (and `execution begin` could log) a frame before `input_bind_ack`.
	 */
	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return FALSE;
	}
	if ((syNetPeerRequireBattleExecSync() != FALSE) && (syNetPeerBattleExecSyncIsComplete() == FALSE))
	{
		return FALSE;
	}
	if (syNetPeerBootstrapIngressSymmetrySatisfied() == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerSessionParamsNegotiationSatisfied() == FALSE)
	{
		return FALSE;
	}
#endif
	return TRUE;
}

sb32 syNetPeerIsClockReadyForSimTick(u32 sim_tick)
{
#if defined(PORT)
	u32 target_tick;
	u32 slack_ticks;
	static u32 s_last_clock_hold_log_tick = ~(u32)0;
	u32 agreed_tick;
	u32 agreed_phase;
	u32 now_phase;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	if (syNetPhaseIsRunning() == FALSE)
	{
		return TRUE;
	}
	if (syNetPeerCheckTickGridSimReady() == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerRequireBattleExecSync() == FALSE)
	{
		return TRUE;
	}
	if (syNetPeerBattleExecSyncIsComplete() == FALSE)
	{
		return FALSE;
	}
	agreed_tick =
	    (sSYNetPeerBootstrapIsHost != FALSE) ? sSYNetPeerExecSyncHostProposedTick : sSYNetPeerExecSyncAgreedTick;
	agreed_phase = (sSYNetPeerBootstrapIsHost != FALSE) ? sSYNetPeerExecSyncHostViPhase : sSYNetPeerExecSyncPeerViPhaseLatch;
	if (agreed_tick == ~(u32)0)
	{
		return FALSE;
	}
	now_phase = syNetPeerCurrentViPhaseBucketNow();
	target_tick = syNetPeerSaturatingAddU32(agreed_tick, now_phase - agreed_phase);

	if (syNetPeerHardLockstepClockGateEnabled() == FALSE)
	{
		return TRUE;
	}
	slack_ticks = syNetPeerHardLockstepClockSlackTicks();
	if (sim_tick > syNetPeerSaturatingAddU32(target_tick, slack_ticks))
	{
		if ((s_last_clock_hold_log_tick == ~(u32)0) || ((sim_tick - s_last_clock_hold_log_tick) >= 30U))
		{
			port_log("SSB64 NetPeer: hard_lockstep_clock_hold tick=%u target_tick=%u now_vi_phase=%u start_vi_phase=%u slack=%u\n",
			         (unsigned int)sim_tick, (unsigned int)target_tick, (unsigned int)now_phase,
			         (unsigned int)agreed_phase, (unsigned int)slack_ticks);
			s_last_clock_hold_log_tick = sim_tick;
		}
		return FALSE;
	}
	return TRUE;
#else
	(void)sim_tick;
	return TRUE;
#endif
}

sb32 syNetPeerCheckStartBarrierReleased(void)
{
	return syNetPeerCheckBattleExecutionReady();
}

static sb32 syNetPeerU8InList(u8 value, const u8 *list, s32 count)
{
	s32 i;

	for (i = 0; i < count; i++)
	{
		if (list[i] == value)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetPeerParseCommaControllerSlots(const char *str, u8 *out_slots, s32 *out_count)
{
	const char *p;
	s32 n;
	s32 v;
	sb32 have_digit;

	if ((str == NULL) || (out_slots == NULL) || (out_count == NULL))
	{
		return FALSE;
	}
	n = 0;
	p = str;
	while ((*p != '\0') && (n < SYNETPEER_MAX_REMOTE_PLAYLIST))
	{
		while ((*p == ' ') || (*p == '\t') || (*p == ','))
		{
			p++;
		}
		if (*p == '\0')
		{
			break;
		}
		v = 0;
		have_digit = FALSE;
		while ((*p >= '0') && (*p <= '9'))
		{
			v = (v * 10) + (*p - '0');
			have_digit = TRUE;
			p++;
		}
		if ((have_digit == FALSE) || (v < 0) || (v >= MAXCONTROLLERS))
		{
			return FALSE;
		}
		if (syNetPeerU8InList((u8)v, out_slots, n) != FALSE)
		{
			return FALSE;
		}
		out_slots[n++] = (u8)v;
		while ((*p == ' ') || (*p == '\t'))
		{
			p++;
		}
		if (*p == ',')
		{
			p++;
		}
	}
	*out_count = n;
	return (n > 0) ? TRUE : FALSE;
}

static void syNetPeerConfigureRemoteReceiveSlots(void)
{
	char *env_slots;

	sSYNetPeerRemoteReceiveSlots[0] = (u8)sSYNetPeerRemotePlayer;
	sSYNetPeerRemoteReceiveCount = 1;
	env_slots = getenv("SSB64_NETPLAY_REMOTE_SLOTS");
	if ((env_slots != NULL) && (env_slots[0] != '\0'))
	{
		if (syNetPeerParseCommaControllerSlots(env_slots, sSYNetPeerRemoteReceiveSlots, &sSYNetPeerRemoteReceiveCount) ==
		    FALSE)
		{
			port_log("SSB64 NetPeer: invalid SSB64_NETPLAY_REMOTE_SLOTS; using remote=%d only\n",
			         sSYNetPeerRemotePlayer);
			sSYNetPeerRemoteReceiveSlots[0] = (u8)sSYNetPeerRemotePlayer;
			sSYNetPeerRemoteReceiveCount = 1;
		}
	}
}

static void syNetPeerConfigurePeerSenderSlots(void)
{
	char *env_slots;

	sSYNetPeerPeerSenderSlots[0] = (u8)sSYNetPeerRemotePlayer;
	sSYNetPeerPeerSenderCount = 1;
	env_slots = getenv("SSB64_NETPLAY_PEER_SENDER_SLOTS");
	if ((env_slots != NULL) && (env_slots[0] != '\0'))
	{
		if (syNetPeerParseCommaControllerSlots(env_slots, sSYNetPeerPeerSenderSlots, &sSYNetPeerPeerSenderCount) ==
		    FALSE)
		{
			port_log("SSB64 NetPeer: invalid SSB64_NETPLAY_PEER_SENDER_SLOTS; using remote=%d only\n",
			         sSYNetPeerRemotePlayer);
			sSYNetPeerPeerSenderSlots[0] = (u8)sSYNetPeerRemotePlayer;
			sSYNetPeerPeerSenderCount = 1;
		}
	}
}

static sb32 syNetPeerIsAllowedPeerSenderSlot(u8 slot)
{
	if (slot == (u8)sSYNetPeerLocalPlayer)
	{
		return FALSE;
	}
	return syNetPeerU8InList(slot, sSYNetPeerPeerSenderSlots, sSYNetPeerPeerSenderCount);
}

static void syNetPeerConfigureExtraLocalSender(void)
{
	char *env_extra;
	s32 v;

	sSYNetPeerExtraLocalSenderSlot = -1;
	env_extra = getenv("SSB64_NETPLAY_EXTRA_LOCAL_PLAYER");
	if ((env_extra == NULL) || (env_extra[0] == '\0'))
	{
		return;
	}
	v = atoi(env_extra);
	if ((v < 0) || (v >= MAXCONTROLLERS) || (v == sSYNetPeerLocalPlayer))
	{
		port_log("SSB64 NetPeer: ignoring invalid SSB64_NETPLAY_EXTRA_LOCAL_PLAYER=%d\n", v);
		return;
	}
	if (syNetPeerU8InList((u8)v, sSYNetPeerRemoteReceiveSlots, sSYNetPeerRemoteReceiveCount) != FALSE)
	{
		port_log(
		    "SSB64 NetPeer: SSB64_NETPLAY_EXTRA_LOCAL_PLAYER=%d conflicts with remote receive slots; ignoring extra bundle\n",
		    v);
		return;
	}
	sSYNetPeerExtraLocalSenderSlot = v;
}

static sb32 syNetPeerValidateRemoteReceiveList(void)
{
	s32 i;
	s32 j;

	for (i = 0; i < sSYNetPeerRemoteReceiveCount; i++)
	{
		if (((s32)sSYNetPeerRemoteReceiveSlots[i] < 0) || ((s32)sSYNetPeerRemoteReceiveSlots[i] >= MAXCONTROLLERS) ||
			((s32)sSYNetPeerRemoteReceiveSlots[i] == sSYNetPeerLocalPlayer))
		{
			port_log("SSB64 NetPeer: invalid remote receive slot list\n");
			return FALSE;
		}
		for (j = i + 1; j < sSYNetPeerRemoteReceiveCount; j++)
		{
			if (sSYNetPeerRemoteReceiveSlots[i] == sSYNetPeerRemoteReceiveSlots[j])
			{
				port_log("SSB64 NetPeer: duplicate remote receive slot list\n");
				return FALSE;
			}
		}
	}
	return TRUE;
}

static sb32 syNetPeerValidatePeerSenderList(void)
{
	s32 i;
	s32 j;

	for (i = 0; i < sSYNetPeerPeerSenderCount; i++)
	{
		if (((s32)sSYNetPeerPeerSenderSlots[i] < 0) || ((s32)sSYNetPeerPeerSenderSlots[i] >= MAXCONTROLLERS) ||
			((s32)sSYNetPeerPeerSenderSlots[i] == sSYNetPeerLocalPlayer))
		{
			port_log("SSB64 NetPeer: invalid peer sender slot list\n");
			return FALSE;
		}
		for (j = i + 1; j < sSYNetPeerPeerSenderCount; j++)
		{
			if (sSYNetPeerPeerSenderSlots[i] == sSYNetPeerPeerSenderSlots[j])
			{
				port_log("SSB64 NetPeer: duplicate peer sender slot list\n");
				return FALSE;
			}
		}
	}
	return TRUE;
}

static sb32 syNetPeerApplyInputSlotsFromMetadata(const SYNetInputReplayMetadata *m)
{
#if defined(PORT)
	u8 host_hw;
	u8 cli_hw;

	if (sSYNetPeerIsConfigured == FALSE)
	{
		return TRUE;
	}
	host_hw = m->netplay_sim_slot_host_hw;
	cli_hw = m->netplay_sim_slot_client_hw;
	if ((host_hw >= MAXCONTROLLERS) || (cli_hw >= MAXCONTROLLERS) || (host_hw == cli_hw))
	{
		port_log("SSB64 NetPeer: invalid netplay sim slots host_hw=%u client_hw=%u\n", (u32)host_hw, (u32)cli_hw);
		return FALSE;
	}
	if ((m->player_count == 2U) && (m->scene_kind == (u32)nSCKindVSBattle))
	{
		if ((host_hw != 0U) || (cli_hw != 1U))
		{
			port_log(
			    "SSB64 NetPeer: netplay sim slots must be host_hw=0 client_hw=1 for 1v1 VS (got %u and %u)\n",
			    (u32)host_hw, (u32)cli_hw);
			return FALSE;
		}
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		sSYNetPeerLocalPlayer = (s32)host_hw;
		sSYNetPeerRemotePlayer = (s32)cli_hw;
	}
	else
	{
		sSYNetPeerLocalPlayer = (s32)cli_hw;
		sSYNetPeerRemotePlayer = (s32)host_hw;
	}
	if ((sSYNetPeerLocalPlayer < 0) || (sSYNetPeerLocalPlayer >= MAXCONTROLLERS) ||
	    (sSYNetPeerRemotePlayer < 0) || (sSYNetPeerRemotePlayer >= MAXCONTROLLERS) ||
	    (sSYNetPeerLocalPlayer == sSYNetPeerRemotePlayer))
	{
		port_log("SSB64 NetPeer: derived local=%d remote=%d invalid\n", sSYNetPeerLocalPlayer,
		         sSYNetPeerRemotePlayer);
		return FALSE;
	}
	syNetPeerConfigureRemoteReceiveSlots();
	syNetPeerConfigurePeerSenderSlots();
	syNetPeerConfigureExtraLocalSender();
	if ((syNetPeerValidateRemoteReceiveList() == FALSE) || (syNetPeerValidatePeerSenderList() == FALSE))
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetPeer: input binding metadata meta_host_sim=%u meta_guest_sim=%u role=%s -> local_sim=%d remote_sim=%d\n",
	    (u32)host_hw, (u32)cli_hw, (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", sSYNetPeerLocalPlayer,
	    sSYNetPeerRemotePlayer);
#endif /* defined(PORT) */
	return TRUE;
}

static sb32 syNetPeerBundleHasWireTick(SYNetPeerPacketFrame *frames, s32 frame_count, u32 wire_tick)
{
	s32 i;

	for (i = 0; i < frame_count; i++)
	{
		if (frames[i].tick == wire_tick)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetPeerAppendInputFrameToBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *frame_count,
                                            SYNetInputFrame *input_frame)
{
	u32 wire_tick;

	if (*frame_count >= SYNETPEER_MAX_PACKET_FRAMES)
	{
		return;
	}
	wire_tick = syNetPeerDelayWireTickFromSim(input_frame->tick);
	if (syNetPeerBundleHasWireTick(frames, *frame_count, wire_tick) != FALSE)
	{
		return;
	}
	frames[*frame_count].tick = wire_tick;
	frames[*frame_count].buttons = input_frame->buttons;
	frames[*frame_count].stick_x = input_frame->stick_x;
	frames[*frame_count].stick_y = input_frame->stick_y;
	(*frame_count)++;
	syNetInputNoteTransmittedSimFrame(slot, input_frame);
}

#ifdef PORT
static void syNetPeerAppendDelayedLocalRowsToBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *frame_count)
{
	SYNetInputFrame delayed_frame;
	u32 sim_tick;
	u32 delay;
	u32 last_tick;
	u32 t;

	if (syNetInputAuthoritativeWireContractEnabled() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	sim_tick = syNetInputGetTick();
	delay = syNetPeerGetCommittedInputDelay();
	last_tick = ((~(u32)0 - sim_tick) < delay) ? ~(u32)0 : (sim_tick + delay);
	t = sim_tick;
	while (TRUE)
	{
		if (syNetInputGetLocalDelayedFrame(slot, t, &delayed_frame) != FALSE)
		{
			syNetPeerAppendInputFrameToBundle(slot, frames, frame_count, &delayed_frame);
		}
		if ((t == last_tick) || (*frame_count >= SYNETPEER_MAX_PACKET_FRAMES))
		{
			break;
		}
		t++;
		if (t == 0U)
		{
			break;
		}
	}
}
#endif

static sb32 syNetPeerGatherHistoryBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *out_frame_count)
{
	SYNetInputFrame published_frame;
	SYNetInputFrame history_frame;
	u32 latest_tick;
	s32 frame_count;
	s32 back;

	frame_count = 0;
#ifdef PORT
	syNetPeerAppendDelayedLocalRowsToBundle(slot, frames, &frame_count);
#endif
	if (syNetInputGetPublishedFrame(slot, &published_frame) == FALSE)
	{
#ifdef PORT
		if ((syNetInputAuthoritativeWireContractEnabled() != FALSE) && (frame_count > 0))
		{
			*out_frame_count = frame_count;
			return TRUE;
		}
#endif
		return FALSE;
	}
	latest_tick = published_frame.tick;
#ifdef PORT
	if ((syNetInputAuthoritativeWireContractEnabled() != FALSE) && (latest_tick != syNetInputGetTick()))
	{
		u32 sim_tick;
		SYNetInputFrame lf;

		sim_tick = syNetInputGetTick();
		syNetInputMakeLocalFrame(slot, sim_tick, &lf);
		syNetPeerAppendInputFrameToBundle(slot, frames, &frame_count, &lf);
		*out_frame_count = frame_count;
		return TRUE;
	}
#endif
	for (back = SYNETPEER_MAX_PACKET_FRAMES - 1; back >= 0; back--)
	{
		if (frame_count >= SYNETPEER_MAX_PACKET_FRAMES)
		{
			break;
		}
		if ((latest_tick >= (u32)back) &&
			(syNetInputGetHistoryFrame(slot, latest_tick - back, &history_frame) != FALSE))
		{
			/* committed delay only — host adaptive ramps commit in ApplyHostDelayRampPending at effective_tick */
			syNetPeerAppendInputFrameToBundle(slot, frames, &frame_count, &history_frame);
		}
	}
	*out_frame_count = frame_count;
	return TRUE;
}

/*
 * Like `syNetPeerGatherHistoryBundle`, but when `allow_synthetic_fallback` and history is empty, synthesize one wire row
 * from `syNetInputMakeLocalFrame` at the current sim tick (bootstrap ingress warmup — Linux UDP).
 */
static sb32 syNetPeerGatherTransmitBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *out_frame_count,
                                          sb32 allow_synthetic_fallback)
{
	if (syNetPeerGatherHistoryBundle(slot, frames, out_frame_count) != FALSE)
	{
		return TRUE;
	}
	if (allow_synthetic_fallback == FALSE)
	{
		return FALSE;
	}
#ifdef PORT
	{
		u32 sim_tick;
		SYNetInputFrame lf;

		sim_tick = syNetInputGetTick();
		syNetInputMakeLocalFrame(slot, sim_tick, &lf);
		frames[0].tick = syNetPeerDelayWireTickFromSim(sim_tick);
		frames[0].buttons = lf.buttons;
		frames[0].stick_x = lf.stick_x;
		frames[0].stick_y = lf.stick_y;
		*out_frame_count = 1;
		return TRUE;
	}
#else
	(void)slot;
	(void)frames;
	(void)out_frame_count;
	return FALSE;
#endif
}

static void syNetPeerMergeIncomingConnectStatus(const s32 *remote_tick, const u8 *remote_disc)
{
	s32 i;
	s32 a;
	s32 b;

	if ((remote_tick == NULL) || (remote_disc == NULL))
	{
		return;
	}
	for (i = 0; i < MAXCONTROLLERS; i++)
	{
		a = sSYNetPeerMergedConnectLastTick[i];
		b = remote_tick[i];
		if (b > a)
		{
			sSYNetPeerMergedConnectLastTick[i] = b;
		}
		sSYNetPeerMergedConnectDisc[i] = (u8)(sSYNetPeerMergedConnectDisc[i] | remote_disc[i]);
	}
	{
		sb32 any_disc = FALSE;
		s32 di;

		for (di = 0; di < MAXCONTROLLERS; di++)
		{
			if (remote_disc[di] != 0U)
			{
				any_disc = TRUE;
				break;
			}
		}
		if (any_disc != FALSE)
		{
			if (sSYNetPeerPeerDisconnectConsec < 0xFFFFU)
			{
				sSYNetPeerPeerDisconnectConsec++;
			}
		}
		else
		{
			sSYNetPeerPeerDisconnectConsec = 0U;
		}
	}
}

#ifdef PORT
/*
 * Append duplicate copies of the last N primary-bundle frames (same ticks) when `SSB64_NETPLAY_INPUT_BUNDLE_REDUNDANCY`
 * is a positive integer (clamped to 8). Increases `frame_count` for checksum + receiver staging; idempotent on duplicate ticks.
 */
static void syNetPeerAppendBundleRedundancyFrames(SYNetPeerPacketFrame *frames, s32 *io_count)
{
	const char *e;
	s32 nwant;
	s32 base;
	s32 dup;
	s32 start;
	s32 i;

	nwant = (s32)syNetSessionParamsGetEffectiveBundleRedundancy();
	if (nwant <= 0)
	{
		e = getenv("SSB64_NETPLAY_INPUT_BUNDLE_REDUNDANCY");
		if ((e == NULL) || (e[0] == '\0'))
		{
			return;
		}
		nwant = atoi(e);
	}
	if (nwant <= 0)
	{
		return;
	}
	if (nwant > 8)
	{
		nwant = 8;
	}
	base = *io_count;
	if (base <= 0)
	{
		return;
	}
	dup = nwant;
	if (dup > base)
	{
		dup = base;
	}
	if (dup > (s32)SYNETPEER_MAX_PACKET_FRAMES - base)
	{
		dup = (s32)SYNETPEER_MAX_PACKET_FRAMES - base;
	}
	if (dup <= 0)
	{
		return;
	}
	start = base - dup;
	for (i = 0; i < dup; i++)
	{
		frames[base + i] = frames[start + i];
	}
	*io_count = base + dup;
}

/*
 * Append up to N extra INPUT rows at wire ticks max(existing)+1 .. max+N (clamped), copying buttons/sticks from the
 * newest bundled row (last index). Lets peers raise `hr` / ring ahead of the latest authored sim wire without changing
 * committed delay; later packets with the same tick supersede when real history arrives. Parsed each BuildPacket.
 */
static void syNetPeerAppendFutureWireAheadFrames(SYNetPeerPacketFrame *frames, s32 *io_count)
{
	const char *e;
	int nwant;
	s32 base;
	s32 k;
	u32 max_w;
	u32 cand;
	s32 i;
	s32 j;
	SYNetPeerPacketFrame tmpl;

	if ((frames == NULL) || (io_count == NULL))
	{
		return;
	}
	base = *io_count;
	if (base <= 0)
	{
		return;
	}
	e = getenv("SSB64_NETPLAY_INPUT_FUTURE_WIRE_TICKS");
	nwant = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (nwant <= 0)
	{
		return;
	}
	if (nwant > 8)
	{
		nwant = 8;
	}
	max_w = 0U;
	for (i = 0; i < base; i++)
	{
		if (frames[i].tick > max_w)
		{
			max_w = frames[i].tick;
		}
	}
	tmpl = frames[base - 1];
	for (k = 1; k <= nwant; k++)
	{
		if (base >= (s32)SYNETPEER_MAX_PACKET_FRAMES)
		{
			break;
		}
		cand = syNetPeerSaturatingAddU32(max_w, (u32)k);
		for (j = 0; j < base; j++)
		{
			if (frames[j].tick == cand)
			{
				goto skip_future_k;
			}
		}
		frames[base].tick = cand;
		frames[base].buttons = tmpl.buttons;
		frames[base].stick_x = tmpl.stick_x;
		frames[base].stick_y = tmpl.stick_y;
		base++;
	skip_future_k:;
	}
	*io_count = base;
}
#endif

static void syNetPeerBuildIngressPacketCore(u8 *buffer, u32 *out_size, sb32 allow_synth_gather)
{
	SYNetPeerPacketFrame frames[SYNETPEER_MAX_PACKET_FRAMES];
	SYNetPeerPacketFrame sec_frames[SYNETPEER_MAX_PACKET_FRAMES];
	SYNetPeerPacketFrame zero_frame;
	u8 *cursor = buffer;
	u32 checksum;
	s32 frame_count = 0;
	s32 sec_frame_count = 0;
	s32 i;
	u16 wire_version;
	u8 secondary_slot_byte;
	s32 conn_last_tick[MAXCONTROLLERS];
	u8 conn_disc[MAXCONTROLLERS];
	s32 conn_symmetric_tick[MAXCONTROLLERS];
	s32 conn_symmetric_target[MAXCONTROLLERS];

	memset(frames, 0, sizeof(frames));
	memset(sec_frames, 0, sizeof(sec_frames));
	memset(&zero_frame, 0, sizeof(zero_frame));

	if (syNetPeerGatherTransmitBundle(sSYNetPeerLocalPlayer, frames, &frame_count, allow_synth_gather) == FALSE)
	{
		*out_size = 0;
		return;
	}
#ifdef PORT
	syNetPeerAppendBundleRedundancyFrames(frames, &frame_count);
	syNetPeerAppendFutureWireAheadFrames(frames, &frame_count);
#endif
	wire_version = SYNETPEER_VERSION;
	secondary_slot_byte = SYNETPEER_SECONDARY_SLOT_ABSENT;
	if ((sSYNetPeerExtraLocalSenderSlot >= 0) &&
	    (syNetPeerGatherTransmitBundle(sSYNetPeerExtraLocalSenderSlot, sec_frames, &sec_frame_count, allow_synth_gather) !=
	     FALSE))
	{
		wire_version = SYNETPEER_VERSION_DUAL_LOCAL;
		secondary_slot_byte = (u8)sSYNetPeerExtraLocalSenderSlot;
#ifdef PORT
		syNetPeerAppendBundleRedundancyFrames(sec_frames, &sec_frame_count);
		syNetPeerAppendFutureWireAheadFrames(sec_frames, &sec_frame_count);
#endif
	}
	syNetInputExportPeerConnectStatus(conn_last_tick, conn_disc, MAXCONTROLLERS);
	syNetRollbackExportPeerSymmetricNotify(conn_symmetric_tick, conn_symmetric_target, MAXCONTROLLERS);
	checksum = syNetPeerChecksumInputPacket(sSYNetPeerSessionID, sSYNetPeerHighestRemoteTick, sSYNetPeerSendSeq,
	                                       wire_version, (u8)sSYNetPeerLocalPlayer, (u8)frame_count, frames,
	                                       secondary_slot_byte, (u8)sec_frame_count, sec_frames, conn_last_tick, conn_disc,
	                                       conn_symmetric_tick, conn_symmetric_target);

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, wire_version);
	syNetPeerWriteU16(&cursor, 0);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, sSYNetPeerHighestRemoteTick);
	syNetPeerWriteU32(&cursor, sSYNetPeerSendSeq);
	syNetPeerWriteU8(&cursor, (u8)sSYNetPeerLocalPlayer);
	syNetPeerWriteU8(&cursor, (u8)frame_count);
	syNetPeerWriteU8(&cursor, (u8)sSYNetPeerLocalPlayer);
	syNetPeerWriteU8(&cursor, (u8)sSYNetPeerRemotePlayer);

	for (i = 0; i < MAXCONTROLLERS; i++)
	{
		u32 sym_tick;
		u32 sym_target;

		syNetPeerWriteU32(&cursor, (u32)conn_last_tick[i]);
		syNetPeerWriteU8(&cursor, conn_disc[i]);
		sym_tick = (conn_symmetric_tick[i] > 0) ? (u32)conn_symmetric_tick[i] : 0U;
		syNetPeerWriteU8(&cursor, (u8)((sym_tick >> 16) & 0xFF));
		syNetPeerWriteU8(&cursor, (u8)((sym_tick >> 8) & 0xFF));
		syNetPeerWriteU8(&cursor, (u8)(sym_tick & 0xFF));
		sym_target = (conn_symmetric_target[i] > 0) ? (u32)conn_symmetric_target[i] : 0U;
		syNetPeerWriteU8(&cursor, (u8)((sym_target >> 16) & 0xFF));
		syNetPeerWriteU8(&cursor, (u8)((sym_target >> 8) & 0xFF));
		syNetPeerWriteU8(&cursor, (u8)(sym_target & 0xFF));
	}

	for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
	{
		SYNetPeerPacketFrame *frame = (i < frame_count) ? &frames[i] : &zero_frame;

		syNetPeerWriteU32(&cursor, frame->tick);
		syNetPeerWriteU16(&cursor, frame->buttons);
		syNetPeerWriteU8(&cursor, (u8)frame->stick_x);
		syNetPeerWriteU8(&cursor, (u8)frame->stick_y);
	}
	if (SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(wire_version) != FALSE)
	{
		syNetPeerWriteU8(&cursor, secondary_slot_byte);
		syNetPeerWriteU8(&cursor, (u8)sec_frame_count);
		for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
		{
			SYNetPeerPacketFrame *frame = (i < sec_frame_count) ? &sec_frames[i] : &zero_frame;

			syNetPeerWriteU32(&cursor, frame->tick);
			syNetPeerWriteU16(&cursor, frame->buttons);
			syNetPeerWriteU8(&cursor, (u8)frame->stick_x);
			syNetPeerWriteU8(&cursor, (u8)frame->stick_y);
		}
		*out_size = SYNETPEER_PACKET_BYTES_V5;
	}
	else
	{
		*out_size = SYNETPEER_PACKET_BYTES_V4;
	}

	syNetPeerWriteU32(&cursor, checksum);
}

void syNetPeerBuildPacket(u8 *buffer, u32 *out_size)
{
	syNetPeerBuildIngressPacketCore(buffer, out_size, FALSE);
}

#if defined(PORT)
static sb32 syNetPeerBootstrapIngressSymmetryEnvEnabled(void)
{
	if (sSYNetPeerBootstrapIngressSymEnv == -999)
	{
		const char *e;

		e = getenv("SSB64_NETPLAY_BOOTSTRAP_INGRESS_SYMMETRY");
		sSYNetPeerBootstrapIngressSymEnv = ((e == NULL) || (e[0] == '\0') || (atoi(e) != 0)) ? 1 : 0;
	}
	return (sSYNetPeerBootstrapIngressSymEnv != 0) ? TRUE : FALSE;
}

static void syNetPeerResetBootstrapIngressSymmetryState(void)
{
	sSYNetPeerBootstrapIngressSymEnv = -999;
	sSYNetPeerBootstrapIngressWarmupOutboundSent = FALSE;
	sSYNetPeerBootstrapIngressWarmupLoggedStart = FALSE;
	sSYNetPeerBootstrapIngressWarmupLoggedDone = FALSE;
}

sb32 syNetPeerBootstrapIngressSymmetrySatisfied(void)
{
	if (syNetPeerBootstrapIngressSymmetryEnvEnabled() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return TRUE;
	}
	if (sSYNetPeerStartupMatchDelayPendingValid != FALSE)
	{
		return TRUE;
	}
	if ((sSYNetPeerBootstrapIngressWarmupOutboundSent != FALSE) && (syNetPeerGetHighestRemoteTick() > 0U))
	{
		if (sSYNetPeerBootstrapIngressWarmupLoggedDone == FALSE)
		{
			sSYNetPeerBootstrapIngressWarmupLoggedDone = TRUE;
			port_log(
			    "SSB64 NetPeer: bootstrap_ingress_warmup complete role=%s outbound=1 hr=%u sim=%u\n",
			    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			    (unsigned int)syNetPeerGetHighestRemoteTick(), (unsigned int)syNetInputGetTick());
		}
		return TRUE;
	}
	return FALSE;
}

static void syNetPeerNoteBootstrapWarmupOutboundSent(void)
{
	if (syNetPeerBootstrapIngressSymmetryEnvEnabled() != FALSE)
	{
		sSYNetPeerBootstrapIngressWarmupOutboundSent = TRUE;
	}
}

static sb32 syNetPeerPrimaryHistoryBundleWouldBeEmpty(void)
{
	SYNetPeerPacketFrame f[SYNETPEER_MAX_PACKET_FRAMES];
	s32 fc = 0;

	return (syNetPeerGatherHistoryBundle(sSYNetPeerLocalPlayer, f, &fc) == FALSE) ? TRUE : FALSE;
}

static void syNetPeerMaybeSendBootstrapWarmupInput(void)
{
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];
	u32 size;
	sb32 need_exec_hold_send;
	sb32 need_empty_bundle_fallback;

	if (syNetPeerBootstrapIngressSymmetryEnvEnabled() == FALSE)
	{
		return;
	}
	if (syNetPeerBootstrapIngressSymmetrySatisfied() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerStartupMatchDelayPendingValid != FALSE)
	{
		return;
	}
	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return;
	}
	need_exec_hold_send = (syNetPeerCheckBattleExecutionReady() == FALSE) ? TRUE : FALSE;
	need_empty_bundle_fallback = syNetPeerPrimaryHistoryBundleWouldBeEmpty();
	if ((need_exec_hold_send == FALSE) && (need_empty_bundle_fallback == FALSE))
	{
		return;
	}
	syNetPeerBuildIngressPacketCore(buffer, &size, TRUE);
	if (size == 0U)
	{
		return;
	}
	if (syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)size, &sSYNetPeerPeerAddress) == (int)size)
	{
		sSYNetPeerPacketsSent++;
		sSYNetPeerSendSeq++;
		syNetPeerNoteBootstrapWarmupOutboundSent();
		if (sSYNetPeerBootstrapIngressWarmupLoggedStart == FALSE)
		{
			sSYNetPeerBootstrapIngressWarmupLoggedStart = TRUE;
			port_log("SSB64 NetPeer: bootstrap_ingress_warmup start role=%s sim=%u exec_rdy=%d primary_hist_empty=%d\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", (unsigned int)syNetInputGetTick(),
			         (syNetPeerCheckBattleExecutionReady() != FALSE) ? 1 : 0,
			         (need_empty_bundle_fallback != FALSE) ? 1 : 0);
		}
	}
}
/*
 * Parses a locally-built INPUT datagram (same layout as syNetPeerBuildPacket) for tick_diag logging.
 * Safe only for wire layouts emitted by BuildPacket (v4 single-local / v5 dual-local).
 */
static void syNetPeerLogInputSendDiag(const u8 *buffer, u32 size)
{
	const u8 *c;
	u16 wv;
	u32 seq;
	u32 pkt_i;
	u8 fc;
	u8 sec_slot;
	u8 sec_fc;

	if (syNetPeerTickDiagLevel() < 1)
	{
		return;
	}
	if ((size != (u32)SYNETPEER_PACKET_BYTES_V4) && (size != (u32)SYNETPEER_PACKET_BYTES_V5))
	{
		return;
	}
	c = buffer;
	(void)syNetPeerReadU32(&c);
	wv = syNetPeerReadU16(&c);
	(void)syNetPeerReadU16(&c);
	(void)syNetPeerReadU32(&c);
	(void)syNetPeerReadU32(&c);
	seq = syNetPeerReadU32(&c);
	(void)syNetPeerReadU8(&c);
	fc = syNetPeerReadU8(&c);
	(void)syNetPeerReadU8(&c);
	(void)syNetPeerReadU8(&c);
	if (SYNETPEER_WIRE_HAS_CONNECT_STATUS(wv) != FALSE)
	{
		c += SYNETPEER_CONNECT_BLOCK_BYTES;
	}
	for (pkt_i = 0U; pkt_i < (u32)SYNETPEER_MAX_PACKET_FRAMES; pkt_i++)
	{
		(void)syNetPeerReadU32(&c);
		(void)syNetPeerReadU16(&c);
		(void)syNetPeerReadU8(&c);
		(void)syNetPeerReadU8(&c);
	}
	sec_slot = (u8)SYNETPEER_SECONDARY_SLOT_ABSENT;
	sec_fc = 0U;
	if (SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(wv) != FALSE)
	{
		sec_slot = syNetPeerReadU8(&c);
		sec_fc = syNetPeerReadU8(&c);
		for (pkt_i = 0U; pkt_i < (u32)SYNETPEER_MAX_PACKET_FRAMES; pkt_i++)
		{
			(void)syNetPeerReadU32(&c);
			(void)syNetPeerReadU16(&c);
			(void)syNetPeerReadU8(&c);
			(void)syNetPeerReadU8(&c);
		}
	}
	port_log(
	    "SSB64 NetPeer: INPUT send seq=%u wire=%u primary_sim_slot=%d frames=%u targets_peer_sim=%d dual=%d secondary_sim_slot=%u sec_frames=%u bytes=%u\n",
	    (unsigned int)seq, (unsigned int)wv, sSYNetPeerLocalPlayer, (unsigned int)fc, sSYNetPeerRemotePlayer,
	    (SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(wv) != FALSE) ? 1 : 0, (unsigned int)sec_slot, (unsigned int)sec_fc,
	    (unsigned int)size);
}
#endif

void syNetPeerSendLocalInput(void)
{
#if defined(PORT)
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];
	u32 size;
	static u32 sLastInputSendSimTick = ~(u32)0;
	static u32 sInputSendsThisSimTick;
	u32 sim_tick;

	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return;
	}
	/* While sim is stalled (strict MISS / skew hold), cap redundant INPUT spam per sim tick. */
	sim_tick = syNetInputGetTick();
	if (sim_tick == sLastInputSendSimTick)
	{
		if (sInputSendsThisSimTick >= 3U)
		{
			return;
		}
		sInputSendsThisSimTick++;
	}
	else
	{
		sLastInputSendSimTick = sim_tick;
		sInputSendsThisSimTick = 0U;
	}
	syNetPeerBuildPacket(buffer, &size);

	if (size == 0)
	{
		return;
	}
	if (syNetPeerOsSendTo(sSYNetPeerSocket, buffer, (size_t)size, &sSYNetPeerPeerAddress) == (int)size)
	{
		syNetPeerLogInputSendDiag(buffer, size);
		sSYNetPeerPacketsSent++;
		sSYNetPeerSendSeq++;
		syNetPeerNoteBootstrapWarmupOutboundSent();
	}
#endif
}

#if defined(PORT)
static int sSYNetPeerUdpFrameTraceEnvCache = -1;

static sb32 syNetPeerWantUdpFrameTrace(void)
{
	const char *e;

	if (sSYNetPeerUdpFrameTraceEnvCache >= 0)
	{
		return (sb32)sSYNetPeerUdpFrameTraceEnvCache;
	}
	e = getenv("SSB64_NETPLAY_UDP_FRAME_TRACE");
	sSYNetPeerUdpFrameTraceEnvCache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (sb32)sSYNetPeerUdpFrameTraceEnvCache;
}

static void syNetPeerLogUdpInputBundleDiag(u32 packet_seq, u32 ack_tick, u32 cur_tick, u8 sender_slot,
					   u8 frame_count, const SYNetPeerPacketFrame *frames, sb32 is_dual,
					   u8 secondary_slot, u8 sec_frame_count, const SYNetPeerPacketFrame *sec_frames)
{
	char buf[384];
	int pos;
	int i;

	if (syNetPeerWantUdpFrameTrace() == FALSE)
	{
		return;
	}
	pos = snprintf(
	    buf,
	    sizeof(buf),
	    "SSB64 NetPeer: udp_frame_trace seq=%u ack=%u cur_tick=%u sender_slot=%u fc=%u ticks=",
	    (unsigned int)packet_seq,
	    (unsigned int)ack_tick,
	    (unsigned int)cur_tick,
	    (unsigned int)sender_slot,
	    (unsigned int)frame_count);
	for (i = 0; i < (int)frame_count && i < (int)SYNETPEER_MAX_PACKET_FRAMES && pos < 300; i++)
	{
		pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s%u", (i > 0) ? "," : "",
		                (unsigned int)frames[i].tick);
	}
	port_log("%s\n", buf);
	if ((is_dual != FALSE) && (sec_frame_count > 0))
	{
		pos = snprintf(
		    buf,
		    sizeof(buf),
		    "SSB64 NetPeer: udp_frame_trace_secondary seq=%u cur_tick=%u sec_slot=%u sec_fc=%u ticks=",
		    (unsigned int)packet_seq,
		    (unsigned int)cur_tick,
		    (unsigned int)secondary_slot,
		    (unsigned int)sec_frame_count);
		for (i = 0; i < (int)sec_frame_count && i < (int)SYNETPEER_MAX_PACKET_FRAMES && pos < 300; i++)
		{
			pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s%u", (i > 0) ? "," : "",
			                (unsigned int)sec_frames[i].tick);
		}
		port_log("%s\n", buf);
	}
}
#endif

static void syNetPeerStagePacketBundle(s32 target_player, const SYNetPeerPacketFrame *frames, s32 frame_count,
                                       u32 current_tick, u32 packet_seq)
{
	s32 i;

	for (i = 0; i < frame_count; i++)
	{
#ifdef PORT
		syNetRollbackDebugOnIncomingRemoteFrame((u32 *)&frames[i].tick, (u16 *)&frames[i].buttons,
		                                       (s8 *)&frames[i].stick_x, (s8 *)&frames[i].stick_y);
#endif
		{
			sb32 is_new_remote_tick = (frames[i].tick > sSYNetPeerHighestRemoteTick) ? TRUE : FALSE;

			if ((is_new_remote_tick != FALSE) && (frames[i].tick < current_tick))
			{
				sSYNetPeerLateFrames++;
			}
			if (is_new_remote_tick != FALSE)
			{
				u32 prev_hr = sSYNetPeerHighestRemoteTick;

				sSYNetPeerHighestRemoteTick = frames[i].tick;
				if ((prev_hr == 0U) && (frames[i].tick > 0U))
				{
					port_log("SSB64 NetPeer: remote_frontier_init role=%s target_slot=%d first_hr=%u from_pkt_tick=%u local_sim=%u\n",
					         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
					         target_player, (unsigned int)sSYNetPeerHighestRemoteTick,
					         (unsigned int)frames[i].tick, (unsigned int)current_tick);
				}
			}
		}
		if (syNetInputSetRemoteInputFromPacket(target_player, frames[i].tick, frames[i].buttons, frames[i].stick_x,
		                                       frames[i].stick_y, packet_seq, current_tick, i) != FALSE)
		{
			sSYNetPeerFramesStaged++;
			sSYNetPeerInputChecksum = syNetPeerChecksumAccumulateU32(sSYNetPeerInputChecksum, (u32)target_player);
			sSYNetPeerInputChecksum = syNetPeerChecksumAccumulateFrame(sSYNetPeerInputChecksum, &frames[i]);
		}
	}
}

#if defined(PORT)
static void syNetPeerHandleFrameCommitPacket(const u8 *buffer, s32 size);
static void syNetPeerHandleRollbackBaselinePacket(const u8 *buffer, s32 size);
static void syNetPeerHandleRollbackSyncPacket(const u8 *buffer, s32 size);
static void syNetPeerHandleResimPostPacket(const u8 *buffer, s32 size);
#endif

void syNetPeerHandlePacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	SYNetPeerPacketFrame frames[SYNETPEER_MAX_PACKET_FRAMES];
	SYNetPeerPacketFrame sec_frames[SYNETPEER_MAX_PACKET_FRAMES];
	u32 magic;
	u32 session_id;
	u32 ack_tick;
	u32 packet_seq;
	u32 checksum;
	u32 expected_checksum;
	u16 wire_version;
	u8 player;
	u8 frame_count;
	u8 packet_local_player;
	u8 packet_remote_player;
	u8 secondary_slot;
	u8 sec_frame_count;
	u32 current_tick = syNetInputGetTick();
	s32 i;
	sb32 is_dual;
	s32 recv_conn_tick[MAXCONTROLLERS];
	u8 recv_conn_disc[MAXCONTROLLERS];
	s32 recv_sym_mismatch_tick[MAXCONTROLLERS];
	s32 recv_sym_target_tick[MAXCONTROLLERS];

	memset(recv_sym_mismatch_tick, 0xFF, sizeof(recv_sym_mismatch_tick));
	memset(recv_sym_target_tick, 0xFF, sizeof(recv_sym_target_tick));
	const s32 *chk_tick = NULL;
	const u8 *chk_disc = NULL;
	u32 ctrl_magic = 0U;
	u16 ctrl_wire = 0U;
	u16 ctrl_type = 0U;
	u32 ctrl_session = 0U;
	sb32 ctrl_header_valid = FALSE;

#if defined(PORT)
	if (size >= (s32)(4 + 2 + 2 + 4))
	{
		const u8 *h = buffer;

		ctrl_magic = syNetPeerReadU32(&h);
		ctrl_wire = syNetPeerReadU16(&h);
		ctrl_type = syNetPeerReadU16(&h);
		ctrl_session = syNetPeerReadU32(&h);
		ctrl_header_valid = TRUE;
	}
	/* Dispatch control packets by header type first to avoid size-collision misroutes (e.g. exec-sync vs time-ping). */
	if ((ctrl_header_valid != FALSE) && (ctrl_magic == SYNETPEER_MAGIC))
	{
		switch (ctrl_type)
		{
			case SYNETPEER_PACKET_MATCH_CONFIG:
				if (size == (s32)SYNETPEER_BOOTSTRAP_PACKET_BYTES)
				{
					syNetPeerHandleBootstrapPacket(buffer, size);
					return;
				}
				break;

#if defined(SSB64_NETMENU)
			case SYNETPEER_PACKET_AUTOMATCH_OFFER:
				if (size == (s32)SYNETPEER_AUTOMATCH_OFFER_BYTES)
				{
					syNetPeerHandleBootstrapPacket(buffer, size);
					return;
				}
				break;
#endif

			case SYNETPEER_PACKET_TIME_PONG:
				if (size == SYNETPEER_TIME_PONG_BYTES)
				{
					syNetPeerHandleTimePongPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_BATTLE_START_TIME:
				if ((size == (s32)SYNETPEER_BATTLE_START_TIME_BYTES_LEGACY) || (size == (s32)SYNETPEER_BATTLE_START_TIME_BYTES))
				{
					syNetPeerHandleBattleStartTimePacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_TIME_PING:
				if (size == SYNETPEER_TIME_PING_BYTES)
				{
					syNetPeerHandleTimePingPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_INPUT_BIND:
				if (size == SYNETPEER_INPUT_BIND_BYTES)
				{
					syNetPeerHandleInputBindPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_BATTLE_EXEC_SYNC:
				if ((size == (s32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES) || (size == (s32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES_LEGACY))
				{
					syNetPeerHandleBattleExecSyncPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_INPUT_DELAY_SYNC:
				if (size == (s32)SYNETPEER_INPUT_DELAY_SYNC_BYTES)
				{
					syNetPeerHandleInputDelaySyncPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_SESSION_PARAMS:
			case SYNETPEER_PACKET_SESSION_PARAMS_ACK:
				if (size == (s32)SYNETPEER_SESSION_PARAMS_WIRE_BYTES)
				{
					syNetPeerHandleSessionParamsPacket(buffer, size, ctrl_type);
					return;
				}
				break;

			case SYNETPEER_PACKET_FRAME_COMMIT:
				if (size == (s32)SYNETPEER_FRAME_COMMIT_BYTES)
				{
					syNetPeerHandleFrameCommitPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_ROLLBACK_BASELINE:
				if ((size == (s32)SYNETPEER_ROLLBACK_BASELINE_BYTES) ||
				    (size == (s32)SYNETPEER_ROLLBACK_BASELINE_BYTES_LEGACY))
				{
					syNetPeerHandleRollbackBaselinePacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_ROLLBACK_SYNC:
				if (size == (s32)SYNETPEER_ROLLBACK_SYNC_BYTES)
				{
					syNetPeerHandleRollbackSyncPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_RESIM_POST:
				if (size == (s32)SYNETPEER_RESIM_POST_BYTES)
				{
					syNetPeerHandleResimPostPacket(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_UDP_SYNC_REQ:
			case SYNETPEER_PACKET_UDP_SYNC_REP:
				if (size == (s32)SYNETPEER_UDP_SYNC_PACKET_BYTES)
				{
					syNetPeerHandleUdpSyncIngress(buffer, size);
					return;
				}
				break;

			case SYNETPEER_PACKET_READY:
			case SYNETPEER_PACKET_START:
			case SYNETPEER_PACKET_BATTLE_READY:
			case SYNETPEER_PACKET_BATTLE_START:
			case SYNETPEER_PACKET_STAGE_SCENE_READY:
			case SYNETPEER_PACKET_STAGE_SCENE_GO:
			case SYNETPEER_PACKET_VS_SESSION_END:
				if (size == SYNETPEER_CONTROL_PACKET_BYTES)
				{
					syNetPeerHandleControlPacket(buffer, size);
					return;
				}
				break;
		}
		/*
		 * Valid SSB64 header but not an INPUT bundle — do not fall through to the INPUT size gate
		 * (drops FRAME_COMMIT / RESIM_POST / ROLLBACK_SYNC before handlers run).
		 */
		if ((size != (s32)SYNETPEER_PACKET_BYTES_LEGACY_V2) && (size != (s32)SYNETPEER_PACKET_BYTES_LEGACY_V3) &&
		    (size != (s32)SYNETPEER_PACKET_BYTES_V4) && (size != (s32)SYNETPEER_PACKET_BYTES_V5))
		{
			if ((size > 0) && (size < 64))
			{
				port_log(
				    "SSB64 NetPeer: ingress_unknown_small size=%d magic=0x%08X wire=%u type=%u sess=%u expect_sess=%u role=%s\n",
				    size,
				    (unsigned int)ctrl_magic,
				    (unsigned int)ctrl_wire,
				    (unsigned int)ctrl_type,
				    (unsigned int)ctrl_session,
				    (unsigned int)sSYNetPeerSessionID,
				    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client");
			}
			sSYNetPeerPacketsDropped++;
			return;
		}
	}
#endif
	is_dual = FALSE;
		sec_frame_count = 0;
		secondary_slot = SYNETPEER_SECONDARY_SLOT_ABSENT;
		if ((size != (s32)SYNETPEER_PACKET_BYTES_LEGACY_V2) && (size != (s32)SYNETPEER_PACKET_BYTES_LEGACY_V3) &&
		    (size != (s32)SYNETPEER_PACKET_BYTES_V4) && (size != (s32)SYNETPEER_PACKET_BYTES_V5))
		{
			if (syNetPeerTickDiagLevel() >= 1)
			{
				port_log("SSB64 NetPeer: INPUT drop reason=invalid_size size=%d\n", size);
			}
			sSYNetPeerPacketsDropped++;
			return;
		}
		memset(frames, 0, sizeof(frames));
		memset(sec_frames, 0, sizeof(sec_frames));
		memset(recv_conn_tick, 0, sizeof(recv_conn_tick));
		memset(recv_conn_disc, 0, sizeof(recv_conn_disc));

		magic = syNetPeerReadU32(&cursor);
		wire_version = syNetPeerReadU16(&cursor);
		(void)syNetPeerReadU16(&cursor);
		if (((size == (s32)SYNETPEER_PACKET_BYTES_LEGACY_V2) &&
		     (wire_version != (u16)SYNETPEER_WIRE_LEGACY_INPUT_SINGLE)) ||
		    ((size == (s32)SYNETPEER_PACKET_BYTES_LEGACY_V3) &&
		     (wire_version != (u16)SYNETPEER_WIRE_LEGACY_INPUT_DUAL)) ||
		    ((size == (s32)SYNETPEER_PACKET_BYTES_V4) && (wire_version != (u16)SYNETPEER_VERSION)) ||
		    ((size == (s32)SYNETPEER_PACKET_BYTES_V5) && (wire_version != (u16)SYNETPEER_VERSION_DUAL_LOCAL)))
		{
			sSYNetPeerPacketsDropped++;
			return;
		}
		is_dual = (SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(wire_version) != FALSE) ? TRUE : FALSE;
		session_id = syNetPeerReadU32(&cursor);
		ack_tick = syNetPeerReadU32(&cursor);
		packet_seq = syNetPeerReadU32(&cursor);
		player = syNetPeerReadU8(&cursor);
		frame_count = syNetPeerReadU8(&cursor);
		packet_local_player = syNetPeerReadU8(&cursor);
		packet_remote_player = syNetPeerReadU8(&cursor);

		if ((magic != SYNETPEER_MAGIC) || (session_id != sSYNetPeerSessionID) || (player != packet_local_player) ||
		    (packet_remote_player != (u8)sSYNetPeerLocalPlayer) || (frame_count > SYNETPEER_MAX_PACKET_FRAMES) ||
		    (syNetPeerIsAllowedPeerSenderSlot(player) == FALSE))
		{
			if (syNetPeerTickDiagLevel() >= 1)
			{
				port_log("SSB64 NetPeer: INPUT drop reason=invalid_header magic=0x%08X sess=%u expect_sess=%u player=%u hdr_local=%u hdr_remote=%u fc=%u\n",
				         (unsigned int)magic, (unsigned int)session_id, (unsigned int)sSYNetPeerSessionID,
				         (unsigned int)player, (unsigned int)packet_local_player, (unsigned int)packet_remote_player,
				         (unsigned int)frame_count);
			}
			sSYNetPeerPacketsDropped++;
			return;
		}
		if (SYNETPEER_WIRE_HAS_CONNECT_STATUS(wire_version) != FALSE)
		{
			for (i = 0; i < MAXCONTROLLERS; i++)
			{
				u32 lt_u;
				u8 sym_b0;
				u8 sym_b1;
				u8 sym_b2;
				u32 sym_tick;
				u32 sym_target;
				u8 sym_t0;
				u8 sym_t1;
				u8 sym_t2;

				lt_u = syNetPeerReadU32(&cursor);
				recv_conn_tick[i] = (s32)lt_u;
				recv_conn_disc[i] = syNetPeerReadU8(&cursor);
				sym_b0 = syNetPeerReadU8(&cursor);
				sym_b1 = syNetPeerReadU8(&cursor);
				sym_b2 = syNetPeerReadU8(&cursor);
				sym_tick = ((u32)sym_b0 << 16) | ((u32)sym_b1 << 8) | (u32)sym_b2;
				sym_t0 = syNetPeerReadU8(&cursor);
				sym_t1 = syNetPeerReadU8(&cursor);
				sym_t2 = syNetPeerReadU8(&cursor);
				sym_target = ((u32)sym_t0 << 16) | ((u32)sym_t1 << 8) | (u32)sym_t2;
				recv_sym_mismatch_tick[i] = (sym_tick != 0U) ? (s32)sym_tick : -1;
				recv_sym_target_tick[i] = (sym_target != 0U) ? (s32)sym_target : -1;
			}
			chk_tick = recv_conn_tick;
			chk_disc = recv_conn_disc;
			syNetPeerMergeIncomingConnectStatus(recv_conn_tick, recv_conn_disc);
		}
		for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
		{
			frames[i].tick = syNetPeerReadU32(&cursor);
			frames[i].buttons = syNetPeerReadU16(&cursor);
			frames[i].stick_x = (s8)syNetPeerReadU8(&cursor);
			frames[i].stick_y = (s8)syNetPeerReadU8(&cursor);
		}
		if (is_dual != FALSE)
		{
			secondary_slot = syNetPeerReadU8(&cursor);
			sec_frame_count = syNetPeerReadU8(&cursor);
			if ((secondary_slot == SYNETPEER_SECONDARY_SLOT_ABSENT) || (sec_frame_count > SYNETPEER_MAX_PACKET_FRAMES) ||
			    (syNetPeerIsAllowedPeerSenderSlot(secondary_slot) == FALSE))
			{
				if (syNetPeerTickDiagLevel() >= 1)
				{
					port_log("SSB64 NetPeer: INPUT drop reason=secondary_header sec_slot=%u sec_fc=%u dual=%d\n",
					         (unsigned int)secondary_slot, (unsigned int)sec_frame_count, (is_dual != FALSE) ? 1 : 0);
				}
				sSYNetPeerPacketsDropped++;
				return;
			}
			for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
			{
				sec_frames[i].tick = syNetPeerReadU32(&cursor);
				sec_frames[i].buttons = syNetPeerReadU16(&cursor);
				sec_frames[i].stick_x = (s8)syNetPeerReadU8(&cursor);
				sec_frames[i].stick_y = (s8)syNetPeerReadU8(&cursor);
			}
		}
		checksum = syNetPeerReadU32(&cursor);
		expected_checksum = syNetPeerChecksumInputPacket(session_id, ack_tick, packet_seq, wire_version, player, frame_count,
		                                                 frames, secondary_slot, sec_frame_count, sec_frames, chk_tick, chk_disc,
		                                                 recv_sym_mismatch_tick, recv_sym_target_tick);

		if (checksum != expected_checksum)
		{
			if (syNetPeerTickDiagLevel() >= 1)
			{
				port_log("SSB64 NetPeer: INPUT drop reason=checksum seq=%u wire=%u got=0x%08X expect=0x%08X\n",
				         (unsigned int)packet_seq, (unsigned int)wire_version,
				         (unsigned int)checksum, (unsigned int)expected_checksum);
			}
			sSYNetPeerPacketsDropped++;
			return;
		}
#if defined(PORT)
		if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
		{
			if (syNetPeerTickDiagLevel() >= 1)
			{
				port_log("SSB64 NetPeer: INPUT drop reason=bind_not_complete seq=%u wire=%u\n",
				         (unsigned int)packet_seq, (unsigned int)wire_version);
			}
			sSYNetPeerPacketsDropped++;
			return;
		}
#endif
		sSYNetPeerPacketsReceived++;
#if defined(PORT)
		if (SYNETPEER_WIRE_HAS_CONNECT_STATUS(wire_version) != FALSE)
		{
			for (i = 0; i < MAXCONTROLLERS; i++)
			{
				if (recv_sym_mismatch_tick[i] > 0)
				{
					syNetRollbackOnPeerSymmetricRollbackNotify(
					    i, (u32)recv_sym_mismatch_tick[i],
					    (recv_sym_target_tick[i] > 0) ? (u32)recv_sym_target_tick[i] : 0U);
				}
			}
		}
#endif

		if (sSYNetPeerRecvSeqInitialized == FALSE)
		{
			sSYNetPeerRecvSeqHighWater = packet_seq;
			sSYNetPeerRecvSeqInitialized = TRUE;
		}
		else
		{
			if (packet_seq > sSYNetPeerRecvSeqHighWater)
			{
				sSYNetPeerSeqGaps += packet_seq - sSYNetPeerRecvSeqHighWater - 1U;
				sSYNetPeerRecvSeqHighWater = packet_seq;
			}
			else if (packet_seq == sSYNetPeerRecvSeqHighWater)
			{
				sSYNetPeerSeqDuplicates++;
			}
			else
			{
				sSYNetPeerSeqOutOfOrder++;
			}
		}
		sSYNetPeerLastPeerAckTick = ack_tick;

	if (frame_count > 0)
	{
		u32 oldest_tick_bundle = frames[0].tick;
		u32 newest_tick_bundle = frames[0].tick;

		for (i = 1; i < frame_count; i++)
		{
			if (frames[i].tick < oldest_tick_bundle)
			{
				oldest_tick_bundle = frames[i].tick;
			}
			if (frames[i].tick > newest_tick_bundle)
			{
				newest_tick_bundle = frames[i].tick;
			}
		}
		if ((is_dual != FALSE) && (sec_frame_count > 0))
		{
			s32 j;

			for (j = 0; j < sec_frame_count; j++)
			{
				if (sec_frames[j].tick < oldest_tick_bundle)
				{
					oldest_tick_bundle = sec_frames[j].tick;
				}
				if (sec_frames[j].tick > newest_tick_bundle)
				{
					newest_tick_bundle = sec_frames[j].tick;
				}
			}
		}
		sSYNetPeerLastPacketOldestTick = oldest_tick_bundle;
		sSYNetPeerLastPacketNewestTick = newest_tick_bundle;
		sSYNetPeerLastPacketTicksValid = TRUE;
	}
	else sSYNetPeerLastPacketTicksValid = FALSE;

#if defined(PORT)
	if (syNetPeerTickDiagLevel() >= 1)
	{
		int sec_st;

		sec_st = ((is_dual != FALSE) && (sec_frame_count > 0)) ? (int)secondary_slot : -1;
		port_log(
		    "SSB64 NetPeer: INPUT recv seq=%u wire=%u peer_primary_sender_slot=%u frames=%u header_peer_targets_us=%u dual=%d secondary_sender_slot=%d sec_frames=%u -> apply_remote_inputs_to_sim_slots %d%s\n",
		    (unsigned int)packet_seq, (unsigned int)wire_version, (unsigned int)player, (unsigned int)frame_count,
		    (unsigned int)packet_remote_player, (is_dual != FALSE) ? 1 : 0, sec_st,
		    (unsigned int)((is_dual != FALSE) ? sec_frame_count : 0U), (int)player,
		    ((is_dual != FALSE) && (sec_frame_count > 0)) ? " +secondary" : "");
	}
#endif

#if defined(PORT)
	syNetPeerLogUdpInputBundleDiag(packet_seq, ack_tick, current_tick, player, frame_count, frames, is_dual,
	                               secondary_slot, sec_frame_count, sec_frames);
#endif

	syNetPeerStagePacketBundle((s32)player, frames, frame_count, current_tick, packet_seq);
	if ((is_dual != FALSE) && (sec_frame_count > 0))
	{
		syNetPeerStagePacketBundle((s32)secondary_slot, sec_frames, sec_frame_count, current_tick, packet_seq);
	}
}

void syNetPeerReceiveRemoteInput(void)
{
#if defined(PORT)
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];

	while (TRUE)
	{
		sb32 wb = FALSE;
		int size = syNetPeerOsRecvFrom(sSYNetPeerSocket, buffer, sizeof(buffer), &wb);

		if (size < 0)
		{
			if (wb == FALSE)
			{
				sSYNetPeerPacketsDropped++;
			}
			break;
		}
		syNetPeerHandlePacket(buffer, (s32)size);
	}
#endif
}

#if defined(PORT)
static u64 sSYNetPeerIngressPumpCalls;
static u64 sSYNetPeerIngressPumpDatagramsTotal;
static int sSYNetPeerIngressDiagEnvCache = -999;

static int syNetPeerGetIngressDiagLevel(void)
{
	const char *e;

	if (sSYNetPeerIngressDiagEnvCache != -999)
	{
		return sSYNetPeerIngressDiagEnvCache;
	}
	e = getenv("SSB64_NETPLAY_INGRESS_DIAG");
	sSYNetPeerIngressDiagEnvCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (sSYNetPeerIngressDiagEnvCache < 0)
	{
		sSYNetPeerIngressDiagEnvCache = 0;
	}
	if (sSYNetPeerIngressDiagEnvCache > 2)
	{
		sSYNetPeerIngressDiagEnvCache = 2;
	}
	return sSYNetPeerIngressDiagEnvCache;
}

static void syNetPeerMaybeLogIngressTransportDiag(const char *tag, u32 dgrams, u32 hr0, u32 hr1)
{
	int lvl;
	u32 push_now;
	static u32 sLastIngressDiagPush;

	lvl = syNetPeerGetIngressDiagLevel();
	if (lvl < 1)
	{
		return;
	}
	push_now = (u32)port_get_push_frame_count();
	if (lvl < 2)
	{
		if ((sLastIngressDiagPush != 0U) && (push_now - sLastIngressDiagPush < 120U))
		{
			return;
		}
		sLastIngressDiagPush = push_now;
	}
	port_log(
	    "SSB64 NetPeer: ingress_diag tag=%s sim=%u hr=%u->%u dgrams=%u push=%u pumps_total=%llu dgrams_total=%llu\n",
	    (tag != NULL) ? tag : "?",
	    (unsigned int)syNetInputGetTick(),
	    (unsigned int)hr0,
	    (unsigned int)hr1,
	    (unsigned int)dgrams,
	    (unsigned int)push_now,
	    (unsigned long long)sSYNetPeerIngressPumpCalls,
	    (unsigned long long)sSYNetPeerIngressPumpDatagramsTotal);
}
#endif

void syNetPeerPumpIngressTransport(const char *caller_tag)
{
#if defined(PORT)
	u32 pk_before;
	u32 pk_after;
	u32 hr0;
	u32 hr1;
	u32 dgrams;
	const char *tag;

	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (sSYNetPeerBootstrapRunInProgress != FALSE)
	{
		syNetPeerReceiveBootstrapPackets();
		return;
	}
	sSYNetPeerIngressPumpCalls++;
	pk_before = sSYNetPeerPacketsReceived;
	hr0 = syNetPeerGetHighestRemoteTick();
	syNetPeerReceiveRemoteInput();
	pk_after = sSYNetPeerPacketsReceived;
	hr1 = syNetPeerGetHighestRemoteTick();
	dgrams = (pk_after >= pk_before) ? (pk_after - pk_before) : 0U;
	sSYNetPeerIngressPumpDatagramsTotal += (u64)dgrams;
	tag = ((caller_tag != NULL) && (caller_tag[0] != '\0')) ? caller_tag : "pump";
	syNetPeerMaybeLogIngressTransportDiag(tag, dgrams, hr0, hr1);
#elif defined(PORT)
	(void)caller_tag;
	/* UDP recv pump is Linux-only in this tree. */
#endif
}

#ifdef PORT
static int sSYNetPeerGcTraversalDiagEnvCache = -999;

static int syNetPeerGetGcTraversalDiagLevel(void)
{
	const char *e;

	if (sSYNetPeerGcTraversalDiagEnvCache != -999)
	{
		return sSYNetPeerGcTraversalDiagEnvCache;
	}
	e = getenv("SSB64_NETPLAY_GC_TRAVERSAL_DIAG");
	sSYNetPeerGcTraversalDiagEnvCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	return sSYNetPeerGcTraversalDiagEnvCache;
}

static int syNetPeerGetDesyncTraceLevel(void)
{
	const char *e;

	if (sSYNetPeerDesyncTraceLevelCache != -999)
	{
		return sSYNetPeerDesyncTraceLevelCache;
	}
	e = getenv("SSB64_NETPLAY_DESYNC_TRACE");
	sSYNetPeerDesyncTraceLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	return sSYNetPeerDesyncTraceLevelCache;
}

static sb32 syNetPeerWantNetSyncExtendedInputDiag(void)
{
	const char *suppress;
	const char *force_rr;

	suppress = getenv("SSB64_NETPLAY_NETSYNC_INPUT_DIAG");
	if ((suppress != NULL) && (suppress[0] != '\0') && (atoi(suppress) == 0))
	{
		force_rr = getenv("SSB64_NETPLAY_REMOTE_RING_CHECKSUM");
		if ((force_rr != NULL) && (force_rr[0] != '\0') && (atoi(force_rr) != 0))
		{
			return TRUE;
		}
		return FALSE;
	}
	if (syNetPeerTickDiagLevel() >= 1)
	{
		return TRUE;
	}
	force_rr = getenv("SSB64_NETPLAY_REMOTE_RING_CHECKSUM");
	if ((force_rr != NULL) && (force_rr[0] != '\0') && (atoi(force_rr) != 0))
	{
		return TRUE;
	}
	return FALSE;
}

#if defined(PORT)
#define SYNETPEER_FRAME_COMMIT_RING 32

static struct SYNetPeerFrameCommitLocalSlot
{
	u32 vtick;
	sb32 valid;
	SYNetFrameCommitToken tok;

} sSYNetPeerFrameCommitLocals[SYNETPEER_FRAME_COMMIT_RING];

static struct SYNetPeerFrameCommitPeerSlot
{
	u32 vtick;
	sb32 valid;
	SYNetFrameCommitToken tok;

} sSYNetPeerFrameCommitPeerPending[SYNETPEER_FRAME_COMMIT_RING];

static u32 sSYNetPeerFrameCommitValidationsSincePeer;
static sb32 sSYNetPeerFrameCommitPeerEver;
static int sSYNetPeerFrameCommitEnvCache = -999;
static int sSYNetPeerFrameCommitDiagEnvCache = -999;

typedef struct SYNetPeerFrameCommitDiag
{
	u32 fc_sent;
	u32 fc_recv;
	u32 fc_compared;
	u32 fc_pairing_fail;
	u32 fc_state_diverge;
	u32 fc_deferred_armed;
	u32 fc_recovery_started;
	u32 fc_recovery_skipped_no_snap;
	u32 fc_pairing_starvation;
} SYNetPeerFrameCommitDiag;

static SYNetPeerFrameCommitDiag sSYNetPeerFrameCommitDiag;
static u32 sSYNetPeerFrameCommitPairingStarvationCount;
#define SYNETPEER_POST_RECOVERY_CONVERGENCE_EPOCHS 2U
static sb32 sSYNetPeerPostRecoveryConvergenceWatch;
static u32 sSYNetPeerConvergenceMatchEpochs;
static u32 sSYNetPeerConvergenceFailEpochs;

void syNetPeerArmPostRecoveryConvergenceWatch(void)
{
	sSYNetPeerPostRecoveryConvergenceWatch = TRUE;
	sSYNetPeerConvergenceMatchEpochs = 0U;
	sSYNetPeerConvergenceFailEpochs = 0U;
}

static void syNetPeerNotePostRecoveryConvergenceEpoch(sb32 matched, u32 validation_tick)
{
	if (sSYNetPeerPostRecoveryConvergenceWatch == FALSE)
	{
		return;
	}
	if (matched != FALSE)
	{
		if (sSYNetPeerConvergenceMatchEpochs < 0xFFFFU)
		{
			sSYNetPeerConvergenceMatchEpochs++;
		}
		sSYNetPeerConvergenceFailEpochs = 0U;
		if (sSYNetPeerConvergenceMatchEpochs >= SYNETPEER_POST_RECOVERY_CONVERGENCE_EPOCHS)
		{
			port_log(
			    "SSB64 NetPeer: POST_RECOVERY_CONVERGENCE_OK validation=%u epochs=%u — watch cleared\n",
			    validation_tick,
			    sSYNetPeerConvergenceMatchEpochs);
			sSYNetPeerPostRecoveryConvergenceWatch = FALSE;
		}
		return;
	}
	if (sSYNetPeerConvergenceFailEpochs < 0xFFFFU)
	{
		sSYNetPeerConvergenceFailEpochs++;
	}
	sSYNetPeerConvergenceMatchEpochs = 0U;
	if (sSYNetPeerConvergenceFailEpochs >= SYNETPEER_POST_RECOVERY_CONVERGENCE_EPOCHS)
	{
		port_log(
		    "SSB64 NetPeer: POST_RECOVERY_CONVERGENCE_FAIL validation=%u fail_epochs=%u — ending VS session\n",
		    validation_tick,
		    sSYNetPeerConvergenceFailEpochs);
		sSYNetPeerPostRecoveryConvergenceWatch = FALSE;
		syNetPeerSendVsSessionEndNotifyPeer();
		syNetPeerStopVSSession();
	}
}

static sb32 syNetPeerFrameCommitDiagEnabled(void)
{
	const char *e;

	if (sSYNetPeerFrameCommitDiagEnvCache != -999)
	{
		return (sSYNetPeerFrameCommitDiagEnvCache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_FRAME_COMMIT_DIAG");
	sSYNetPeerFrameCommitDiagEnvCache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (sSYNetPeerFrameCommitDiagEnvCache != 0) ? TRUE : FALSE;
}

void syNetPeerFrameCommitDiagNoteDeferredArmed(void)
{
	sSYNetPeerFrameCommitDiag.fc_deferred_armed++;
}

void syNetPeerFrameCommitDiagNoteRecoveryStarted(void)
{
	sSYNetPeerFrameCommitDiag.fc_recovery_started++;
}

void syNetPeerFrameCommitDiagNoteRecoverySkippedNoSnap(void)
{
	sSYNetPeerFrameCommitDiag.fc_recovery_skipped_no_snap++;
}

void syNetPeerEmitFrameCommitDiagReport(void)
{
	port_log(
	    "SSB64 NetPeer: FRAME_COMMIT_DIAG sent=%u recv=%u compared=%u pairing_fail=%u state_diverge=%u "
	    "deferred_armed=%u recovery_started=%u recovery_skipped_no_snap=%u pairing_starvation=%u\n",
	    sSYNetPeerFrameCommitDiag.fc_sent,
	    sSYNetPeerFrameCommitDiag.fc_recv,
	    sSYNetPeerFrameCommitDiag.fc_compared,
	    sSYNetPeerFrameCommitDiag.fc_pairing_fail,
	    sSYNetPeerFrameCommitDiag.fc_state_diverge,
	    sSYNetPeerFrameCommitDiag.fc_deferred_armed,
	    sSYNetPeerFrameCommitDiag.fc_recovery_started,
	    sSYNetPeerFrameCommitDiag.fc_recovery_skipped_no_snap,
	    sSYNetPeerFrameCommitDiag.fc_pairing_starvation);
}

sb32 syNetPeerStrictTeardownFastPathActive(void)
{
	if (sSYNetPeerVsSessionEndReceived != FALSE)
	{
		return TRUE;
	}
	if (sSYNetPeerPeerDisconnectConsec >= SYNETPEER_STRICT_DISCONNECT_LATCH_K)
	{
		return TRUE;
	}
	return FALSE;
}

static int syNetPeerFrameCommitGetEnv(void)
{
	const char *e;

	if (sSYNetPeerFrameCommitEnvCache != -999)
	{
		return sSYNetPeerFrameCommitEnvCache;
	}
	e = getenv("SSB64_NETPLAY_FRAME_COMMIT_TOKEN");
	if ((e == NULL) || (e[0] == '\0'))
	{
		sSYNetPeerFrameCommitEnvCache = 1;
	}
	else
	{
		sSYNetPeerFrameCommitEnvCache = atoi(e);
		if (sSYNetPeerFrameCommitEnvCache < 0)
		{
			sSYNetPeerFrameCommitEnvCache = 0;
		}
		if (sSYNetPeerFrameCommitEnvCache > 1)
		{
			sSYNetPeerFrameCommitEnvCache = 1;
		}
	}
	return sSYNetPeerFrameCommitEnvCache;
}

static void syNetPeerFrameCommitReset(void)
{
	u32 i;

	for (i = 0U; i < (u32)SYNETPEER_FRAME_COMMIT_RING; i++)
	{
		sSYNetPeerFrameCommitLocals[i].valid = FALSE;
		sSYNetPeerFrameCommitPeerPending[i].valid = FALSE;
	}
	sSYNetPeerFrameCommitValidationsSincePeer = 0U;
	sSYNetPeerFrameCommitPeerEver = FALSE;
	sSYNetPeerFrameCommitPairingStarvationCount = 0U;
	sSYNetPeerPostRecoveryConvergenceWatch = FALSE;
	sSYNetPeerConvergenceMatchEpochs = 0U;
	sSYNetPeerConvergenceFailEpochs = 0U;
	memset(&sSYNetPeerFrameCommitDiag, 0, sizeof(sSYNetPeerFrameCommitDiag));
}

static void syNetPeerFrameCommitStoreLocal(u32 vtick, const SYNetFrameCommitToken *t)
{
	u32 i;

	i = vtick % (u32)SYNETPEER_FRAME_COMMIT_RING;
	sSYNetPeerFrameCommitLocals[i].vtick = vtick;
	sSYNetPeerFrameCommitLocals[i].tok = *t;
	sSYNetPeerFrameCommitLocals[i].valid = TRUE;
}

static sb32 syNetPeerFrameCommitLoadLocal(u32 vtick, SYNetFrameCommitToken *out)
{
	u32 i;

	i = vtick % (u32)SYNETPEER_FRAME_COMMIT_RING;
	if ((sSYNetPeerFrameCommitLocals[i].valid == FALSE) || (sSYNetPeerFrameCommitLocals[i].vtick != vtick))
	{
		return FALSE;
	}
	*out = sSYNetPeerFrameCommitLocals[i].tok;
	return TRUE;
}

static void syNetPeerFrameCommitStorePeerPending(u32 vtick, const SYNetFrameCommitToken *t)
{
	u32 i;

	i = vtick % (u32)SYNETPEER_FRAME_COMMIT_RING;
	sSYNetPeerFrameCommitPeerPending[i].vtick = vtick;
	sSYNetPeerFrameCommitPeerPending[i].tok = *t;
	sSYNetPeerFrameCommitPeerPending[i].valid = TRUE;
}

static sb32 syNetPeerFrameCommitLoadPeerPending(u32 vtick, SYNetFrameCommitToken *out)
{
	u32 i;

	i = vtick % (u32)SYNETPEER_FRAME_COMMIT_RING;
	if ((sSYNetPeerFrameCommitPeerPending[i].valid == FALSE) || (sSYNetPeerFrameCommitPeerPending[i].vtick != vtick))
	{
		return FALSE;
	}
	*out = sSYNetPeerFrameCommitPeerPending[i].tok;
	sSYNetPeerFrameCommitPeerPending[i].valid = FALSE;
	return TRUE;
}

static void syNetPeerFrameCommitTryCompare(u32 vtick, const SYNetFrameCommitToken *local, const SYNetFrameCommitToken *peer)
{
	sb32 df;
	sb32 di;
	sb32 ds;
	sb32 dt;
	u32 anchor_diff;

	sSYNetPeerFrameCommitDiag.fc_compared++;
	if (local->frame_id != peer->frame_id)
	{
		sSYNetPeerFrameCommitDiag.fc_pairing_fail++;
		if (syNetPeerFrameCommitDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetPeer: FRAME_COMMIT_PAIRING_FAIL validation=%u reason=frame_id local=%d peer=%d anchor_local=%u anchor_peer=%u\n",
			    vtick,
			    (int)local->frame_id,
			    (int)peer->frame_id,
			    local->tick_anchor,
			    peer->tick_anchor);
		}
		syNetPeerNotePostRecoveryConvergenceEpoch(FALSE, vtick);
		return;
	}
	anchor_diff = (local->tick_anchor >= peer->tick_anchor) ? (local->tick_anchor - peer->tick_anchor)
								: (peer->tick_anchor - local->tick_anchor);
	if (anchor_diff > 1U)
	{
		sSYNetPeerFrameCommitDiag.fc_pairing_fail++;
		if (sSYNetPeerFrameCommitPairingStarvationCount < 0xFFFFU)
		{
			sSYNetPeerFrameCommitPairingStarvationCount++;
		}
		sSYNetPeerFrameCommitDiag.fc_pairing_starvation = sSYNetPeerFrameCommitPairingStarvationCount;
		if (syNetPeerFrameCommitDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetPeer: FRAME_COMMIT_PAIRING_FAIL validation=%u reason=tick_anchor anchor_local=%u anchor_peer=%u diff=%u\n",
			    vtick,
			    local->tick_anchor,
			    peer->tick_anchor,
			    anchor_diff);
		}
		if (sSYNetPeerFrameCommitPairingStarvationCount >= 4U)
		{
			port_log(
			    "SSB64 NetPeer: FRAME_COMMIT_PAIRING_STARVATION validation=%u count=%u — ending VS session\n",
			    vtick,
			    sSYNetPeerFrameCommitPairingStarvationCount);
			syNetPeerSendVsSessionEndNotifyPeer();
			syNetPeerStopVSSession();
		}
		syNetPeerNotePostRecoveryConvergenceEpoch(FALSE, vtick);
		return;
	}
	sSYNetPeerFrameCommitPairingStarvationCount = 0U;
	if (syNetPeerFrameCommitDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetPeer: FRAME_COMMIT_COMPARE validation=%u frame_id=%d anchor_local=%u anchor_peer=%u\n",
		    vtick,
		    (int)local->frame_id,
		    local->tick_anchor,
		    peer->tick_anchor);
	}
	if (syNetFrameCommitTokensDesync(local, peer, &df, &di, &ds, &dt) != FALSE)
	{
		syNetDesyncClassifierOnFrameCommitTokenMismatch(vtick, local, peer);
	}
	if (syNetFrameCommitStateDigestsDiverge(local, peer) != FALSE)
	{
		sSYNetPeerFrameCommitDiag.fc_state_diverge++;
		syNetPeerNotePostRecoveryConvergenceEpoch(FALSE, vtick);
		syNetRollbackOnPeerFrameCommitStateMismatch(vtick, local, peer);
		return;
	}
	syNetPeerNotePostRecoveryConvergenceEpoch(TRUE, vtick);
}

static void syNetPeerSendFrameCommitPacket(u32 validation_tick, const SYNetFrameCommitToken *t)
{
	u8 buf[SYNETPEER_FRAME_COMMIT_BYTES];
	u8 *cursor;
	u32 chk;

	if ((syNetPeerOsSocketIsValid(sSYNetPeerSocket) == FALSE) || (syNetPeerFrameCommitGetEnv() == 0))
	{
		return;
	}
	cursor = buf;
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_FRAME_COMMIT);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, validation_tick);
	syNetPeerWriteU32(&cursor, (u32)t->frame_id);
	syNetPeerWriteU32(&cursor, t->input_digest);
	syNetPeerWriteU32(&cursor, t->slot_binding_hash);
	syNetPeerWriteU32(&cursor, t->tick_anchor);
	syNetPeerWriteU32(&cursor, t->fighter_digest);
	syNetPeerWriteU32(&cursor, t->world_digest);
	syNetPeerWriteU32(&cursor, t->item_digest);
	syNetPeerWriteU32(&cursor, t->rng_digest);
	chk = syNetPeerChecksumBytes(buf, (u32)(sizeof(buf) - 4U));
	syNetPeerWriteU32(&cursor, chk);
	if (syNetPeerOsSendTo(sSYNetPeerSocket, buf, (size_t)sizeof(buf), &sSYNetPeerPeerAddress) != (int)sizeof(buf))
	{
		return;
	}
	sSYNetPeerPacketsSent++;
	sSYNetPeerFrameCommitDiag.fc_sent++;
}

static void syNetPeerHandleFrameCommitPacket(const u8 *buffer, s32 size)
{
	const u8 *c;
	u32 magic;
	u16 wire_version;
	u16 packet_type;
	u32 session_id;
	u32 validation_tick;
	u32 frame_id_u;
	SYNetFrameCommitToken peer;
	SYNetFrameCommitToken local;
	u32 checksum;
	u32 expected;

	if (size != (s32)SYNETPEER_FRAME_COMMIT_BYTES)
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	expected = syNetPeerChecksumBytes(buffer, (u32)size - 4U);
	c = buffer;
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	validation_tick = syNetPeerReadU32(&c);
	frame_id_u = syNetPeerReadU32(&c);
	peer.input_digest = syNetPeerReadU32(&c);
	peer.slot_binding_hash = syNetPeerReadU32(&c);
	peer.tick_anchor = syNetPeerReadU32(&c);
	peer.fighter_digest = syNetPeerReadU32(&c);
	peer.world_digest = syNetPeerReadU32(&c);
	peer.item_digest = syNetPeerReadU32(&c);
	peer.rng_digest = syNetPeerReadU32(&c);
	checksum = syNetPeerReadU32(&c);
	peer.frame_id = (s32)frame_id_u;
	if ((magic != SYNETPEER_MAGIC) || (wire_version != SYNETPEER_VERSION) ||
	    (packet_type != (u16)SYNETPEER_PACKET_FRAME_COMMIT) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	sSYNetPeerPacketsReceived++;
	sSYNetPeerFrameCommitDiag.fc_recv++;
	sSYNetPeerFrameCommitPeerEver = TRUE;
	sSYNetPeerFrameCommitValidationsSincePeer = 0U;
	syNetDesyncClassifierOnFrameCommitPeerTokenReceived(validation_tick);
	if (syNetPeerFrameCommitLoadLocal(validation_tick, &local) != FALSE)
	{
		syNetPeerFrameCommitTryCompare(validation_tick, &local, &peer);
	}
	else
	{
		syNetPeerFrameCommitStorePeerPending(validation_tick, &peer);
	}
}

void syNetPeerTrySendRollbackBaselineDigest(void)
{
	u8 buf[SYNETPEER_ROLLBACK_BASELINE_BYTES];
	u8 *cursor;
	u32 load_tick;
	u32 figh;
	u32 world;
	u32 item;
	u32 rng;
	u32 anim;
	u32 weapon;
	u32 map;
	u32 camera;
	u32 fighter_slot[GMCOMMON_PLAYERS_MAX];
	u32 chk;
	s32 si;
	int sent;

	if (syNetRollbackTakePeerBaselineDigestForSend(&load_tick, &figh, &world, &item, &rng, &anim, &weapon, &map,
						     &camera, fighter_slot, GMCOMMON_PLAYERS_MAX) == FALSE)
	{
		return;
	}
	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) == FALSE)
	{
		port_log("SSB64 NetPeer: RESIM_BASELINE_SEND_FAIL load_tick=%u reason=invalid_socket\n", load_tick);
		return;
	}
	cursor = buf;
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_ROLLBACK_BASELINE);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, load_tick);
	syNetPeerWriteU32(&cursor, figh);
	syNetPeerWriteU32(&cursor, world);
	syNetPeerWriteU32(&cursor, item);
	syNetPeerWriteU32(&cursor, rng);
	syNetPeerWriteU32(&cursor, anim);
	syNetPeerWriteU32(&cursor, weapon);
	syNetPeerWriteU32(&cursor, map);
	syNetPeerWriteU32(&cursor, camera);
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		syNetPeerWriteU32(&cursor, fighter_slot[si]);
	}
	chk = syNetPeerChecksumBytes(buf, (u32)(sizeof(buf) - 4U));
	syNetPeerWriteU32(&cursor, chk);
	sent = syNetPeerOsSendTo(sSYNetPeerSocket, buf, (size_t)sizeof(buf), &sSYNetPeerPeerAddress);
	if (sent != (int)sizeof(buf))
	{
		port_log(
		    "SSB64 NetPeer: RESIM_BASELINE_SEND_FAIL load_tick=%u figh=0x%08X sent=%d expected=%u\n",
		    load_tick,
		    figh,
		    sent,
		    (unsigned int)sizeof(buf));
		return;
	}
	sSYNetPeerPacketsSent++;
	port_log(
	    "SSB64 NetPeer: RESIM_BASELINE_SEND load_tick=%u figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X bytes=%u\n",
	    load_tick,
	    figh,
	    world,
	    item,
	    rng,
	    (unsigned int)sizeof(buf));
	syNetRollbackNotePeerBaselineDigestSent();
}

void syNetPeerTrySendRollbackSyncNotice(void)
{
	u8 buf[SYNETPEER_ROLLBACK_SYNC_BYTES];
	s32 conn_symmetric_tick[MAXCONTROLLERS];
	s32 conn_symmetric_target[MAXCONTROLLERS];
	s32 slot;

	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) == FALSE)
	{
		return;
	}
	memset(conn_symmetric_tick, 0, sizeof(conn_symmetric_tick));
	memset(conn_symmetric_target, 0, sizeof(conn_symmetric_target));
	syNetRollbackExportPeerSymmetricNotify(conn_symmetric_tick, conn_symmetric_target, MAXCONTROLLERS);
	for (slot = 0; slot < MAXCONTROLLERS; slot++)
	{
		u8 *cursor;
		u32 mismatch_tick;
		u32 target_tick;
		u32 chk;
		int sent;

		if (conn_symmetric_tick[slot] <= 0)
		{
			continue;
		}
		mismatch_tick = (u32)conn_symmetric_tick[slot];
		target_tick = (conn_symmetric_target[slot] > 0) ? (u32)conn_symmetric_target[slot] : 0U;
		cursor = buf;
		syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
		syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
		syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_ROLLBACK_SYNC);
		syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
		syNetPeerWriteU32(&cursor, mismatch_tick);
		syNetPeerWriteU32(&cursor, target_tick);
		syNetPeerWriteU8(&cursor, (u8)slot);
		syNetPeerWriteU8(&cursor, 1U);
		syNetPeerWriteU16(&cursor, 0);
		chk = syNetPeerChecksumBytes(buf, (u32)(sizeof(buf) - 4U));
		syNetPeerWriteU32(&cursor, chk);
		sent = syNetPeerOsSendTo(sSYNetPeerSocket, buf, (size_t)sizeof(buf), &sSYNetPeerPeerAddress);
		if (sent == (int)sizeof(buf))
		{
			sSYNetPeerPacketsSent++;
			port_log(
			    "SSB64 NetPeer: ROLLBACK_SYNC_SEND slot=%d mismatch_tick=%u target_tick=%u bytes=%u\n",
			    (int)slot,
			    mismatch_tick,
			    target_tick,
			    (unsigned int)sizeof(buf));
		}
	}
}

static void syNetPeerHandleRollbackSyncPacket(const u8 *buffer, s32 size)
{
	const u8 *c;
	u32 magic;
	u16 wire_version;
	u16 packet_type;
	u32 session_id;
	u32 mismatch_tick;
	u32 target_tick;
	u8 slot;
	u8 flags;
	u16 reserved;
	u32 checksum;
	u32 expected;

	if (size != (s32)SYNETPEER_ROLLBACK_SYNC_BYTES)
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	expected = syNetPeerChecksumBytes(buffer, (u32)size - 4U);
	c = buffer;
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	mismatch_tick = syNetPeerReadU32(&c);
	target_tick = syNetPeerReadU32(&c);
	slot = syNetPeerReadU8(&c);
	flags = syNetPeerReadU8(&c);
	reserved = syNetPeerReadU16(&c);
	checksum = syNetPeerReadU32(&c);
	(void)flags;
	(void)reserved;
	if ((magic != SYNETPEER_MAGIC) || (packet_type != (u16)SYNETPEER_PACKET_ROLLBACK_SYNC) ||
	    (session_id != sSYNetPeerSessionID) || (checksum != expected))
	{
		sSYNetPeerPacketsDropped++;
		port_log(
		    "SSB64 NetPeer: ROLLBACK_SYNC_RECV_DROP mismatch_tick=%u chk=0x%08X expected=0x%08X session=%u\n",
		    mismatch_tick,
		    checksum,
		    expected,
		    (session_id != sSYNetPeerSessionID) ? 1U : 0U);
		return;
	}
	if (syNetRollbackAcceptPeerSymmetricRollbackNotify((s32)slot, mismatch_tick, target_tick) == FALSE)
	{
		sSYNetPeerPacketsReceived++;
		return;
	}
	sSYNetPeerPacketsReceived++;
	port_log(
	    "SSB64 NetPeer: ROLLBACK_SYNC_RECV slot=%d mismatch_tick=%u target_tick=%u wire=%u\n",
	    (int)slot,
	    mismatch_tick,
	    target_tick,
	    (unsigned int)wire_version);
	syNetRollbackOnPeerSymmetricRollbackNotify((s32)slot, mismatch_tick, target_tick);
}

static void syNetPeerHandleRollbackBaselinePacket(const u8 *buffer, s32 size)
{
	const u8 *c;
	u32 magic;
	u16 wire_version;
	u16 packet_type;
	u32 session_id;
	u32 load_tick;
	u32 figh;
	u32 world;
	u32 item;
	u32 rng;
	u32 anim;
	u32 weapon;
	u32 map;
	u32 camera;
	u32 fighter_slot[GMCOMMON_PLAYERS_MAX];
	u32 checksum;
	u32 expected;
	sb32 has_fighter_slots;
	s32 si;

	if ((size != (s32)SYNETPEER_ROLLBACK_BASELINE_BYTES) &&
	    (size != (s32)SYNETPEER_ROLLBACK_BASELINE_BYTES_LEGACY))
	{
		sSYNetPeerPacketsDropped++;
		port_log("SSB64 NetPeer: RESIM_BASELINE_RECV_DROP reason=size size=%d\n", (int)size);
		return;
	}
	has_fighter_slots = (size == (s32)SYNETPEER_ROLLBACK_BASELINE_BYTES) ? TRUE : FALSE;
	expected = syNetPeerChecksumBytes(buffer, (u32)size - 4U);
	c = buffer;
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	load_tick = syNetPeerReadU32(&c);
	figh = syNetPeerReadU32(&c);
	world = syNetPeerReadU32(&c);
	item = syNetPeerReadU32(&c);
	rng = syNetPeerReadU32(&c);
	anim = syNetPeerReadU32(&c);
	weapon = syNetPeerReadU32(&c);
	map = syNetPeerReadU32(&c);
	camera = syNetPeerReadU32(&c);
	if (has_fighter_slots != FALSE)
	{
		for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
		{
			fighter_slot[si] = syNetPeerReadU32(&c);
		}
	}
	checksum = syNetPeerReadU32(&c);
	if ((magic != SYNETPEER_MAGIC) || (wire_version != SYNETPEER_VERSION) ||
	    (packet_type != (u16)SYNETPEER_PACKET_ROLLBACK_BASELINE) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected))
	{
		sSYNetPeerPacketsDropped++;
		port_log(
		    "SSB64 NetPeer: RESIM_BASELINE_RECV_DROP load_tick=%u reason=header chk=0x%08X expected=0x%08X session=%u\n",
		    load_tick,
		    checksum,
		    expected,
		    session_id);
		return;
	}
	sSYNetPeerPacketsReceived++;
	port_log(
	    "SSB64 NetPeer: RESIM_BASELINE_RECV load_tick=%u figh=0x%08X world=0x%08X item=0x%08X rng=0x%08X fighter_slots=%d\n",
	    load_tick,
	    figh,
	    world,
	    item,
	    rng,
	    (has_fighter_slots != FALSE) ? 1 : 0);
	syNetRollbackOnPeerBaselineDigest(load_tick, figh, world, item, rng, anim, weapon, map, camera,
					  (has_fighter_slots != FALSE) ? fighter_slot : NULL);
}

void syNetPeerTrySendResimPostDigest(u32 epoch_id, u32 load_tick, u32 mismatch_tick, u32 target_tick, u32 figh,
				     u32 world, u32 item, u32 rng, u32 input_digest)
{
	u8 buf[SYNETPEER_RESIM_POST_BYTES];
	u8 *cursor;
	u32 chk;
	int sent;

	if (syNetPeerOsSocketIsValid(sSYNetPeerSocket) == FALSE)
	{
		return;
	}
	cursor = buf;
	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_RESIM_POST);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, epoch_id);
	syNetPeerWriteU32(&cursor, load_tick);
	syNetPeerWriteU32(&cursor, mismatch_tick);
	syNetPeerWriteU32(&cursor, target_tick);
	syNetPeerWriteU32(&cursor, figh);
	syNetPeerWriteU32(&cursor, world);
	syNetPeerWriteU32(&cursor, item);
	syNetPeerWriteU32(&cursor, rng);
	syNetPeerWriteU32(&cursor, input_digest);
	chk = syNetPeerChecksumBytes(buf, (u32)(sizeof(buf) - 4U));
	syNetPeerWriteU32(&cursor, chk);
	sent = syNetPeerOsSendTo(sSYNetPeerSocket, buf, (size_t)sizeof(buf), &sSYNetPeerPeerAddress);
	if (sent == (int)sizeof(buf))
	{
		sSYNetPeerPacketsSent++;
	}
}

static void syNetPeerHandleResimPostPacket(const u8 *buffer, s32 size)
{
	const u8 *c;
	u32 magic;
	u16 wire_version;
	u16 packet_type;
	u32 session_id;
	u32 epoch_id;
	u32 load_tick;
	u32 mismatch_tick;
	u32 target_tick;
	u32 figh;
	u32 world;
	u32 item;
	u32 rng;
	u32 input_digest;
	u32 checksum;
	u32 expected;

	if (size != (s32)SYNETPEER_RESIM_POST_BYTES)
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	expected = syNetPeerChecksumBytes(buffer, (u32)size - 4U);
	c = buffer;
	magic = syNetPeerReadU32(&c);
	wire_version = syNetPeerReadU16(&c);
	packet_type = syNetPeerReadU16(&c);
	session_id = syNetPeerReadU32(&c);
	epoch_id = syNetPeerReadU32(&c);
	load_tick = syNetPeerReadU32(&c);
	mismatch_tick = syNetPeerReadU32(&c);
	target_tick = syNetPeerReadU32(&c);
	figh = syNetPeerReadU32(&c);
	world = syNetPeerReadU32(&c);
	item = syNetPeerReadU32(&c);
	rng = syNetPeerReadU32(&c);
	input_digest = syNetPeerReadU32(&c);
	checksum = syNetPeerReadU32(&c);
	if ((magic != SYNETPEER_MAGIC) || (wire_version != SYNETPEER_VERSION) ||
	    (packet_type != (u16)SYNETPEER_PACKET_RESIM_POST) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	sSYNetPeerPacketsReceived++;
	syNetRollbackOnPeerResimPostDigest(epoch_id, load_tick, mismatch_tick, target_tick, figh, world, item, rng,
					   input_digest);
}

static void syNetPeerFrameCommitAfterValidation(u32 validation_tick, u32 win_begin, u32 win_len)
{
	SYNetFrameCommitToken tok;
	SYNetFrameCommitToken pending;

	if (syNetPeerFrameCommitGetEnv() == 0)
	{
		return;
	}
	if ((sSYNetPeerIsActive == FALSE) || (syNetPeerCheckBattleExecutionReady() == FALSE))
	{
		return;
	}
	syNetFrameCommitBuildToken(&tok, validation_tick, win_begin, win_len, sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
				  sSYNetPeerExtraLocalSenderSlot, sSYNetPeerPeerSenderCount, sSYNetPeerPeerSenderSlots);
	syNetPeerFrameCommitStoreLocal(validation_tick, &tok);
	syNetPeerSendFrameCommitPacket(validation_tick, &tok);
	sSYNetPeerFrameCommitValidationsSincePeer++;
	syNetDesyncClassifierOnFrameCommitValidationSent(validation_tick, sSYNetPeerFrameCommitValidationsSincePeer);
	if (syNetPeerFrameCommitLoadPeerPending(validation_tick, &pending) != FALSE)
	{
		syNetPeerFrameCommitTryCompare(validation_tick, &tok, &pending);
	}
}
static void syNetPeerRefreshCachedNetplayEnvCachesOnly(void)
{
	syNetInputRefreshCachedNetplayEnvForNewMatch();
	sSYNetPeerTickDiagEnvCache = -999;
	sSYNetPeerDesyncTraceLevelCache = -999;
	sSYNetPeerMpTicDiagAssertEnvCache = -999;
	sSYNetPeerStateDetailDiagEnvCache = -999;
	sSYNetPeerGcTraversalDiagEnvCache = -999;
	sSYNetPeerFrameCommitEnvCache = -999;
	sSYNetPeerUdpFrameTraceEnvCache = -1;
	sSYNetPeerDelaySyncDiagEnvCache = -999;
	syNetPeerResetMatchBufferMinSlackEnv();
	syNetPeerResetDelaySyncCommitLeadEnv();
	sSYNetPeerIngressDiagEnvCache = -999;
	sSYNetPeerPhaseLockPredictionWindowEnv = -999;
}

void syNetPeerRefreshCachedNetplayEnvForNewMatch(void)
{
	syNetPeerRefreshCachedNetplayEnvCachesOnly();
	syNetSessionParamsResetForNewMatch();
	syNetPeerSessionParamsResetTransport();
	syNetInputClearSessionTransportOverrides();
}

void syNetPeerLogNetSyncValidation(u32 tick)
{
	u32 checksums[MAXCONTROLLERS];
	u32 inp_all = 0;
	u32 fighter_hash;
	u32 map_hash;
	u32 world_hash;
	u32 item_hash;
	u32 weapon_hash;
	u32 rng_hash;
	u32 camera_hash;
	u32 animation_hash;
	u32 win_begin = 0;
	u32 win_length = tick;

	if ((sSYNetPeerIsActive == FALSE) || (syNetPeerCheckBattleExecutionReady() == FALSE))
	{
		return;
	}
	if (tick >= SYNETPEER_VALIDATION_INPUT_WINDOW)
	{
		win_begin = tick - SYNETPEER_VALIDATION_INPUT_WINDOW;
		win_length = SYNETPEER_VALIDATION_INPUT_WINDOW;
	}
	{
		s32 abort_mask = syNetInputGetAbortOnInputMismatchMask();

		if ((abort_mask & 1) != 0)
		{
			s32 mis_player = 0;
			u32 mis_tick = 0U;
			u32 mis_kind = 0U;

			if (syNetInputDiagFindFirstPublishedRemoteMismatch(win_begin, win_length, &mis_player, &mis_tick,
			                                                   &mis_kind) != FALSE)
			{
				port_log(
				    "SSB64 NetPeer: ABORT_ON_INPUT_MISMATCH (bit1) pub_vs_remote kind=%s player=%d tick=%u "
				    "validation_tick=%u win=[%u,%u) — %s\n",
				    (mis_kind == 0U) ? "presence" : "values",
				    (int)mis_player,
				    (unsigned int)mis_tick,
				    tick,
				    win_begin,
				    win_begin + win_length,
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
					    "SSB64 NetPeer: ABORT_ON_INPUT_MISMATCH_FATAL skipped (sync pipeline phase=%d; hard-abort only "
					    "in Running)\n",
					    (int)syNetPeerGetSyncPipelinePhase());
				}
			}
		}
	}
	syNetInputGetHistoryInputValueChecksumWindow(win_begin, win_length, checksums, &inp_all);

	fighter_hash = syNetSyncHashBattleFighters();
	{
		const char *dual_env;
		static int sDualHashProbeCache = -999;

		if (sDualHashProbeCache == -999)
		{
			dual_env = getenv("SSB64_NETPLAY_VALIDATION_DUAL_HASH");
			sDualHashProbeCache = ((dual_env != NULL) && (dual_env[0] != '\0') && (atoi(dual_env) != 0)) ? 1 : 0;
		}
		if (sDualHashProbeCache != 0)
		{
			u32 figh_full = syNetSyncHashBattleFightersFull();

			if (figh_full != fighter_hash)
			{
				port_log(
				    "SSB64 NetSync: validation_dual_hash tick=%u figh_light=0x%08X figh_full=0x%08X\n",
				    tick,
				    fighter_hash,
				    figh_full);
			}
		}
	}
	map_hash = syNetSyncHashMapCollisionKinematics();
	world_hash = syNetSyncHashRollbackWorld();
	item_hash = syNetSyncHashActiveItemsForRollback();
	weapon_hash = syNetSyncHashActiveWeaponsForRollback();
	rng_hash = syNetSyncHashRNGSeed();
	camera_hash = syNetSyncHashGMCamera();
	animation_hash = syNetSyncHashFighterAnimationStateForRollback();
	{
		if (sSYNetPeerMpTicDiagAssertEnvCache == -999)
		{
			char *e = getenv("SSB64_NETPLAY_ASSERT_MP_TIC");

			sSYNetPeerMpTicDiagAssertEnvCache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
		}
		if (sSYNetPeerMpTicDiagAssertEnvCache != 0)
		{
			port_log("SSB64 NetSync: mp_tic_diag sim_tick=%u mp_collision_tic=%u\n", syNetInputGetTick(),
			         (unsigned int)gMPCollisionUpdateTic);
		}
	}
	syNetDesyncClassifierOnNetSyncValidation(tick, win_begin, win_length, inp_all, fighter_hash, map_hash,
					       sSYNetPeerLateFrames, sSYNetPeerSeqGaps);
	syNetPeerFrameCommitAfterValidation(tick, win_begin, win_length);

	port_log(
		"SSB64 NetSync: role=%s lp=%d rp=%d tick=%u hist_win=[%u,%u) all=0x%08X p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X figh=0x%08X mph=0x%08X world=0x%08X item=0x%08X wpn=0x%08X rng=0x%08X cam=0x%08X anim=0x%08X snd_next=%u rcv_hw=%u gap=%u dup=%u ooo=%u puck=%u pko=%u pkn=%u sent=%u recv=%u dropped=%u stg=%u hr=%u commit_gen=%u late=%u inpchk=0x%08X pkt_valid=%d rb=%u lf=%u delay=%u ring=%u rscan=%u\n",
		(sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		sSYNetPeerLocalPlayer,
		sSYNetPeerRemotePlayer,
		tick,
		win_begin,
		win_begin + win_length,
		inp_all,
		checksums[0],
		checksums[1],
		checksums[2],
		checksums[3],
		fighter_hash,
		map_hash,
		world_hash,
		item_hash,
		weapon_hash,
		rng_hash,
		camera_hash,
		animation_hash,
		sSYNetPeerSendSeq,
		sSYNetPeerRecvSeqHighWater,
		sSYNetPeerSeqGaps,
		sSYNetPeerSeqDuplicates,
		sSYNetPeerSeqOutOfOrder,
		sSYNetPeerLastPeerAckTick,
		(sSYNetPeerLastPacketTicksValid != FALSE) ? sSYNetPeerLastPacketOldestTick : (~(u32)0),
		(sSYNetPeerLastPacketTicksValid != FALSE) ? sSYNetPeerLastPacketNewestTick : (~(u32)0),
		sSYNetPeerPacketsSent,
		sSYNetPeerPacketsReceived,
		sSYNetPeerPacketsDropped,
		sSYNetPeerFramesStaged,
		sSYNetPeerHighestRemoteTick,
		sSYNetPeerGlobalCommitGen,
		sSYNetPeerLateFrames,
		sSYNetPeerInputChecksum,
		sSYNetPeerLastPacketTicksValid,
		syNetRollbackGetAppliedResimCount(),
		syNetRollbackGetLoadFailCount(),
		sSYNetPeerInputDelay,
		(u32)SYNETINPUT_HISTORY_LENGTH,
		(u32)SYNETROLLBACK_SCAN_WINDOW);
	{
		int sdd = syNetPeerGetStateDetailDiagLevel();

		if (sdd >= 1)
		{
			syNetSyncLogRollbackWorldDetail("netsync", tick);
		}
		if (sdd >= 2)
		{
			syNetSyncLogFighterDetail("netsync", tick);
		}
	}
	{
		int gtd = syNetPeerGetGcTraversalDiagLevel();

		if (gtd >= 1)
		{
			u32 gch;
			u32 ngobj;
			u32 ngobj_run;
			u32 nproc_run;

			gcPortGcRunAllTraversalFingerprintEx(&gch, &ngobj, &ngobj_run, &nproc_run);
			if (gtd >= 2)
			{
				char pairs[384];

				gcPortSnprintGcRunAllTraversalHeadPairs(pairs, sizeof(pairs), 16);
				port_log(
				    "SSB64 NetSync: gc_traversal tick=%u gch=0x%08X gobj=%u grun=%u prun=%u pairs=\"%s\"\n",
				    tick, gch, ngobj, ngobj_run, nproc_run, pairs);
			}
			else
			{
				port_log("SSB64 NetSync: gc_traversal tick=%u gch=0x%08X gobj=%u grun=%u prun=%u\n", tick, gch,
				         ngobj, ngobj_run, nproc_run);
			}
		}
	}
#if defined(SSB64_NETMENU)
	syNetFighterPhaseTraceEmitNetSyncLines(tick);
#endif
	{
		int dtl = syNetPeerGetDesyncTraceLevel();

		if (dtl >= 1)
		{
			u32 needle;

			needle = (tick >= 1U) ? (tick - 1U) : 0U;
			syNetInputLogDesyncNeedle(tick, needle, dtl);
			if (sSYNetPeerDesyncTracePrevValid != FALSE)
			{
				if (fighter_hash != sSYNetPeerDesyncTracePrevFigh)
				{
					port_log(
					    "SSB64 NetSync: desync_trace figh_transition validation_tick=%u needle_tick=%u "
					    "figh 0x%08X -> 0x%08X mph=0x%08X\n",
					    tick,
					    needle,
					    sSYNetPeerDesyncTracePrevFigh,
					    fighter_hash,
					    map_hash);
				}
			}
			sSYNetPeerDesyncTracePrevFigh = fighter_hash;
			sSYNetPeerDesyncTracePrevValid = TRUE;
		}
	}
	if (syNetPeerWantNetSyncExtendedInputDiag() != FALSE)
	{
		u32 rsums[MAXCONTROLLERS];
		u32 rall = 0U;
		u32 hdiag[MAXCONTROLLERS];
		u32 hall_diag = 0U;
		u32 rdiag[MAXCONTROLLERS];
		u32 rall_diag = 0U;
		s32 mis_player = 0;
		u32 mis_tick = 0U;
		u32 mis_kind = 0U;

		syNetInputGetRemoteHistoryValueChecksumWindow(win_begin, win_length, rsums, &rall);
		port_log(
		    "SSB64 NetSync: remote_ring_hist_win=[%u,%u) all=0x%08X p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X\n",
		    win_begin, win_begin + win_length, rall, rsums[0], rsums[1], rsums[2], rsums[3]);
		syNetInputGetHistoryInputDiagChecksumWindow(win_begin, win_length, hdiag, &hall_diag);
		syNetInputGetRemoteHistoryDiagChecksumWindow(win_begin, win_length, rdiag, &rall_diag);
		port_log(
		    "SSB64 NetSync: hist_diag_win=[%u,%u) all=0x%08X p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X "
		    "(+src/pred/valid)\n",
		    win_begin, win_begin + win_length, hall_diag, hdiag[0], hdiag[1], hdiag[2], hdiag[3]);
		port_log(
		    "SSB64 NetSync: remote_ring_diag_win=[%u,%u) all=0x%08X p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X "
		    "(+src/pred/valid)\n",
		    win_begin, win_begin + win_length, rall_diag, rdiag[0], rdiag[1], rdiag[2], rdiag[3]);
		if (syNetInputDiagFindFirstPublishedRemoteMismatch(win_begin, win_length, &mis_player, &mis_tick,
		                                                   &mis_kind) != FALSE)
		{
			port_log(
			    "SSB64 NetSync: pub_vs_remote mismatch kind=%s player=%d tick=%u (published history vs "
			    "remote ring; buttons/sticks/tick)\n",
			    (mis_kind == 0U) ? "presence" : "values", (int)mis_player, (unsigned int)mis_tick);
		}
	}
	if (syNetPeerTickDiagLevel() >= 1)
	{
		u64 ums;

		ums = syNetPeerNowUnixMs();
		port_log(
		    "SSB64 NetSync: tick_diag tick=%u push=%d tm_up=%u tm_fr=%u scene=%u unix_ms=%llu tick_minus_hr=%d bar_rel=%d exec_rdy=%d\n",
		    tick, port_get_push_frame_count(), dSYTaskmanUpdateCount, dSYTaskmanFrameCount,
		    (unsigned int)(u32)gSCManagerSceneData.scene_curr, (unsigned long long)ums,
		    (int)((s32)tick - (s32)sSYNetPeerHighestRemoteTick), (sSYNetPeerBattleBarrierReleased != FALSE) ? 1 : 0,
		    (syNetPeerCheckBattleExecutionReady() != FALSE) ? 1 : 0);
	}
}
#endif /* #if defined(PORT) at ~6512 (frame commit + NetSync validation) */
#endif /* #ifdef PORT at ~6456 (gc / desync helpers) */

static u32 syNetPeerNetSyncLogInterval(void)
{
	static s32 sCachedInterval = -999;
	s32 parsed;

	if (sCachedInterval != -999)
	{
		return (u32)sCachedInterval;
	}
	parsed = SYNETPEER_LOG_INTERVAL;
	{
		const char *env;

		env = getenv("SSB64_NETPLAY_NETSYNC_LOG_INTERVAL");
		if ((env != NULL) && (env[0] != '\0'))
		{
			parsed = atoi(env);
		}
	}
	if (parsed <= 0)
	{
		parsed = SYNETPEER_LOG_INTERVAL;
	}
	sCachedInterval = parsed;
	return (u32)sCachedInterval;
}

void syNetPeerLogStats(void)
{
#ifdef PORT
	u32 tick = syNetInputGetTick();

	if ((tick == 0) || ((tick - sSYNetPeerLastLogTick) < syNetPeerNetSyncLogInterval()))
	{
		return;
	}
	sSYNetPeerLastLogTick = tick;

	port_log(
	    "SSB64 NetPeer: role=%s local=%d remote=%d barrier=%d execution_ready=%d tick=%u commit_gen=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u snd_next=%u rcv_hw=%u seq_gap=%u seq_dup=%u seq_ooo=%u peer_ack=%u inpchk=0x%08X delay=%u push=%d tm_up=%u tm_fr=%u scene=%u skew_pace_frames=%u\n",
	    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
	    sSYNetPeerBattleBarrierReleased, syNetPeerCheckBattleExecutionReady(), tick, sSYNetPeerGlobalCommitGen, sSYNetPeerPacketsSent,
	    sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped, sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick,
	    sSYNetPeerLateFrames, sSYNetPeerSendSeq, sSYNetPeerRecvSeqHighWater, sSYNetPeerSeqGaps,
	    sSYNetPeerSeqDuplicates, sSYNetPeerSeqOutOfOrder, sSYNetPeerLastPeerAckTick, sSYNetPeerInputChecksum,
	    sSYNetPeerInputDelay, port_get_push_frame_count(), dSYTaskmanUpdateCount, dSYTaskmanFrameCount,
	    (unsigned int)(u32)gSCManagerSceneData.scene_curr, syNetPeerGetSkewPacingHoldFrameCount());

	syNetPeerLogNetSyncValidation(tick);
#endif
}

void syNetPeerLogExecutionHold(void)
{
#ifdef PORT
	if ((sSYNetPeerExecutionHoldFrames == 1) ||
		((sSYNetPeerExecutionHoldFrames % SYNETPEER_BARRIER_LOG_INTERVAL) == 0))
	{
		port_log("SSB64 NetPeer: execution hold role=%s local=%d remote=%d tick=%u hold=%u barrier_wait=%u peer_ready=%d start_sent=%d start_recv=%d highest_remote=%u late=%u bind=%d execsync_req=%d execsync_done=%d host_sent=%d host_echo=%d client_got=%d client_echo=%d\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerExecutionHoldFrames, sSYNetPeerBattleBarrierWaitFrames,
		         sSYNetPeerBattlePeerReady, sSYNetPeerBattleStartSent,
		         sSYNetPeerBattleStartReceived, sSYNetPeerHighestRemoteTick,
		         sSYNetPeerLateFrames,
		         (syNetPeerInputBindIsComplete() != FALSE) ? 1 : 0,
		         (syNetPeerRequireBattleExecSync() != FALSE) ? 1 : 0,
		         (syNetPeerBattleExecSyncIsComplete() != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncHostSent != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncHostPeerEcho != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncClientGotHost != FALSE) ? 1 : 0,
		         (sSYNetPeerExecSyncClientEchoSent != FALSE) ? 1 : 0);
	}
#endif
}

void syNetPeerLogExecutionBegin(void)
{
#ifdef PORT
	if (sSYNetPeerExecutionBeginLogged == FALSE)
	{
		sSYNetPeerExecutionBeginLogged = TRUE;
		sSYNetPeerDelaySyncDiagExecReadyMark = syNetInputGetTick();
		port_log(
		    "SSB64 NetPeer: execution begin role=%s local=%d remote=%d tick=%u hold=%u barrier_wait=%u highest_remote=%u late=%u push=%d tm_up=%u tm_fr=%u scene=%u\n",
		    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
		    syNetInputGetTick(), sSYNetPeerExecutionHoldFrames, sSYNetPeerBattleBarrierWaitFrames,
		    sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames, port_get_push_frame_count(),
		    dSYTaskmanUpdateCount, dSYTaskmanFrameCount, (unsigned int)(u32)gSCManagerSceneData.scene_curr);
		if (syNetPeerTickDiagLevel() >= 1)
		{
			syNetPeerLogTickFrameSnapshot("exec_begin", TRUE);
		}
	}
#endif
}

void syNetPeerLogBarrierWait(void)
{
#ifdef PORT
	if ((sSYNetPeerBattleBarrierWaitFrames % SYNETPEER_BARRIER_LOG_INTERVAL) == 0)
	{
		port_log(
		    "SSB64 NetPeer: barrier wait role=%s local=%d remote=%d tick=%u local_ready=%d peer_ready=%d start_sent=%d start_recv=%d sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u unix_ms=%llu deadline_valid=%d deadline_ms=%llu deadline_vi_ph=%u gran_ms=%u vi_hz=%u vi_align=%d\n",
		    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		    sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		    sSYNetPeerBattleLocalReady, sSYNetPeerBattlePeerReady,
		    sSYNetPeerBattleStartSent, sSYNetPeerBattleStartReceived, sSYNetPeerPacketsSent,
		    sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		    sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames,
		    (unsigned long long)syNetPeerNowUnixMs(), (sSYNetPeerBarrierDeadlineValid != FALSE) ? 1 : 0,
		    (unsigned long long)((sSYNetPeerBarrierDeadlineValid != FALSE) ? sSYNetPeerBarrierDeadlineUnixMs : 0ULL),
		    (unsigned int)syNetPeerBarrierDeadlineViPhaseBucket(), (unsigned int)syNetPeerBarrierFrameGranularityMs(),
		    (unsigned int)sSYNetPeerBarrierViHz, (sSYNetPeerBarrierViAlign != FALSE) ? 1 : 0);
	}
#endif
}

/*--------------------------------------------------------------------
 * Barrier: hold VS execution until BATTLE_READY + scheduled wall-clock deadline.
 *-------------------------------------------------------------------*/
void syNetPeerReleaseBattleBarrier(const char *reason)
{
	if (sSYNetPeerBattleBarrierReleased == FALSE)
	{
		sSYNetPeerBattleBarrierReleased = TRUE;

#ifdef PORT
		if (sSYNetPeerBattleBarrierEnabled != FALSE)
		{
			/*
			 * VS battle scene only: do not zero dSYTaskman* or port push-frame here.
			 * Barrier release is wall-clock aligned and can land on different local frames
			 * per peer; snapping taskman / PortPushFrame counters at that instant desyncs
			 * execution diagnostics and pacing from the VS taskman epoch established in
			 * syTaskmanLoadScene (which already zeros dSYTaskman* and resets push_frame
			 * when scene_curr is VSBattle).
			 *
			 * Still re-latch decouple sim deadlines and seed tick-grid lock from barrier
			 * authority here — first post-go wall alignment without clobbering taskman.
			 */
			if ((u32)gSCManagerSceneData.scene_curr == (u32)nSCKindVSBattle)
			{
				port_reset_vs_decouple_pacing_for_net_barrier();
				syNetTickGridLockOnBarrierReleased((sSYNetPeerBootstrapIsHost != FALSE) ? TRUE : FALSE);
			}
#ifdef PORT
			syNetPhaseOnBattleBarrierReleased();
#endif
		}
		{
			u64 ums;
			u32 rel_gran;
			u32 rel_vi_phase;
			u32 scene_u;
			int taskman_resync_applied;
			u64 deadline_latched_ms;
			u32 deadline_vi_ph;

			ums = syNetPeerNowUnixMs();
			rel_gran = syNetPeerBarrierFrameGranularityMs();
			rel_vi_phase = (rel_gran > 0U) ? (u32)(ums / (u64)rel_gran) : 0U;
			scene_u = (u32)gSCManagerSceneData.scene_curr;
			taskman_resync_applied =
			    ((sSYNetPeerBattleBarrierEnabled != FALSE) && (scene_u == (u32)nSCKindVSBattle)) ? 1 : 0;
			deadline_latched_ms =
			    (sSYNetPeerBarrierDeadlineValid != FALSE) ? sSYNetPeerBarrierDeadlineUnixMs : 0ULL;
			deadline_vi_ph = syNetPeerBarrierDeadlineViPhaseBucket();
			port_log(
			    "SSB64 NetPeer: barrier release role=%s reason=%s local=%d remote=%d tick=%u wait=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u unix_ms=%llu gran_ms=%u vi_phase_bucket=%u contract_spread_ms=%lld skew_retries_latched=%u port_push_frame=%d taskman_frame=%u scene_curr=%u taskman_resync=%d deadline_latched_ms=%llu deadline_vi_ph=%u\n",
			    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", reason,
			    sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
			    sSYNetPeerBattleBarrierWaitFrames, sSYNetPeerPacketsSent,
			    sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
			    sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames,
			    (unsigned long long)ums, (unsigned int)rel_gran, (unsigned int)rel_vi_phase,
			    (long long)sSYNetPeerLastBarrierContractOffsetSpreadMs,
			    (unsigned int)sSYNetPeerBarrierSkewRetriesLatchedForLog, port_get_push_frame_count(),
			    (unsigned int)dSYTaskmanFrameCount, (unsigned int)scene_u, taskman_resync_applied,
			    (unsigned long long)deadline_latched_ms, (unsigned int)deadline_vi_ph);
			syNetPeerLogTickFrameSnapshot("barrier_release", FALSE);
		}
#endif
	}
}

#if defined(PORT)
static u32 syNetPeerBarrierEscapeMsLimit(void)
{
	static sb32 s_init = FALSE;
	static u32 s_ms = 600U;
	char *e;

	if (s_init == FALSE)
	{
		e = getenv("SSB64_NETPLAY_BARRIER_ESCAPE_MS");
		if ((e != NULL) && (e[0] != '\0'))
		{
			s_ms = (u32)atoi(e);
		}
		s_init = TRUE;
	}
	return s_ms;
}

static u32 syNetPeerBarrierRequeueMsLimit(void)
{
	static sb32 s_init = FALSE;
	static u32 s_ms = 8000U;
	char *e;

	if (s_init == FALSE)
	{
		e = getenv("SSB64_NETPLAY_BARRIER_REQUEUE_MS");
		if ((e != NULL) && (e[0] != '\0'))
		{
			s_ms = (u32)atoi(e);
		}
		s_init = TRUE;
	}
	return s_ms;
}

static void syNetPeerPollBarrierWallTimeouts(void)
{
	u64 now;
	u64 elapsed;
	u32 esc_limit;
	u32 rq_limit;

	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (sSYNetPeerBarrierWallClockStartMs == 0ULL)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() != FALSE)
	{
		return;
	}

	now = syNetPeerNowUnixMs();
	if (now < sSYNetPeerBarrierWallClockStartMs)
	{
		elapsed = 0ULL;
	}
	else
	{
		elapsed = now - sSYNetPeerBarrierWallClockStartMs;
	}

	esc_limit = syNetPeerBarrierEscapeMsLimit();
	if ((esc_limit > 0U) && (sSYNetPeerBarrierEscapeApplied == FALSE) &&
	    (sSYNetPeerBootstrapIsEnabled != FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE) &&
	    (sSYNetPeerBattleBarrierReleased == FALSE) && (elapsed >= (u64)esc_limit))
	{
		sSYNetPeerBarrierEscapeApplied = TRUE;
		port_log("SSB64 NetPeer: barrier escape after %llu ms (limit=%u) -> forced release\n",
		         (unsigned long long)elapsed, (unsigned int)esc_limit);
		syNetPeerReleaseBattleBarrier("wall-timeout-escape-ms");
	}

	rq_limit = syNetPeerBarrierRequeueMsLimit();
#if defined(SSB64_NETMENU)
	if ((rq_limit > 0U) && (sSYNetPeerBarrierRequeueApplied == FALSE) && (elapsed >= (u64)rq_limit))
	{
		if (gSCManagerSceneData.is_vs_automatch_battle != FALSE)
		{
			sSYNetPeerBarrierRequeueApplied = TRUE;
			port_log(
			    "SSB64 NetPeer: VS sync not ready after %llu ms (limit=%u) -> automatch re-queue\n",
			    (unsigned long long)elapsed, (unsigned int)rq_limit);
			mnVSNetAutomatchForceRequeueAfterBarrierTimeout();
		}
	}
#endif /* SSB64_NETMENU */
}
#endif /* defined(PORT) */

void syNetPeerUpdateStartBarrier(void)
{
#if defined(PORT)
	if ((sSYNetPeerBattleBarrierEnabled == FALSE) || (sSYNetPeerBattleBarrierReleased != FALSE))
	{
		return;
	}
	sSYNetPeerBattleBarrierWaitFrames++;
	syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_READY);

	if (sSYNetPeerBattlePeerReady == FALSE)
	{
		syNetPeerLogBarrierWait();
		return;
	}

	/*
	 * Timestamp path owns barrier completion; final contract is a BATTLE_START_TIME deadline.
	 * Both sides release only when local wall clock >= scheduled deadline.
	 */
	if (sSYNetPeerBattleBarrierEnabled != FALSE)
	{
		if (sSYNetPeerBootstrapIsHost != FALSE)
		{
			if (sSYNetPeerClockAlignEnabled == FALSE)
			{
				/* No sync samples configured: host still gates release through a timestamp deadline. */
				if (sSYNetPeerBattleStartTimeSent == FALSE)
				{
					sSYNetPeerBattleStartUnixMs = syNetPeerNowUnixMs();
					sSYNetPeerBattleStartOffsetMs = 0;
					sSYNetPeerBarrierDeadlineUnixMs = sSYNetPeerBattleStartUnixMs;
					sSYNetPeerBarrierDeadlineValid = TRUE;
					syNetPeerSendBattleStartTimePacket(sSYNetPeerBattleStartUnixMs, sSYNetPeerBattleStartOffsetMs);
					sSYNetPeerBattleStartTimeSent = TRUE;
					sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
				}
				if (syNetPeerCheckBarrierDeadlineReached() != FALSE)
				{
					sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
					syNetPeerReleaseBattleBarrier("clock-deadline-host-nosync");
				}
			}
			else if (sSYNetPeerClockSyncSampleCount < sSYNetPeerClockSyncTargetTotal)
			{
				char *ed;

				if (sSYNetPeerTimePingAwaitingAck != FALSE)
				{
					ed = getenv("SSB64_NETPLAY_BARRIER_CLOCK_STALL_DIAG");
					if ((ed != NULL) && (ed[0] != '\0') && (atoi(ed) != 0) && (sSYNetPeerBattleBarrierWaitFrames >= 300U) &&
					    ((sSYNetPeerBattleBarrierWaitFrames % 300U) == 0U))
					{
						port_log(
						    "SSB64 NetPeer: barrier_clock_await sample=%u/%u seq=%u wait_frames=%u (no TIME_PONG progress; "
						    "check peer ingress)\n",
						    (unsigned int)sSYNetPeerClockSyncSampleCount,
						    (unsigned int)sSYNetPeerClockSyncTargetTotal, (unsigned int)sSYNetPeerTimePingSeq,
						    (unsigned int)sSYNetPeerBattleBarrierWaitFrames);
					}
				}
				if (sSYNetPeerTimePingAwaitingAck == FALSE)
				{
					sSYNetPeerTimePingT0Sent = syNetPeerNowUnixMs();
					sSYNetPeerTimePingSeq = sSYNetPeerClockSyncSampleCount;
					syNetPeerSendTimePingPacket(sSYNetPeerTimePingSeq, sSYNetPeerTimePingT0Sent);
					sSYNetPeerTimePingAwaitingAck = TRUE;
				}
				else
				{
					syNetPeerSendTimePingPacket(sSYNetPeerTimePingSeq, sSYNetPeerTimePingT0Sent);
				}
			}
			else if (sSYNetPeerBattleStartTimeSent != FALSE)
			{
				if (syNetPeerCheckBarrierDeadlineReached() != FALSE)
				{
					sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
					syNetPeerReleaseBattleBarrier("clock-deadline-host");
				}
			}
		}
		else if (sSYNetPeerBattleStartTimeReceived != FALSE)
		{
			if (syNetPeerCheckBarrierDeadlineReached() != FALSE)
			{
				sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
				syNetPeerReleaseBattleBarrier("clock-deadline-client");
			}
		}
		syNetPeerLogBarrierWait();
		return;
	}

	syNetPeerLogBarrierWait();
#endif
}

/*
 * Per-frame ingest + delay-contract driver (runs before gameplay input send).
 * Startup barrier + strict INPUT_BIND + BATTLE_EXEC_SYNC transport are driven here (after ingress) so
 * `syNetPeerCheckBattleExecutionReady()` can clear without relying on unrelated code paths.
 */
void syNetPeerUpdateBattleGate(void)
{
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
#if defined(PORT)
	if (sSYNetPeerBootstrapRunInProgress != FALSE)
	{
		syNetPeerReceiveBootstrapPackets();
		return;
	}
#endif
	syNetPhaseTickWallClock();
	syNetPeerPumpIngressTransport("funcread");
#if defined(PORT)
	syNetPeerApplyPendingDelayContract();
	syNetPeerMaybeApplyStartupDelaySkewAlignment();
	syNetPeerPollBarrierWallTimeouts();
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	syNetPeerUpdateStartBarrier();
	syNetPeerInputBindServiceTransport();
	/*
	 * Optional tick-grid wall calibration (`SSB64_NETPLAY_TICK_GRID_CALIBRATE_MS`) while waiting on
	 * BATTLE_EXEC_SYNC — `syNetPhaseOnBattleBarrierReleased` skips when already RUNNING (no clock barrier).
	 */
	if ((sSYNetPeerOptionalWallCalFromExecHoldStarted == FALSE) && (syNetPeerRequireBattleExecSync() != FALSE) &&
	    (syNetPeerBattleExecSyncIsComplete() == FALSE))
	{
		if ((syNetPeerRequireInputBindStrict() == FALSE) || (syNetPeerInputBindIsComplete() != FALSE))
		{
			syNetPhaseBeginOptionalWallCalibrationFromRunning();
			sSYNetPeerOptionalWallCalFromExecHoldStarted = TRUE;
		}
	}
	/* Pump BATTLE_EXEC_SYNC propose/echo; ingress already ran so client can echo same frame after recv. */
	syNetPeerBattleExecSyncServiceTransport();
	syNetPeerSessionParamsServiceNegotiation();
	/*
	 * Staging / decouple paths pump UpdateBattleGate without syNetPeerUpdate — still emit warmup INPUT
	 * so bootstrap ingress symmetry (outbound sent + hr>0) can clear before VS battle FuncRead runs.
	 */
	syNetPeerMaybeSendBootstrapWarmupInput();
#endif

	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
#if defined(PORT)
		if ((sSYNetPeerExecutionHoldFrames & 63U) == 0U)
		{
			port_log(
			    "SSB64 NetPeer: battle_gate_wait role=%s sim=%u scene=%u bind=%d execsync_req=%d execsync_done=%d hr=%u recv=%u drop=%u ingress_sym=%d warmup_out=%d\n",
			    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", (unsigned int)syNetInputGetTick(),
			    (unsigned int)(u32)gSCManagerSceneData.scene_curr, (syNetPeerInputBindIsComplete() != FALSE) ? 1 : 0,
			    (syNetPeerRequireBattleExecSync() != FALSE) ? 1 : 0,
			    (syNetPeerBattleExecSyncIsComplete() != FALSE) ? 1 : 0, (unsigned int)sSYNetPeerHighestRemoteTick,
			    (unsigned int)sSYNetPeerPacketsReceived, (unsigned int)sSYNetPeerPacketsDropped,
			    (syNetPeerBootstrapIngressSymmetrySatisfied() != FALSE) ? 1 : 0,
			    (sSYNetPeerBootstrapIngressWarmupOutboundSent != FALSE) ? 1 : 0);
		}
		{
			static u32 sSYNetPeerExecGateHoldLastLogTick = ~(u32)0;
			u32 t;

			t = syNetInputGetTick();
			if (t != sSYNetPeerExecGateHoldLastLogTick)
			{
				sSYNetPeerExecGateHoldLastLogTick = t;
				port_log(
				    "SSB64 NetPeer: exec_gate_hold tick=%u both_latched=%d latched_agreed=%d monotonic_ok=%d\n",
				    (unsigned int)t,
				    (sSYNetPeerBothSidesLatchedStartup != FALSE) ? 1 : 0,
				    (sSYNetPeerLatchedStartupTick != ~(u32)0) ? (int)sSYNetPeerLatchedStartupTick : -1,
				    ((sSYNetPeerLatchedStartupTick == ~(u32)0) || (t >= sSYNetPeerLatchedStartupTick)) ? 1 : 0);
			}
		}
#endif
		sSYNetPeerExecutionHoldFrames++;
		syNetPeerLogExecutionHold();
	}
	else syNetPeerLogExecutionBegin();
}

#ifdef PORT
/*
 * Env `SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=N` (N>0): log fighter + map kinematics hashes every N sim ticks on the
 * live NetPeer path (after rollback). Diff two peers' logs to find the first divergent tick when `all=` matches but
 * `figh=` diverges (nondeterministic state) or to correlate with rollback lines.
 *
 * Env `SSB64_NETPLAY_SIM_TRACE_NEEDLE_MIN` (+ optional `SSB64_NETPLAY_SIM_TRACE_NEEDLE_MAX`): when set, for each sim
 * tick in [MIN, MAX] (MAX defaults to MIN), emit `desync_needle` pub vs remote ring checksums like NetSync validation.
 * Optional `SSB64_NETPLAY_SIM_TRACE_NEEDLE_LEVEL` (default 2): 1 = CRC lines only, 2 = per-slot frame detail.
 * Works without SIM_STATE_TICK_INTERVAL; combine with INTERVAL=1 to correlate hashes and needles on the same ticks.
 */
static void syNetPeerMaybeLogSimStateTickTrace(void)
{
	const char *e;
	const char *e_min;
	const char *e_max;
	const char *e_lvl;
	int interval;
	int needle_lvl;
	u32 tick;
	u32 needle_tick;
	u32 n_min;
	u32 n_max;
	u32 f;
	u32 m;

	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	tick = syNetInputGetTick();
	if (tick == 0U)
	{
		return;
	}

	e_min = getenv("SSB64_NETPLAY_SIM_TRACE_NEEDLE_MIN");
	if ((e_min != NULL) && (e_min[0] != '\0'))
	{
		n_min = (u32)atoi(e_min);
		e_max = getenv("SSB64_NETPLAY_SIM_TRACE_NEEDLE_MAX");
		if ((e_max != NULL) && (e_max[0] != '\0'))
		{
			n_max = (u32)atoi(e_max);
		}
		else
		{
			n_max = n_min;
		}
		if (n_max < n_min)
		{
			u32 tswap;

			tswap = n_min;
			n_min = n_max;
			n_max = tswap;
		}
		if ((tick >= n_min) && (tick <= n_max))
		{
			needle_lvl = 2;
			e_lvl = getenv("SSB64_NETPLAY_SIM_TRACE_NEEDLE_LEVEL");
			if ((e_lvl != NULL) && (e_lvl[0] != '\0'))
			{
				needle_lvl = atoi(e_lvl);
				if (needle_lvl < 1)
				{
					needle_lvl = 1;
				}
			}
			needle_tick = (tick >= 1U) ? (tick - 1U) : 0U;
			syNetInputLogDesyncNeedle(tick, needle_tick, needle_lvl);
		}
	}

	e = getenv("SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return;
	}
	interval = atoi(e);
	if (interval <= 0)
	{
		return;
	}
	if ((tick % (u32)interval) != 0U)
	{
		return;
	}
	f = syNetSyncHashBattleFighters();
	m = syNetSyncHashMapCollisionKinematics();
	{
		u32 world_h = syNetSyncHashRollbackWorld();
		u32 item_h = syNetSyncHashActiveItemsForRollback();
		u32 wpn_h = syNetSyncHashActiveWeaponsForRollback();
		u32 rng_h = syNetSyncHashRNGSeed();
		u32 cam_h = syNetSyncHashGMCamera();
		u32 anim_h = syNetSyncHashFighterAnimationStateForRollback();
		u32 gch_h = syNetSyncHashGcRunAllTraversalFingerprint();
		const char *ht_env;
		static sb32 sHashTransitionLogCache = -999;
		static u32 sHashTransitionPrevTick;
		static u32 sHashTransitionPrevFigh;
		static u32 sHashTransitionPrevWorld;
		static u32 sHashTransitionPrevAnim;
		static u32 sHashTransitionPrevItem;
		static u32 sHashTransitionPrevRng;
		static u32 sHashTransitionPrevGch;

		if (sHashTransitionLogCache == -999)
		{
			ht_env = getenv("SSB64_NETPLAY_HASH_TRANSITION_LOG");
			sHashTransitionLogCache = ((ht_env != NULL) && (ht_env[0] != '\0') && (atoi(ht_env) != 0)) ? 1 : 0;
			sHashTransitionPrevTick = ~(u32)0;
		}
		if (sHashTransitionLogCache != 0)
		{
			if ((sHashTransitionPrevTick != ~(u32)0) && (tick > sHashTransitionPrevTick))
			{
				if (f != sHashTransitionPrevFigh)
				{
					port_log(
					    "SSB64 NetSync: hash_transition tick=%u partition=figh old=0x%08X new=0x%08X\n",
					    tick,
					    sHashTransitionPrevFigh,
					    f);
				}
				if (world_h != sHashTransitionPrevWorld)
				{
					port_log(
					    "SSB64 NetSync: hash_transition tick=%u partition=world old=0x%08X new=0x%08X\n",
					    tick,
					    sHashTransitionPrevWorld,
					    world_h);
				}
				if (anim_h != sHashTransitionPrevAnim)
				{
					port_log(
					    "SSB64 NetSync: hash_transition tick=%u partition=anim old=0x%08X new=0x%08X\n",
					    tick,
					    sHashTransitionPrevAnim,
					    anim_h);
				}
				if (item_h != sHashTransitionPrevItem)
				{
					port_log(
					    "SSB64 NetSync: hash_transition tick=%u partition=item old=0x%08X new=0x%08X\n",
					    tick,
					    sHashTransitionPrevItem,
					    item_h);
				}
				if (rng_h != sHashTransitionPrevRng)
				{
					port_log(
					    "SSB64 NetSync: hash_transition tick=%u partition=rng old=0x%08X new=0x%08X\n",
					    tick,
					    sHashTransitionPrevRng,
					    rng_h);
				}
				if (gch_h != sHashTransitionPrevGch)
				{
					port_log(
					    "SSB64 NetSync: hash_transition tick=%u partition=gch old=0x%08X new=0x%08X\n",
					    tick,
					    sHashTransitionPrevGch,
					    gch_h);
				}
			}
			sHashTransitionPrevTick = tick;
			sHashTransitionPrevFigh = f;
			sHashTransitionPrevWorld = world_h;
			sHashTransitionPrevAnim = anim_h;
			sHashTransitionPrevItem = item_h;
			sHashTransitionPrevRng = rng_h;
			sHashTransitionPrevGch = gch_h;
		}
		port_log(
		    "SSB64 NetSync: sim_state_tick tick=%u figh=0x%08X mph=0x%08X world=0x%08X item=0x%08X wpn=0x%08X rng=0x%08X cam=0x%08X anim=0x%08X gch=0x%08X rb_applied=%u rb_load_fail=%u push=%d\n",
		    tick,
		    f,
		    m,
		    world_h,
		    item_h,
		    wpn_h,
		    rng_h,
		    cam_h,
		    anim_h,
		    gch_h,
		    (unsigned int)syNetRollbackGetAppliedResimCount(),
		    (unsigned int)syNetRollbackGetLoadFailCount(),
		    port_get_push_frame_count());
	}
}
#endif

void syNetPeerUpdate(void)
{
	sb32 allow_without_exec = FALSE;
#ifdef PORT
	if ((syNetPeerIsVSSessionActive() != FALSE) && (syNetInputAuthoritativeWireContractEnabled() != FALSE))
	{
		allow_without_exec = TRUE;
	}
	if ((syNetPeerIsVSSessionActive() != FALSE) && (syNetRollbackIsResimulating() != FALSE))
	{
		if (sSYNetPeerIsActive != FALSE)
		{
			/*
			 * Rollback coordination transport during resim: symmetric notify (INPUT padding or
			 * ROLLBACK_SYNC), baseline echo/recv. PumpIngressTransport is a no-op while resimming.
			 */
			syNetPeerReceiveRemoteInput();
			syNetPeerSendLocalInput();
			syNetRollbackPumpResimBaselineIfAwaiting();
			syNetPeerTrySendRollbackSyncNotice();
		}
		syNetRollbackUpdate();
		return;
	}
#endif
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	syNetPeerUpdateBattleGate();
#if defined(PORT)
	syNetPeerMaybeSendBootstrapWarmupInput();
#endif

	if ((allow_without_exec == FALSE) && (syNetPeerCheckBattleExecutionReady() == FALSE))
	{
		return;
	}
	/*
	 * Post-barrier retransmit window: host echoes BATTLE_START_TIME for UDP loss.
	 * Client ignores duplicate payloads (deadline stays latched in syNetPeerHandleBattleStartTimePacket).
	 *
	 * When clock_align && barrier_on && !host, no packet is sent but the counter still counts down —
	 * harmless no-op decrement from the client's first receive path having armed repeat frames.
	 */
	if (sSYNetPeerBattleStartRepeatFrames != 0)
	{
#if defined(PORT)
		if ((sSYNetPeerClockAlignEnabled != FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE) &&
		    (sSYNetPeerBootstrapIsHost != FALSE))
		{
			syNetPeerSendBattleStartTimePacket(sSYNetPeerBattleStartUnixMs, sSYNetPeerBattleStartOffsetMs);
		}
		else if ((sSYNetPeerClockAlignEnabled == FALSE) || (sSYNetPeerBattleBarrierEnabled == FALSE))
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_START);
		}
#else
		syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_START);
#endif
		sSYNetPeerBattleStartRepeatFrames--;
	}
	syNetPeerSendLocalInput();
#if defined(PORT)
	{
		u32 tick_now = syNetInputGetTick();

		syNetPeerMaybeLogRunwayFrontierSample(tick_now);
		syNetPeerMaybeAutoRunwayDelayBump(tick_now);
		syNetPeerRunAdaptiveInputDelaySimStep(tick_now);
	}
#endif
	syNetPeerLogStats();
	syNetRollbackUpdate();
#if defined(PORT)
	syNetPeerMaybeLogSimStateTickTrace();
#endif
}

void syNetPeerStopVSSession(void)
{
#ifdef PORT
	syNetPhaseReset();
	syNetRollbackStopVSSession();
#endif
#if defined(PORT)
	if (sSYNetPeerIsActive != FALSE)
	{
		syNetDesyncClassifierEmitFrameCommitReportOnVsStop();
		syNetDesyncClassifierEmitReportOnVsStop();
		port_log("SSB64 NetPeer: VS session stop sent=%u recv=%u dropped=%u staged=%u late=%u checksum=0x%08X\n",
		         sSYNetPeerPacketsSent, sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		         sSYNetPeerFramesStaged, sSYNetPeerLateFrames, sSYNetPeerInputChecksum);
		syNetInputLogAdmissionStatsSummary("vs_stop", TRUE);
	}
	syNetPeerInputBindReset();
	syNetPeerBattleExecSyncReset();
	syNetPeerResetBootstrapIngressSymmetryState();
	syNetPeerResetDelaySyncPending();
	syNetPeerCloseSocket();
#if defined(SSB64_NETMENU)
	syNetPeerResetAutomatchBootstrapAttemptState();
#endif
	sSYNetPeerBarrierWallClockStartMs = 0ULL;
	sSYNetPeerBarrierEscapeApplied = FALSE;
	sSYNetPeerBarrierRequeueApplied = FALSE;
#endif
#ifdef PORT
	syNetPeerResetSkewPacingSessionStats();
	syNetPeerResetDesyncTraceSession();
#endif
	sSYNetPeerIsActive = FALSE;
}

sb32 syNetPeerIsVSSessionActive(void)
{
	return sSYNetPeerIsActive;
}

u32 syNetPeerGetVsContractViHz(void)
{
#ifdef PORT
	if (sSYNetPeerIsActive == FALSE)
	{
		return 0U;
	}
	return sSYNetPeerBarrierViHz;
#else
	return 0U;
#endif
}

sb32 syNetPeerIsOnlineP2PHardwareDecoupleActive(void)
{
	return (sSYNetPeerIsEnabled != FALSE) && (sSYNetPeerIsConfigured != FALSE) && (sSYNetPeerIsActive != FALSE);
}

s32 syNetPeerResolveLocalHardwareDevice(s32 sim_player)
{
#ifdef PORT
	if ((syNetPeerIsVSSessionActive() == FALSE) || (sim_player != sSYNetPeerLocalPlayer))
	{
		return sim_player;
	}
	return syNetPeerGetPrimaryLocalHardwareDeviceIndex();
#else
	return sim_player;
#endif
}

s32 syNetPeerGetLocalSimSlot(void)
{
#ifdef PORT
	return sSYNetPeerLocalPlayer;
#else
	return 0;
#endif
}

s32 syNetPeerGetRemotePlayerSlot(void)
{
	return sSYNetPeerRemotePlayer;
}

s32 syNetPeerGetExtraLocalSenderSimSlot(void)
{
	return sSYNetPeerExtraLocalSenderSlot;
}

u32 syNetPeerGetHighestRemoteTick(void)
{
	return sSYNetPeerHighestRemoteTick;
}

u32 syNetPeerGetCommittedInputDelay(void)
{
	return sSYNetPeerInputDelay;
}

u32 syNetPeerGetInputDelay(void)
{
	return syNetPeerGetCommittedInputDelay();
}

static u32 syNetPeerSaturatingAddU32(u32 a, u32 b)
{
	if (a > ~(u32)0U - b)
	{
		return ~(u32)0U;
	}
	return a + b;
}

u32 syNetPeerGetBaseRequiredWireTick(u32 sim_tick)
{
	return syNetPeerSaturatingAddU32(sim_tick, syNetPeerGetInputDelay());
}

u32 syNetPeerGetStrictRequiredWireTick(u32 sim_tick)
{
#ifdef PORT
	u32 base;
	u32 slack;

	base = syNetPeerGetBaseRequiredWireTick(sim_tick);
	slack = (u32)syNetInputGetStrictExtraSlack();
	return syNetPeerSaturatingAddU32(base, slack);
#else
	return syNetPeerGetBaseRequiredWireTick(sim_tick);
#endif
}

#ifdef PORT
static u32 syNetPeerStartupAdmissionGraceTicks(void)
{
	u32 startup_grace_ticks;

	startup_grace_ticks = syNetPeerSaturatingAddU32(syNetPeerGetBaseRequiredWireTick(0U), 2U);
	if (startup_grace_ticks < 8U)
	{
		startup_grace_ticks = 8U;
	}
	return startup_grace_ticks;
}

static u32 syNetPeerEffectiveWireFrontierFromHr(u32 sim_tick, u32 hr)
{
	u32 base_required_wire;
	u32 strict_target_wire;
	u32 required_wire;

	base_required_wire = syNetPeerGetBaseRequiredWireTick(sim_tick);
	strict_target_wire = syNetPeerGetStrictRequiredWireTick(sim_tick);
	required_wire = (base_required_wire >= hr) ? base_required_wire : hr;
	if (required_wire > strict_target_wire)
	{
		required_wire = strict_target_wire;
	}
	return required_wire;
}

static u32 syNetPeerAdmissionAdjustedSimTick(u32 sim_tick)
{
	s32 bias;

	bias = sSYNetPeerAdmissionWireBiasTicks;
	if (bias <= 0)
	{
		return sim_tick;
	}
	if ((u32)bias >= sim_tick)
	{
		return 0U;
	}
	return sim_tick - (u32)bias;
}

static void syNetPeerUpdateAdmissionWireBias(void)
{
	u32 sim_tick;
	u32 hr;
	s32 target_bias;
	const u32 adjust_interval_ticks = 2U;
	const s32 max_bias = 24;
	u32 startup_grace_ticks;
	s32 want_obs;
	s32 want_clock;
	u32 agreed_tick;
	u32 agreed_phase;
	u32 now_phase;
	u32 clock_target_tick;
	u32 remote_frontier_sim;
	sb32 have_clock;
	sb32 have_obs;

	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	sim_tick = syNetInputGetTick();
	startup_grace_ticks = syNetPeerStartupAdmissionGraceTicks();
	if (sim_tick < syNetPeerSaturatingAddU32(startup_grace_ticks, 8U))
	{
		return;
	}
	/*
	 * Phase-lock local sim tick -> remote wire indexing:
	 * 1) Exec-sync agreed sim tick + live VI phase bucket (post-barrier sim grid; one-shot wall alignment at barrier),
	 * 2) Observed remote sim frontier from highest remote wire tick (hr).
	 *
	 * Target bias maps local sim tick -> remote sim index: remote_index_sim = local_sim - bias.
	 */
	hr = sSYNetPeerHighestRemoteTick;
	have_obs = (hr != 0U) ? TRUE : FALSE;
	have_clock = FALSE;
	clock_target_tick = 0U;
	agreed_tick =
	    (sSYNetPeerBootstrapIsHost != FALSE) ? sSYNetPeerExecSyncHostProposedTick : sSYNetPeerExecSyncAgreedTick;
	agreed_phase = (sSYNetPeerBootstrapIsHost != FALSE) ? sSYNetPeerExecSyncHostViPhase : sSYNetPeerExecSyncPeerViPhaseLatch;
	if ((sSYNetPeerBattleBarrierReleased != FALSE) && (syNetPeerBattleExecSyncIsComplete() != FALSE) && (agreed_tick != ~(u32)0))
	{
		now_phase = syNetPeerCurrentViPhaseBucketNow();
		clock_target_tick = syNetPeerSaturatingAddU32(agreed_tick, now_phase - agreed_phase);
		have_clock = TRUE;
	}
	if ((have_clock == FALSE) && (have_obs == FALSE))
	{
		return;
	}
	want_clock = (have_clock != FALSE) ? ((s32)sim_tick - (s32)clock_target_tick) : 0;
	remote_frontier_sim = (have_obs != FALSE) ? syNetPeerDelaySimTickFromWire(hr) : 0U;
	want_obs = (have_obs != FALSE) ? ((s32)sim_tick - (s32)remote_frontier_sim) : 0;
	if ((have_clock != FALSE) && (have_obs != FALSE))
	{
		/* Fuse predictive clock phase and observed remote frontier; bias toward observation when available. */
		target_bias = (want_obs * 3 + want_clock) / 4;
	}
	else if (have_obs != FALSE)
	{
		target_bias = want_obs;
	}
	else
	{
		target_bias = want_clock;
	}
	if (target_bias < 0)
	{
		target_bias = 0;
	}
	if (target_bias > max_bias)
	{
		target_bias = max_bias;
	}
	if ((sim_tick - sSYNetPeerAdmissionBiasLastAdjustTick) < adjust_interval_ticks)
	{
		return;
	}
	if (target_bias > sSYNetPeerAdmissionWireBiasTicks)
	{
		sSYNetPeerAdmissionWireBiasTicks++;
		if (sSYNetPeerAdmissionWireBiasTicks > max_bias)
		{
			sSYNetPeerAdmissionWireBiasTicks = max_bias;
		}
		sSYNetPeerAdmissionBiasLastAdjustTick = sim_tick;
		port_log(
		    "SSB64 NetPeer: admission_wire_bias up=%d target=%d sim=%u hr=%u remote_frontier=%u clock_target=%u want_obs=%d want_clock=%d\n",
		    sSYNetPeerAdmissionWireBiasTicks, target_bias, (unsigned int)sim_tick, (unsigned int)hr,
		    (unsigned int)remote_frontier_sim, (unsigned int)clock_target_tick, (int)want_obs, (int)want_clock);
	}
	else if (target_bias < sSYNetPeerAdmissionWireBiasTicks)
	{
		sSYNetPeerAdmissionWireBiasTicks--;
		if (sSYNetPeerAdmissionWireBiasTicks < 0)
		{
			sSYNetPeerAdmissionWireBiasTicks = 0;
		}
		sSYNetPeerAdmissionBiasLastAdjustTick = sim_tick;
		port_log(
		    "SSB64 NetPeer: admission_wire_bias down=%d target=%d sim=%u hr=%u remote_frontier=%u clock_target=%u want_obs=%d want_clock=%d\n",
		    sSYNetPeerAdmissionWireBiasTicks, target_bias, (unsigned int)sim_tick, (unsigned int)hr,
		    (unsigned int)remote_frontier_sim, (unsigned int)clock_target_tick, (int)want_obs, (int)want_clock);
	}
}

static u32 syNetPeerAdmissionAdjustedSimTickForWireLookup(u32 sim_tick)
{
	syNetPeerUpdateAdmissionWireBias();
	return syNetPeerAdmissionAdjustedSimTick(sim_tick);
}

u32 syNetPeerGetEffectiveWireFrontierForAdmission(u32 sim_tick)
{
	return syNetPeerGetBaseRequiredWireTick(sim_tick);
}

static u32 sSYNetPeerMatchBufferMinSlackTicksB;
static sb32 sSYNetPeerMatchBufferMinSlackEnvLoaded;

static void syNetPeerResetMatchBufferMinSlackEnv(void)
{
	sSYNetPeerMatchBufferMinSlackEnvLoaded = FALSE;
}

static u32 syNetPeerMatchBufferMinSlackGetB(void)
{
	const char *e;
	int v;

	if (sSYNetPeerMatchBufferMinSlackEnvLoaded != FALSE)
	{
		return sSYNetPeerMatchBufferMinSlackTicksB;
	}
	sSYNetPeerMatchBufferMinSlackTicksB = 0U;
	e = getenv("SSB64_NETPLAY_MATCH_INPUT_BUFFER_MIN_SLACK_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v < 0)
		{
			v = 0;
		}
		if (v > 32)
		{
			v = 32;
		}
		sSYNetPeerMatchBufferMinSlackTicksB = (u32)v;
	}
	sSYNetPeerMatchBufferMinSlackEnvLoaded = TRUE;
	return sSYNetPeerMatchBufferMinSlackTicksB;
}

u32 syNetPeerGetMatchInputBufferMinSlackTicks(void)
{
	return syNetPeerMatchBufferMinSlackGetB();
}

void syNetPeerResetStrictRingFuzzEnvCacheForNewMatch(void)
{
	sSYNetPeerStrictRingFuzzTicksEnvCache = -999;
	sSYNetPeerSessionStrictRingFuzzOverrideValid = FALSE;
	sSYNetPeerSessionStrictRingFuzzOverride = 0;
}

static u32 syNetPeerStrictRingFuzzTicksForAdmission(void)
{
	int v;
	const char *e;

	if (sSYNetPeerSessionStrictRingFuzzOverrideValid != FALSE)
	{
		return (u32)sSYNetPeerSessionStrictRingFuzzOverride;
	}
	if (sSYNetPeerStrictRingFuzzTicksEnvCache >= 0)
	{
		return (u32)sSYNetPeerStrictRingFuzzTicksEnvCache;
	}
	v = 0;
	e = getenv("SSB64_NETPLAY_STRICT_RING_FUZZ_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
	}
	if (v < 0)
	{
		v = 0;
	}
	if (v > 2)
	{
		v = 2;
	}
	sSYNetPeerStrictRingFuzzTicksEnvCache = v;
	return (u32)v;
}
#endif /* PORT */

sb32 syNetPeerHasBothSidesLatchedStartup(void)
{
#ifdef PORT
	if (sSYNetPeerBothSidesLatchedStartup == FALSE)
	{
		return FALSE;
	}
	if ((sSYNetPeerLatchedStartupTick != ~(u32)0U) && (syNetInputGetTick() < sSYNetPeerLatchedStartupTick))
	{
		return FALSE;
	}
#endif
	return TRUE;
}

sb32 syNetPeerIsRemoteInputReadyForSimTickEx(u32 sim_tick, sb32 full_aux_checks)
{
#ifdef PORT
	u32 sim_tick_for_wire;
	u32 required_wire;
	u32 hr;
	u32 lead_b;
	u32 lead_need;
	u32 startup_grace_ticks;
	s32 n;
	s32 i;
	s32 slot;
	u32 slack;
	u32 b_apply;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	/*
	 * Startup grace: couple to base delay (not strict slack) so high delay sessions have enough runway
	 * without letting strict-slack/match-delay inflate pre-enforcement indefinitely.
	 */
	startup_grace_ticks = syNetPeerStartupAdmissionGraceTicks();
	if (sim_tick < startup_grace_ticks)
	{
		return TRUE;
	}
	sim_tick_for_wire = syNetPeerAdmissionAdjustedSimTickForWireLookup(sim_tick);
	/* Enforce base delay contract; apply strict slack only up to observed remote frontier. */
	hr = syNetPeerGetHighestRemoteTick();
	required_wire = syNetPeerEffectiveWireFrontierFromHr(sim_tick_for_wire, hr);
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputHasRemoteInputForWireTick(slot, required_wire) != FALSE)
		{
			continue;
		}
		/*
		 * Optional fuzzy ring probe: when the effective frontier is **ahead** of observed `hr` (base `sim+D` pinned),
		 * allow confirming one of `required_wire - f` for `1 <= f <= fuzz` iff `hr + f >= required_wire`.
		 * When `required_wire <= hr`, do not relax — avoids admitting on stale wire rows while waiting for the true frontier cell.
		 */
		{
			u32 fuzz;
			u32 f;
			sb32 admitted;

			admitted = FALSE;
			fuzz = syNetPeerStrictRingFuzzTicksForAdmission();
			if ((fuzz > 0U) && (required_wire > hr))
			{
				for (f = 1U; ((f <= fuzz) && (required_wire >= f)); f++)
				{
					if (((u64)hr + (u64)f) < (u64)required_wire)
					{
						continue;
					}
					if (syNetInputHasRemoteInputForWireTick(slot, required_wire - f) != FALSE)
					{
						admitted = TRUE;
						break;
					}
				}
			}
			if (admitted == FALSE)
			{
				return FALSE;
			}
		}
	}
	if (full_aux_checks == FALSE)
	{
		return TRUE;
	}
	{
		u32 b_extra;
		u32 base_required_wire;
		sb32 b_match;
		sb32 b_buffer_ok;

		b_match = (syNetInputEnvGetMatchInputDelayOrNeg1() >= 0) ? TRUE : FALSE;
		b_extra = (b_match != FALSE) ? syNetPeerMatchBufferMinSlackGetB() : 0U;
		base_required_wire = syNetPeerGetBaseRequiredWireTick(sim_tick_for_wire);
		lead_b = syNetInputGetStrictRemoteLeadBufferTicks();
		if (lead_b == 0U)
		{
			if (b_extra == 0U)
			{
				return TRUE;
			}
			/*
			 * Match buffer slack B (peer-excess semantics): only when **ahead** on the wire
			 * (`hr > sim+D`) require `(hr - (sim+D)) >= B`. When `hr <= wire_base` (lockstep
			 * tie or behind), do not require impossible `hr >= wire_base + B`.
			 */
			if (hr <= base_required_wire)
			{
				return TRUE;
			}
			return ((hr - base_required_wire) >= b_extra) ? TRUE : FALSE;
		}
		slack = (hr > required_wire) ? (hr - required_wire) : 0U;
		b_apply = (slack < lead_b) ? slack : lead_b;
		lead_need = syNetPeerSaturatingAddU32(required_wire, b_apply);
		if (b_extra == 0U)
		{
			return (hr >= lead_need) ? TRUE : FALSE;
		}
		b_buffer_ok =
		    (hr <= base_required_wire) ? TRUE : (((hr - base_required_wire) >= b_extra) ? TRUE : FALSE);
		return ((hr >= lead_need) && (b_buffer_ok != FALSE)) ? TRUE : FALSE;
	}
#else
	(void)sim_tick;
	(void)full_aux_checks;
	return TRUE;
#endif
}

sb32 syNetPeerIsRemoteInputReadyForSimTick(u32 sim_tick)
{
	return syNetPeerIsRemoteInputReadyForSimTickEx(sim_tick, TRUE);
}

#ifdef PORT
#define SYNETPEER_STARVATION_ENTER_DEFAULT 4U
#define SYNETPEER_STARVATION_EXIT_DEFAULT 2U
#define SYNETPEER_STARVATION_HYST_DEFAULT 0U

static sb32 sSYNetPeerStarvationLatched;
static u32 sSYNetPeerStarvationEnterRun;
static u32 sSYNetPeerStarvationExitRun;
static sb32 sSYNetPeerStarvationHandlerOn;
static u32 sSYNetPeerStarvationEnterNeed;
static u32 sSYNetPeerStarvationExitNeed;
static u32 sSYNetPeerStarvationHysteresisWire;
static u32 sSYNetPeerStarvationExitHrLeadTicks;
static sb32 sSYNetPeerStarvationEnvLoaded;
static u32 sSYNetPeerStarvationLastLogTick;
static sb32 sSYNetPeerStarvationAdaptBumpOn;
static u32 sSYNetPeerStarvationAdaptBumpStep;
static u32 sSYNetPeerStarvationAdaptCooldownTicks;
static u32 sSYNetPeerStarvationAdaptLastBumpTick;

static void syNetPeerStarvationLoadEnvOnce(void)
{
	const char *e;
	int v;

	if (sSYNetPeerStarvationEnvLoaded != FALSE)
	{
		return;
	}
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_STARVATION_HANDLER");
	sSYNetPeerStarvationHandlerOn = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_STARVATION_ENTER_FRAMES");
	v = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : (int)SYNETPEER_STARVATION_ENTER_DEFAULT;
	if (v < 1)
	{
		v = 1;
	}
	if (v > 60)
	{
		v = 60;
	}
	sSYNetPeerStarvationEnterNeed = (u32)v;
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_STARVATION_EXIT_FRAMES");
	v = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : (int)SYNETPEER_STARVATION_EXIT_DEFAULT;
	if (v < 1)
	{
		v = 1;
	}
	if (v > 60)
	{
		v = 60;
	}
	sSYNetPeerStarvationExitNeed = (u32)v;
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_STARVATION_HYSTERESIS_TICKS");
	v = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : (int)SYNETPEER_STARVATION_HYST_DEFAULT;
	if (v < 0)
	{
		v = 0;
	}
	if (v > 16)
	{
		v = 16;
	}
	sSYNetPeerStarvationHysteresisWire = (u32)v;
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_STARVATION_EXIT_HR_LEAD_TICKS");
	v = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (v < 0)
	{
		v = 0;
	}
	if (v > 32)
	{
		v = 32;
	}
	sSYNetPeerStarvationExitHrLeadTicks = (u32)v;
	e = getenv("SSB64_NETPLAY_STARVATION_ADAPTIVE_DELAY_BUMP");
	sSYNetPeerStarvationAdaptBumpOn = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
	sSYNetPeerStarvationAdaptBumpStep = 1U;
	e = getenv("SSB64_NETPLAY_STARVATION_ADAPTIVE_DELAY_STEP");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v < 1)
		{
			v = 1;
		}
		if (v > 4)
		{
			v = 4;
		}
		sSYNetPeerStarvationAdaptBumpStep = (u32)v;
	}
	sSYNetPeerStarvationAdaptCooldownTicks = 120U;
	e = getenv("SSB64_NETPLAY_STARVATION_ADAPTIVE_DELAY_COOLDOWN_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v < 0)
		{
			v = 0;
		}
		if (v > 600)
		{
			v = 600;
		}
		sSYNetPeerStarvationAdaptCooldownTicks = (u32)v;
	}
	sSYNetPeerStarvationEnvLoaded = TRUE;
}

static void syNetPeerMaybeQueueHostDelayBumpForStarvationLatch(u32 sim_tick)
{
	u32 proposed;
	u32 cd;

	if (sSYNetPeerStarvationAdaptBumpOn == FALSE)
	{
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
	if (syNetInputEnvGetMatchInputDelayOrNeg1() < 0)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	if ((syNetSessionParamsAreNegotiated() != FALSE) && (syNetSessionParamsRollbackEnabled() != FALSE))
	{
		return;
	}
	if (sSYNetPeerHostDelayRampPendingValid != FALSE)
	{
		return;
	}
	if (sSYNetPeerDelaySyncPendingValid != FALSE)
	{
		return;
	}
	if (sSYNetPeerInputDelay >= sSYNetPeerInputDelayCeil)
	{
		return;
	}
	cd = sSYNetPeerStarvationAdaptCooldownTicks;
	if (cd > 0U)
	{
		if ((sSYNetPeerStarvationAdaptLastBumpTick != ~(u32)0U) &&
		    ((sim_tick - sSYNetPeerStarvationAdaptLastBumpTick) < cd))
		{
			return;
		}
	}
	proposed = syNetPeerClampInputDelayToContract(sSYNetPeerInputDelay + sSYNetPeerStarvationAdaptBumpStep);
	if (proposed <= sSYNetPeerInputDelay)
	{
		return;
	}
	sSYNetPeerHostDelayRampTarget = proposed;
	sSYNetPeerHostDelayRampEffectiveTick = syNetPeerSaturatingAddU32(sim_tick, syNetPeerDelaySyncCommitLeadTicks());
	sSYNetPeerHostDelayRampPendingValid = TRUE;
	sSYNetPeerStarvationAdaptLastBumpTick = sim_tick;
	port_log(
	    "SSB64 NetPeer: starvation_adaptive_delay queued -> %u eff_tick=%u (step=%u cooldown=%u sim=%u)\n",
	    (unsigned int)proposed, (unsigned int)sSYNetPeerHostDelayRampEffectiveTick,
	    (unsigned int)sSYNetPeerStarvationAdaptBumpStep, (unsigned int)cd, (unsigned int)sim_tick);
}

static void syNetPeerResetMatchDelayStarvationSession(void)
{
	sSYNetPeerStarvationLatched = FALSE;
	sSYNetPeerStarvationEnterRun = 0U;
	sSYNetPeerStarvationExitRun = 0U;
	sSYNetPeerStarvationEnvLoaded = FALSE;
	sSYNetPeerStarvationLastLogTick = ~(u32)0U;
	sSYNetPeerStarvationAdaptLastBumpTick = ~(u32)0U;
}

sb32 syNetPeerMatchDelayStarvationUpdateAndShouldHold(u32 sim_tick, u32 required_wire, u32 hr)
{
	u32 exit_bar;
	sb32 underrun;
	sb32 exit_ok;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return FALSE;
	}
	syNetPeerStarvationLoadEnvOnce();
	if (sSYNetPeerStarvationHandlerOn == FALSE)
	{
		if (sSYNetPeerStarvationLatched != FALSE)
		{
			syNetPeerResetMatchDelayStarvationSession();
		}
		return FALSE;
	}
	if (syNetInputEnvGetMatchInputDelayOrNeg1() < 0)
	{
		sSYNetPeerStarvationLatched = FALSE;
		sSYNetPeerStarvationEnterRun = 0U;
		sSYNetPeerStarvationExitRun = 0U;
		return FALSE;
	}
	if (sim_tick < syNetPeerStartupAdmissionGraceTicks())
	{
		sSYNetPeerStarvationEnterRun = 0U;
		sSYNetPeerStarvationExitRun = 0U;
		if (sSYNetPeerStarvationLatched != FALSE)
		{
			sSYNetPeerStarvationLatched = FALSE;
		}
		return FALSE;
	}
	underrun = (hr < required_wire) ? TRUE : FALSE;
	{
		u32 req_floor;

		req_floor = syNetPeerSaturatingAddU32(required_wire, sSYNetPeerStarvationHysteresisWire);
		if (sSYNetPeerStarvationExitHrLeadTicks > 0U)
		{
			u32 base_w;

			/*
			 * Extra lead on `required_wire + hyst` was unsatisfiable when `required_wire` tracks `hr`
			 * (`hr >= hr + lead`). Anchor lead to **wire_base** (`sim + D`) instead; still require
			 * `hr >= req_floor` so we do not clear while `hr` is below the effective admission row + hyst.
			 */
			base_w = syNetPeerGetBaseRequiredWireTick(sim_tick);
			exit_bar = syNetPeerSaturatingAddU32(base_w, sSYNetPeerStarvationHysteresisWire);
			exit_bar = syNetPeerSaturatingAddU32(exit_bar, sSYNetPeerStarvationExitHrLeadTicks);
			exit_ok = ((hr >= exit_bar) && (hr >= req_floor)) ? TRUE : FALSE;
		}
		else
		{
			exit_bar = req_floor;
			exit_ok = (hr >= exit_bar) ? TRUE : FALSE;
		}
	}
	if (sSYNetPeerStarvationLatched != FALSE)
	{
		if (exit_ok != FALSE)
		{
			sSYNetPeerStarvationEnterRun = 0U;
			sSYNetPeerStarvationExitRun++;
			if (sSYNetPeerStarvationExitRun >= sSYNetPeerStarvationExitNeed)
			{
				sSYNetPeerStarvationLatched = FALSE;
				sSYNetPeerStarvationExitRun = 0U;
				if ((sSYNetPeerStarvationLastLogTick == ~(u32)0U) ||
				    (sim_tick - sSYNetPeerStarvationLastLogTick) >= 120U)
				{
					port_log("SSB64 NetPeer: delay_sync_starvation cleared tick=%u hr=%u req=%u\n",
					         (unsigned int)sim_tick, (unsigned int)hr, (unsigned int)required_wire);
					sSYNetPeerStarvationLastLogTick = sim_tick;
				}
				return FALSE;
			}
			return TRUE;
		}
		sSYNetPeerStarvationExitRun = 0U;
		return TRUE;
	}
	if (underrun != FALSE)
	{
		sSYNetPeerStarvationEnterRun++;
		sSYNetPeerStarvationExitRun = 0U;
		if (sSYNetPeerStarvationEnterRun >= sSYNetPeerStarvationEnterNeed)
		{
			sSYNetPeerStarvationLatched = TRUE;
			sSYNetPeerStarvationEnterRun = 0U;
			syNetPeerMaybeQueueHostDelayBumpForStarvationLatch(sim_tick);
			if ((sSYNetPeerStarvationLastLogTick == ~(u32)0U) ||
			    (sim_tick - sSYNetPeerStarvationLastLogTick) >= 120U)
			{
				port_log("SSB64 NetPeer: delay_sync_starvation latched tick=%u hr=%u req=%u enter_need=%u\n",
				         (unsigned int)sim_tick, (unsigned int)hr, (unsigned int)required_wire,
				         (unsigned int)sSYNetPeerStarvationEnterNeed);
				sSYNetPeerStarvationLastLogTick = sim_tick;
			}
			return TRUE;
		}
		return FALSE;
	}
	sSYNetPeerStarvationEnterRun = 0U;
	sSYNetPeerStarvationExitRun = 0U;
	return FALSE;
}
#endif /* PORT */

u32 syNetPeerDelayWireTickFromSim(u32 sim_tick)
{
	return syNetPeerGetBaseRequiredWireTick(sim_tick);
}

u32 syNetPeerDelayWireLookupTickFromSim(u32 sim_tick)
{
	return syNetPeerDelayWireTickFromSim(sim_tick);
}

u32 syNetPeerDelaySimTickFromWire(u32 wire_tick)
{
	u32 d;

	d = syNetPeerGetCommittedInputDelay();
	/* wire_tick = sim_tick + D => sim = wire - D. When D==0, sim index equals wire index (do not fold to 0). */
	if (d == 0U)
	{
		return wire_tick;
	}
	if (wire_tick < d)
	{
		return 0U;
	}
	return wire_tick - d;
}

#ifdef PORT
static u32 sSYNetPeerSkewPacingHoldFrameCount;
static u32 sSYNetPeerSkewPacingLastLogTick = ~(u32)0;
static u32 sSYNetPeerSkewPacingLeadMaxTicks = SYNETPEER_SKEW_PACING_LEAD_MAX_TICKS_DEFAULT;
static u32 sSYNetPeerSkewBehindMaxTicks;
static u32 sSYNetPeerCatchUpBehindLastLogTick = ~(u32)0;

static void syNetPeerRefreshSkewPacingLeadMaxFromEnv(void)
{
	const char *e;
	int v;

	e = getenv("SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS");
	if ((e == NULL) || (e[0] == '\0'))
	{
		sSYNetPeerSkewPacingLeadMaxTicks = SYNETPEER_SKEW_PACING_LEAD_MAX_TICKS_DEFAULT;
		return;
	}
	v = atoi(e);
	if (v < 0)
	{
		v = 0;
	}
	if (v > 10000)
	{
		v = 10000;
	}
	sSYNetPeerSkewPacingLeadMaxTicks = (u32)v;
}

void syNetPeerApplyAutoNegotiatedSkewLeadMax(u32 lead_max_ticks)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return;
	}
	if (lead_max_ticks > 10000U)
	{
		lead_max_ticks = 10000U;
	}
	sSYNetPeerSkewPacingLeadMaxTicks = lead_max_ticks;
}

static void syNetPeerRefreshSkewBehindMaxFromEnv(void)
{
	const char *e;
	int v;

	e = getenv("SSB64_NETPLAY_SKEW_BEHIND_MAX_TICKS");
	if ((e == NULL) || (e[0] == '\0'))
	{
		sSYNetPeerSkewBehindMaxTicks = 0U;
		return;
	}
	v = atoi(e);
	if (v < 0)
	{
		v = 0;
	}
	if (v > 10000)
	{
		v = 10000;
	}
	sSYNetPeerSkewBehindMaxTicks = (u32)v;
}

static sb32 sSYNetPeerSkewGapEwmaPacingEnabled;
static u32 sSYNetPeerSkewGapEwmaCapTicks = SYNETPEER_SKEW_GAP_EWMA_CAP_TICKS_DEFAULT;
static u32 sSYNetPeerSkewGapEwmaMinEffectiveLead = SYNETPEER_SKEW_GAP_EWMA_MIN_EFFECTIVE_LEAD_DEFAULT;
static s32 sSYNetPeerSkewGapEwmaTicks;

static void syNetPeerRefreshSkewGapEwmaPacingFromEnv(void)
{
	const char *e;
	int v;

	e = getenv("SSB64_NETPLAY_SKEW_GAP_EWMA_PACING");
	sSYNetPeerSkewGapEwmaPacingEnabled = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
	sSYNetPeerSkewGapEwmaCapTicks = SYNETPEER_SKEW_GAP_EWMA_CAP_TICKS_DEFAULT;
	e = getenv("SSB64_NETPLAY_SKEW_GAP_EWMA_CAP_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v < 1)
		{
			v = 1;
		}
		if (v > 32)
		{
			v = 32;
		}
		sSYNetPeerSkewGapEwmaCapTicks = (u32)v;
	}
	sSYNetPeerSkewGapEwmaMinEffectiveLead = SYNETPEER_SKEW_GAP_EWMA_MIN_EFFECTIVE_LEAD_DEFAULT;
	e = getenv("SSB64_NETPLAY_SKEW_GAP_EWMA_MIN_LEAD_TICKS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if (v < 0)
		{
			v = 0;
		}
		if (v > 32)
		{
			v = 32;
		}
		sSYNetPeerSkewGapEwmaMinEffectiveLead = (u32)v;
	}
}

static void syNetPeerMaybeLogCatchUpBehind(u32 local_sim_tick, u32 hr, const char *action)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_SKEW_BEHIND_LOG");
	if ((e == NULL) || (e[0] == '\0') || (atoi(e) == 0))
	{
		return;
	}
	if ((sSYNetPeerCatchUpBehindLastLogTick != ~(u32)0) && (local_sim_tick - sSYNetPeerCatchUpBehindLastLogTick) < 60U)
	{
		return;
	}
	sSYNetPeerCatchUpBehindLastLogTick = local_sim_tick;
	port_log(
	    "SSB64 NetPeer: catch_up_behind tick=%u hr=%u gap=%lld thr=%u action=%s\n",
	    (unsigned int)local_sim_tick, (unsigned int)hr, (long long)((s64)hr - (s64)local_sim_tick),
	    (unsigned int)sSYNetPeerSkewBehindMaxTicks, action);
}

static sb32 syNetPeerCatchUpBehindThresholdMet(u32 local_sim_tick, u32 *out_hr)
{
	u32 hr;
	s64 gap;

	if (sSYNetPeerSkewBehindMaxTicks == 0U)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	hr = sSYNetPeerHighestRemoteTick;
	if (out_hr != NULL)
	{
		*out_hr = hr;
	}
	gap = (s64)hr - (s64)local_sim_tick;
	if (gap < (s64)sSYNetPeerSkewBehindMaxTicks)
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetPeerRunCatchUpBehindBeforeInputStall(u32 local_sim_tick)
{
	u32 hr;

	if (syNetPeerCatchUpBehindThresholdMet(local_sim_tick, &hr) == FALSE)
	{
		return FALSE;
	}
	syNetPeerUpdateBattleGate();
	syNetPeerMaybeLogCatchUpBehind(local_sim_tick, hr, "pump_gate");
	return TRUE;
}

sb32 syNetPeerShouldRelaxStallUntilRemoteForCatchUp(u32 local_sim_tick)
{
	u32 hr;

	if (syNetPeerCatchUpBehindThresholdMet(local_sim_tick, &hr) == FALSE)
	{
		return FALSE;
	}
	syNetPeerMaybeLogCatchUpBehind(local_sim_tick, hr, "relax_stall");
	return TRUE;
}

static void syNetPeerMaybeLogSkewPacingHold(u32 tick, s32 skew, u32 effective_lead_max)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_PACING_LOG");
	if ((e == NULL) || (e[0] == '\0') || (atoi(e) == 0))
	{
		return;
	}
	if ((sSYNetPeerSkewPacingLastLogTick != ~(u32)0) && (tick - sSYNetPeerSkewPacingLastLogTick) < 30U)
	{
		return;
	}
	sSYNetPeerSkewPacingLastLogTick = tick;
	if (sSYNetPeerSkewGapEwmaPacingEnabled != FALSE)
	{
		port_log(
		    "SSB64 NetPeer: skew pacing holding tick advance tick=%u skew=%d hr=%u lead_max=%u eff_lead=%u ewma=%d hold_frames=%u\n",
		    tick, skew, sSYNetPeerHighestRemoteTick, (unsigned int)sSYNetPeerSkewPacingLeadMaxTicks,
		    (unsigned int)effective_lead_max, (int)sSYNetPeerSkewGapEwmaTicks, sSYNetPeerSkewPacingHoldFrameCount);
	}
	else
	{
		port_log(
		    "SSB64 NetPeer: skew pacing holding tick advance tick=%u skew=%d hr=%u lead_max=%u hold_frames=%u\n",
		    tick, skew, sSYNetPeerHighestRemoteTick, (unsigned int)sSYNetPeerSkewPacingLeadMaxTicks,
		    sSYNetPeerSkewPacingHoldFrameCount);
	}
}

sb32 syNetPeerShouldHoldSimTickForSkewPacing(u32 tick, s32 *out_skew)
{
	u32 hr;
	u32 remote_sim_frontier;
	s32 skew;
	u32 effective_lead_max;
	u32 reduce;
	u32 ema_pos;
	u32 tmp_lead;

	if (out_skew != NULL)
	{
		*out_skew = 0;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return FALSE;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return FALSE;
	}
	if ((sSYNetPeerTickGridExecGate != FALSE) && (syNetPhaseIsRunning() != FALSE) && (syNetTickGridLockIsLocked() != FALSE))
	{
		return FALSE;
	}
	if (sSYNetPeerSkewPacingLeadMaxTicks == 0U)
	{
		return FALSE;
	}
	hr = sSYNetPeerHighestRemoteTick;
	remote_sim_frontier = syNetPeerDelaySimTickFromWire(hr);
	skew = (s32)tick - (s32)remote_sim_frontier;
	if (out_skew != NULL)
	{
		*out_skew = skew;
	}
	effective_lead_max = sSYNetPeerSkewPacingLeadMaxTicks;
	if ((sSYNetPeerSkewGapEwmaPacingEnabled != FALSE) && (hr != 0U))
	{
		sSYNetPeerSkewGapEwmaTicks += (skew - sSYNetPeerSkewGapEwmaTicks) >> 3;
		if (sSYNetPeerSkewGapEwmaTicks < 0)
		{
			sSYNetPeerSkewGapEwmaTicks = 0;
		}
		if (sSYNetPeerSkewGapEwmaTicks > (s32)SYNETPEER_SKEW_GAP_EWMA_STORE_MAX)
		{
			sSYNetPeerSkewGapEwmaTicks = (s32)SYNETPEER_SKEW_GAP_EWMA_STORE_MAX;
		}
		ema_pos = (u32)sSYNetPeerSkewGapEwmaTicks;
		reduce = (ema_pos < sSYNetPeerSkewGapEwmaCapTicks) ? ema_pos : sSYNetPeerSkewGapEwmaCapTicks;
		if (reduce < effective_lead_max)
		{
			tmp_lead = effective_lead_max - reduce;
		}
		else
		{
			tmp_lead = 0U;
		}
		if (tmp_lead < sSYNetPeerSkewGapEwmaMinEffectiveLead)
		{
			tmp_lead = sSYNetPeerSkewGapEwmaMinEffectiveLead;
		}
		if (tmp_lead > sSYNetPeerSkewPacingLeadMaxTicks)
		{
			tmp_lead = sSYNetPeerSkewPacingLeadMaxTicks;
		}
		effective_lead_max = tmp_lead;
	}
	if (skew > (s32)effective_lead_max)
	{
		sSYNetPeerSkewPacingHoldFrameCount++;
		syNetPeerMaybeLogSkewPacingHold(tick, skew, effective_lead_max);
		return TRUE;
	}
	return FALSE;
}

u32 syNetPeerGetSkewPacingHoldFrameCount(void)
{
	return sSYNetPeerSkewPacingHoldFrameCount;
}

static void syNetPeerResetSkewPacingSessionStats(void)
{
	sSYNetPeerSkewPacingHoldFrameCount = 0U;
	sSYNetPeerSkewPacingLastLogTick = ~(u32)0;
	sSYNetPeerSkewGapEwmaTicks = 0;
	sSYNetPeerCatchUpBehindLastLogTick = ~(u32)0;
#ifdef PORT
	syNetPeerResetMatchBufferMinSlackEnv();
	syNetPeerResetMatchDelayStarvationSession();
#endif
}

static void syNetPeerResetDesyncTraceSession(void)
{
	sSYNetPeerDesyncTracePrevValid = FALSE;
	sSYNetPeerDesyncTracePrevFigh = 0U;
	sSYNetPeerDesyncTraceLevelCache = -999;
}

/*
 * After tick-grid contract is locked, decouple deadline pacing must not throttle sim-tick indexing
 * (PortPushFrame); display may still refresh faster than sim.
 */
sb32 syNetPeerShouldBypassDecoupleSimPacingForTickGrid(void)
{
	if (sSYNetPeerTickGridExecGate == FALSE)
	{
		return FALSE;
	}
	if (syNetPhaseIsRunning() == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return FALSE;
	}
	return syNetTickGridLockIsLocked();
}
#endif /* PORT */

s32 syNetPeerGetRemoteHumanSlotCount(void)
{
	return sSYNetPeerRemoteReceiveCount;
}

sb32 syNetPeerGetRemoteHumanSlotByIndex(s32 index, s32 *out_slot)
{
	if ((index < 0) || (index >= sSYNetPeerRemoteReceiveCount) || (out_slot == NULL))
	{
		return FALSE;
	}
	*out_slot = (s32)sSYNetPeerRemoteReceiveSlots[index];
	return TRUE;
}

#ifdef PORT
sb32 syNetPeerShouldPumpBattleGateOnHostFrame(void)
{
	char *e;

	if (sSYNetPeerIsActive == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerCheckBattleExecutionReady() != FALSE)
	{
		return FALSE;
	}
	e = getenv("SSB64_NETPLAY_HOSTFRAME_GATE_PUMP");
	if ((e != NULL) && (atoi(e) == 0))
	{
		return FALSE;
	}
	return TRUE;
}

void syNetPeerPumpBattleGateOnHostFrame(void)
{
	if (syNetPeerShouldPumpBattleGateOnHostFrame() != FALSE)
	{
		syNetPeerUpdateBattleGate();
	}
}

sb32 syNetPeerWantsSyncPresentHold(void)
{
	char *e;

	if (sSYNetPeerIsActive == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerCheckBattleExecutionReady() != FALSE)
	{
		return FALSE;
	}
	if ((u32)gSCManagerSceneData.scene_curr != (u32)nSCKindVSBattle)
	{
		return FALSE;
	}
	e = getenv("SSB64_NETPLAY_SYNC_PRESENT_HOLD");
	if ((e != NULL) && (atoi(e) == 0))
	{
		return FALSE;
	}
	return TRUE;
}
#endif /* PORT */
#endif /* defined(PORT) - main netpeer PORT translation unit */
