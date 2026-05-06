#include <sys/netpeer.h>

#include <ft/fighter.h>
#include <gr/ground.h>
#include <sc/scmanager.h>
#include <sys/netinput.h>
#include <sys/netreplay.h>
#include <sys/netrollback.h>
#include <sys/netsync.h>
#include <sys/utils.h>
#include <sys/taskman.h>

#ifdef SSB64_NETMENU
extern sb32 mnVSNetLevelPrefsMapsCheckLocked(s32 gkind);
extern s32 mnVSNetLevelPrefsMapsGetGroundKind(s32 slot);
#endif

#ifdef PORT
#include "gameloop.h"

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

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

extern sb32 sSYNetPeerIsActive;

/*
 * Effective tick/frame diag level: `SSB64_NETPLAY_TICK_DIAG` env (cached) with a floor of 1 while a VS UDP
 * session is active so match logs always include tick_diag snapshots, NetSync extras (including extended input
 * diagnostics when `SSB64_NETPLAY_NETSYNC_INPUT_DIAG` is not set to 0), and INPUT endpoint
 * routing logs without requiring env setup. `clock_sync_sample` and `tick_diag tag=barrier_release` are
 * always emitted on Linux UDP (not gated by this level).
 */
static int syNetPeerTickDiagLevel(void)
{
	static int s_env_lvl = -999;
	int effective;
	char *e;

	if (s_env_lvl == -999)
	{
		e = getenv("SSB64_NETPLAY_TICK_DIAG");
		if ((e != NULL) && (e[0] != '\0'))
		{
			s_env_lvl = atoi(e);
			if (s_env_lvl < 0)
			{
				s_env_lvl = 0;
			}
		}
		else
		{
			s_env_lvl = 0;
		}
	}
	effective = s_env_lvl;
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
#endif

#if defined(PORT) && !defined(_WIN32)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
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
#define SYNETPEER_CONNECT_BLOCK_BYTES ((MAXCONTROLLERS)*8)
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
#define SYNETPEER_DEFAULT_SESSION_ID 1
#define SYNETPEER_DEFAULT_BOOTSTRAP_SEED 12345
#define SYNETPEER_LOG_INTERVAL 120
/* Host adaptive delay: run policy + broadcast on sim ticks (decoupled from stats logging interval). */
#define SYNETPEER_ADAPT_DELAY_SIM_INTERVAL 120U
#define SYNETPEER_BOOTSTRAP_RETRY_COUNT 180
#define SYNETPEER_BOOTSTRAP_RETRY_USECS 16666
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
#define SYNETPEER_UDP_SYNC_PACKET_BYTES (4 + 2 + 2 + 4 + 2 + 2 + 4)
#define SYNETPEER_UDP_LINK_SYNC_ROUNDS 5
/* INPUT layout predicates (peer_connect_status introduced at wire 4/5). */
#define SYNETPEER_WIRE_HAS_CONNECT_STATUS(W) (((u16)(W) == (u16)SYNETPEER_VERSION) || ((u16)(W) == (u16)SYNETPEER_VERSION_DUAL_LOCAL))
#define SYNETPEER_WIRE_HAS_SECONDARY_BUNDLE(W)                                                                           \
	(((u16)(W) == (u16)SYNETPEER_WIRE_LEGACY_INPUT_DUAL) || ((u16)(W) == (u16)SYNETPEER_VERSION_DUAL_LOCAL))

#define SYNETPEER_AUTOMATCH_OFFER_BYTES (4 + 2 + 2 + 4 + 2 + 1 + 1 + 4 + 4)
#define SYNETPEER_INPUT_BIND_BYTES (4 + 2 + 2 + 4 + 1 + 1 + 1 + 1 + 4)
#define SYNETPEER_BATTLE_EXEC_SYNC_BYTES_LEGACY (4 + 2 + 2 + 4 + 4 + 4 + 4)
#define SYNETPEER_BATTLE_EXEC_SYNC_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 4 + 4)
#define SYNETPEER_INPUT_DELAY_SYNC_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 4)
#define SYNETPEER_BARRIER_SKEW_RETRY_MAX 3U

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
                                       u32 current_tick);

sb32 sSYNetPeerIsEnabled;
sb32 sSYNetPeerIsConfigured;
sb32 sSYNetPeerIsActive;
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
static sb32 sSYNetPeerAdaptiveDelayEnabled;
static sb32 sSYNetPeerAdaptivePrimed;
static u32 sSYNetPeerAdaptivePrevLateFrames;
static u32 sSYNetPeerAdaptivePrevLoadFail;
static u32 sSYNetPeerAdaptiveStableIntervals;
static u32 sSYNetPeerAdaptNextSimTick;
static u32 sSYNetPeerDelaySyncPending;
static u32 sSYNetPeerDelaySyncEffectiveTick;
static sb32 sSYNetPeerDelaySyncPendingValid;

static u32 syNetPeerClampInputDelayToContract(u32 delay);
static void syNetPeerResetDelaySyncPending(void);
static void syNetPeerApplyPendingInputDelaySync(void);

static void syNetPeerResetAdaptiveDelayTracking(void)
{
	sSYNetPeerAdaptivePrimed = FALSE;
	sSYNetPeerAdaptivePrevLateFrames = 0;
	sSYNetPeerAdaptivePrevLoadFail = 0;
	sSYNetPeerAdaptiveStableIntervals = 0;
	sSYNetPeerAdaptNextSimTick = 0U;
	syNetPeerResetDelaySyncPending();
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

static void syNetPeerResetDelaySyncPending(void)
{
	sSYNetPeerDelaySyncPendingValid = FALSE;
	sSYNetPeerDelaySyncPending = 0U;
	sSYNetPeerDelaySyncEffectiveTick = 0U;
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
		sSYNetPeerInputDelay = syNetPeerClampInputDelayToContract(sSYNetPeerDelaySyncPending);
		syNetPeerResetDelaySyncPending();
	}
}
#endif

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)
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
#endif

#if defined(PORT) && !defined(_WIN32)
s32 sSYNetPeerSocket = -1;
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
static sb32 syNetPeerRequireInputBindStrict(void);
static void syNetPeerInputBindReset(void);
static sb32 syNetPeerInputBindIsComplete(void);
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
                                 const u8 *connect_disconnected)
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
				checksum = syNetPeerChecksumAccumulateU32(checksum, (u32)connect_last_tick[i]);
				checksum = syNetPeerChecksumAccumulateU32(checksum, connect_disconnected[i]);
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

#if defined(PORT) && !defined(_WIN32)
void syNetPeerSendBytes(const u8 *buffer, u32 size);

void syNetPeerSleepBootstrapRetry(void)
{
	usleep(SYNETPEER_BOOTSTRAP_RETRY_USECS);
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
	if (sSYNetPeerSocket >= 0)
	{
		close(sSYNetPeerSocket);
		sSYNetPeerSocket = -1;
	}
}

sb32 syNetPeerOpenSocket(void)
{
	s32 flags;
	s32 reuse = 1;

	if (sSYNetPeerSocket >= 0)
	{
		return TRUE;
	}
	sSYNetPeerSocket = socket(AF_INET, SOCK_DGRAM, 0);

	if (sSYNetPeerSocket < 0)
	{
		port_log("SSB64 NetPeer: socket failed errno=%d\n", errno);
		return FALSE;
	}
	setsockopt(sSYNetPeerSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	if (bind(sSYNetPeerSocket, (struct sockaddr*)&sSYNetPeerBindAddress, sizeof(sSYNetPeerBindAddress)) != 0)
	{
		port_log("SSB64 NetPeer: bind failed errno=%d\n", errno);
		syNetPeerCloseSocket();
		return FALSE;
	}
	flags = fcntl(sSYNetPeerSocket, F_GETFL, 0);

	if ((flags < 0) || (fcntl(sSYNetPeerSocket, F_SETFL, flags | O_NONBLOCK) != 0))
	{
		port_log("SSB64 NetPeer: nonblocking setup failed errno=%d\n", errno);
		syNetPeerCloseSocket();
		return FALSE;
	}
	return TRUE;
}

/*--------------------------------------------------------------------
 * POSIX clock sync + barrier deadlines (Linux UDP netpeer only).
 *
 * Wall-clock ms (CLOCK_REALTIME) timestamps TIME_PING/TIME_PONG samples and
 * anchors BATTLE_START_TIME so peers wait locally on the barrier deadline.
 *-------------------------------------------------------------------*/
static u64 syNetPeerNowUnixMs(void)
{
	struct timespec ts;
	u64 ms;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		return 0;
	}
	ms = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
	return ms;
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

/* Drops ping/pong progress and start-time/barrier bookkeeping (VS session start). */
static void syNetPeerResetClockAlignState(void)
{
	s32 i;

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
	if ((seq != sSYNetPeerClockSyncSampleCount) || (h0 != sSYNetPeerTimePingT0Sent))
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
#if defined(SSB64_NETMENU) && defined(PORT) && !defined(_WIN32)
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

#if defined(PORT) && !defined(_WIN32)
void syNetPeerSendBytes(const u8 *buffer, u32 size)
{
	sendto(sSYNetPeerSocket, buffer, size, 0,
	       (struct sockaddr*)&sSYNetPeerPeerAddress, sizeof(sSYNetPeerPeerAddress));
}
#endif

#ifdef PORT
static void syNetPeerMaybeAdaptInputDelay(void)
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
		if (sSYNetPeerInputDelay < sSYNetPeerInputDelayCeil)
		{
			sSYNetPeerInputDelay++;
			port_log("SSB64 NetPeer: adaptive delay up -> %u (late_delta=%u lf_delta=%u ceil=%u late=%u)\n",
			         sSYNetPeerInputDelay, d_late, d_lf, sSYNetPeerInputDelayCeil, late);
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
				sSYNetPeerInputDelay--;
				port_log("SSB64 NetPeer: adaptive delay down -> %u floor=%u\n",
				         sSYNetPeerInputDelay, sSYNetPeerInputDelayFloor);
			}
		}
	}
}
#endif

#if defined(PORT) && !defined(_WIN32)
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
	if (sendto(sSYNetPeerSocket, buffer, SYNETPEER_INPUT_DELAY_SYNC_BYTES, 0,
	           (struct sockaddr *)&sSYNetPeerPeerAddress, sizeof(sSYNetPeerPeerAddress)) ==
	    (ssize_t)SYNETPEER_INPUT_DELAY_SYNC_BYTES)
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
	if ((effective_tick != 0U) && (syNetInputGetTick() < effective_tick))
	{
		sSYNetPeerDelaySyncPending = delay_wire;
		sSYNetPeerDelaySyncEffectiveTick = effective_tick;
		sSYNetPeerDelaySyncPendingValid = TRUE;
	}
	else
	{
		sSYNetPeerInputDelay = delay_wire;
		syNetPeerResetDelaySyncPending();
	}
}

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
	syNetPeerMaybeAdaptInputDelay();
	(void)prev_delay;
	syNetPeerSendInputDelaySyncPacket(sSYNetPeerInputDelay, tick + 2U);
}
#endif

void syNetPeerSendControlPacket(u16 packet_type)
{
#if defined(PORT) && !defined(_WIN32)
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

#if defined(PORT) && !defined(_WIN32)
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

#if defined(SSB64_NETMENU)
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
	for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
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
#endif /* SSB64_NETMENU */

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
}

#if defined(PORT) && !defined(_WIN32)

static sb32 sSYNetPeerUdpLinkSyncEnvEnabled = TRUE;
static sb32 sSYNetPeerUdpLinkEnvLoaded;
static sb32 sSYNetPeerUdpLinkComplete = TRUE;
static u32 sSYNetPeerUdpLinkRoundsRemaining;
static u16 sSYNetPeerUdpLinkPendingToken;
static u32 sSYNetPeerUdpLinkNonce;

static void syNetPeerMergedConnectReset(void)
{
	s32 i;

	for (i = 0; i < MAXCONTROLLERS; i++)
	{
		sSYNetPeerMergedConnectLastTick[i] = -1;
		sSYNetPeerMergedConnectDisc[i] = 0;
	}
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
	syNetPeerUdpSyncSendPayload(SYNETPEER_PACKET_UDP_SYNC_REQ, sSYNetPeerUdpLinkPendingToken, 0);
}

static void syNetPeerUdpSyncSendReplyEcho(u16 challenge)
{
	syNetPeerUdpSyncSendPayload(SYNETPEER_PACKET_UDP_SYNC_REP, challenge, 0);
}

static void syNetPeerHandleUdpSyncIngress(const u8 *buffer, ssize_t size)
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

	if (size != (ssize_t)SYNETPEER_UDP_SYNC_PACKET_BYTES)
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
	ssize_t n;

	for (;;)
	{
		n = recvfrom(sSYNetPeerSocket, buf, sizeof(buf), 0, NULL, NULL);
		if (n < 0)
		{
			break;
		}
		if (n == (ssize_t)SYNETPEER_UDP_SYNC_PACKET_BYTES)
		{
			syNetPeerHandleUdpSyncIngress(buf, n);
		}
	}
}

static sb32 syNetPeerRunUdpLinkSync(void)
{
	s32 i;

	syNetPeerLoadUdpLinkSyncEnvOnce();
	if (sSYNetPeerUdpLinkSyncEnvEnabled == FALSE)
	{
		sSYNetPeerUdpLinkComplete = TRUE;
		sSYNetPeerUdpLinkRoundsRemaining = 0U;
		return TRUE;
	}
	sSYNetPeerUdpLinkComplete = FALSE;
	sSYNetPeerUdpLinkRoundsRemaining = SYNETPEER_UDP_LINK_SYNC_ROUNDS;
	sSYNetPeerUdpLinkNonce = (u32)(sSYNetPeerSessionID ^ 0x9E3779B9U);
	syNetPeerUdpSyncSendRequest();
	for (i = 0; i < (s32)(SYNETPEER_BOOTSTRAP_RETRY_COUNT * 8); i++)
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
		if ((i % 4) == 0)
		{
			syNetPeerUdpSyncSendRequest();
		}
	}
	port_log("SSB64 NetPeer: UDP link sync FAILED (timeout)\n");
	return FALSE;
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
	if (sendto(sSYNetPeerSocket, buffer, SYNETPEER_INPUT_BIND_BYTES, 0, (struct sockaddr *)&sSYNetPeerPeerAddress,
	           sizeof(sSYNetPeerPeerAddress)) == (ssize_t)SYNETPEER_INPUT_BIND_BYTES)
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
	u32 tick;

	if ((sSYNetPeerIsActive == FALSE) || (syNetPeerRequireInputBindStrict() == FALSE) ||
	    (syNetPeerInputBindIsComplete() != FALSE))
	{
		return;
	}
	tick = syNetInputGetTick();
	if ((tick % 15U) != 0U)
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
}

static sb32 syNetPeerBattleExecSyncIsComplete(void)
{
	if (syNetPeerRequireBattleExecSync() == FALSE)
	{
		return TRUE;
	}
	if ((sSYNetPeerBattleBarrierEnabled == FALSE) || (sSYNetPeerBootstrapIsEnabled == FALSE))
	{
		return TRUE;
	}
	if (sSYNetPeerBattleBarrierReleased == FALSE)
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

static u32 syNetPeerExecSyncComputeViPhaseBucket(void)
{
	return syNetPeerBarrierDeadlineViPhaseBucket();
}

static void syNetPeerSendBattleExecSyncPacket(u32 agreed_sim_tick, u32 vi_phase_bucket)
{
	u8 buffer[SYNETPEER_BATTLE_EXEC_SYNC_BYTES];
	u8 *cursor = buffer;
	u32 checksum;
	int push_diag;

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
	if (sendto(sSYNetPeerSocket, buffer, SYNETPEER_BATTLE_EXEC_SYNC_BYTES, 0, (struct sockaddr *)&sSYNetPeerPeerAddress,
	           sizeof(sSYNetPeerPeerAddress)) != (ssize_t)SYNETPEER_BATTLE_EXEC_SYNC_BYTES)
	{
		return;
	}
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
		sSYNetPeerPacketsDropped++;
		return;
	}
	sSYNetPeerPacketsReceived++;
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

#endif /* defined(PORT) && !defined(_WIN32) */

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
	u8 buffer[SYNETPEER_BOOTSTRAP_PACKET_BYTES];

	while (TRUE)
	{
		ssize_t size = recvfrom(sSYNetPeerSocket, buffer, sizeof(buffer), 0, NULL, NULL);

		if (size < 0)
		{
			if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			{
				sSYNetPeerPacketsDropped++;
			}
			break;
		}
		syNetPeerHandleBootstrapPacket(buffer, (s32)size);
	}
}

static void syNetPeerBootstrapFailTeardown(void)
{
	syNetPeerCloseSocket();
	sSYNetPeerIsActive = FALSE;
	sSYNetPeerBootstrapPeerReady = FALSE;
	sSYNetPeerBootstrapStartReceived = FALSE;
	sSYNetPeerBootstrapMetadataApplied = FALSE;
	sSYNetPeerBootstrapMetadataStaged = FALSE;
#if defined(SSB64_NETMENU)
	sSYAutoGotPeerOffer = FALSE;
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
	if (syNetPeerOpenSocket() == FALSE)
	{
		return FALSE;
	}
	sSYNetPeerIsActive = TRUE;
#if defined(PORT) && !defined(_WIN32)
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

		for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
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
		for (i = 0; i < 30; i++)
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_START);
			syNetPeerSleepBootstrapRetry();
		}
		port_log("SSB64 NetPeer: bootstrap host sent START stage=%u seed=%u\n",
		         sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed);
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
		for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
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
	for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
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
	return TRUE;
}

#if defined(SSB64_NETMENU)

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
		char *adapt_env = getenv("SSB64_NETPLAY_ADAPTIVE_DELAY");
		char *delay_max_env = getenv("SSB64_NETPLAY_DELAY_MAX");

		sSYNetPeerAdaptiveDelayEnabled = FALSE;
		sSYNetPeerInputDelayFloor = input_delay;
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
	sSYNetPeerIsEnabled = TRUE;
	sSYNetPeerBootstrapIsEnabled = TRUE;
	sSYNetPeerBootstrapIsHost = (you_are_host != FALSE) ? TRUE : FALSE;
	sSYNetPeerSessionID = (session_id != 0U) ? session_id : SYNETPEER_DEFAULT_SESSION_ID;
	sSYNetPeerInputDelay = (input_delay > 99U) ? SYNETPEER_DEFAULT_INPUT_DELAY : input_delay;

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

	sSYNetPeerIsConfigured = TRUE;
	port_log("SSB64 NetPeer automatch: configured bind=%s peer=%s session=%u host=%d delay=%u\n", bind_hostport,
	         peer_hostport, sSYNetPeerSessionID, sSYNetPeerBootstrapIsHost, sSYNetPeerInputDelay);

	return TRUE;
}

s32 syNetPeerGetUdpSocketFd(void)
{
	return (sSYNetPeerSocket >= 0) ? sSYNetPeerSocket : -1;
}
#endif /* SSB64_NETMENU */

#endif /* defined(PORT) && !defined(_WIN32) */

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
	if (delay_env != NULL)
	{
		s32 delay = atoi(delay_env);

		if (delay >= 0)
		{
			sSYNetPeerInputDelay = delay;
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
#if !defined(_WIN32)
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
#else
	port_log("SSB64 NetPeer: debug UDP netplay is not implemented on Windows yet\n");
#endif
#endif
}

#if defined(PORT) && !defined(_WIN32)
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

void syNetPeerStartVSSession(void)
{
#if defined(PORT) && !defined(_WIN32)
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
		syNetPeerApplySimSlotInputSources();
		return;
	}
	sSYNetPeerHighestRemoteTick = 0;
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
	sSYNetPeerIsActive = TRUE;
	sSYNetPeerBattleBarrierEnabled = sSYNetPeerBootstrapIsEnabled;
	sSYNetPeerBattleLocalReady = sSYNetPeerBattleBarrierEnabled;
	sSYNetPeerBattlePeerReady = FALSE;
	sSYNetPeerBattleStartSent = FALSE;
	sSYNetPeerBattleStartReceived = FALSE;
	sSYNetPeerBattleBarrierReleased = (sSYNetPeerBattleBarrierEnabled == FALSE) ? TRUE : FALSE;
	sSYNetPeerBattleBarrierWaitFrames = 0;
	sSYNetPeerBattleStartRepeatFrames = 0;
	sSYNetPeerExecutionHoldFrames = 0;
	sSYNetPeerExecutionBeginLogged = (sSYNetPeerBattleBarrierEnabled == FALSE) ? TRUE : FALSE;

	syNetPeerLoadBarrierTimingEnvFromConfig();
	sSYNetPeerClockSyncTargetBaseline = sSYNetPeerClockSyncTargetTotal;
	sSYNetPeerBarrierSkewRetryCount = 0U;
	sSYNetPeerBarrierEpochExtraLeadMs = 0U;
	sSYNetPeerLastBarrierContractOffsetSpreadMs = 0;
	sSYNetPeerBarrierSkewRetriesLatchedForLog = 0U;
	syNetPeerResetClockAlignState();
#ifdef PORT
	syNetPeerResetAdaptiveDelayTracking();
#endif

	syNetPeerMergedConnectReset();
#if defined(PORT) && !defined(_WIN32)
	syNetPeerLoadUdpLinkSyncEnvOnce();
	if (sSYNetPeerBootstrapIsEnabled == FALSE)
	{
		sSYNetPeerUdpLinkComplete = TRUE;
	}
#endif

	syNetPeerApplySimSlotInputSources();

	syNetRollbackStartVSSession();

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
#if defined(PORT) && !defined(_WIN32)
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
#endif
	return TRUE;
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
#if defined(PORT) && !defined(_WIN32)
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
#endif /* defined(PORT) && !defined(_WIN32) */
	return TRUE;
}

static sb32 syNetPeerGatherHistoryBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *out_frame_count)
{
	SYNetInputFrame published_frame;
	SYNetInputFrame history_frame;
	u32 latest_tick;
	s32 frame_count;
	s32 back;

	if (syNetInputGetPublishedFrame(slot, &published_frame) == FALSE)
	{
		return FALSE;
	}
	latest_tick = published_frame.tick;
	frame_count = 0;
	for (back = SYNETPEER_MAX_PACKET_FRAMES - 1; back >= 0; back--)
	{
		if ((latest_tick >= (u32)back) &&
			(syNetInputGetHistoryFrame(slot, latest_tick - back, &history_frame) != FALSE))
		{
			frames[frame_count].tick = history_frame.tick + sSYNetPeerInputDelay;
			frames[frame_count].buttons = history_frame.buttons;
			frames[frame_count].stick_x = history_frame.stick_x;
			frames[frame_count].stick_y = history_frame.stick_y;
			frame_count++;
		}
	}
	*out_frame_count = frame_count;
	return TRUE;
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

	e = getenv("SSB64_NETPLAY_INPUT_BUNDLE_REDUNDANCY");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return;
	}
	nwant = atoi(e);
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
#endif

void syNetPeerBuildPacket(u8 *buffer, u32 *out_size)
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

	memset(frames, 0, sizeof(frames));
	memset(sec_frames, 0, sizeof(sec_frames));
	memset(&zero_frame, 0, sizeof(zero_frame));

	if (syNetPeerGatherHistoryBundle(sSYNetPeerLocalPlayer, frames, &frame_count) == FALSE)
	{
		*out_size = 0;
		return;
	}
#ifdef PORT
	syNetPeerAppendBundleRedundancyFrames(frames, &frame_count);
#endif
	wire_version = SYNETPEER_VERSION;
	secondary_slot_byte = SYNETPEER_SECONDARY_SLOT_ABSENT;
	if ((sSYNetPeerExtraLocalSenderSlot >= 0) &&
	    (syNetPeerGatherHistoryBundle(sSYNetPeerExtraLocalSenderSlot, sec_frames, &sec_frame_count) != FALSE))
	{
		wire_version = SYNETPEER_VERSION_DUAL_LOCAL;
		secondary_slot_byte = (u8)sSYNetPeerExtraLocalSenderSlot;
#ifdef PORT
		syNetPeerAppendBundleRedundancyFrames(sec_frames, &sec_frame_count);
#endif
	}
	syNetInputExportPeerConnectStatus(conn_last_tick, conn_disc, MAXCONTROLLERS);
	checksum = syNetPeerChecksumInputPacket(sSYNetPeerSessionID, sSYNetPeerHighestRemoteTick, sSYNetPeerSendSeq,
	                                       wire_version, (u8)sSYNetPeerLocalPlayer, (u8)frame_count, frames,
	                                       secondary_slot_byte, (u8)sec_frame_count, sec_frames, conn_last_tick, conn_disc);

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
		syNetPeerWriteU32(&cursor, (u32)conn_last_tick[i]);
		syNetPeerWriteU8(&cursor, conn_disc[i]);
		syNetPeerWriteU8(&cursor, 0);
		syNetPeerWriteU8(&cursor, 0);
		syNetPeerWriteU8(&cursor, 0);
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

#if defined(PORT) && !defined(_WIN32)
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
#if defined(PORT) && !defined(_WIN32)
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];
	u32 size;

	if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
	{
		return;
	}
	syNetPeerBuildPacket(buffer, &size);

	if (size == 0)
	{
		return;
	}
	if (sendto(sSYNetPeerSocket, buffer, size, 0,
	           (struct sockaddr*)&sSYNetPeerPeerAddress, sizeof(sSYNetPeerPeerAddress)) == (ssize_t)size)
	{
		syNetPeerLogInputSendDiag(buffer, size);
		sSYNetPeerPacketsSent++;
		sSYNetPeerSendSeq++;
	}
#endif
}

static void syNetPeerStagePacketBundle(s32 target_player, const SYNetPeerPacketFrame *frames, s32 frame_count, u32 current_tick)
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
				sSYNetPeerHighestRemoteTick = frames[i].tick;
			}
		}
		syNetInputSetRemoteInput(target_player, frames[i].tick, frames[i].buttons, frames[i].stick_x, frames[i].stick_y);
		sSYNetPeerFramesStaged++;
		sSYNetPeerInputChecksum = syNetPeerChecksumAccumulateU32(sSYNetPeerInputChecksum, (u32)target_player);
		sSYNetPeerInputChecksum = syNetPeerChecksumAccumulateFrame(sSYNetPeerInputChecksum, &frames[i]);
	}
}

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
	const s32 *chk_tick = NULL;
	const u8 *chk_disc = NULL;

#if defined(PORT) && !defined(_WIN32)
	if (size == SYNETPEER_TIME_PONG_BYTES)
	{
		syNetPeerHandleTimePongPacket(buffer, size);
		return;
	}
	if ((size == (s32)SYNETPEER_BATTLE_START_TIME_BYTES_LEGACY) || (size == (s32)SYNETPEER_BATTLE_START_TIME_BYTES))
	{
		syNetPeerHandleBattleStartTimePacket(buffer, size);
		return;
	}
	if (size == SYNETPEER_TIME_PING_BYTES)
	{
		syNetPeerHandleTimePingPacket(buffer, size);
		return;
	}
	if (size == SYNETPEER_INPUT_BIND_BYTES)
	{
		syNetPeerHandleInputBindPacket(buffer, size);
		return;
	}
	if ((size == (s32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES) || (size == (s32)SYNETPEER_BATTLE_EXEC_SYNC_BYTES_LEGACY))
	{
		syNetPeerHandleBattleExecSyncPacket(buffer, size);
		return;
	}
	if (size == (s32)SYNETPEER_INPUT_DELAY_SYNC_BYTES)
	{
		syNetPeerHandleInputDelaySyncPacket(buffer, size);
		return;
	}
	if (size == SYNETPEER_CONTROL_PACKET_BYTES)
	{
		syNetPeerHandleControlPacket(buffer, size);
		return;
	}
#endif
	is_dual = FALSE;
		sec_frame_count = 0;
		secondary_slot = SYNETPEER_SECONDARY_SLOT_ABSENT;
		if ((size != (s32)SYNETPEER_PACKET_BYTES_LEGACY_V2) && (size != (s32)SYNETPEER_PACKET_BYTES_LEGACY_V3) &&
		    (size != (s32)SYNETPEER_PACKET_BYTES_V4) && (size != (s32)SYNETPEER_PACKET_BYTES_V5))
		{
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
			sSYNetPeerPacketsDropped++;
			return;
		}
		if (SYNETPEER_WIRE_HAS_CONNECT_STATUS(wire_version) != FALSE)
		{
			for (i = 0; i < MAXCONTROLLERS; i++)
			{
				u32 lt_u;

				lt_u = syNetPeerReadU32(&cursor);
				recv_conn_tick[i] = (s32)lt_u;
				recv_conn_disc[i] = syNetPeerReadU8(&cursor);
				(void)syNetPeerReadU8(&cursor);
				(void)syNetPeerReadU8(&cursor);
				(void)syNetPeerReadU8(&cursor);
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
		                                                 frames, secondary_slot, sec_frame_count, sec_frames, chk_tick, chk_disc);

		if (checksum != expected_checksum)
		{
			sSYNetPeerPacketsDropped++;
			return;
		}
#if defined(PORT) && !defined(_WIN32)
		if ((syNetPeerRequireInputBindStrict() != FALSE) && (syNetPeerInputBindIsComplete() == FALSE))
		{
			sSYNetPeerPacketsDropped++;
			return;
		}
#endif
		sSYNetPeerPacketsReceived++;

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

#if defined(PORT) && !defined(_WIN32)
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

	syNetPeerStagePacketBundle((s32)player, frames, frame_count, current_tick);
	if ((is_dual != FALSE) && (sec_frame_count > 0))
	{
		syNetPeerStagePacketBundle((s32)secondary_slot, sec_frames, sec_frame_count, current_tick);
	}
}

void syNetPeerReceiveRemoteInput(void)
{
#if defined(PORT) && !defined(_WIN32)
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];

	while (TRUE)
	{
		ssize_t size = recvfrom(sSYNetPeerSocket, buffer, sizeof(buffer), 0, NULL, NULL);

		if (size < 0)
		{
			if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			{
				sSYNetPeerPacketsDropped++;
			}
			break;
		}
		syNetPeerHandlePacket(buffer, (s32)size);
	}
#endif
}

void syNetPeerPumpIngressBeforeInputRead(void)
{
#if defined(PORT) && !defined(_WIN32)
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	syNetPeerReceiveRemoteInput();
	syNetPeerApplyPendingInputDelaySync();
#endif
}

#ifdef PORT
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

void syNetPeerLogNetSyncValidation(u32 tick)
{
	u32 checksums[MAXCONTROLLERS];
	u32 inp_all = 0;
	u32 fighter_hash;
	u32 map_hash;
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
	syNetInputGetHistoryInputValueChecksumWindow(win_begin, win_length, checksums, &inp_all);

	fighter_hash = syNetSyncHashBattleFighters();
	map_hash = syNetSyncHashMapCollisionKinematics();

	port_log(
		"SSB64 NetSync: role=%s lp=%d rp=%d tick=%u hist_win=[%u,%u) all=0x%08X p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X figh=0x%08X mph=0x%08X snd_next=%u rcv_hw=%u gap=%u dup=%u ooo=%u puck=%u pko=%u pkn=%u sent=%u recv=%u dropped=%u stg=%u hr=%u late=%u inpchk=0x%08X pkt_valid=%d rb=%u lf=%u delay=%u ring=%u rscan=%u\n",
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
		sSYNetPeerLateFrames,
		sSYNetPeerInputChecksum,
		sSYNetPeerLastPacketTicksValid,
		syNetRollbackGetAppliedResimCount(),
		syNetRollbackGetLoadFailCount(),
		sSYNetPeerInputDelay,
		(u32)SYNETINPUT_HISTORY_LENGTH,
		(u32)SYNETROLLBACK_SCAN_WINDOW);
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
#if !defined(_WIN32)
	if (syNetPeerTickDiagLevel() >= 1)
	{
		struct timespec ts;
		u64 ums;

		ums = 0ULL;
		if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
		{
			ums = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
		}
		port_log(
		    "SSB64 NetSync: tick_diag tick=%u push=%d tm_up=%u tm_fr=%u scene=%u unix_ms=%llu tick_minus_hr=%d bar_rel=%d exec_rdy=%d\n",
		    tick, port_get_push_frame_count(), dSYTaskmanUpdateCount, dSYTaskmanFrameCount,
		    (unsigned int)(u32)gSCManagerSceneData.scene_curr, (unsigned long long)ums,
		    (int)((s32)tick - (s32)sSYNetPeerHighestRemoteTick), (sSYNetPeerBattleBarrierReleased != FALSE) ? 1 : 0,
		    (syNetPeerCheckBattleExecutionReady() != FALSE) ? 1 : 0);
	}
#endif
}
#endif

void syNetPeerLogStats(void)
{
#ifdef PORT
	u32 tick = syNetInputGetTick();

	if ((tick == 0) || ((tick - sSYNetPeerLastLogTick) < SYNETPEER_LOG_INTERVAL))
	{
		return;
	}
	sSYNetPeerLastLogTick = tick;

	port_log(
	    "SSB64 NetPeer: role=%s local=%d remote=%d barrier=%d execution_ready=%d tick=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u snd_next=%u rcv_hw=%u seq_gap=%u seq_dup=%u seq_ooo=%u peer_ack=%u inpchk=0x%08X delay=%u push=%d tm_up=%u tm_fr=%u scene=%u\n",
	    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
	    sSYNetPeerBattleBarrierReleased, syNetPeerCheckBattleExecutionReady(), tick, sSYNetPeerPacketsSent,
	    sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped, sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick,
	    sSYNetPeerLateFrames, sSYNetPeerSendSeq, sSYNetPeerRecvSeqHighWater, sSYNetPeerSeqGaps,
	    sSYNetPeerSeqDuplicates, sSYNetPeerSeqOutOfOrder, sSYNetPeerLastPeerAckTick, sSYNetPeerInputChecksum,
	    sSYNetPeerInputDelay, port_get_push_frame_count(), dSYTaskmanUpdateCount, dSYTaskmanFrameCount,
	    (unsigned int)(u32)gSCManagerSceneData.scene_curr);

	syNetPeerLogNetSyncValidation(tick);
#endif
}

void syNetPeerLogExecutionHold(void)
{
#ifdef PORT
	if ((sSYNetPeerExecutionHoldFrames == 1) ||
		((sSYNetPeerExecutionHoldFrames % SYNETPEER_BARRIER_LOG_INTERVAL) == 0))
	{
		port_log("SSB64 NetPeer: execution hold role=%s local=%d remote=%d tick=%u hold=%u barrier_wait=%u peer_ready=%d start_sent=%d start_recv=%d highest_remote=%u late=%u\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerExecutionHoldFrames, sSYNetPeerBattleBarrierWaitFrames,
		         sSYNetPeerBattlePeerReady, sSYNetPeerBattleStartSent,
		         sSYNetPeerBattleStartReceived, sSYNetPeerHighestRemoteTick,
		         sSYNetPeerLateFrames);
	}
#endif
}

void syNetPeerLogExecutionBegin(void)
{
#ifdef PORT
	if (sSYNetPeerExecutionBeginLogged == FALSE)
	{
		sSYNetPeerExecutionBeginLogged = TRUE;
		port_log(
		    "SSB64 NetPeer: execution begin role=%s local=%d remote=%d tick=%u hold=%u barrier_wait=%u highest_remote=%u late=%u push=%d tm_up=%u tm_fr=%u scene=%u\n",
		    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
		    syNetInputGetTick(), sSYNetPeerExecutionHoldFrames, sSYNetPeerBattleBarrierWaitFrames,
		    sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames, port_get_push_frame_count(),
		    dSYTaskmanUpdateCount, dSYTaskmanFrameCount, (unsigned int)(u32)gSCManagerSceneData.scene_curr);
#if !defined(_WIN32)
		if (syNetPeerTickDiagLevel() >= 1)
		{
			syNetPeerLogTickFrameSnapshot("exec_begin", TRUE);
		}
#endif
	}
#endif
}

void syNetPeerLogBarrierWait(void)
{
#ifdef PORT
	if ((sSYNetPeerBattleBarrierWaitFrames % SYNETPEER_BARRIER_LOG_INTERVAL) == 0)
	{
#if !defined(_WIN32)
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
#else
		port_log("SSB64 NetPeer: barrier wait role=%s local=%d remote=%d tick=%u local_ready=%d peer_ready=%d start_sent=%d start_recv=%d sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerBattleLocalReady, sSYNetPeerBattlePeerReady,
		         sSYNetPeerBattleStartSent, sSYNetPeerBattleStartReceived, sSYNetPeerPacketsSent,
		         sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		         sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames);
#endif
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
			 * Only resync taskman counters once we are actually running the VS battle scene.
			 * The clock barrier may release while scene_curr is still VSNetMatchStaging;
			 * resetting dSYTaskman* there corrupts the staging taskman's frame cadence
			 * and can fault on the following VS transition
			 * (free(prev heap) then scVSBattleStartBattle). syTaskmanLoadScene already zeros
			 * these counters when VS battle taskman starts.
			 */
			if ((u32)gSCManagerSceneData.scene_curr == (u32)nSCKindVSBattle)
			{
				syTaskmanResyncCountersAfterNetBarrier();
				port_reset_push_frame_count_for_net_barrier();
			}
		}
#if !defined(_WIN32)
		{
			struct timespec ts;
			u64 ums;
			u32 rel_gran;
			u32 rel_vi_phase;
			u32 scene_u;
			int taskman_resync_applied;
			u64 deadline_latched_ms;
			u32 deadline_vi_ph;

			ums = 0;
			if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
			{
				ums = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
			}
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
#else
		{
			u32 scene_u;
			int taskman_resync_applied;

			scene_u = (u32)gSCManagerSceneData.scene_curr;
			taskman_resync_applied =
			    ((sSYNetPeerBattleBarrierEnabled != FALSE) && (scene_u == (u32)nSCKindVSBattle)) ? 1 : 0;
			port_log(
			    "SSB64 NetPeer: barrier release role=%s reason=%s local=%d remote=%d tick=%u wait=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u port_push_frame=%d taskman_frame=%u scene_curr=%u taskman_resync=%d\n",
			    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", reason,
			    sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
			    sSYNetPeerBattleBarrierWaitFrames, sSYNetPeerPacketsSent,
			    sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
			    sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames,
			    port_get_push_frame_count(), (unsigned int)dSYTaskmanFrameCount, (unsigned int)scene_u,
			    taskman_resync_applied);
		}
#endif
#endif
	}
}

void syNetPeerUpdateStartBarrier(void)
{
#if defined(PORT) && !defined(_WIN32)
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
 * Per-frame ingest + barrier driver (runs before gameplay input send).
 * Order matters: process inbound UDP (may deliver BATTLE_START_TIME), advance barrier, then input-bind retries.
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
	syNetPeerReceiveRemoteInput();
#if defined(PORT) && !defined(_WIN32)
	syNetPeerApplyPendingInputDelaySync();
#endif
	syNetPeerUpdateStartBarrier();
#if defined(PORT) && !defined(_WIN32)
	syNetPeerInputBindServiceTransport();
	syNetPeerBattleExecSyncServiceTransport();
#endif

	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		sSYNetPeerExecutionHoldFrames++;
		syNetPeerLogExecutionHold();
	}
	else syNetPeerLogExecutionBegin();
}

void syNetPeerUpdate(void)
{
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	syNetPeerUpdateBattleGate();

	if (syNetPeerCheckBattleExecutionReady() == FALSE)
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
#if defined(PORT) && !defined(_WIN32)
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
#if defined(PORT) && !defined(_WIN32)
	syNetPeerRunAdaptiveInputDelaySimStep(syNetInputGetTick());
#endif
	syNetPeerLogStats();
	syNetRollbackUpdate();
}

void syNetPeerStopVSSession(void)
{
#ifdef PORT
	syNetRollbackStopVSSession();
#endif
#if defined(PORT) && !defined(_WIN32)
	if (sSYNetPeerIsActive != FALSE)
	{
		port_log("SSB64 NetPeer: VS session stop sent=%u recv=%u dropped=%u staged=%u late=%u checksum=0x%08X\n",
		         sSYNetPeerPacketsSent, sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		         sSYNetPeerFramesStaged, sSYNetPeerLateFrames, sSYNetPeerInputChecksum);
	}
	syNetPeerInputBindReset();
	syNetPeerBattleExecSyncReset();
	syNetPeerResetDelaySyncPending();
	syNetPeerCloseSocket();
#endif
	sSYNetPeerIsActive = FALSE;
}

sb32 syNetPeerIsVSSessionActive(void)
{
	return sSYNetPeerIsActive;
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

u32 syNetPeerGetHighestRemoteTick(void)
{
	return sSYNetPeerHighestRemoteTick;
}

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
#if !defined(_WIN32)
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
#else /* _WIN32 */
sb32 syNetPeerShouldPumpBattleGateOnHostFrame(void)
{
	return FALSE;
}

void syNetPeerPumpBattleGateOnHostFrame(void)
{
}

sb32 syNetPeerWantsSyncPresentHold(void)
{
	return FALSE;
}
#endif /* !_WIN32 */
#endif /* PORT */

